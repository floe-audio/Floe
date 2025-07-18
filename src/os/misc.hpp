// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "threading.hpp"

ErrorCode ErrnoErrorCode(s64 error_code,
                         char const* info_for_developer = nullptr,
                         SourceLocation source_location = SourceLocation::Current());

// Strings can be empty
struct OsInfo {
    DynamicArrayBounded<char, 48> name; // never empty
    DynamicArrayBounded<char, 48> version;
    DynamicArrayBounded<char, 96> pretty_name;
    DynamicArrayBounded<char, 32> build;
    DynamicArrayBounded<char, 32> kernel_version;
    DynamicArrayBounded<char, 96> distribution_name; // linux only
    DynamicArrayBounded<char, 32> distribution_version; // linux only
    DynamicArrayBounded<char, 96> distribution_pretty_name; // linux only
};
OsInfo GetOsInfo();

String GetFileBrowserAppName();

struct SystemStats {
    static constexpr String Arch() {
        switch (k_arch) {
            case Arch::X86_64: return "x86_64"_s; break;
            case Arch::Aarch64: return "aarch64"_s; break;
        }
        return "";
    }
    u32 num_logical_cpus = 0;
    u32 page_size = 0;
    DynamicArrayBounded<char, 256> cpu_name {};
    double frequency_mhz = 0;
    usize total_ram_bytes = 0;
};

SystemStats GetSystemStats();

PUBLIC SystemStats CachedSystemStats() {
    static SystemStats stats = GetSystemStats();
    return stats;
}

int CurrentProcessId();

void OpenFolderInFileBrowser(String path);
void OpenUrlInBrowser(String url);

u64 RandomSeed();

void* AllocatePages(usize bytes);
void FreePages(void* ptr, usize bytes);
void TryShrinkPages(void* ptr, usize old_size, usize new_size);

void* AlignedAlloc(usize alignment, usize size);
void AlignedFree(void* ptr);

// NOT thread-safe. Use only in single-threaded contexts. We use getenv() on Unix.
Optional<MutableString> GetEnvironmentVariable(char const* name, Allocator& a);
Optional<MutableString> GetEnvironmentVariable(String name, Allocator& a);

// LockableSharedMemory is never closed, we rely on the OS to clean it up which usually happens after reboot.
// The memory is shared between processes.
struct LockableSharedMemory {
    Span<u8> data; // initialised to 0
    OpaqueHandle<IS_WINDOWS ? 16 : 8> native;
};

// name must be alphanumeric 32 characters or less
ErrorCodeOr<LockableSharedMemory> CreateLockableSharedMemory(String name, usize size);
void LockSharedMemory(LockableSharedMemory& memory);
void UnlockSharedMemory(LockableSharedMemory& memory);

enum class LibraryHandle : uintptr {};
ErrorCodeOr<LibraryHandle> LoadLibrary(String path);
ErrorCodeOr<void*> SymbolFromLibrary(LibraryHandle library, String symbol_name);
void UnloadLibrary(LibraryHandle library);

bool IsRunningUnderWine();

void WindowsRaiseException(u32 code);

// We can also use __builtin_debugtrap().
bool IsRunningUnderDebugger();

class Malloc final : public Allocator {
  public:
    Span<u8> DoCommand(AllocatorCommandUnion const& command) override {
        CheckAllocatorCommandIsValid(command);

        switch (command.tag) {
            case AllocatorCommand::Allocate: {
                auto const& cmd = command.Get<AllocateCommand>();
                auto ptr = AlignedAlloc(cmd.alignment, cmd.size);
                if (ptr == nullptr) Panic("out of memory");
                return {(u8*)ptr, cmd.size};
            }
            case AllocatorCommand::Free: {
                auto const& cmd = command.Get<FreeCommand>();
                if constexpr (RUNTIME_SAFETY_CHECKS_ON)
                    FillMemory(cmd.allocation.data, 0xCD, cmd.allocation.size);
                AlignedFree(cmd.allocation.data);
                break;
            }
            case AllocatorCommand::Resize: {
                auto const& cmd = command.Get<ResizeCommand>();

                if (cmd.new_size > cmd.allocation.size) {
                    // IMPROVE: use realloc if no move_mem

                    auto const alignment =
                        cmd.allocation.data
                            ? Max((uintptr)cmd.allocation.data & (~(uintptr)cmd.allocation.data - 1),
                                  k_max_alignment)
                            : k_max_alignment;

                    // fallback: new allocation and move memory
                    auto new_allocation = AlignedAlloc(alignment, cmd.new_size);
                    if (cmd.move_memory_handler.function)
                        cmd.move_memory_handler.function({.context = cmd.move_memory_handler.context,
                                                          .destination = new_allocation,
                                                          .source = cmd.allocation.data,
                                                          .num_bytes = cmd.allocation.size});
                    AlignedFree(cmd.allocation.data);

                    return {(u8*)new_allocation, cmd.new_size};
                } else if (cmd.new_size < cmd.allocation.size) {
                    // IMPROVE: use realloc

                    return {cmd.allocation.data, cmd.new_size};
                } else {
                    return cmd.allocation;
                }
            }
        }
        return {};
    }

    static Allocator& Instance() {
        static Malloc a;
        return a;
    }
};

// Allocate whole pages at a time: 4kb or 16kb each; this is the smallest size that the OS gives out.
class PageAllocator final : public Allocator {
    static usize AlignUpToPageSize(usize size) { return AlignForward(size, CachedSystemStats().page_size); }

  public:
    Span<u8> DoCommand(AllocatorCommandUnion const& command_union) {
        CheckAllocatorCommandIsValid(command_union);

        switch (command_union.tag) {
            case AllocatorCommand::Allocate: {
                auto const& cmd = command_union.Get<AllocateCommand>();

                Span<u8> result;
                auto const request_page_size = AlignUpToPageSize(cmd.size);
                auto mem = AllocatePages(request_page_size);
                if (mem == nullptr) Panic("out of memory");
                result = {(u8*)mem, request_page_size};

                ASSERT_EQ(__builtin_align_up(result.data, cmd.alignment), result.data);

                return {result.data, cmd.allow_oversized_result ? result.size : cmd.size};
            }

            case AllocatorCommand::Free: {
                auto const& cmd = command_union.Get<FreeCommand>();
                if (cmd.allocation.size == 0) return {};

                FreePages(cmd.allocation.data, AlignUpToPageSize(cmd.allocation.size));
                return {};
            }

            case AllocatorCommand::Resize: {
                auto const& cmd = command_union.Get<ResizeCommand>();

                if (cmd.new_size < cmd.allocation.size) {
                    TryShrinkPages(cmd.allocation.data, AlignUpToPageSize(cmd.allocation.size), cmd.new_size);
                    return {cmd.allocation.data, cmd.new_size};
                } else if (cmd.new_size > cmd.allocation.size) {
                    // IMPROVE: can the OS grow the page?

                    return ResizeUsingNewAllocation(cmd, k_max_alignment);
                } else {
                    return cmd.allocation;
                }
            }
        }
        return {};
    }

    static Allocator& Instance() {
        static PageAllocator a;
        return a;
    }
};

enum class StdStream { Out, Err };

// Unbuffered, signal-safe on Unix
ErrorCodeOr<void> StdPrint(StdStream stream, String str);
Writer StdWriter(StdStream stream);

Mutex& StdStreamMutex(StdStream stream);

template <typename... Args>
void StdPrintF(StdStream stream, String format, Args const&... args) {
    auto _ = fmt::FormatToWriter(StdWriter(stream), format, args...);
}
template <typename... Args>
void StdPrintFLocked(StdStream stream, String format, Args const&... args) {
    auto& mutex = StdStreamMutex(stream);
    mutex.Lock();
    auto _ = fmt::FormatToWriter(StdWriter(stream), format, args...);
    mutex.Unlock();
}

ErrorCodeOr<String> ReadAllStdin(Allocator& allocator);

s128 NanosecondsSinceEpoch(); // signal-safe
s64 MicrosecondsSinceEpoch(); // signal-safe
s128 NanosecondsSinceEpoch(DateAndTime const&);
DateAndTime LocalTimeFromNanosecondsSinceEpoch(s128 nanoseconds); // not signal-safe
DateAndTime UtcTimeFromNanosecondsSinceEpoch(s128 nanoseconds); // signal-safe

PUBLIC inline DateAndTime LocalTimeNow() {
    return LocalTimeFromNanosecondsSinceEpoch(NanosecondsSinceEpoch());
}

PUBLIC inline DateAndTime UtcTimeNow() { return UtcTimeFromNanosecondsSinceEpoch(NanosecondsSinceEpoch()); }
PUBLIC fmt::TimestampRfc3339UtcArray TimestampRfc3339UtcNow() {
    return fmt::TimestampRfc3339Utc(UtcTimeNow());
}

inline DateAndTime LocalTimeFromMicrosecondsSinceEpoch(s64 microseconds) {
    return LocalTimeFromNanosecondsSinceEpoch((s128)microseconds * 1'000);
}
inline DateAndTime UtcTimeFromMicrosecondsSinceEpoch(s64 microseconds) {
    return UtcTimeFromNanosecondsSinceEpoch((s128)microseconds * 1'000);
}

DynamicArrayBounded<char, fmt::k_timestamp_str_size> Timestamp(); // not signal-safe
DynamicArrayBounded<char, fmt::k_timestamp_str_size> TimestampUtc(); // signal-safe

// RFC 3339, YYYY-MM-DDThh:mm:ss.sssZ
ErrorCodeOr<void> IsoUtcTimestamp(DateAndTime date, Writer writer);

// A point in time. It has no defined reference. You can't get seconds-from-Epoch from it, for example.
class TimePoint {
  public:
    TimePoint() {}
    explicit TimePoint(s64 t) : m_time(t) {}
    static TimePoint Now();

    f64 SecondsFromNow() const { return Now() - *this; }
    s64 Raw() const { return m_time; }

    // returns seconds
    friend f64 operator-(TimePoint lhs, TimePoint rhs);
    friend TimePoint operator+(TimePoint t, f64 seconds);

    explicit operator bool() const { return m_time != 0; }

    constexpr bool operator<(TimePoint const& other) const { return m_time < other.m_time; }
    constexpr bool operator<=(TimePoint const& other) const { return m_time <= other.m_time; }
    constexpr bool operator>(TimePoint const& other) const { return m_time > other.m_time; }
    constexpr bool operator>=(TimePoint const& other) const { return m_time >= other.m_time; }
    constexpr bool operator==(TimePoint const& other) const { return m_time == other.m_time; }

  private:
    s64 m_time {};
};

f64 operator-(TimePoint lhs, TimePoint rhs);
TimePoint operator+(TimePoint t, f64 s);

struct Stopwatch {
    Stopwatch() : start(TimePoint::Now()) {}
    f64 SecondsElapsed() const { return TimePoint::Now() - start; }
    f64 MicrosecondsElapsed() const { return SecondsToMicroseconds(SecondsElapsed()); }
    f64 MillisecondsElapsed() const { return SecondsToMilliseconds(SecondsElapsed()); }
    void Reset() { start = TimePoint::Now(); }

    TimePoint start;
};

namespace fmt {

PUBLIC ErrorCodeOr<void> CustomValueToString(Writer writer, Stopwatch value, FormatOptions options) {
    char buffer[32];
    auto const size =
        CheckedCast<usize>(stbsp_snprintf(buffer, sizeof(buffer), "%.4f ms", value.MillisecondsElapsed()));
    TRY(PadToRequiredWidthIfNeeded(writer, options, size));
    return writer.WriteChars({buffer, size});
}

} // namespace fmt
