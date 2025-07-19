// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h> // strerror
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <valgrind/valgrind.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "utils/debug/debug.hpp"
#include "utils/logger/logger.hpp"

#include "time.h"

Memory GlobalAlloc(AllocOptions options) {
    // "The size parameter must be an integral multiple of alignment."
    options.size = AlignForward(options.size, options.align);
    auto ptr = aligned_alloc(options.align, options.size);
    TracyAlloc(ptr, options.size);
    auto result = Memory {ptr, options.size};
    if (options.zero) FillMemory(result, 0);
    return result;
}

Memory GlobalRealloc(Memory allocation, ReallocOptions options) {
    // IMPROVE: maybe there's a way to do this without freeing and reallocating but there doesn't appear to be
    // a simple solution.
    auto new_mem = GlobalAlloc({.size = options.size, .align = options.align});
    CopyMemory(new_mem.data, allocation.data, Min(new_mem.size, allocation.size));
    GlobalFree(allocation);
    return new_mem;
}

void GlobalFreeNoSize(void* ptr) { GlobalFree({ptr, 0}); }

void GlobalFree(Memory allocation) {
    TracyFree(allocation.data);
    free(allocation.data);
}

void* AllocatePages(usize bytes) {
    if constexpr (!PRODUCTION_BUILD) {
        if (RUNNING_ON_VALGRIND) {
            auto const p = aligned_alloc(256, bytes);
            TracyAlloc(p, bytes);
            return p;
        }
    }

    auto p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    TracyAlloc(p, bytes);
    if (p == MAP_FAILED) return nullptr;
    return p;
}

void FreePages(void* ptr, usize bytes) {
    TracyFree(ptr);

    if constexpr (!PRODUCTION_BUILD) {
        if (RUNNING_ON_VALGRIND) {
            free(ptr);
            return;
        }
    }
    munmap(ptr, bytes);
}

// IMPROVE: safer ways of getting environment variables. It seems to be frought with danger on Unix, but
// possible options:
// - macOS: NSProcessInfo environment property
// - Linux: read /proc/self/environ
Optional<MutableString> GetEnvironmentVariable(char const* name, Allocator& a) {
    auto const value = getenv(name); // NOLINT(concurrency-mt-unsafe)
    if (value == nullptr) return k_nullopt;
    return a.Clone(FromNullTerminated(value));
}

Optional<MutableString> GetEnvironmentVariable(String name, Allocator& a) {
    ArenaAllocatorWithInlineStorage<500> scratch {PageAllocator::Instance()};
    return GetEnvironmentVariable(NullTerminated(name, scratch), a);
}

struct LockableSharedMemoryNative {
    sem_t* sema;
};

static void SemWait(sem_t* sema) {
    // In the case another process crashed while holding the semaphore, we can't wait forever, so when we
    // detect a significant delay we try to recover. It's not perfect.

    constexpr u8 k_deadlock_timeout_seconds = 3;

    struct timespec start;
    clock_gettime(CLOCK_REALTIME, &start);
    start.tv_sec += k_deadlock_timeout_seconds;

#if _POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600
    if (auto r = sem_timedwait(sema, &start); r == -1) {
        if (errno == ETIMEDOUT) {
            sem_post(sema);
            SemWait(sema);
        }
    }
#else
    struct timespec current;
    while (1) {
        if (sem_trywait(sema) == 0) return; // Successfully acquired

        if (errno != EAGAIN) return; // Error occurred

        // Check if we've exceeded our timeout
        clock_gettime(CLOCK_REALTIME, &current);
        if (current.tv_sec > start.tv_sec ||
            (current.tv_sec == start.tv_sec && current.tv_nsec >= start.tv_nsec)) {
            sem_post(sema);
            SemWait(sema);
            return;
        }

        // sleep for 1ms
        struct timespec sleep_time = {.tv_sec = 0, .tv_nsec = 1'000'000};
        nanosleep(&sleep_time, nullptr);
    }
#endif
}

ErrorCodeOr<LockableSharedMemory> CreateLockableSharedMemory(String name, usize size) {
    ASSERT(name.size <= 32);
    ASSERT(!Contains(name, '/'));

    LockableSharedMemory result {};
    auto& native = result.native.As<LockableSharedMemoryNative>();

    auto const posix_name = fmt::FormatInline<40>("/{}\0", name);

    // open sema for use by all processes, if it already exists, we just open it
    native.sema = sem_open(posix_name.data, O_CREAT | O_EXCL, 0666, 1);
    if (native.sema == SEM_FAILED) {
        if (errno == EEXIST) {
            native.sema = sem_open(posix_name.data, O_RDWR);
            if (native.sema == SEM_FAILED) return ErrnoErrorCode(errno, "sem_open");
        } else {
            return ErrnoErrorCode(errno, "sem_open");
        }
    }

    // lock the semaphore so that we can initialize the shared memory
    SemWait(native.sema);
    DEFER { sem_post(native.sema); };

    // create shared memory, if it fails because it already exists, we just open it
    auto fd = shm_open(posix_name.data, O_CREAT | O_EXCL | O_RDWR, 0666);
    bool created = false;
    if (fd == -1) {
        if (errno == EEXIST) {
            fd = shm_open(posix_name.data, O_RDWR, 0666);
            if (fd == -1) return ErrnoErrorCode(errno, "shm_open");
        } else {
            return ErrnoErrorCode(errno, "shm_open");
        }
    } else {
        created = true;
        // we created it, so we need to set the size
        if (ftruncate(fd, (off_t)size) == -1) {
            close(fd);
            return ErrnoErrorCode(errno, "ftruncate");
        }
    }
    DEFER { close(fd); };

    // map the shared memory
    auto data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) Panic("mmap failed");

    // init to zero if we created it
    if (created) FillMemory(data, 0, size);

    result.data = {(u8*)data, size};
    return result;
}

void LockSharedMemory(LockableSharedMemory& memory) {
    auto& native = memory.native.As<LockableSharedMemoryNative>();
    SemWait(native.sema);
}

void UnlockSharedMemory(LockableSharedMemory& memory) {
    auto& native = memory.native.As<LockableSharedMemoryNative>();
    sem_post(native.sema);
}

ErrorCodeOr<String> ReadAllStdin(Allocator& allocator) {
    DynamicArray<char> result {allocator};
    char buffer[4096];
    while (true) {
        auto const read = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (read == -1) return ErrnoErrorCode(errno);
        if (read == 0) break;
        dyn::AppendSpan(result, Span {buffer, (usize)read});
    }
    return result.ToOwnedSpan();
}

ErrorCodeOr<LibraryHandle> LoadLibrary(String path) {
    PathArena path_arena {Malloc::Instance()};
    auto handle = dlopen(NullTerminated(path, path_arena), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) return ErrnoErrorCode(errno, "dlopen");
    return (LibraryHandle)(uintptr)handle;
}

ErrorCodeOr<void*> SymbolFromLibrary(LibraryHandle library, String symbol_name) {
    PathArena path_arena {Malloc::Instance()};
    auto symbol = dlsym((void*)library, NullTerminated(symbol_name, path_arena));
    if (symbol == nullptr) return ErrnoErrorCode(errno, "dlsym");
    return symbol;
}

void UnloadLibrary(LibraryHandle library) { dlclose((void*)library); }

int CurrentProcessId() { return getpid(); }

void TryShrinkPages(void* ptr, usize old_size, usize new_size) {
    if constexpr (!PRODUCTION_BUILD) {
        if (RUNNING_ON_VALGRIND) return;
    }

    TracyFree(ptr);
    auto const page_size = CachedSystemStats().page_size;
    auto const current_num_pages = old_size / page_size;
    auto const new_num_pages = (new_size == 0) ? 0 : ((new_size / page_size) + 1);
    if (current_num_pages != new_num_pages) {
        auto const num_pages = current_num_pages - new_num_pages;
        Span<u8> const unused_pages {(u8*)ptr + (new_num_pages * page_size), page_size * num_pages};
        ASSERT(ContainsPointer(unused_pages, unused_pages.data));
        ASSERT(ContainsPointer(unused_pages, &Last(unused_pages)));

        auto result = munmap(unused_pages.data, unused_pages.size);
        ASSERT(result == 0, "munmap failed");
    }
    TracyAlloc(ptr, new_size);
}

ErrorCodeOr<void> StdPrint(StdStream stream, String str) {
    auto const num_written = write(({
                                       int f = STDOUT_FILENO;
                                       switch (stream) {
                                           case StdStream::Out: f = STDOUT_FILENO; break;
                                           case StdStream::Err: f = STDERR_FILENO; break;
                                       }
                                       f;
                                   }),
                                   str.data,
                                   str.size);
    if (num_written < 0) return ErrnoErrorCode(errno, "StdPrint");
    return k_success;
}

s128 NanosecondsSinceEpoch() {
#if __APPLE__
    return (s128)clock_gettime_nsec_np(CLOCK_REALTIME);
#else
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((s128)ts.tv_sec * (s128)1e+9) + (s128)ts.tv_nsec;
#endif
}

s64 MicrosecondsSinceEpoch() {
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return CheckedCast<s64>(((u64)ts.tv_sec * 1'000'000) + ((u64)ts.tv_nsec / 1'000));
}

DateAndTime LocalTimeFromNanosecondsSinceEpoch(s128 nanoseconds) {
    timespec ts;
    ts.tv_sec = (time_t)(nanoseconds / (s128)1e+9);
    ts.tv_nsec = (long)(nanoseconds % (s128)1e+9);
    struct tm result {};
    localtime_r(&ts.tv_sec, &result);

    auto ns = ts.tv_nsec;
    auto const milliseconds = CheckedCast<s16>(ns / 1'000'000);
    ns %= 1'000'000;
    auto const microseconds = CheckedCast<s16>(ns / 1'000);
    ns %= 1'000;

    return DateAndTime {
        .year = (s16)(result.tm_year + 1900),
        .months_since_jan = (s8)result.tm_mon,
        .day_of_month = (s8)result.tm_mday,
        .days_since_sunday = (s8)result.tm_wday,
        .hour = (s8)result.tm_hour,
        .minute = (s8)result.tm_min,
        .second = (s8)result.tm_sec,
        .millisecond = milliseconds,
        .microsecond = microseconds,
        .nanosecond = (s16)ns,
    };
}

DateAndTime UtcTimeFromNanosecondsSinceEpoch(s128 nanoseconds) {
    // We don't use gmtime_r because it's not signal safe.

    DateAndTime dt {};

    // Extract nanosecond components
    dt.nanosecond = nanoseconds % 1000;
    nanoseconds /= 1000;

    dt.microsecond = nanoseconds % 1000;
    nanoseconds /= 1000;

    dt.millisecond = nanoseconds % 1000;
    nanoseconds /= 1000;

    // Now we have seconds since epoch
    auto total_seconds = CheckedCast<s64>(nanoseconds);

    // Extract time components
    dt.second = total_seconds % 60;
    total_seconds /= 60;

    dt.minute = total_seconds % 60;
    total_seconds /= 60;

    dt.hour = total_seconds % 24;
    total_seconds /= 24;

    // Now we have days since epoch (January 1, 1970)
    s64 total_days = total_seconds;

    // Calculate day of week (January 1, 1970 was a Thursday, so add 4)
    dt.days_since_sunday = (total_days + 4) % 7;
    ASSERT(dt.days_since_sunday >= 0 && dt.days_since_sunday <= 6);

    // Calculate year and remaining days
    dt.year = 1970;
    while (true) {
        int days_in_year = IsLeapYear(dt.year) ? 366 : 365;
        if (total_days < days_in_year) break;
        total_days -= days_in_year;
        dt.year++;
        ASSERT(dt.year < 3000);
    }

    // Calculate month and day
    dt.months_since_jan = 0;
    while (dt.months_since_jan < 12) {
        int days_in_month = DaysOfMonth(dt.months_since_jan, dt.year);
        if (total_days < days_in_month) break;
        total_days -= days_in_month;
        dt.months_since_jan++;
        ASSERT(dt.months_since_jan < 12);
    }

    dt.day_of_month = CheckedCast<s8>(total_days + 1);
    ASSERT(dt.day_of_month >= 1 && dt.day_of_month <= 31);

    return dt;
}

TimePoint TimePoint::Now() {
#if __APPLE__
    return TimePoint((s64)clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW));
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts); // signal-safe
    return TimePoint(((s64)ts.tv_sec * (s64)1e+9) + (s64)ts.tv_nsec);
#endif
}

f64 operator-(TimePoint lhs, TimePoint rhs) {
    auto const nanoseconds = lhs.m_time - rhs.m_time;
    return (f64)nanoseconds / 1e+9;
}

TimePoint operator+(TimePoint t, f64 s) { return TimePoint(t.m_time + (s64)SecondsToNanoseconds(s)); }

static constexpr auto k_signals = Array {
    SIGABRT, // "abort", abnormal termination.
    SIGFPE, // floating point exception.
    SIGILL, // "illegal", invalid instruction.
    SIGSEGV, // "segmentation violation", invalid memory access.
    SIGBUS, // Bus error (bad memory access)
    // SIGPIPE, // Broken pipe. Bitwig Studio triggers this often but it doens't seem to be a problem.
    SIGTRAP, // Trace/breakpoint trap
};

u32 g_signals_installed = 0;
static struct sigaction g_previous_signal_actions[k_signals.size] {};

Atomic<CrashHookFunction> g_crash_hook {};

static String SignalString(int signal_num, siginfo_t* info) {
    String message = "unknown signal";
    switch (signal_num) {
        case SIGILL:
            message = "illegal Instruction";
            switch (info->si_code) {
                case ILL_ILLOPC: message = "illegal opcode"; break;
                case ILL_ILLOPN: message = "illegal operand"; break;
                case ILL_ILLADR: message = "illegal addressing mode"; break;
                case ILL_ILLTRP: message = "illegal trap"; break;
                case ILL_PRVOPC: message = "privileged opcode"; break;
                case ILL_PRVREG: message = "privileged register"; break;
                case ILL_COPROC: message = "coprocessor error"; break;
                case ILL_BADSTK: message = "internal stack error"; break;
                default: break;
            }
            break;
        case SIGFPE:
            message = "floating-point exception";
            switch (info->si_code) {
                case FPE_INTDIV: message = "integer divide by zero"; break;
                case FPE_INTOVF: message = "integer overflow"; break;
                case FPE_FLTDIV: message = "floating-point divide by zero"; break;
                case FPE_FLTOVF: message = "floating-point overflow"; break;
                case FPE_FLTUND: message = "floating-point underflow"; break;
                case FPE_FLTRES: message = "floating-point inexact result"; break;
                case FPE_FLTINV: message = "floating-point invalid operation"; break;
                case FPE_FLTSUB: message = "subscript out of range"; break;
                default: break;
            }
            break;
        case SIGSEGV:
            message = "invalid memory reference";
            switch (info->si_code) {
                case SEGV_MAPERR: message = "address not mapped to object"; break;
                case SEGV_ACCERR: message = "invalid permissions for mapped object"; break;
                default: break;
            }
            break;
        case SIGPIPE: message = "broken pipe"; break;
        case SIGBUS:
            message = "bus error";
            switch (info->si_code) {
                case BUS_ADRALN: message = "invalid address alignment"; break;
                case BUS_ADRERR: message = "nonexistent physical address"; break;
                case BUS_OBJERR: message = "object-specific hardware error"; break;
                default: break;
            }
            break;
        case SIGTRAP: message = "trace/breakpoint"; break;
        case SIGABRT: message = "abort() called"; break;
        case SIGTERM: message = "termination request"; break;
        case SIGINT: message = "interactive attention signal"; break;
    }
    return message;
}

constexpr StdStream k_signal_output_stream = StdStream::Err;

static bool IsValidMemoryAddress(void* addr) {
    auto const ptr = (uintptr_t)addr;
    if (ptr == 0) return false;
    if (ptr & (sizeof(void*) - 1)) return false;
    if (ptr < 0x100000 || ptr > 0x7FFFFFFFFFFF) return false;
    return true;
}

static StacktraceStack GetInterruptedStackTrace(ucontext_t* uctx) {
    StacktraceStack result;

    uintptr crash_pc = 0;
    uintptr fp = 0;
    uintptr sp = 0;
    uintptr req_alignment = 0;

    // Platform-specific register extraction
#if defined(__APPLE__)
#if defined(__aarch64__)
    // macOS ARM64
    crash_pc = uctx->uc_mcontext->__ss.__pc;
    fp = uctx->uc_mcontext->__ss.__fp;
    sp = uctx->uc_mcontext->__ss.__sp;
    req_alignment = 16;
#elif defined(__x86_64__)
    // macOS x86_64
    crash_pc = uctx->uc_mcontext->__ss.__rip;
    fp = uctx->uc_mcontext->__ss.__rbp;
    sp = uctx->uc_mcontext->__ss.__rsp;
    req_alignment = 8;
#else
#error "Unsupported architecture on macOS"
#endif
#elif defined(__linux__)
#if defined(__aarch64__)
    // Linux ARM64
    crash_pc = uctx->uc_mcontext.pc;
    fp = uctx->uc_mcontext.regs[29]; // x29 is frame pointer
    sp = uctx->uc_mcontext.sp;
    req_alignment = 16;
#elif defined(__x86_64__)
    // Linux x86_64
    crash_pc = CheckedCast<uintptr>(uctx->uc_mcontext.gregs[REG_RIP]);
    fp = CheckedCast<uintptr>(uctx->uc_mcontext.gregs[REG_RBP]);
    sp = CheckedCast<uintptr>(uctx->uc_mcontext.gregs[REG_RSP]);
    req_alignment = 8;
#else
#error "Unsupported architecture on Linux"
#endif
#else
#error "Unsupported platform"
#endif

    dyn::Append(result, crash_pc);

    while (fp != 0 && result.size < result.Capacity()) {
        if (fp < sp || fp > sp + 0x100000) break;
        if (!IsAligned((void const*)fp, req_alignment)) break;

        auto* frame = (uintptr*)fp;

        if (!IsValidMemoryAddress(frame)) break;

        auto return_addr = frame[1];
        if (return_addr == 0) break;

        dyn::Append(result, return_addr - 1);

        // Move to previous frame
        auto prev_fp = frame[0];
        if (prev_fp <= fp) break; // Prevent infinite loops
        fp = prev_fp;
    }

    return result;
}

// remember we can only use async-signal-safe functions here:
// https://man7.org/linux/man-pages/man7/signal-safety.7.html
static void SignalHandler(int signal_num, siginfo_t* info, void* context) {
    static sig_atomic_t volatile first_call = 1;

    if (first_call) {
        first_call = 0;

        g_in_crash_handler = true;
        auto signal_description = SignalString(signal_num, info);

        if constexpr (!PRODUCTION_BUILD) {
#if IS_LINUX
            psiginfo(info, nullptr);
#else
            auto _ =
                StdPrint(k_signal_output_stream,
                         fmt::FormatInline<200>("Received signal {} ({})\n", signal_num, signal_description));
#endif
        }

        if (auto hook = g_crash_hook.Load(LoadMemoryOrder::Acquire)) {
            auto const stacktrace = GetInterruptedStackTrace((ucontext_t*)(context));
            hook(signal_description, stacktrace);
        }

        for (auto [index, s] : Enumerate(k_signals)) {
            if (s == signal_num) {
                g_in_crash_handler = true;
                auto& prev_action = g_previous_signal_actions[index];

                enum class Type { HandlerFunction, HandlerWithInfoFunction };

                using HandlerFunction = TaggedUnion<
                    Type,
                    TypeAndTag<decltype(prev_action.sa_handler), Type::HandlerFunction>,
                    TypeAndTag<decltype(prev_action.sa_sigaction), Type::HandlerWithInfoFunction>>;

                auto const previous_handler_function = ({
                    HandlerFunction h {decltype(prev_action.sa_handler) {}};
                    if (prev_action.sa_flags & SA_SIGINFO)
                        h = prev_action.sa_sigaction;
                    else
                        h = prev_action.sa_handler;
                    h;
                });

                switch (previous_handler_function.tag) {
                    case Type::HandlerFunction: {
                        auto h = previous_handler_function.GetFromTag<Type::HandlerFunction>();
                        if (h == nullptr) {
                            if constexpr (!PRODUCTION_BUILD)
                                auto _ =
                                    StdPrint(k_signal_output_stream, "Previous signal handler is null\n");
                            _exit(EXIT_FAILURE);
                        } else if (*h == SIG_DFL) {
                            // If the previous is the default action, we set the default active again and
                            // raise it.
                            if constexpr (!PRODUCTION_BUILD)
                                auto _ = StdPrint(k_signal_output_stream, "Calling default signal handler\n");
                            g_in_crash_handler = false;
                            signal(signal_num, SIG_DFL);
                            raise(signal_num);
                        } else if (*h == SIG_IGN) {
                            _exit(EXIT_FAILURE);
                            return;
                        } else if (*h == SIG_ERR) {
                            if constexpr (!PRODUCTION_BUILD)
                                auto _ = StdPrint(k_signal_output_stream, "Error in signal handler\n");
                            _exit(EXIT_FAILURE);
                            return;
                        }
                        break;
                    }
                    case Type::HandlerWithInfoFunction: {
                        auto h = previous_handler_function.GetFromTag<Type::HandlerWithInfoFunction>();
                        if (h == nullptr) {
                            if constexpr (!PRODUCTION_BUILD)
                                auto _ = StdPrint(StdStream::Out, "Previous info signal handler is null\n");
                            _exit(EXIT_FAILURE);
                        }
                        break;
                    }
                }

                if ((prev_action.sa_flags & SA_NODEFER) == 0) sigaddset(&(prev_action.sa_mask), signal_num);
                if ((prev_action.sa_flags & (int)SA_RESETHAND) != 0) prev_action.sa_handler = SIG_DFL;

                sigset_t oset;
                sigemptyset(&oset);
                pthread_sigmask(SIG_SETMASK, &(prev_action.sa_mask), &oset);

                switch (previous_handler_function.tag) {
                    case Type::HandlerFunction: {
                        if constexpr (!PRODUCTION_BUILD)
                            auto _ = StdPrint(k_signal_output_stream, "Calling previous signal handler\n");
                        auto h = previous_handler_function.GetFromTag<Type::HandlerFunction>();
                        g_in_crash_handler = false;
                        (*h)(signal_num);
                        break;
                    }
                    case Type::HandlerWithInfoFunction: {
                        if constexpr (!PRODUCTION_BUILD)
                            auto _ =
                                StdPrint(k_signal_output_stream, "Calling previous info signal handler\n");
                        auto h = previous_handler_function.GetFromTag<Type::HandlerWithInfoFunction>();
                        g_in_crash_handler = false;
                        (*h)(signal_num, info, context);
                        break;
                    }
                }

                pthread_sigmask(SIG_SETMASK, &oset, nullptr);

                break;
            }
        }
    }

    // because of SA_RESETHAND, the signal handler is reset to the default handler
    raise(signal_num);
    _exit(EXIT_FAILURE);
}

bool TrySigFunction(int return_code, String message) {
    if (return_code == 0) return true;

    char buffer[200] = {};
#if IS_LINUX
    auto const err_str = strerror_r(errno, buffer, sizeof(buffer));
#else
    auto _ = strerror_r(errno, buffer, sizeof(buffer));
    auto const err_str = buffer;
#endif
    LogError(ModuleName::Global,
             "failed {}, errno({}) {}",
             message,
             errno,
             FromNullTerminated(err_str ? err_str : buffer));

    return false;
}

void BeginCrashDetection(CrashHookFunction hook) {
    ZoneScoped;
    g_crash_hook.Store(hook, StoreMemoryOrder::Release);

    ++g_signals_installed;
    if (g_signals_installed > 1) return;

    for (auto [index, signal] : Enumerate(k_signals)) {
        struct sigaction action {};
        action.sa_flags =
            (int)(SA_SIGINFO | SA_NODEFER | SA_RESETHAND); // NOLINT(readability-redundant-casting)
        if (!TrySigFunction(sigfillset(&action.sa_mask), "sigfillset")) continue;
        if (!TrySigFunction(sigdelset(&action.sa_mask, signal), "sigdelset")) continue;
        action.sa_sigaction = &SignalHandler;

        TrySigFunction(sigaction(signal, &action, &g_previous_signal_actions[index]), "sigaction");
    }
}

void EndCrashDetection() {
    ZoneScoped;
    ASSERT(g_signals_installed > 0);
    --g_signals_installed;
    if (g_signals_installed != 0) return;

    for (auto [index, signal] : Enumerate(k_signals)) {
        sigaction(signal, &g_previous_signal_actions[index], nullptr);
        g_previous_signal_actions[index] = {};
    }
}
