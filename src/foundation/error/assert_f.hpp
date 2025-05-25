// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/utils/format.hpp"

// Sometimes don't want to depend on our usual string formatting because that code could be the cause of the
// problem we're trying to debug.
struct InlineSprintfBuffer {
    InlineSprintfBuffer() { buffer[0] = 0; }

    void Append(char const* fmt, ...) __attribute__((__format__(__printf__, 2, 3))) {
        va_list args;
        va_start(args, fmt);
        auto const n = stbsp_vsnprintf(write_ptr, size_remaining, fmt, args);
        va_end(args);

        if (n < 0) return;

        auto const num_written = Min(size_remaining, n);
        write_ptr += num_written;
        size_remaining -= num_written;
    }

    String AsString() { return {buffer, ArraySize(buffer) - (usize)size_remaining}; }
    char const* CString() { return buffer; }

    char buffer[1024]; // always null-terminated
    int size_remaining = (int)ArraySize(buffer);
    char* write_ptr = buffer;
};

template <typename... Args>
[[noreturn]] PUBLIC void PanicF(SourceLocation loc, String format, Args const&... args) {
    DynamicArrayBounded<char, 1000> buffer {};
    fmt::Append(buffer, format, args...);
    Panic(dyn::NullTerminated(buffer), loc);
}

// We could add ASSERT_LE, ASSERT_GE, etc. but it it doesn't tend to be useful to get the values of less-than
// or greater-than comparisons in the panic message. It also makes the code harder to read. I think ASSERT_EQ
// is the most useful and most readable so that's all we have for now.

#define ASSERT_EQ(a, b)                                                                                      \
    do {                                                                                                     \
        if constexpr (RUNTIME_SAFETY_CHECKS_ON) {                                                            \
            auto x = a;                                                                                      \
            auto y = b;                                                                                      \
            if (!(x == y) && !PanicOccurred()) {                                                             \
                PanicF(SourceLocation::Current(), "assertion failed: " #a " == " #b " | {} == {}", x, y);    \
            }                                                                                                \
        } else {                                                                                             \
            ASSUME((a) == (b));                                                                              \
        }                                                                                                    \
    } while (0)

#define ASSERT_NEQ(a, b)                                                                                     \
    do {                                                                                                     \
        if constexpr (RUNTIME_SAFETY_CHECKS_ON) {                                                            \
            auto x = a;                                                                                      \
            auto y = b;                                                                                      \
            if (!(x != y) && !PanicOccurred()) {                                                             \
                PanicF(SourceLocation::Current(), "assertion failed: " #a " != " #b " | {} != {}", x, y);    \
            }                                                                                                \
        } else {                                                                                             \
            ASSUME((a) != (b));                                                                              \
        }                                                                                                    \
    } while (0)

#define PANICF(format, ...) PanicF(SourceLocation::Current(), format, ##__VA_ARGS__)
