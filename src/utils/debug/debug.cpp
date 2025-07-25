// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Contains a section of code from the LLVM project that is licenced differently, see below for full details.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) LLVM Project contributors

#include "debug.hpp"

#include <signal.h>
#include <stdlib.h> // EXIT_FAILURE
#include <unwind.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "utils/debug_info/debug_info.h"
#include "utils/logger/logger.hpp"

static void DefaultPanicHook(char const* message, SourceLocation loc, uintptr pc) {
    constexpr StdStream k_panic_stream = StdStream::Err;
    InlineSprintfBuffer buffer;
    // we style the source location to look like the first item of a call stack and then print the stack
    buffer.Append("\nPanic: " ANSI_COLOUR_SET_FOREGROUND_RED "%s" ANSI_COLOUR_RESET
                  "\n[0] " ANSI_COLOUR_SET_FOREGROUND_BLUE "%s" ANSI_COLOUR_RESET ":%d: %s\n",
                  message,
                  loc.file,
                  loc.line,
                  loc.function);
    auto _ = StdPrint(k_panic_stream, buffer.AsString());
    auto _ = PrintCurrentStacktrace(k_panic_stream, {.ansi_colours = true}, ProgramCounter {pc});
    auto _ = StdPrint(k_panic_stream, "\n");
}

Atomic<PanicHook> g_panic_hook = DefaultPanicHook;

void SetPanicHook(PanicHook hook) { g_panic_hook.Store(hook, StoreMemoryOrder::Release); }
PanicHook GetPanicHook() { return g_panic_hook.Load(LoadMemoryOrder::Acquire); }

thread_local bool g_in_crash_handler {};

static Atomic<bool> g_panic_occurred {};

bool PanicOccurred() { return g_panic_occurred.Load(LoadMemoryOrder::Acquire); }
void ResetPanic() { g_panic_occurred.Store(false, StoreMemoryOrder::Release); }

// signal-safe
static void WriteDisasterFile(char const* message_c_str, String additional_message, SourceLocation loc) {
    auto _ = StdPrint(StdStream::Err, additional_message);
    static thread_local bool writing_disaster_file {};
    if (writing_disaster_file) return;
    writing_disaster_file = true;
    DEFER { writing_disaster_file = false; };
    auto const log_folder = TRY_OPT_OR(LogFolder(), return);
    auto const message = FromNullTerminated(message_c_str);
    auto const hash = Hash(message);
    DynamicArrayBounded<char, 1000> filepath {log_folder};
    dyn::Append(filepath, path::k_dir_separator);
    fmt::Append(filepath, ConcatArrays("{}."_ca, k_floe_disaster_file_extension), hash);
    auto file = TRY_OR(OpenFile(filepath, FileMode::Write()), return);

    BufferedWriter<1000> buffered_writer {file.Writer()};
    auto writer = buffered_writer.Writer();
    DEFER { buffered_writer.FlushReset(); };

    auto _ = writer.WriteChars(message);
    auto _ = writer.WriteChars("\n");
    if (additional_message.size) {
        auto _ = writer.WriteChars(additional_message);
        auto _ = writer.WriteChars("\n");
    }
    auto _ = writer.WriteChars(FromNullTerminated(loc.file));
    auto _ = writer.WriteChars(":");
    auto _ = writer.WriteChars(fmt::IntToString(loc.line, fmt::IntToStringOptions {}));
    auto _ = writer.WriteChars("\n");
    auto _ = writer.WriteChars("os:");
    auto _ = writer.WriteChars(({
        String s {};
        if constexpr (IS_WINDOWS)
            s = "Windows"_s;
        else if constexpr (IS_LINUX)
            s = "Linux"_s;
        else if constexpr (IS_MACOS)
            s = "macOS"_s;
        s;
    }));
}

// noinline because we want __builtin_return_address(0) to return the address of the call site
[[noreturn]] __attribute__((noinline)) void Panic(char const* message, SourceLocation loc) {
    static thread_local u8 in_panic_hook {};
    if (g_in_crash_handler) {
        WriteDisasterFile(message, "Panic occurred while in a signal handler", loc);
        _Exit(EXIT_FAILURE);
    }

    switch (in_panic_hook) {
        // First time we've panicked.
        case 0: {
            ++in_panic_hook;
            g_panic_hook.Load(LoadMemoryOrder::Acquire)(message, loc, CALL_SITE_PROGRAM_COUNTER);
            --in_panic_hook;

            g_panic_occurred.Store(true, StoreMemoryOrder::Release);
            throw PanicException();
        }

        // Panicked inside the panic hook.
        default: {
            --in_panic_hook;
            g_panic_occurred.Store(true, StoreMemoryOrder::Release);

            // We try to get our crash system to handle this as that is probably the best way to get some
            // information out of it.
            auto _ =
                StdPrint(StdStream::Err,
                         "Panic occurred while handling a panic, raising unrecoverable exception/SIGABRT\n");

            if constexpr (IS_WINDOWS)
                WindowsRaiseException(k_windows_nested_panic_code);
            else
                raise(SIGABRT);

            // While the above options are probably no-return, on Windows at least it's possible control
            // returns to this point after the exception handler runs.
            throw PanicException();
        }
    }
}

static void HandleUbsanError(String msg) {
    InlineSprintfBuffer buffer;
    buffer.Append("undefined behaviour: %.*s", (int)msg.size, msg.data);
    Panic(buffer.CString(), SourceLocation::Current());
}

#define INTERFACE extern "C" __attribute__((visibility("default")))

namespace ubsan {

// Code taken based LLVM's UBSan runtime implementation.
// https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/ubsan/ubsan_handlers.h
// Modified by Sam Windell to integrate with the rest of the codebase
// Copyright 2018-2024 Sam Windell
// Start of LLVM-based code
// =============================================================================================================

struct SourceLocation {
    char const* file;
    u32 line;
    u32 column;
};

using ValueHandle = uintptr_t;

struct TypeDescriptor {
    u16 kind;
    u16 info;
    char name[1];

    enum Kind { TkInteger = 0x0000, TkFloat = 0x0001, TkUnknown = 0xffff };
};

struct Value {
    TypeDescriptor const& type;
    ValueHandle val;
};

struct TypeMismatchData {
    SourceLocation loc;
    TypeDescriptor const& type;
    unsigned char log_alignment;
    unsigned char type_check_kind;
};

struct OverflowData {
    SourceLocation loc;
    TypeDescriptor const& type;
};

struct ShiftOutOfBoundsData {
    SourceLocation loc;
    TypeDescriptor const& lhs_type;
    TypeDescriptor const& rhs_type;
};

struct OutOfBoundsData {
    SourceLocation loc;
    TypeDescriptor const& array_type;
    TypeDescriptor const& index_type;
};

struct UnreachableData {
    SourceLocation loc;
};
struct VLABoundData {
    SourceLocation loc;
    TypeDescriptor const& type;
};

struct FloatCastOverflowDataV2 {
    SourceLocation loc;
    TypeDescriptor const& from_type;
    TypeDescriptor const& to_type;
};

struct InvalidBuiltinData {
    SourceLocation loc;
    unsigned char kind;
};

struct NonNullArgData {
    SourceLocation loc;
    SourceLocation attr_loc;
    int arg_index;
};

struct PointerOverflowData {
    SourceLocation loc;
};

struct DynamicTypeCacheMissData {
    SourceLocation loc;
    TypeDescriptor const& type;
    void* type_info;
    unsigned char type_check_kind;
};

} // namespace ubsan

// Full UBSan runtime
// NOLINTNEXTLINE
INTERFACE uintptr_t __ubsan_vptr_type_cache[128] = {};
INTERFACE void __ubsan_handle_dynamic_type_cache_miss([[maybe_unused]] ubsan::DynamicTypeCacheMissData* data,
                                                      [[maybe_unused]] ubsan::ValueHandle pointer,
                                                      [[maybe_unused]] ubsan::ValueHandle cache) {
    // I don't think this is necessarily a problem?
}
INTERFACE void __ubsan_handle_pointer_overflow([[maybe_unused]] ubsan::PointerOverflowData* data,
                                               [[maybe_unused]] ubsan::ValueHandle base,
                                               [[maybe_unused]] ubsan::ValueHandle result) {
    HandleUbsanError("pointer-overflow");
}
INTERFACE void __ubsan_handle_nonnull_arg([[maybe_unused]] ubsan::NonNullArgData* data) {
    HandleUbsanError("nonnull-arg: null was passed as an argument when it was explicitly marked as non-null");
}
INTERFACE void __ubsan_handle_float_cast_overflow([[maybe_unused]] ubsan::FloatCastOverflowDataV2* data,
                                                  [[maybe_unused]] ubsan::ValueHandle from) {
    HandleUbsanError("f32-cast-overflow");
}
INTERFACE void __ubsan_handle_invalid_builtin([[maybe_unused]] ubsan::InvalidBuiltinData* data) {
    HandleUbsanError("invalid-builtin");
}

INTERFACE void __ubsan_handle_add_overflow([[maybe_unused]] ubsan::OverflowData* data,
                                           [[maybe_unused]] ubsan::ValueHandle lhs,
                                           [[maybe_unused]] ubsan::ValueHandle rhs) {
    HandleUbsanError("add-overflow");
}
INTERFACE void __ubsan_handle_sub_overflow([[maybe_unused]] ubsan::OverflowData* data,
                                           [[maybe_unused]] ubsan::ValueHandle lhs,
                                           [[maybe_unused]] ubsan::ValueHandle rhs) {
    HandleUbsanError("sub-overflow");
}
INTERFACE void __ubsan_handle_mul_overflow([[maybe_unused]] ubsan::OverflowData* data,
                                           [[maybe_unused]] ubsan::ValueHandle lhs,
                                           [[maybe_unused]] ubsan::ValueHandle rhs) {
    HandleUbsanError("mul-overflow");
}
INTERFACE void __ubsan_handle_negate_overflow([[maybe_unused]] ubsan::OverflowData* data,
                                              [[maybe_unused]] ubsan::ValueHandle old_val) {
    HandleUbsanError("negate-overflow");
}
INTERFACE void __ubsan_handle_divrem_overflow([[maybe_unused]] ubsan::OverflowData* data,
                                              [[maybe_unused]] ubsan::ValueHandle lhs,
                                              [[maybe_unused]] ubsan::ValueHandle rhs) {
    HandleUbsanError("divrem-overflow");
}
INTERFACE void __ubsan_handle_type_mismatch_v1([[maybe_unused]] ubsan::TypeMismatchData* data,
                                               [[maybe_unused]] ubsan::ValueHandle pointer) {
    if (pointer == 0)
        HandleUbsanError("Null pointer access");
    else if (data->log_alignment != 0 && IsAligned((void*)pointer, data->log_alignment))
        HandleUbsanError("Unaligned memory access");
    else
        HandleUbsanError("Type mismatch: insufficient size");
}
INTERFACE void __ubsan_handle_out_of_bounds([[maybe_unused]] ubsan::OutOfBoundsData* data,
                                            [[maybe_unused]] ubsan::ValueHandle index) {
    HandleUbsanError("out-of-bounds");
}
INTERFACE void __ubsan_handle_shift_out_of_bounds([[maybe_unused]] ubsan::ShiftOutOfBoundsData* _data,
                                                  [[maybe_unused]] ubsan::ValueHandle lhs,
                                                  [[maybe_unused]] ubsan::ValueHandle rhs) {
    HandleUbsanError("shift-out-of-bounds");
}
INTERFACE void __ubsan_handle_builtin_unreachable([[maybe_unused]] void* _data) {
    HandleUbsanError("builtin-unreachable");
}
INTERFACE void __ubsan_handle_load_invalid_value([[maybe_unused]] void* _data, [[maybe_unused]] void* val) {
    HandleUbsanError("load-invalid-value");
}
INTERFACE void __ubsan_handle_alignment_assumption([[maybe_unused]] void* _data,
                                                   [[maybe_unused]] unsigned long ptr,
                                                   [[maybe_unused]] unsigned long align,
                                                   [[maybe_unused]] unsigned long offset) {
    HandleUbsanError("alignment-assumption");
}
INTERFACE void __ubsan_handle_missing_return([[maybe_unused]] void* _data) {
    HandleUbsanError("missing-return");
}

// Minimal UBSan runtime
#define MINIMAL_HANDLER_RECOVER(name, msg)                                                                   \
    INTERFACE void __ubsan_handle_##name##_minimal() { HandleUbsanError(msg); }

#define MINIMAL_HANDLER_NORECOVER(name, msg)                                                                 \
    INTERFACE void __ubsan_handle_##name##_minimal_abort() {                                                 \
        HandleUbsanError(msg);                                                                               \
        __builtin_abort();                                                                                   \
    }

#define MINIMAL_HANDLER(name, msg)                                                                           \
    MINIMAL_HANDLER_RECOVER(name, msg)                                                                       \
    MINIMAL_HANDLER_NORECOVER(name, msg)

MINIMAL_HANDLER(type_mismatch, "type-mismatch")
MINIMAL_HANDLER(alignment_assumption, "alignment-assumption")
MINIMAL_HANDLER(add_overflow, "add-overflow")
MINIMAL_HANDLER(sub_overflow, "sub-overflow")
MINIMAL_HANDLER(mul_overflow, "mul-overflow")
MINIMAL_HANDLER(negate_overflow, "negate-overflow")
MINIMAL_HANDLER(divrem_overflow, "divrem-overflow")
MINIMAL_HANDLER(shift_out_of_bounds, "shift-out-of-bounds")
MINIMAL_HANDLER(out_of_bounds, "out-of-bounds")
MINIMAL_HANDLER_RECOVER(builtin_unreachable, "builtin-unreachable")
MINIMAL_HANDLER_RECOVER(missing_return, "missing-return")
MINIMAL_HANDLER(vla_bound_not_positive, "vla-bound-not-positive")
MINIMAL_HANDLER(float_cast_overflow, "f32-cast-overflow")
MINIMAL_HANDLER(load_invalid_value, "load-invalid-value")
MINIMAL_HANDLER(invalid_builtin, "invalid-builtin")
MINIMAL_HANDLER(invalid_objc_cast, "invalid-objc-cast")
MINIMAL_HANDLER(function_type_mismatch, "function-type-mismatch")
MINIMAL_HANDLER(implicit_conversion, "implicit-conversion")
MINIMAL_HANDLER(nonnull_arg, "nonnull-arg")
MINIMAL_HANDLER(nonnull_return, "nonnull-return")
MINIMAL_HANDLER(nullability_arg, "nullability-arg")
MINIMAL_HANDLER(nullability_return, "nullability-return")
MINIMAL_HANDLER(pointer_overflow, "pointer-overflow")
MINIMAL_HANDLER(cfi_check_fail, "cfi-check-fail")

// End of LLVM-based code
// =============================================================================================================

void DumpInfoAboutUBSan(StdStream stream) {
    auto _ = StdPrint(stream, "Possibly undefined behaviour found with UBSan. UBSan checks include:\n");
    constexpr String k_ubsan_checks[] = {
        "  type-mismatch\n",       "  alignment-assumption\n",   "  add-overflow\n",
        "  sub-overflow\n",        "  mul-overflow\n",           "  negate-overflow\n",
        "  divrem-overflow\n",     "  shift-out-of-bounds\n",    "  out-of-bounds\n",
        "  builtin-unreachable\n", "  missing-return\n",         "  vla-bound-not-positive\n",
        "  f32-cast-overflow\n",   "  load-invalid-value\n",     "  invalid-builtin\n",
        "  invalid-objc-cast\n",   "  function-type-mismatch\n", "  implicit-conversion\n",
        "  nonnull-arg\n",         "  nonnull-return\n",         "  nullability-arg\n",
        "  nullability-return\n",  "  pointer-overflow\n",       "  cfi-check-fail\n",
    };
    for (auto check : k_ubsan_checks)
        auto _ = StdPrint(stream, check);
}

ErrorCodeCategory const g_stacktrace_error_category {
    .category_id = "ST",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((StacktraceError)code.code) {
            case StacktraceError::NotInitialised: str = "not initialised"; break;
        }
        return writer.WriteChars(str);
    },
};

struct BacktraceState {
    Optional<DynamicArrayBounded<char, 256>> failed_init_error {};
    SelfModuleHandle state = nullptr;
};

alignas(BacktraceState) static u8 g_backtrace_state_storage[sizeof(BacktraceState)] {};
static Atomic<BacktraceState*> g_backtrace_state {};
static CountedInitFlag g_init {};

Optional<String> InitStacktraceState() {
    ZoneScoped;
    CountedInit(g_init, [] {
        auto state = PLACEMENT_NEW(g_backtrace_state_storage) BacktraceState;

        state->failed_init_error.Emplace();
        state->state =
            CreateSelfModuleInfo(state->failed_init_error->data, state->failed_init_error->Capacity());
        if (state->state)
            state->failed_init_error.Clear();
        else
            state->failed_init_error->size = NullTerminatedSize(state->failed_init_error->data);

        g_backtrace_state.Store(state, StoreMemoryOrder::Release);
    });

    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);
    if (state->failed_init_error) {
        LogDebug(ModuleName::Global, "Failed to initialise backtrace state: {}", *state->failed_init_error);
        return *state->failed_init_error;
    }
    return k_nullopt;
}

void ShutdownStacktraceState() {
    ZoneScoped;
    CountedDeinit(g_init, [] {
        if (auto state = g_backtrace_state.Exchange(nullptr, RmwMemoryOrder::AcquireRelease)) {
            DestroySelfModuleInfo(state->state);
            state->~BacktraceState();
        }
    });
}

static void SkipUntil(StacktraceStack& stack, uintptr pc) {
    ASSERT(pc);
    for (auto const i : Range(stack.size))
        if (stack[i] == pc) {
            dyn::Remove(stack, 0, i);
            return;
        }
}

Optional<StacktraceStack> CurrentStacktrace(StacktraceSkipOptions skip) {
    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);

    if (!state || state->failed_init_error) return k_nullopt;

    StacktraceStack result;
    _Unwind_Backtrace(
        [](struct _Unwind_Context* context, void* user) -> _Unwind_Reason_Code {
            auto& stack = *(StacktraceStack*)user;
            int ip_before = 0;
            auto pc = _Unwind_GetIPInfo(context, &ip_before);
            if (pc == 0) return _URC_END_OF_STACK;
            if (!ip_before) --pc;

            if (stack.size != stack.Capacity()) dyn::Append(stack, pc);

            return _URC_NO_REASON;
        },
        &result);

    switch (skip.tag) {
        case StacktraceSkipType::Frames: dyn::Remove(result, 0, ToInt(skip.Get<StacktraceFrames>())); break;
        case StacktraceSkipType::UntilProgramCounter:
            SkipUntil(result, ToInt(skip.Get<ProgramCounter>()));
            break;
    }

    if (auto const pc = skip.TryGet<ProgramCounter>()) SkipUntil(result, (uintptr)*pc);

    return result;
}

struct StacktraceContext {
    StacktracePrintOptions options;
    Writer writer;
    u32 line_num = 1;
    ErrorCodeOr<void> return_value;
};

// Our Zig code calls this function when it panics.
void PanicHandler(char const* message, size_t message_length) {
    char buffer[256];
    CopyStringIntoBufferWithNullTerm(buffer, String {message, message_length});
    Panic(buffer, SourceLocation::Current());
}

ErrorCodeOr<void> WriteStacktrace(Span<uintptr const> stack, Writer writer, StacktracePrintOptions options) {
    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);
    if (!state) return ErrorCode {StacktraceError::NotInitialised};

    if (state->failed_init_error) return fmt::FormatToWriter(writer, "{}", *state->failed_init_error);

    StacktraceContext ctx {.options = options, .writer = writer};

    SymbolInfo(state->state,
               stack.data,
               stack.size,
               &ctx,
               [](void* user_data, struct SymbolInfoData const* symbol) {
                   auto& ctx = *(StacktraceContext*)user_data;
                   if (ctx.return_value.HasError()) return;
                   FrameInfo const frame {
                       .address = symbol->address,
                       .function_name = FromNullTerminated(symbol->name),
                       .filename = symbol->file ? FromNullTerminated(symbol->file)
                                                : FromNullTerminated(symbol->compile_unit_name),
                       .line = symbol->line,
                       .column = symbol->column,
                       .in_self_module = symbol->address_in_self_module != 0,
                   };
                   ctx.return_value = frame.Write(ctx.line_num++, ctx.writer, ctx.options);
               });
    if (ctx.return_value.HasError()) return ctx.return_value;

    return k_success;
}

ErrorCodeOr<void>
WriteCurrentStacktrace(Writer writer, StacktracePrintOptions options, StacktraceSkipOptions skip) {
    if (auto stack = CurrentStacktrace(skip)) return WriteStacktrace(*stack, writer, options);
    return ErrorCode {StacktraceError::NotInitialised};
}

MutableString StacktraceString(Span<uintptr const> stack, Allocator& a, StacktracePrintOptions options) {
    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);
    if (!state) return a.Clone("Stacktrace error: not initialised"_s);
    if (state->failed_init_error) return a.Clone(*state->failed_init_error);

    DynamicArray<char> result {a};
    StacktraceContext ctx {.options = options, .writer = dyn::WriterFor(result)};
    SymbolInfo(state->state,
               stack.data,
               stack.size,
               &ctx,
               [](void* user_data, struct SymbolInfoData const* symbol) {
                   auto& ctx = *(StacktraceContext*)user_data;
                   if (ctx.return_value.HasError()) return;
                   FrameInfo const frame {
                       .address = symbol->address,
                       .function_name = FromNullTerminated(symbol->name),
                       .filename = symbol->file ? FromNullTerminated(symbol->file)
                                                : FromNullTerminated(symbol->compile_unit_name),
                       .line = symbol->line,
                       .column = symbol->column,
                       .in_self_module = symbol->address_in_self_module != 0,
                   };
                   ctx.return_value = frame.Write(ctx.line_num++, ctx.writer, ctx.options);
               });

    return result.ToOwnedSpan();
}

MutableString
CurrentStacktraceString(Allocator& a, StacktracePrintOptions options, StacktraceSkipOptions skip) {
    if (auto stack = CurrentStacktrace(skip)) return StacktraceString(*stack, a, options);
    return a.Clone("Stacktrace error: not initialised"_s);
}

void StacktraceToCallback(Span<uintptr const> stack,
                          FunctionRef<void(FrameInfo const&)> callback,
                          StacktracePrintOptions options) {
    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);
    if (!state || state->failed_init_error) return;

    using CallbackType = decltype(callback);

    struct Context {
        CallbackType& callback;
        StacktracePrintOptions const& options;
    };
    Context context {callback, options};

    SymbolInfo(state->state,
               stack.data,
               stack.size,
               &context,
               [](void* data, struct SymbolInfoData const* symbol) {
                   auto& ctx = *(Context*)data;
                   FrameInfo const frame {
                       .address = symbol->address,
                       .function_name = FromNullTerminated(symbol->name),
                       .filename = symbol->file ? FromNullTerminated(symbol->file)
                                                : FromNullTerminated(symbol->compile_unit_name),
                       .line = symbol->line,
                       .column = symbol->column,
                       .in_self_module = symbol->address_in_self_module != 0,
                   };
                   ctx.callback(frame);
               });
}

void CurrentStacktraceToCallback(FunctionRef<void(FrameInfo const&)> callback,
                                 StacktracePrintOptions options,
                                 StacktraceSkipOptions skip) {
    auto stack = CurrentStacktrace(skip);
    if (stack) StacktraceToCallback(*stack, callback, options);
}

ErrorCodeOr<void>
PrintCurrentStacktrace(StdStream stream, StacktracePrintOptions options, StacktraceSkipOptions skip) {
    return WriteCurrentStacktrace(StdWriter(stream), options, skip);
}

bool HasAddressesInCurrentModule(Span<uintptr const> addresses) {
    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);
    if (!state || state->failed_init_error) return true;
    for (auto const address : addresses)
        if (IsAddressInCurrentModule(state->state, address)) return true;
    return false;
}

bool IsAddressInCurrentModule(usize address) {
    auto state = g_backtrace_state.Load(LoadMemoryOrder::Acquire);
    if (!state || state->failed_init_error) return false;
    return IsAddressInCurrentModule(state->state, address);
}
