// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "error_reporting.hpp"

#include "sentry/sentry_background_queue.hpp"

static CountedInitFlag g_init_flag {};
static Atomic<sentry::BackgroundQueue*> g_queue = nullptr;
alignas(sentry::BackgroundQueue) static u8 g_worker_storage[sizeof(sentry::BackgroundQueue)];
static MutexThin g_reported_error_ids_mutex = {};
static DynamicArrayBounded<u64, 48> g_reported_error_ids = {};

constexpr bool k_online_reporting_disabled_default = false;
constexpr String k_online_reporting_disabled_preference_key = "online_reporting_disabled"_s;

prefs::Descriptor const& IsOnlineReportingDisabledDescriptor() {
    static prefs::Descriptor const d {
        .key = k_online_reporting_disabled_preference_key,
        .value_requirements = prefs::ValueType::Bool,
        .default_value = k_online_reporting_disabled_default,
        .gui_label = "Disable anonymous error reports",
        .long_description =
            "If an error occurs, Floe sends anonymous data about the error, your system, and Floe's state to a server. Additionally, Floe sends anonymous data points about when a session starts and ends for determining software health.",
    };
    return d;
}

void ErrorReportingOnPreferenceChanged(prefs::Key const& key, prefs::Value const* value) {
    if (auto const v = prefs::Match(key, value, IsOnlineReportingDisabledDescriptor())) {
        if (auto sentry = sentry::GlobalSentry())
            sentry->online_reporting_disabled.Store(v->Get<bool>(), StoreMemoryOrder::Relaxed);
    }
}

bool IsOnlineReportingDisabled() {
    ArenaAllocatorWithInlineStorage<Kb(4)> arena {PageAllocator::Instance()};
    auto try_read = [&]() -> ErrorCodeOr<bool> {
        auto const file_data = TRY(prefs::ReadEntirePreferencesFile(PreferencesFilepath(), arena)).file_data;
        auto const table = prefs::ParsePreferencesFile(file_data, arena);
        return prefs::LookupBool(table, k_online_reporting_disabled_preference_key)
            .ValueOr(k_online_reporting_disabled_default);
    };

    auto const outcome = try_read();
    if (outcome.HasError()) {
        if (outcome.Error() == FilesystemError::PathDoesNotExist) return k_online_reporting_disabled_default;

        // We couldn't read the file, so we can't know either way. It could just be an temporary filesystem
        // error, so we can't assume the user's preference so we'll go for the less controversial option:
        // disable online reporting.
        return true;
    }

    return outcome.Value();
}

void InitBackgroundErrorReporting(Span<sentry::Tag const> tags) {
    ZoneScoped;
    CountedInit(g_init_flag, [&]() {
        auto existing = g_queue.Load(LoadMemoryOrder::Acquire);
        if (existing) return;

        WebGlobalInit();

        auto worker = PLACEMENT_NEW(g_worker_storage) sentry::BackgroundQueue {};
        sentry::StartThread(*worker, tags);
        g_queue.Store(worker, StoreMemoryOrder::Release);
    });
}

namespace detail {

bool ErrorSentBefore(u64 error_id) {
    g_reported_error_ids_mutex.Lock();
    DEFER { g_reported_error_ids_mutex.Unlock(); };
    return Contains(g_reported_error_ids, error_id);
}

[[nodiscard]] static bool SetErrorSent(u64 error_id) {
    g_reported_error_ids_mutex.Lock();
    DEFER { g_reported_error_ids_mutex.Unlock(); };
    if (g_reported_error_ids.size == g_reported_error_ids.Capacity()) return false;
    dyn::Append(g_reported_error_ids, error_id);
    return true;
}

void ReportError(sentry::Error&& error, Optional<u64> error_id) {
    ZoneScoped;
    if (error_id)
        if (!SetErrorSent(*error_id)) return;

    // For debug purposes, log the error.
    Log(ModuleName::ErrorReporting, LogLevel::Debug, [&error](Writer writer) -> ErrorCodeOr<void> {
        TRY(fmt::FormatToWriter(writer, "Error reported: {}\n", error.message));
        if (error.stacktrace)
            TRY(WriteStacktrace(*error.stacktrace,
                                writer,
                                {
                                    .ansi_colours = false,
                                    .demangle = true,
                                }));
        return k_success;
    });

    // Best option: enqueue the error for the background thread
    if (auto q = g_queue.Load(LoadMemoryOrder::Acquire); q && !PanicOccurred()) {
        if (sentry::TryEnqueueError(*q, Move(error))) return;
    }

    // Fallback option: write the message to file directly
    auto _ = WriteErrorToFile(*sentry::SentryOrFallback {}, error);
}

} // namespace detail

void ShutdownBackgroundErrorReporting() {
    ZoneScoped;
    CountedDeinit(g_init_flag, [&]() {
        LogDebug({}, "Shutting down background error reporting");
        auto q = g_queue.Load(LoadMemoryOrder::Acquire);
        ASSERT(q);
        sentry::RequestThreadEnd(*q);
        sentry::WaitForThreadEnd(*q);

        WebGlobalCleanup();
    });
}

static bool EmailIsValid(String email) {
    if (!email.size) return false;
    if (email.size > 256) return false;
    if (email[0] == '@') return false;

    auto at_pos = Find(email, '@');
    if (!at_pos) return false;

    email.RemovePrefix(*at_pos + 1);
    if (!email.size) return false;
    if (email[0] == '.') return false;

    auto dot_pos = Find(email, '.');
    if (!dot_pos) return false;

    return true;
}

ReportFeedbackReturnCode
ReportFeedback(String description, Optional<String> email, bool include_diagnostics) {
    if (description.size == 0) return ReportFeedbackReturnCode::DescriptionEmpty;
    if (description.size > sentry::FeedbackEvent::k_max_message_length)
        return ReportFeedbackReturnCode::DescriptionTooLong;

    if (email && !EmailIsValid(*email)) return ReportFeedbackReturnCode::InvalidEmail;

    auto queue = g_queue.Load(LoadMemoryOrder::Acquire);
    ASSERT(queue);

    sentry::Feedback feedback {};
    feedback.message = feedback.arena.Clone(description);
    if (email) feedback.email = feedback.arena.Clone(*email);
    feedback.include_diagnostics = include_diagnostics;

    if (!sentry::TryEnqueueFeedback(*queue, Move(feedback))) return ReportFeedbackReturnCode::Busy;
    return ReportFeedbackReturnCode::Success;
}
