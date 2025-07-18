// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h> // needs to be first
//
#include <dbghelp.h>
#include <errhandlingapi.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <shellapi.h>
#include <versionhelpers.h>

//
#include "os/undef_windows_macros.h"
//

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc_windows.hpp"
#include "os/threading.hpp"
#include "tests/framework.hpp"
#include "utils/debug/debug.hpp"

#include "misc.hpp"
#include "misc_windows.hpp"

// This function based on code from the JUCE core library
// https://github.com/juce-framework/JUCE/blob/master/modules/juce_foundation/native/juce_SystemStats_windows.cpp
// Copyright (c) 2022 - Raw Material Software Limited
// SPDX-License-Identifier: ISC
OsInfo GetOsInfo() {
    auto const windows_version_info = []() -> RTL_OSVERSIONINFOW {
        RTL_OSVERSIONINFOW version_info = {};

        if (auto* module_handle = ::GetModuleHandleW(L"ntdll.dll")) {
            using RtlGetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

            if (auto* rtl_get_version =
                    (RtlGetVersion)(void*)::GetProcAddress(module_handle, "RtlGetVersion")) {
                version_info.dwOSVersionInfoSize = sizeof(version_info);
                LONG status_success = 0;

                if (rtl_get_version(&version_info) != status_success) version_info = {};
            }
        }

        return version_info;
    };

    auto const version_info = windows_version_info();
    auto const major = version_info.dwMajorVersion;
    auto const minor = version_info.dwMinorVersion;
    auto const build = version_info.dwBuildNumber;

    OsInfo result {};
    result.name = "Windows"_s;
    fmt::Assign(result.version, "{}.{}.{}"_s, major, minor, build);

    // pretty name
    if (!PRODUCTION_BUILD) ASSERT(major <= 10); // update so we have a pretty name for new versions

    if (major == 10 && build >= 22000)
        result.pretty_name = "Windows 11"_s;
    else if (major == 10)
        result.pretty_name = "Windows 10"_s;
    else if (major == 6 && minor == 3)
        result.pretty_name = "Windows 8.1"_s;
    else if (major == 6 && minor == 2)
        result.pretty_name = "Windows 8"_s;
    else if (major == 6 && minor == 1)
        result.pretty_name = "Windows 7"_s;
    else if (major == 6 && minor == 0)
        result.pretty_name = "Windows Vista"_s;
    else if (major == 5 && minor == 1)
        result.pretty_name = "Windows XP"_s;
    else if (major == 5 && minor == 0)
        result.pretty_name = "Windows 2000"_s;

    fmt::Assign(result.build, "{}"_s, build);

    return result;
}

Optional<MutableString> GetEnvironmentVariable(char const* name, Allocator& a) {
    return GetEnvironmentVariable(FromNullTerminated(name), a);
}

Optional<MutableString> GetEnvironmentVariable(String name, Allocator& a) {
    ArenaAllocatorWithInlineStorage<4000> scratch {PageAllocator::Instance()};
    auto const w_name = WidenAllocNullTerm(scratch, name).Value();
    auto const size = GetEnvironmentVariableW(w_name.data, nullptr, 0);
    if (size == 0) return k_nullopt;
    auto const buffer = scratch.AllocateExactSizeUninitialised<WCHAR>(size);
    GetEnvironmentVariableW(w_name.data, buffer.data, size);
    return Narrow(a, buffer).Value();
}

u64 RandomSeed() {
    auto const pid = GetCurrentProcessId();
    auto const tid = GetCurrentThreadId();
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);
    return (u64)pid + (u64)tid + (u64)ticks.QuadPart;
}

bool IsRunningUnderWine() {
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        if (GetProcAddress(nt, "wine_get_version")) return true;
    }
    return false;
}

void WindowsRaiseException(u32 code) { RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr); }

void* AlignedAlloc(usize alignment, usize size) {
    ASSERT(IsPowerOfTwo(alignment));
    return _aligned_malloc(size, alignment);
}
void AlignedFree(void* ptr) { return _aligned_free(ptr); }

void* AllocatePages(usize bytes) {
    auto p = VirtualAlloc(nullptr, (DWORD)bytes, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    TracyAlloc(p, bytes);
    return p;
}

void FreePages(void* ptr, usize bytes) {
    (void)bytes; // VirtualFree requires the size to be 0 when MEM_RELEASE is used
    TracyFree(ptr);
    auto result = VirtualFree(ptr, 0, MEM_RELEASE);
    ASSERT(result != 0, "VirtualFree failed");
}

void TryShrinkPages(void* ptr, usize old_size, usize new_size) {
    (void)ptr;
    (void)old_size;
    (void)new_size;
    // IMPROVE: actually shrink the memory
}

ErrorCodeOr<String> ReadAllStdin(Allocator& allocator) {
    DynamicArray<char> result {allocator};
    HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    if (stdin_handle == INVALID_HANDLE_VALUE) return Win32ErrorCode(GetLastError(), "GetStdHandle");

    while (true) {
        char buffer[4096];
        DWORD num_read = 0;
        if (!ReadFile(stdin_handle, buffer, sizeof(buffer), &num_read, nullptr))
            return Win32ErrorCode(GetLastError(), "ReadFile");
        if (num_read == 0) break;
        dyn::AppendSpan(result, Span {buffer, num_read});
    }

    return result.ToOwnedSpan();
}

struct LockableSharedMemoryNative {
    HANDLE mutex;
    HANDLE mapping;
};

ErrorCodeOr<LockableSharedMemory> CreateLockableSharedMemory(String name, usize size) {
    ASSERT(name.size <= 32);
    LockableSharedMemory result {};
    auto& native = result.native.As<LockableSharedMemoryNative>();

    bool success = false;

    auto const mutex_name = fmt::FormatInline<40>("Global\\{}_mutex", name);

    // Global mapping requires SeCreateGlobalPrivilege, which doesn't seems available sometimes. Using Local
    // instead should be fine since I don't think we need to share memory between different terminal server
    // sessions.
    auto const mapping_name = fmt::FormatInline<40>("Local\\{}_mapping", name);

    native.mutex = CreateMutexA(nullptr, FALSE, mutex_name.data);
    if (!native.mutex) return Win32ErrorCode(GetLastError(), "CreateMutexA");
    DEFER {
        if (!success) CloseHandle(native.mutex);
    };

    // Lock
    if (WaitForSingleObject(native.mutex, INFINITE) != WAIT_OBJECT_0)
        return Win32ErrorCode(GetLastError(), "WaitForSingleObject");

    DEFER { ReleaseMutex(native.mutex); };

    native.mapping = CreateFileMappingA(INVALID_HANDLE_VALUE,
                                        nullptr,
                                        PAGE_READWRITE,
                                        (DWORD)((size >> 32) & 0xFFFFFFFF),
                                        (DWORD)(size & 0xFFFFFFFF),
                                        mapping_name.data);
    if (!native.mapping) return Win32ErrorCode(GetLastError(), "CreateFileMappingA");
    DEFER {
        if (!success) CloseHandle(native.mapping);
    };

    bool const created = GetLastError() != ERROR_ALREADY_EXISTS;

    // Map view of the file
    void* data = MapViewOfFile(native.mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);

    if (!data) return Win32ErrorCode(GetLastError(), "MapViewOfFile");

    // Initialize memory if we created it
    if (created) FillMemory(data, 0, size);

    result.data = {(u8*)data, size};
    success = true;
    return result;
}

void LockSharedMemory(LockableSharedMemory& memory) {
    auto& native = memory.native.As<LockableSharedMemoryNative>();
    WaitForSingleObject(native.mutex, INFINITE);
}

void UnlockSharedMemory(LockableSharedMemory& memory) {
    auto& native = memory.native.As<LockableSharedMemoryNative>();
    ReleaseMutex(native.mutex);
}

ErrorCodeOr<LibraryHandle> LoadLibrary(String path) {
    PathArena temp_allocator {Malloc::Instance()};
    auto const w_path = TRY(path::MakePathForWin32(path, temp_allocator, true));
    auto handle = LoadLibraryW(w_path.path.data);
    if (handle == nullptr) return Win32ErrorCode(GetLastError(), "LoadLibrary");
    return (LibraryHandle)(uintptr)handle;
}

ErrorCodeOr<void*> SymbolFromLibrary(LibraryHandle library, String symbol_name) {
    ArenaAllocatorWithInlineStorage<200> temp_allocator {Malloc::Instance()};
    auto result = GetProcAddress((HMODULE)library, NullTerminated(symbol_name, temp_allocator));
    if (result == nullptr) return Win32ErrorCode(GetLastError(), "GetProcAddress");
    return (void*)result;
}

void UnloadLibrary(LibraryHandle library) {
    auto const result = FreeLibrary((HMODULE)(uintptr)library);
    ASSERT(result);
}

int CurrentProcessId() { return _getpid(); }

static String ExceptionCodeString(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
            return "EXCEPTION_ACCESS_VIOLATION: The thread tried to read from or write to a virtual address for which it does not have the appropriate access.";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED: The thread tried to access an array element that is out of bounds and the underlying hardware supports bounds checking.";
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "EXCEPTION_DATATYPE_MISALIGNMENT: The thread tried to read or write data that is misaligned on hardware that does not provide alignment. For example, 16-bit values must be aligned on 2-byte boundaries; 32-bit values on 4-byte boundaries, and so on.";
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            return "EXCEPTION_FLT_DENORMAL_OPERAND: One of the operands in a floating-point operation is denormal. A denormal value is one that is too small to represent as a standard floating-point value.";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            return "EXCEPTION_FLT_DIVIDE_BY_ZERO: The thread tried to divide a floating-point value by a floating-point divisor of zero.";
        case EXCEPTION_FLT_INEXACT_RESULT:
            return "EXCEPTION_FLT_INEXACT_RESULT: The result of a floating-point operation cannot be represented exactly as a decimal fraction.";
        case EXCEPTION_FLT_INVALID_OPERATION:
            return "EXCEPTION_FLT_INVALID_OPERATION: This exception represents any floating-point exception not included in this list.";
        case EXCEPTION_FLT_OVERFLOW:
            return "EXCEPTION_FLT_OVERFLOW: The exponent of a floating-point operation is greater than the magnitude allowed by the corresponding type.";
        case EXCEPTION_FLT_STACK_CHECK:
            return "EXCEPTION_FLT_STACK_CHECK: The stack overflowed or underflowed as the result of a floating-point operation.";
        case EXCEPTION_FLT_UNDERFLOW:
            return "EXCEPTION_FLT_UNDERFLOW: The exponent of a floating-point operation is less than the magnitude allowed by the corresponding type.";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "EXCEPTION_ILLEGAL_INSTRUCTION: The thread tried to execute an invalid instruction.";
        case EXCEPTION_IN_PAGE_ERROR:
            return "EXCEPTION_IN_PAGE_ERROR: The thread tried to access a page that was not present, and the system was unable to load the page. For example, this exception might occur if a network connection is lost while running a program over the network.";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "EXCEPTION_INT_DIVIDE_BY_ZERO: The thread tried to divide an integer value by an integer divisor of zero.";
        case EXCEPTION_INT_OVERFLOW:
            return "EXCEPTION_INT_OVERFLOW: The result of an integer operation caused a carry out of the most significant bit of the result.";
        case EXCEPTION_INVALID_DISPOSITION:
            return "EXCEPTION_INVALID_DISPOSITION: An exception handler returned an invalid disposition to the exception dispatcher. Programmers using a high-level language such as C should never encounter this exception.";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            return "EXCEPTION_NONCONTINUABLE_EXCEPTION: The thread tried to continue execution after a noncontinuable exception occurred.";
        case EXCEPTION_PRIV_INSTRUCTION:
            return "EXCEPTION_PRIV_INSTRUCTION: The thread tried to execute an instruction whose operation is not allowed in the current machine mode.";
        case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW: The thread used up its stack. ";
        case k_windows_nested_panic_code: return "Floe nested panic exception";
    }
    return {};
}

bool IsValidMemoryAddressWindows(void* addr) {
    if (addr == nullptr) return false;

    auto const ptr = (uintptr)addr;

    if (ptr & (sizeof(void*) - 1)) return false;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) return false;

    // Check if memory is committed and readable
    if (mbi.State != MEM_COMMIT) return false;
    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
        return false;

    return true;
}

StacktraceStack GetInterruptedStackTraceWindows(PEXCEPTION_POINTERS exception_info) {
    StacktraceStack result;

    PCONTEXT context = exception_info->ContextRecord;
    uintptr crash_pc = 0;
    uintptr fp = 0;
    uintptr sp = 0;
    uintptr req_alignment = 0;

#if defined(_M_X64) || defined(__x86_64__)
    // x86_64 Windows
    crash_pc = context->Rip;
    fp = context->Rbp;
    sp = context->Rsp;
    req_alignment = 8;
#elif defined(_M_ARM64) || defined(__aarch64__)
    // ARM64 Windows
    crash_pc = context->Pc;
    fp = context->Fp; // x29
    sp = context->Sp;
    req_alignment = 16;
#error "Unsupported architecture on Windows"
#endif

    // Start with the crash PC
    dyn::Append(result, crash_pc);

    while (fp != 0 && result.size < result.Capacity()) {
        if (fp < sp || fp > sp + 0x100000) break;
        if (!IsAligned((void const*)fp, req_alignment)) break;

        auto* frame = (uintptr*)fp;

        if (!IsValidMemoryAddressWindows(frame)) break;

        auto return_addr = frame[1];
        if (return_addr == 0) break;

        dyn::Append(result, return_addr - 1);

        auto prev_fp = frame[0];
        if (prev_fp <= fp) break; // Prevent infinite loops
        fp = prev_fp;
    }

    return result;
}

static void* g_exception_handler = nullptr;
static Atomic<CrashHookFunction> g_crash_hook {};
static CountedInitFlag g_crash_hook_init_flag {};

// IMPROVE: support signal handling as well.
// On Windows, vectored exception handlers are called for some hardware exceptions and win32 exceptions
// (RaiseException, SEH). For example, a null pointer dereference will trigger this exception. However, we
// should also handle signals like SIGABRT, SIGSEGV, etc.
void BeginCrashDetection(CrashHookFunction hook) {
    CountedInit(g_crash_hook_init_flag, [hook]() {
        g_crash_hook.Store(hook, StoreMemoryOrder::Release);
        g_exception_handler = AddVectoredExceptionHandler(1, [](PEXCEPTION_POINTERS exception_info) -> LONG {
            g_in_crash_handler = true;
            DEFER { g_in_crash_handler = false; };

            auto const msg = ExceptionCodeString(exception_info->ExceptionRecord->ExceptionCode);

            // Some exceptions are expected and should be ignored; for example Lua will trigger exceptions.
            if (!msg.size) return EXCEPTION_CONTINUE_SEARCH;

            if (auto hook = g_crash_hook.Load(LoadMemoryOrder::Acquire)) {
                auto const stacktrace = GetInterruptedStackTraceWindows(exception_info);
                hook(msg, stacktrace);
            }

            return EXCEPTION_CONTINUE_SEARCH;
        });
    });
}
void EndCrashDetection() {
    CountedDeinit(g_crash_hook_init_flag, [] {
        if (g_exception_handler) RemoveVectoredExceptionHandler(g_exception_handler);
    });
}

ErrorCodeOr<void> StdPrint(StdStream stream, String str) {
    static CallOnceFlag flag;
    CallOnce(flag, []() { SetConsoleOutputCP(CP_UTF8); });

    DWORD bytes_written;
    auto const result = WriteFile(GetStdHandle(({
                                      DWORD h = STD_OUTPUT_HANDLE;
                                      switch (stream) {
                                          case StdStream::Out: h = STD_OUTPUT_HANDLE; break;
                                          case StdStream::Err: h = STD_ERROR_HANDLE; break;
                                      }
                                      h;
                                  })),
                                  str.data,
                                  (DWORD)str.size,
                                  &bytes_written,
                                  nullptr);
    if (!result) return Win32ErrorCode(GetLastError(), "StdPrint WriteFile");
    return k_success;
}

// the FILETIME structure represents the number of 100-nanosecond intervals since January 1, 1601 UTC
constexpr s64 k_epoch_offset = 116444736000000000;

s128 NanosecondsSinceEpoch() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER const li {
        .LowPart = ft.dwLowDateTime,
        .HighPart = ft.dwHighDateTime,
    };
    return ((s128)(li.QuadPart) - k_epoch_offset) * 100;
}

s64 MicrosecondsSinceEpoch() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER const li {
        .LowPart = ft.dwLowDateTime,
        .HighPart = ft.dwHighDateTime,
    };
    return ((s64)(li.QuadPart) - k_epoch_offset) / 10;
}

DateAndTime LocalTimeFromNanosecondsSinceEpoch(s128 nanoseconds) {
    ULARGE_INTEGER const li {
        .QuadPart = (ULONGLONG)(nanoseconds / 100) + k_epoch_offset,
    };
    FILETIME const gmt {
        .dwLowDateTime = li.LowPart,
        .dwHighDateTime = li.HighPart,
    };

    FILETIME local;
    FileTimeToLocalFileTime(&gmt, &local);

    // IMPROVE: find a way to get beyond millisecond precision
    SYSTEMTIME st;
    FileTimeToSystemTime(&local, &st);
    return {
        .year = (s16)st.wYear,
        .months_since_jan = (s8)(st.wMonth - 1),
        .day_of_month = (s8)st.wDay,
        .days_since_sunday = (s8)st.wDayOfWeek,
        .hour = (s8)st.wHour,
        .minute = (s8)st.wMinute,
        .second = (s8)st.wSecond,
        .millisecond = (s16)st.wMilliseconds,
        .microsecond = 0,
        .nanosecond = 0,
    };
}

DateAndTime UtcTimeFromNanosecondsSinceEpoch(s128 nanoseconds) {
    ULARGE_INTEGER const li {
        .QuadPart = (ULONGLONG)(nanoseconds / 100) + k_epoch_offset,
    };
    FILETIME const gmt {
        .dwLowDateTime = li.LowPart,
        .dwHighDateTime = li.HighPart,
    };

    // IMPROVE: find a way to get beyond millisecond precision
    SYSTEMTIME st;
    FileTimeToSystemTime(&gmt, &st);
    return {
        .year = (s16)st.wYear,
        .months_since_jan = (s8)(st.wMonth - 1),
        .day_of_month = (s8)st.wDay,
        .days_since_sunday = (s8)st.wDayOfWeek,
        .hour = (s8)st.wHour,
        .minute = (s8)st.wMinute,
        .second = (s8)st.wSecond,
        .millisecond = (s16)st.wMilliseconds,
        .microsecond = 0,
        .nanosecond = 0,
    };
}

TimePoint TimePoint::Now() {
    s64 result;
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    result = counter.QuadPart;
    return TimePoint(result);
}

static f64 CountsPerSecond() {
    static f64 result = []() {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        return (f64)frequency.QuadPart;
    }();
    return result;
}

f64 operator-(TimePoint lhs, TimePoint rhs) { return (f64)(lhs.m_time - rhs.m_time) / CountsPerSecond(); }

TimePoint operator+(TimePoint t, f64 s) {
    t.m_time += (s64)(s * CountsPerSecond());
    return t;
}

String GetFileBrowserAppName() { return "File Explorer"; }

SystemStats GetSystemStats() {
    SystemStats result {};
    SYSTEM_INFO system_info {};
    GetNativeSystemInfo(&system_info);
    result.num_logical_cpus = (u32)system_info.dwNumberOfProcessors;
    result.page_size = (u32)system_info.dwPageSize;
    ASSERT(result.page_size);

    {
        MEMORYSTATUSEX mem_status {};
        mem_status.dwLength = sizeof(mem_status);
        if (GlobalMemoryStatusEx(&mem_status)) result.total_ram_bytes = mem_status.ullTotalPhys;
    }

    HKEY hkey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0,
                      KEY_READ,
                      &hkey) == ERROR_SUCCESS) {
        // Get CPU Name
        Array<WCHAR, result.cpu_name.Capacity()> wide_name;
        DWORD size_bytes = sizeof(wide_name);
        if (RegQueryValueExW(hkey,
                             L"ProcessorNameString",
                             nullptr,
                             nullptr,
                             (u8*)wide_name.data,
                             &size_bytes) == ERROR_SUCCESS) {
            auto const size = size_bytes / sizeof(WCHAR);
            if (((size_bytes % sizeof(WCHAR)) == 0) && size && size <= wide_name.size) {
                Array<char, MaxNarrowedStringSize(wide_name.size)> narrow_name;
                if (auto const narrow_size =
                        NarrowToBuffer(narrow_name.data, {(const WCHAR*)wide_name.data, size})) {
                    dyn::Assign(result.cpu_name, String {narrow_name.data, *narrow_size});
                    while (result.cpu_name.size &&
                           (Last(result.cpu_name) == '\0' || IsSpacing(Last(result.cpu_name))))
                        result.cpu_name.size--;
                    ASSERT(IsValidUtf8(result.cpu_name));
                }
            }
        }

        // Get CPU Frequency
        size_bytes = sizeof(DWORD);
        DWORD mhz;
        if (RegQueryValueExW(hkey, L"~MHz", nullptr, nullptr, (LPBYTE)&mhz, &size_bytes) == ERROR_SUCCESS)
            result.frequency_mhz = mhz;

        RegCloseKey(hkey);
    }

    return result;
}

static void WindowsShellExecute(String arg) {
    DynamicArray<wchar_t> wide_arg {Malloc::Instance()};
    if (WidenAppend(wide_arg, arg)) {
        dyn::Append(wide_arg, L'\0');
        // We do this in a separate thread because ShellExecuteW is really slow
        Thread thread;
        thread.Start(
            [wide_arg = wide_arg.ToOwnedSpan()]() mutable {
                try {
                    DEFER { Malloc::Instance().Free(wide_arg.ToByteSpan()); };

                    auto const com_init_hr =
                        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                    DEFER {
                        if (SUCCEEDED(com_init_hr)) CoUninitialize();
                    };

                    ShellExecuteW(nullptr, L"open", wide_arg.data, nullptr, nullptr, SW_SHOW);
                } catch (PanicException) {
                    // pass
                }
            },
            "shellexe");
        thread.Detach();
    }
}

void OpenFolderInFileBrowser(String path) { WindowsShellExecute(path); }
void OpenUrlInBrowser(String url) { WindowsShellExecute(url); }

bool IsRunningUnderDebugger() { return IsDebuggerPresent(); }

static constexpr ErrorCodeCategory k_error_category {
    .category_id = "WIN",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        WCHAR buf[200];
        DWORD num_chars_written = 0;

        // Try languages in order: US English first, then default language
        for (auto const lang_id :
             Array {(DWORD)MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), LANG_SYSTEM_DEFAULT}) {
            num_chars_written = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                               nullptr,
                                               (DWORD)code.code,
                                               lang_id,
                                               buf,
                                               (DWORD)ArraySize(buf),
                                               nullptr);

            if (num_chars_written == 0)
                if (GetLastError() == ERROR_MUI_FILE_NOT_FOUND)
                    continue; // Try again if the language is not available.
                else
                    break; // Some other error occurred, stop trying.
            else
                break; // Success.
        }

        if (num_chars_written) {
            WString error_text_wide {(const WCHAR*)buf, num_chars_written};
            while (error_text_wide.size && (Last(error_text_wide) == L'\n' || Last(error_text_wide) == L'\r'))
                error_text_wide.size--;
            ArenaAllocatorWithInlineStorage<sizeof(buf) * 4> temp_allocator {Malloc::Instance()};
            if (auto o = Narrow(temp_allocator, error_text_wide); o.HasValue())
                return writer.WriteChars(o.Value());
        }
        return fmt::FormatToWriter(writer, "FormatMessage failed: {}", GetLastError());
    }};

ErrorCode Win32ErrorCode(DWORD win32_code, char const* extra_debug_info, SourceLocation loc) {
    return ErrorCode {k_error_category, (s64)win32_code, extra_debug_info, loc};
}

//=================================================
//  _______        _
// |__   __|      | |
//    | | ___  ___| |_ ___
//    | |/ _ \/ __| __/ __|
//    | |  __/\__ \ |_\__ \
//    |_|\___||___/\__|___/
//
//=================================================

TEST_CASE(TestWindowsErrors) {
    auto& a = tester.scratch_arena;

    SUBCASE("Win32") {
        auto e = Win32ErrorCode(ERROR_TOO_MANY_OPEN_FILES, "ERROR_TOO_MANY_OPEN_FILES");
        auto message = fmt::Format(a, "{}", e);
        REQUIRE(message.size);
        tester.log.Debug("{}", e);
    }

    SUBCASE("Win32 HRESULT") {
        auto e = HresultErrorCode(E_OUTOFMEMORY, "E_OUTOFMEMORY");
        auto message = fmt::Format(a, "{}", e);
        REQUIRE(message.size);
        tester.log.Debug("{}", e);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterWindowsSpecificTests) { REGISTER_TEST(TestWindowsErrors); }
