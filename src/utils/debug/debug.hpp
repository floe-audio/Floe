// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "tracy_wrapped.hpp"

enum class StacktraceError {
    NotInitialised,
};

extern ErrorCodeCategory const g_stacktrace_error_category;
inline ErrorCodeCategory const& ErrorCategoryForEnum(StacktraceError) { return g_stacktrace_error_category; }

struct StacktracePrintOptions {
    bool ansi_colours = false;
    bool demangle = true; // demangling is not signal-safe
};

// make sure to only use this in a noinline function
#define CALL_SITE_PROGRAM_COUNTER ((uintptr)__builtin_extract_return_addr(__builtin_return_address(0)) - 1)

enum class ProgramCounter : uintptr {};
enum class StacktraceFrames : u32 {};
enum class StacktraceSkipType { Frames, UntilProgramCounter };
using StacktraceSkipOptions = TaggedUnion<StacktraceSkipType,
                                          TypeAndTag<ProgramCounter, StacktraceSkipType::UntilProgramCounter>,
                                          TypeAndTag<StacktraceFrames, StacktraceSkipType::Frames>>;

using StacktraceStack = DynamicArrayBounded<uintptr, 32>;
Optional<StacktraceStack> CurrentStacktrace(StacktraceSkipOptions skip = StacktraceFrames {1});
Optional<String> InitStacktraceState(Optional<String> current_binary_path); // returns error message if failed
void ShutdownStacktraceState();

struct FrameInfo {
    ErrorCodeOr<void> Write(u32 frame_index, Writer writer, StacktracePrintOptions options) const {
        return fmt::FormatToWriter(writer,
                                   "[{}] {x} {}{}{}:{}:{}: {}\n",
                                   frame_index,
                                   address,
                                   options.ansi_colours ? ANSI_COLOUR_SET_FOREGROUND_BLUE : ""_s,
                                   filename,
                                   options.ansi_colours ? ANSI_COLOUR_RESET : ""_s,
                                   line,
                                   column,
                                   function_name);
    }

    static FrameInfo FromSourceLocation(SourceLocation loc, uintptr address, bool in_self_module) {
        return {
            .address = address,
            .function_name = FromNullTerminated(loc.function),
            .filename = FromNullTerminated(loc.file),
            .line = loc.line,
            .column = -1,
            .in_self_module = in_self_module,
        };
    }

    uintptr address;
    String function_name;
    String filename;
    int line;
    int column = -1;
    bool in_self_module = false; // if the filename is in the current module
};

MutableString CurrentStacktraceString(Allocator& a,
                                      StacktracePrintOptions options = {},
                                      StacktraceSkipOptions skip = StacktraceFrames {1});
MutableString StacktraceString(Span<uintptr const> stack, Allocator& a, StacktracePrintOptions options = {});
void CurrentStacktraceToCallback(FunctionRef<void(FrameInfo const&)> callback,
                                 StacktracePrintOptions options = {},
                                 StacktraceSkipOptions skip = StacktraceFrames {1});
void StacktraceToCallback(Span<uintptr const> stack,
                          FunctionRef<void(FrameInfo const&)> callback,
                          StacktracePrintOptions options = {});
ErrorCodeOr<void> PrintCurrentStacktrace(StdStream stream,
                                         StacktracePrintOptions options,
                                         StacktraceSkipOptions skip = StacktraceFrames {1});
ErrorCodeOr<void> WriteStacktrace(Span<uintptr const> stack, Writer writer, StacktracePrintOptions options);
ErrorCodeOr<void> WriteCurrentStacktrace(Writer writer,
                                         StacktracePrintOptions options,
                                         StacktraceSkipOptions skip = StacktraceFrames {1});

bool HasAddressesInCurrentModule(Span<uintptr const> addresses);
inline bool IsAddressInCurrentModule(uintptr address) { return HasAddressesInCurrentModule({&address, 1}); }

// Call once at the start/end of your progam. When a crash occurs g_crash_handler will be called. It must be
// async-signal-safe on Unix. It should return normally, not throw exceptions or call abort().
//
// About crashes:
// If there's a crash something has gone very wrong. We can't do much really other than write to a file
// since we need to be async-signal-safe. Crashes are different to Panics, panics are controlled failure - we
// have an opportunity to try and clean up and exit with a bit more grace.
using CrashHookFunction = void (*)(String message, Optional<StacktraceStack> stacktrace);
void BeginCrashDetection(CrashHookFunction);
void EndCrashDetection();

struct TracyMessageConfig {
    String category;
    u32 colour;
    Optional<uintptr_t> object_id;
};

template <typename... Args>
PUBLIC void TracyMessageEx(TracyMessageConfig config, String format, Args const&... args) {
    if constexpr (!k_tracy_enable) return;

    DynamicArrayBounded<char, 5000> msg;
    dyn::Append(msg, '[');
    dyn::AppendSpan(msg, config.category);
    dyn::AppendSpan(msg, "] "_s);

    if (config.object_id) fmt::Append(msg, "{}: ", *config.object_id);

    if constexpr (sizeof...(args))
        fmt::Append(msg, format, args...);
    else
        dyn::AppendSpan(msg, format);
    TracyMessageC(msg.data, msg.size, config.colour);
}

#define ZoneKeyNum(key, num)                                                                                 \
    do {                                                                                                     \
        if constexpr (k_tracy_enable) {                                                                      \
            const auto CONCAT(zone_key_num, __LINE__) = fmt::FormatInline<100>("{}: {}", key, num);          \
            ZoneText(CONCAT(zone_key_num, __LINE__).data, CONCAT(zone_key_num, __LINE__).size);              \
            (void)CONCAT(zone_key_num, __LINE__);                                                            \
        }                                                                                                    \
    } while (0)
