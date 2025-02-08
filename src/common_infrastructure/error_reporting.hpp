// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sentry/sentry.hpp"

// Reporting an error means sending it the online service (if enabled), or writing it a file - ready to be
// sent later (either automatically or when manually requested as part of a bug report).

// not thread-safe, call once near the start of the program
void InitBackgroundErrorReporting(Span<sentry::Tag const> tags);

// not thread-safe, call near the end of the program
void ShutdownBackgroundErrorReporting();

// thread-safe, not signal-safe, works even if InitErrorReporting() was not called
void ReportError(sentry::Error&& error);

template <typename... Args>
__attribute__((noinline)) void ReportError(sentry::Error::Level level, String format, Args const&... args) {
    sentry::Error error {};
    error.level = level;
    error.message = fmt::Format(error.arena, format, args...);
    error.stacktrace = CurrentStacktrace(ProgramCounter {CALL_SITE_PROGRAM_COUNTER});
    ReportError(Move(error));
}
