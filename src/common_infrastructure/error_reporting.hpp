// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "preferences.hpp"
#include "sentry/sentry.hpp"

// Reporting an error means sending it the online service (if enabled), or writing it a file - ready to be
// sent later (either automatically or when manually requested as part of a bug report).

// not thread-safe, call once near the start of the program
void InitBackgroundErrorReporting(Span<sentry::Tag const> tags);

// not thread-safe, call near the end of the program
void ShutdownBackgroundErrorReporting();

namespace detail {
void ReportError(sentry::Error&& error, Optional<u64> error_id);
bool ErrorSentBefore(u64 error_id);
} // namespace detail

enum class ErrorLevel { Debug, Info, Warning, Error, Fatal };

// thread-safe, not signal-safe, works even if InitErrorReporting() was not called
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
    detail::ReportError(Move(error), error_id);
}

enum class ReportFeedbackReturnCode {
    Success,
    InvalidEmail,
    Busy,
    DescriptionTooLong,
    DescriptionEmpty,
};

ReportFeedbackReturnCode ReportFeedback(String description, Optional<String> email, bool include_diagnostics);

// Use this with prefs::SetValue, prefs::GetValue
prefs::Descriptor const& IsOnlineReportingDisabledDescriptor();
void ErrorReportingOnPreferenceChanged(prefs::Key const& key, prefs::Value const* value);

// Slow version, reads the preferences file directly. Allows you to get the value without relying on any
// preferences object.
bool IsOnlineReportingDisabled();
