// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "preferences.hpp"
#include "sentry/sentry.hpp"

// Reporting an error means sending it the online service (if enabled), or writing it a file - ready to be
// sent later (either automatically or when manually requested as part of a bug report).

// Not thread-safe. Call once near the start of the program.
void InitBackgroundErrorReporting(Span<sentry::Tag const> tags);

// Not thread-safe. Call near the end of the program.
void ShutdownBackgroundErrorReporting();

namespace detail {
void ReportError(sentry::Error&& error, Optional<u64> error_id);
bool ErrorSentBefore(u64 error_id);
} // namespace detail

enum class ErrorLevel : u8 { Debug, Info, Warning, Error, Fatal };

// Thread-safe. Not signal-safe. Works even if InitBackgroundErrorReporting() was not called.
template <typename... Args>
__attribute__((noinline)) void
ReportError(ErrorLevel level, Optional<u64> error_id, String format, Args const&... args) {
    if (error_id)
        if (detail::ErrorSentBefore(*error_id)) return;
    sentry::Error error {};
    error.level = ({
        sentry::Error::Level l;
        switch (level) {
            case ErrorLevel::Debug: l = sentry::Error::Level::Debug; break;
            case ErrorLevel::Info: l = sentry::Error::Level::Info; break;
            case ErrorLevel::Warning: l = sentry::Error::Level::Warning; break;
            case ErrorLevel::Error: l = sentry::Error::Level::Error; break;
            case ErrorLevel::Fatal: l = sentry::Error::Level::Fatal; break;
        }
        l;
    });
    error.message = fmt::Format(error.arena, format, args...);
    error.stacktrace = CurrentStacktrace(ProgramCounter {CALL_SITE_PROGRAM_COUNTER});
    auto const thread_name = ThreadName(false);
    error.thread = sentry::Error::Thread {
        .id = CurrentThreadId(),
        .is_main = g_is_logical_main_thread != 0,
        .name = thread_name ? Optional<String> {error.arena.Clone((String)*thread_name)} : k_nullopt,
    };
    // Sentry groups events by (exception.type, exception.value, stacktrace). Without an exception,
    // the stack frames are invisible to grouping and every event collapses by message alone.
    String const exception_type = error_id ? (String)fmt::Format(error.arena, "error_{x}", *error_id) : ({
        String s;
        switch (level) {
            case ErrorLevel::Debug: s = "Debug"_s; break;
            case ErrorLevel::Info: s = "Info"_s; break;
            case ErrorLevel::Warning: s = "Warning"_s; break;
            case ErrorLevel::Error: s = "Error"_s; break;
            case ErrorLevel::Fatal: s = "Fatal"_s; break;
        }
        s;
    });
    error.exception = sentry::Error::Exception {
        .type = exception_type,
        .value = error.message,
    };
    detail::ReportError(Move(error), error_id);
}

enum class ReportFeedbackReturnCode : u8 {
    Success,
    InvalidEmail,
    Busy,
    DescriptionTooLong,
    DescriptionEmpty,
};

ReportFeedbackReturnCode ReportFeedback(String description, Optional<String> email, bool include_diagnostics);

// Use this with prefs::SetValue and prefs::GetValue.
prefs::Descriptor const& IsOnlineReportingDisabledDescriptor();
void ErrorReportingOnPreferenceChanged(prefs::Key const& key, prefs::Value const* value);

// Slow version, reads the preferences file directly. Allows you to get the value without relying on any
// preferences object.
bool IsOnlineReportingDisabled();
