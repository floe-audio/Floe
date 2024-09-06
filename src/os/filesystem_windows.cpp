// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
//
#include <aclapi.h>
#include <dbghelp.h>
#include <fileapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <winnt.h>

//
#include "os/undef_windows_macros.h"
//

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "os/misc_windows.hpp"
#include "utils/debug/tracy_wrapped.hpp"
#include "utils/logger/logger.hpp"

#include "filesystem.hpp"

static constexpr Optional<FilesystemError> TranslateWin32Code(DWORD win32_code) {
    switch (win32_code) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: return FilesystemError::PathDoesNotExist;
        case ERROR_TOO_MANY_OPEN_FILES: return FilesystemError::TooManyFilesOpen;
        case ERROR_ACCESS_DENIED: return FilesystemError::AccessDenied;
        case ERROR_SHARING_VIOLATION: return FilesystemError::AccessDenied;
        case ERROR_ALREADY_EXISTS: return FilesystemError::PathAlreadyExists;
        case ERROR_FILE_EXISTS: return FilesystemError::PathAlreadyExists;
        case ERROR_NOT_SAME_DEVICE: return FilesystemError::DifferentFilesystems;
        case ERROR_HANDLE_DISK_FULL: return FilesystemError::DiskFull;
        case ERROR_PATH_BUSY: return FilesystemError::FilesystemBusy;
        case ERROR_DIR_NOT_EMPTY: return FilesystemError::NotEmpty;
    }
    return {};
}

static ErrorCode FilesystemWin32ErrorCode(DWORD win32_code,
                                          char const* extra_debug_info = nullptr,
                                          SourceLocation loc = SourceLocation::Current()) {
    if (auto code = TranslateWin32Code(win32_code)) return ErrorCode(code.Value(), extra_debug_info, loc);
    return Win32ErrorCode(win32_code, extra_debug_info, loc);
}

ErrorCodeOr<void> File::Lock(FileLockType type) {
    DWORD flags = 0;
    switch (type) {
        case FileLockType::Exclusive: flags = LOCKFILE_EXCLUSIVE_LOCK; break;
        case FileLockType::Shared: flags = 0; break;
    }

    OVERLAPPED overlapped {};
    if (!LockFileEx(m_file, flags, 0, MAXDWORD, MAXDWORD, &overlapped))
        return FilesystemWin32ErrorCode(GetLastError(), "LockFileEx");
    return k_success;
}

ErrorCodeOr<void> File::Unlock() {
    OVERLAPPED overlapped {};
    if (!UnlockFileEx(m_file, 0, MAXDWORD, MAXDWORD, &overlapped))
        return FilesystemWin32ErrorCode(GetLastError(), "UnlockFileEx");
    return k_success;
}

void File::CloseFile() {
    if (m_file) CloseHandle(m_file);
}

ErrorCodeOr<void> File::Flush() {
    if (!FlushFileBuffers(m_file)) return FilesystemWin32ErrorCode(GetLastError(), "Flush");
    return k_success;
}

ErrorCodeOr<u64> File::CurrentPosition() {
    LARGE_INTEGER pos;
    if (!SetFilePointerEx(m_file, {.QuadPart = 0}, &pos, FILE_CURRENT))
        return FilesystemWin32ErrorCode(GetLastError(), "SetFilePointerEx");
    return (u64)pos.QuadPart;
}

ErrorCodeOr<void> File::Seek(int64_t offset, SeekOrigin origin) {
    auto const move_method = ({
        DWORD m;
        switch (origin) {
            case SeekOrigin::Start: m = FILE_BEGIN; break;
            case SeekOrigin::End: m = FILE_END; break;
            case SeekOrigin::Current: m = FILE_CURRENT; break;
        }
        m;
    });
    if (!SetFilePointerEx(m_file, {.QuadPart = offset}, nullptr, move_method))
        return FilesystemWin32ErrorCode(GetLastError(), "SetFilePointerEx");
    return k_success;
}

ErrorCodeOr<usize> File::Write(Span<u8 const> data) {
    DWORD num_written;
    if (!WriteFile(m_file, data.data, CheckedCast<DWORD>(data.size), &num_written, nullptr))
        return FilesystemWin32ErrorCode(GetLastError(), "WriteFile");
    return CheckedCast<usize>(num_written);
}

ErrorCodeOr<usize> File::Read(void* data, usize num_bytes) {
    DWORD num_read;
    if (!ReadFile(m_file, data, CheckedCast<DWORD>(num_bytes), &num_read, nullptr))
        return FilesystemWin32ErrorCode(GetLastError(), "ReadFile");
    return CheckedCast<usize>(num_read);
}

ErrorCodeOr<u64> File::FileSize() {
    LARGE_INTEGER size;
    if (!GetFileSizeEx(m_file, &size)) return FilesystemWin32ErrorCode(GetLastError(), "GetFileSize");
    return CheckedCast<u64>(size.QuadPart);
}

ErrorCodeOr<File> OpenFile(String filename, FileMode mode) {
    PathArena temp_allocator;

    auto const w_path =
        TRY(path::MakePathForWin32(filename, temp_allocator, path::IsAbsolute(filename))).path;

    const DWORD desired_access = ({
        DWORD d;
        switch (mode) {
            case FileMode::Read: d = GENERIC_READ; break;
            case FileMode::Write: d = GENERIC_WRITE; break;
            case FileMode::Append: d = FILE_APPEND_DATA; break;
            case FileMode::WriteNoOverwrite: d = GENERIC_WRITE; break;
        }
        d;
    });

    const DWORD creation_disposition = ({
        DWORD c;
        switch (mode) {
            case FileMode::Read: c = OPEN_EXISTING; break;
            case FileMode::Write: c = CREATE_ALWAYS; break;
            case FileMode::Append: c = OPEN_ALWAYS; break;
            case FileMode::WriteNoOverwrite: c = CREATE_NEW; break;
        }
        c;
    });

    const DWORD share_mode = ({
        DWORD s;
        switch (mode) {
            case FileMode::Read: s = FILE_SHARE_READ; break;
            case FileMode::Write: s = 0; break;
            case FileMode::Append: s = 0; break;
            case FileMode::WriteNoOverwrite: s = 0; break;
        }
        s;
    });

    auto handle = CreateFileW(w_path.data,
                              desired_access,
                              share_mode,
                              nullptr,
                              creation_disposition,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (handle == INVALID_HANDLE_VALUE) return FilesystemWin32ErrorCode(GetLastError(), "CreateFileW");

    return File {handle};
}

EXTERN_C IMAGE_DOS_HEADER __ImageBase; // NOLINT(readability-identifier-naming, bugprone-reserved-identifier)

struct Win32KnownPath {
    Win32KnownPath(PWSTR p) : path(FromNullTerminated(p)) {}
    Win32KnownPath(Win32KnownPath const& other) = delete;
    Win32KnownPath(Win32KnownPath&& other) : path(other.path) { other.path = {}; }
    ~Win32KnownPath() {
        if (path.size) CoTaskMemFree((PWSTR)path.data);
    }
    WString path;
};

// Does not contain a trailing backslash
static ErrorCodeOr<Win32KnownPath> Win32GetKnownFilepath(GUID folder_id, bool create) {
    PWSTR wide_file_path = nullptr;
    auto hr =
        SHGetKnownFolderPath(folder_id, create ? KF_FLAG_CREATE : KF_FLAG_DEFAULT, nullptr, &wide_file_path);
    if (hr != S_OK) {
        // The API says it should be freed regardless of if SHGetKnownFolderPath succeeded
        CoTaskMemFree(wide_file_path);
        return FilesystemWin32ErrorCode(HresultToWin32(hr), "SHGetKnownFolderPath");
    }
    return wide_file_path;
}

ErrorCodeOr<void> WindowsSetFileAttributes(String path, Optional<WindowsFileAttributes> attributes) {
    ASSERT(path::IsAbsolute(path));

    DWORD attribute_flags = FILE_ATTRIBUTE_NORMAL;
    if (attributes) {
        attribute_flags = 0;
        if (attributes->hidden) attribute_flags |= FILE_ATTRIBUTE_HIDDEN;
    }

    PathArena temp_path_arena;
    if (!SetFileAttributesW(TRY(path::MakePathForWin32(path, temp_path_arena, true)).path.data,
                            attribute_flags))
        return FilesystemWin32ErrorCode(GetLastError(), "SetFileAttributesW");
    return k_success;
}

static bool CreateDirectoryWithAttributes(WCHAR* path, DWORD attributes) {
    if (!CreateDirectoryW(path, nullptr)) return false;
    SetFileAttributesW(path, attributes);
    return true;
}

static DWORD AttributesForDir(WCHAR* path, usize path_size, CreateDirectoryOptions options) {
    ASSERT(path_size);
    ASSERT(path);
    ASSERT(path[path_size] == L'\0');

    DWORD attributes = 0;
    if (options.win32_hide_dirs_starting_with_dot) {
        usize last_slash = 0;
        for (usize i = path_size - 1; i != usize(-1); --i)
            if (path[i] == L'\\') {
                last_slash = i;
                break;
            }
        if (last_slash + 1 < path_size && path[last_slash + 1] == L'.') attributes |= FILE_ATTRIBUTE_HIDDEN;
    }

    return attributes ? attributes : FILE_ATTRIBUTE_NORMAL;
}

ErrorCodeOr<void> CreateDirectory(String path, CreateDirectoryOptions options) {
    ASSERT(path::IsAbsolute(path));
    PathArena temp_path_arena;
    auto const wide_path = TRY(path::MakePathForWin32(path, temp_path_arena, true));

    if (CreateDirectoryW(wide_path.path.data, nullptr) != 0)
        return k_success;
    else {
        auto const err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS && !options.fail_if_exists) return k_success;

        // if intermeiates do not exist, create them
        if (err == ERROR_PATH_NOT_FOUND && options.create_intermediate_directories) {
            // skip the drive (C:\) or network drive (\\server\)
            auto const skipped_root = PathSkipRootW(wide_path.path.data + wide_path.prefix_size);
            usize offset = 0;
            if (skipped_root)
                offset = (usize)(skipped_root - wide_path.path.data);
            else
                return ErrorCode(FilesystemError::PathDoesNotExist);
            while (offset < wide_path.path.size && wide_path.path[offset] == L'\\')
                ++offset;

            while (offset < wide_path.path.size) {
                auto slash_pos = Find(wide_path.path, L'\\', offset);
                usize path_size = 0;
                if (slash_pos) {
                    path_size = *slash_pos;
                    offset = *slash_pos + 1;
                    wide_path.path[*slash_pos] = L'\0';
                } else {
                    path_size = wide_path.path.size;
                    offset = wide_path.path.size;
                }

                if (!CreateDirectoryWithAttributes(
                        wide_path.path.data,
                        AttributesForDir(wide_path.path.data, path_size, options))) {
                    auto const err_inner = GetLastError();
                    if (err_inner != ERROR_ALREADY_EXISTS)
                        return FilesystemWin32ErrorCode(err_inner, "CreateDirectoryW");
                }

                if (slash_pos) wide_path.path[*slash_pos] = L'\\';
            }

            return k_success;
        }

        return FilesystemWin32ErrorCode(err, "CreateDirectoryW");
    }
}

static ErrorCodeOr<DynamicArray<wchar_t>> Win32GetRunningProgramName(Allocator& a) {
    DynamicArray<wchar_t> result(a);

    result.Reserve(MAX_PATH + 1);
    auto try_get_module_file_name = [&]() -> ErrorCodeOr<bool> {
        auto path_len = GetModuleFileNameW(CheckedPointerCast<HINSTANCE>(&__ImageBase),
                                           result.data,
                                           (DWORD)result.Capacity());
        if (path_len == 0)
            return FilesystemWin32ErrorCode(GetLastError(), "GetModuleFileNameW");
        else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            return false;
        dyn::Resize(result, (usize)path_len);
        return true;
    };

    auto const successfully_got_path = TRY(try_get_module_file_name());
    if (!successfully_got_path) {
        // try with a much larger buffer
        result.Reserve(result.Capacity() * 4);
        auto const successfully_got_path_attempt2 = TRY(try_get_module_file_name());
        if (!successfully_got_path_attempt2) Panic("GetModuleFileNameW expects unreasonable path size");
    }

    return result;
}

ErrorCodeOr<MutableString> CurrentExecutablePath(Allocator& a) {
    PathArena temp_path_arena;
    auto const full_wide_path = TRY(Win32GetRunningProgramName(temp_path_arena));
    return Narrow(a, full_wide_path).Value();
}

ErrorCodeOr<DynamicArrayBounded<char, 200>> NameOfRunningExecutableOrLibrary() {
    PathArena temp_path_arena;
    auto const full_wide_path = TRY(Win32GetRunningProgramName(temp_path_arena));
    auto full_path = Narrow(temp_path_arena, full_wide_path).Value();
    return String {path::Filename(full_path)};
}

ErrorCodeOr<MutableString> KnownDirectory(Allocator& a, KnownDirectoryType type, bool create) {
    GUID folder_id {};
    Span<WString const> subfolders {};
    switch (type) {
        case KnownDirectoryType::Temporary: {
            WCHAR buffer[MAX_PATH + 1];
            auto size = GetTempPathW((DWORD)ArraySize(buffer), buffer);
            if (size == 0) return FilesystemWin32ErrorCode(GetLastError(), "GetTempPathW");

            if (auto const last = buffer[size - 1]; last == L'\\' || last == L'/') --size;

            WString const wide_path {buffer, (usize)size};
            auto result = Narrow(a, wide_path).Value();
            ASSERT(!path::IsPathSeparator(Last(result)));
            ASSERT(path::IsAbsolute(result));
            return result;
        }
        case KnownDirectoryType::Logs: folder_id = FOLDERID_LocalAppData; break;
        case KnownDirectoryType::Documents: folder_id = FOLDERID_Documents; break;
        case KnownDirectoryType::Downloads: folder_id = FOLDERID_Downloads; break;
        case KnownDirectoryType::GlobalData: folder_id = FOLDERID_Public; break;

        case KnownDirectoryType::GlobalClapPlugins:
            folder_id = FOLDERID_ProgramFilesCommon;
            subfolders = Array {L"CLAP"_s};
            break;
        case KnownDirectoryType::UserClapPlugins:
            folder_id = FOLDERID_LocalAppData;
            subfolders = Array {L"CLAP"_s};
            break;
        case KnownDirectoryType::GlobalVst3Plugins:
            folder_id = FOLDERID_ProgramFilesCommon;
            subfolders = Array {L"VST3"_s};
            break;
        case KnownDirectoryType::UserVst3Plugins:
            folder_id = IsRunningUnderWine() ? FOLDERID_LocalAppData : FOLDERID_UserProgramFilesCommon;
            subfolders = Array {L"VST3"_s};
            break;

        case KnownDirectoryType::LegacyAllUsersData: folder_id = FOLDERID_Public; break;
        case KnownDirectoryType::LegacyAllUsersSettings: folder_id = FOLDERID_ProgramData; break;
        case KnownDirectoryType::LegacyPluginSettings: folder_id = FOLDERID_RoamingAppData; break;
        case KnownDirectoryType::LegacyData: folder_id = FOLDERID_RoamingAppData; break;

        case KnownDirectoryType::Count: PanicIfReached(); break;
    }

    // NOTE: Not all KNOWNFOLDERID values are present on all systems. We might get a E_INVALIDARG
    auto const wide_path = TRY(Win32GetKnownFilepath(folder_id, create));

    MutableString result;
    if (subfolders.size) {
        PathArena temp_path_arena;
        DynamicArray<wchar_t> wide_result {wide_path.path, temp_path_arena};
        for (auto const subfolder : subfolders) {
            dyn::Append(wide_result, L'\\');
            dyn::AppendSpan(wide_result, subfolder);
            if (create) {
                if (!CreateDirectoryW(wide_result.data, nullptr)) {
                    auto const err = GetLastError();
                    if (err != ERROR_ALREADY_EXISTS) return FilesystemWin32ErrorCode(err, "CreateDirectoryW");
                }
            }
        }
        result = Narrow(a, wide_result).Value();
    } else {
        result = Narrow(a, wide_path.path).Value();
    }

    ASSERT(!path::IsPathSeparator(Last(result)));
    ASSERT(path::IsAbsolute(result));

    return result;
}

ErrorCodeOr<FileType> GetFileType(String absolute_path) {
    PathArena temp_path_arena;

    auto const attributes =
        GetFileAttributesW(TRY(path::MakePathForWin32(absolute_path, temp_path_arena, true)).path.data);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return FilesystemWin32ErrorCode(GetLastError(), "GetFileAttributesW");

    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return FileType::Directory;
    return FileType::File;
}

ErrorCodeOr<MutableString> AbsolutePath(Allocator& a, String path) {
    ASSERT(path.size);

    PathArena temp_path_arena;
    // relative paths cannot start with the long-path prefix: //?/
    auto const wide_path = TRY(path::MakePathForWin32(path, temp_path_arena, false));

    DynamicArray<wchar_t> wide_result {temp_path_arena};
    wide_result.Reserve(MAX_PATH + 1);

    auto path_len =
        GetFullPathNameW(wide_path.path.data, (DWORD)wide_result.Capacity(), wide_result.data, nullptr);
    if (path_len == 0) return FilesystemWin32ErrorCode(GetLastError(), "GetFullPathNameW");

    if (path_len >= (DWORD)wide_result.Capacity()) {
        wide_result.Reserve(path_len + 1);
        path_len =
            GetFullPathNameW(wide_path.path.data, (DWORD)wide_result.Capacity(), wide_result.data, nullptr);
        if (path_len == 0) return FilesystemWin32ErrorCode(GetLastError(), "GetFullPathNameW");
    }
    dyn::Resize(wide_result, (usize)path_len);

    auto result = Narrow(a, wide_result).Value();
    ASSERT(!path::IsPathSeparator(Last(result)));
    ASSERT(path::IsAbsolute(result));
    return result;
}

ErrorCodeOr<MutableString> CanonicalizePath(Allocator& a, String path) {
    auto result = TRY(AbsolutePath(a, path));
    for (auto& c : result)
        if (c == '/') c = '\\';
    return result;
}

static ErrorCodeOr<void> Win32DeleteDirectory(WString windows_path, ArenaAllocator& arena) {
    DynamicArray<wchar_t> path_buffer {windows_path, arena};
    dyn::AppendSpan(path_buffer, L"\\*");

    WIN32_FIND_DATAW data {};
    auto handle = FindFirstFileW(dyn::NullTerminated(path_buffer), &data);
    if (handle == INVALID_HANDLE_VALUE) return FilesystemWin32ErrorCode(GetLastError(), "FindFirstFileW");
    DEFER { FindClose(handle); };

    bool keep_iterating = true;

    do {
        auto const file_name = FromNullTerminated(data.cFileName);

        if (file_name != L"."_s && file_name != L".."_s) {
            dyn::Resize(path_buffer, windows_path.size);
            dyn::Append(path_buffer, L'\\');
            dyn::AppendSpan(path_buffer, file_name);

            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                TRY(Win32DeleteDirectory(WString(path_buffer), arena));
            else if (!DeleteFileW(dyn::NullTerminated(path_buffer)))
                return FilesystemWin32ErrorCode(GetLastError(), "DeleteFileW");
        }

        if (!FindNextFileW(handle, &data)) {
            if (GetLastError() == ERROR_NO_MORE_FILES)
                keep_iterating = false;
            else
                return FilesystemWin32ErrorCode(GetLastError(), "FindNextFileW");
        }

    } while (keep_iterating);

    {
        dyn::Resize(path_buffer, windows_path.size);
        if (!RemoveDirectoryW(dyn::NullTerminated(path_buffer)))
            return FilesystemWin32ErrorCode(GetLastError(), "RemoveDirectoryW");
    }

    return k_success;
}

ErrorCodeOr<void> Delete(String path, DeleteOptions options) {
    PathArena temp_path_arena;
    auto const wide_path = TRY(path::MakePathForWin32(path, temp_path_arena, true));

    if (options.type == DeleteOptions::Type::Any) {
        if (DeleteFileW(wide_path.path.data) != 0)
            return k_success;
        else if (GetLastError() == ERROR_FILE_NOT_FOUND && !options.fail_if_not_exists)
            return k_success;
        else if (GetLastError() == ERROR_ACCESS_DENIED) // it's probably a directory
            options.type = DeleteOptions::Type::DirectoryRecursively;
        else
            return FilesystemWin32ErrorCode(GetLastError(), "RemoveDirectoryW");
    }

    switch (options.type) {
        case DeleteOptions::Type::File: {
            if (DeleteFileW(wide_path.path.data) != 0) {
                return k_success;
            } else {
                if (GetLastError() == ERROR_FILE_NOT_FOUND && !options.fail_if_not_exists) return k_success;
                return FilesystemWin32ErrorCode(GetLastError(), "DeleteW");
            }
            break;
        }
        case DeleteOptions::Type::DirectoryOnlyIfEmpty: {
            if (RemoveDirectoryW(wide_path.path.data) != 0) {
                return k_success;
            } else {
                if (GetLastError() == ERROR_FILE_NOT_FOUND && !options.fail_if_not_exists) return k_success;
                return FilesystemWin32ErrorCode(GetLastError(), "RemoveDirectoryW");
            }
            break;
        }
        case DeleteOptions::Type::Any: {
            PanicIfReached();
            break;
        }
        case DeleteOptions::Type::DirectoryRecursively: {
            if (RemoveDirectoryW(wide_path.path.data) != 0) {
                return k_success;
            } else {
                if (GetLastError() == ERROR_FILE_NOT_FOUND && !options.fail_if_not_exists) return k_success;
                if (GetLastError() == ERROR_DIR_NOT_EMPTY)
                    return Win32DeleteDirectory(wide_path.path, temp_path_arena);
                return FilesystemWin32ErrorCode(GetLastError(), "RemoveDirectoryW");
            }
            break;
        }
    }
}

ErrorCodeOr<void> CopyFile(String from, String to, ExistingDestinationHandling existing) {
    ASSERT(path::IsAbsolute(from));
    ASSERT(path::IsAbsolute(to));
    PathArena temp_path_arena;

    auto const fail_if_exists = ({
        BOOL f = TRUE;
        switch (existing) {
            case ExistingDestinationHandling::Fail: f = TRUE; break;
            case ExistingDestinationHandling::Overwrite: f = FALSE; break;
            case ExistingDestinationHandling::Skip: f = TRUE; break;
        }
        f;
    });
    auto const from_wide = TRY(path::MakePathForWin32(from, temp_path_arena, true)).path.data;
    auto const to_wide = TRY(path::MakePathForWin32(to, temp_path_arena, true)).path.data;
    if (!CopyFileW(from_wide, to_wide, fail_if_exists)) {
        auto err = GetLastError();
        if (err == ERROR_ACCESS_DENIED && existing == ExistingDestinationHandling::Overwrite) {
            // "This function fails with ERROR_ACCESS_DENIED if the destination file already exists and has
            // the FILE_ATTRIBUTE_HIDDEN or FILE_ATTRIBUTE_READONLY attribute set."
            if (SetFileAttributesW(to_wide, FILE_ATTRIBUTE_NORMAL)) {
                if (CopyFileW(from_wide, to_wide, fail_if_exists)) return k_success;
                err = GetLastError();
            }
        }
        if (err == ERROR_FILE_EXISTS && existing == ExistingDestinationHandling::Skip) return k_success;
        return FilesystemWin32ErrorCode(err, "CopyFileW");
    }
    return k_success;
}

// There's a function PathIsDirectoryEmptyW but it does not seem to support long paths, so we implement our
// own.
static bool PathIsANonEmptyDirectory(WString path) {
    PathArena temp_path_arena;

    WIN32_FIND_DATAW data {};
    DynamicArray<wchar_t> search_path {path, temp_path_arena};
    dyn::AppendSpan(search_path, L"\\*");
    SetLastError(0);

    auto handle = FindFirstFileW(dyn::NullTerminated(search_path), &data);
    if (handle == INVALID_HANDLE_VALUE) return false; // Not a directory, or inaccessible
    DEFER { FindClose(handle); };

    if (GetLastError() == ERROR_FILE_NOT_FOUND) return false; // Empty directory

    while (true) {
        auto const file_name = FromNullTerminated(data.cFileName);
        if (file_name != L"."_s && file_name != L".."_s) return true;
        if (FindNextFileW(handle, &data)) {
            continue;
        } else {
            if (GetLastError() == ERROR_NO_MORE_FILES) {
                // Empty directory. If we made it here we can't have found any files since we 'return true' if
                // anything was found
                return false;
            }
            return false; // an error occurred, we can't determine if the directory is non-empty
        }
    }

    return false;
}

ErrorCodeOr<void> Rename(String from, String to) {
    ASSERT(path::IsAbsolute(from));
    ASSERT(path::IsAbsolute(to));
    PathArena temp_path_arena;

    auto to_wide = TRY(path::MakePathForWin32(to, temp_path_arena, true)).path;

    // Only succeeds if the destination is an empty directory. We do this to make Rename consistent across
    // Windows and POSIX rename().
    RemoveDirectoryW(to_wide.data);

    if (!MoveFileWithProgressW(TRY(path::MakePathForWin32(from, temp_path_arena, true)).path.data,
                               to_wide.data,
                               nullptr,
                               nullptr,
                               MOVEFILE_REPLACE_EXISTING)) {
        auto err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            // When the destination is a non-empty directory we don't get ERROR_DIR_NOT_EMPTY as we might
            // expect, but instead ERROR_ACCESS_DENIED. Let's try and fix that.
            if (PathIsANonEmptyDirectory(to_wide)) err = ERROR_DIR_NOT_EMPTY;
        }
        return FilesystemWin32ErrorCode(err, "MoveFileW");
    }
    return k_success;
}

ErrorCodeOr<s64> LastWriteTime(String path) {
    ASSERT(path::IsAbsolute(path));
    PathArena temp_path_arena;

    auto handle = CreateFileW(TRY(path::MakePathForWin32(path, temp_path_arena, true)).path.data,
                              GENERIC_READ,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (handle == INVALID_HANDLE_VALUE) return FilesystemWin32ErrorCode(GetLastError(), "CreateFileW");
    DEFER { CloseHandle(handle); };

    FILETIME file_time;
    if (!GetFileTime(handle, nullptr, nullptr, &file_time))
        return FilesystemWin32ErrorCode(GetLastError(), "GetFileTime");

    ULARGE_INTEGER file_time_int;
    file_time_int.LowPart = file_time.dwLowDateTime;
    file_time_int.HighPart = file_time.dwHighDateTime;

    // The windows epoch starts 1601-01-01T00:00:00Z. It's 11644473600 seconds before the UNIX/Linux epoch
    // (1970-01-01T00:00:00Z). Windows ticks are in 100 nanoseconds.
    constexpr s64 k_ticks_per_second = 10000000;
    constexpr s64 k_sec_to_unix_epoch = 11644473600ll;

    return (s64)((file_time_int.QuadPart / k_ticks_per_second) - k_sec_to_unix_epoch);
}

//
// ==========================================================================================================

static ErrorCodeOr<void>
FillDirectoryEntry(DirectoryEntry& e, const WIN32_FIND_DATAW& data, usize base_path_size) {
    PathArena temp_path_arena;
    auto filename = Narrow(temp_path_arena, FromNullTerminated(data.cFileName)).Value();
    dyn::Resize(e.path, base_path_size);
    path::JoinAppend(e.path, filename);
    e.type = FileType::File;
    e.file_size = (data.nFileSizeHigh * (MAXDWORD + 1)) + data.nFileSizeLow;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) e.type = FileType::Directory;
    return k_success;
}

static bool ShouldSkipFile(const WCHAR* null_term_filename, bool skip_dot_files) {
    auto const filename = FromNullTerminated(null_term_filename);
    return filename == L"."_s || filename == L".."_s ||
           (skip_dot_files && filename.size && filename[0] == L'.');
}

ErrorCodeOr<DirectoryIterator>
DirectoryIterator::Create(Allocator& a, String path_uncanonical, DirectoryIteratorOptions options) {
    ASSERT(options.wildcard.size);

    PathArena temp_path_arena;
    auto path = DynamicArray<char>::FromOwnedSpan(TRY(CanonicalizePath(temp_path_arena, path_uncanonical)),
                                                  temp_path_arena);

    DirectoryIterator result {path, a};

    path::JoinAppend(path, options.wildcard);

    WIN32_FIND_DATAW data {};
    auto handle = FindFirstFileExW(TRY(path::MakePathForWin32(path, temp_path_arena, true)).path.data,
                                   FindExInfoBasic,
                                   &data,
                                   FindExSearchNameMatch,
                                   nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            // The search could not find any files.
            result.m_reached_end = true;
            return result;
        }
        return FilesystemWin32ErrorCode(GetLastError(), "FindFirstFileW");
    }

    result.m_handle = handle;
    result.m_base_path_size = result.m_e.path.size;
    result.m_get_file_size = options.get_file_size;
    result.m_skip_dot_files = options.skip_dot_files;

    if (ShouldSkipFile(data.cFileName, result.m_skip_dot_files))
        TRY(result.Increment());
    else
        TRY(FillDirectoryEntry(result.m_e, data, result.m_base_path_size));

    return result;
}

ErrorCodeOr<void> DirectoryIterator::Increment() {
    if (m_reached_end) {
        PanicIfReached();
        return k_success;
    }

    WIN32_FIND_DATAW data {};
    if (FindNextFileW(m_handle, &data) == 0) {
        if (GetLastError() != ERROR_NO_MORE_FILES) {
            return FilesystemWin32ErrorCode(GetLastError(), "FindNextFileW");
        } else {
            m_reached_end = true;
            return k_success;
        }
    }

    if (ShouldSkipFile(data.cFileName, m_skip_dot_files)) return Increment();

    TRY(FillDirectoryEntry(m_e, data, m_base_path_size));
    return k_success;
}

DirectoryIterator::~DirectoryIterator() {
    if (m_handle) FindClose(m_handle);
}
//
// ==========================================================================================================

#define FP_HRESULT_TRY(windows_call)                                                                         \
    if (auto hr = windows_call; !SUCCEEDED(hr)) {                                                            \
        return FilesystemWin32ErrorCode(HresultToWin32(hr), #windows_call);                                  \
    }

ErrorCodeOr<Span<MutableString>> FilesystemDialog(DialogArguments args) {
    auto const com_library_usage = TRY(ScopedWin32ComUsage::Create());

    auto const ids = ({
        struct Ids {
            IID rclsid;
            IID riid;
        };
        Ids i;
        switch (args.type) {
            case DialogArguments::Type::SaveFile: {
                i.rclsid = CLSID_FileSaveDialog;
                i.riid = IID_IFileSaveDialog;
                break;
            }
            case DialogArguments::Type::OpenFile:
            case DialogArguments::Type::SelectFolder: {
                i.rclsid = CLSID_FileOpenDialog;
                i.riid = IID_IFileOpenDialog;
                break;
            }
        }
        i;
    });

    IFileDialog* f;
    FP_HRESULT_TRY(CoCreateInstance(ids.rclsid, nullptr, CLSCTX_ALL, ids.riid, reinterpret_cast<void**>(&f)));
    DEFER { f->Release(); };

    if (args.default_path) {
        PathArena temp_path_arena;

        if (auto const narrow_dir = path::Directory(*args.default_path)) {
            auto dir = WidenAllocNullTerm(temp_path_arena, *narrow_dir).Value();
            Replace(dir, L'/', L'\\');
            IShellItem* item = nullptr;
            FP_HRESULT_TRY(SHCreateItemFromParsingName(dir.data, nullptr, IID_PPV_ARGS(&item)));
            DEFER { item->Release(); };

            constexpr bool k_forced_default_folder = true;
            if constexpr (k_forced_default_folder)
                f->SetFolder(item);
            else
                f->SetDefaultFolder(item);
        }

        if (args.type == DialogArguments::Type::SaveFile) {
            auto filename = path::Filename(*args.default_path);
            f->SetFileName(WidenAllocNullTerm(temp_path_arena, filename).Value().data);
        }
    }

    if (args.filters.size) {
        PathArena temp_path_arena;
        DynamicArray<COMDLG_FILTERSPEC> win32_filters {temp_path_arena};
        win32_filters.Reserve(args.filters.size);
        for (auto filter : args.filters) {
            dyn::Append(
                win32_filters,
                COMDLG_FILTERSPEC {
                    .pszName = WidenAllocNullTerm(temp_path_arena, filter.description).Value().data,
                    .pszSpec = WidenAllocNullTerm(temp_path_arena, filter.wildcard_filter).Value().data,
                });
        }
        f->SetFileTypes((UINT)win32_filters.size, win32_filters.data);
    }

    {
        PathArena temp_path_arena;
        auto wide_title = WidenAllocNullTerm(temp_path_arena, args.title).Value();
        FP_HRESULT_TRY(f->SetTitle(wide_title.data));
    }

    if (args.type == DialogArguments::Type::SelectFolder) {
        DWORD flags = 0;
        FP_HRESULT_TRY(f->GetOptions(&flags));
        FP_HRESULT_TRY(f->SetOptions(flags | FOS_PICKFOLDERS));
    }

    auto const multiple_selection = ids.rclsid == CLSID_FileOpenDialog && args.allow_multiple_selection;
    if (multiple_selection) {
        DWORD flags = 0;
        FP_HRESULT_TRY(f->GetOptions(&flags));
        FP_HRESULT_TRY(f->SetOptions(flags | FOS_ALLOWMULTISELECT));
    }

    if (auto hr = f->Show((HWND)args.parent_window); hr != S_OK) {
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return Span<MutableString> {};
        return FilesystemWin32ErrorCode(HresultToWin32(hr), "Show()");
    }

    auto utf8_path_from_shell_item = [&](IShellItem* p_item) -> ErrorCodeOr<MutableString> {
        PWSTR wide_path = nullptr;
        FP_HRESULT_TRY(p_item->GetDisplayName(SIGDN_FILESYSPATH, &wide_path));
        DEFER { CoTaskMemFree(wide_path); };

        auto narrow_path = Narrow(args.allocator, FromNullTerminated(wide_path)).Value();
        ASSERT(!path::IsPathSeparator(Last(narrow_path)));
        ASSERT(path::IsAbsolute(narrow_path));
        return narrow_path;
    };

    if (!multiple_selection) {
        IShellItem* p_item = nullptr;
        FP_HRESULT_TRY(f->GetResult(&p_item));
        DEFER { p_item->Release(); };

        auto span = args.allocator.AllocateExactSizeUninitialised<MutableString>(1);
        span[0] = TRY(utf8_path_from_shell_item(p_item));
        return span;
    } else {
        IShellItemArray* p_items = nullptr;
        FP_HRESULT_TRY(((IFileOpenDialog*)f)->GetResults(&p_items));
        DEFER { p_items->Release(); };

        DWORD count;
        FP_HRESULT_TRY(p_items->GetCount(&count));
        auto result = args.allocator.AllocateExactSizeUninitialised<MutableString>(CheckedCast<usize>(count));
        for (auto const item_index : Range(count)) {
            IShellItem* p_item = nullptr;
            FP_HRESULT_TRY(p_items->GetItemAt(item_index, &p_item));
            DEFER { p_item->Release(); };

            result[item_index] = TRY(utf8_path_from_shell_item(p_item));
        }
        return result;
    }
}

// Directory watcher
// Jim Beveridge's excellent blog post on the ReadDirectoryChangesW API:
// https://qualapps.blogspot.com/2010/05/understanding-readdirectorychangesw_19.html

constexpr DWORD k_directory_changes_filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                             FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;

struct WindowsWatchedDirectory {
    alignas(16) Array<u8, Kb(35)> buffer;
    HANDLE handle;
    OVERLAPPED overlapped;
};

static void UnwatchDirectory(WindowsWatchedDirectory* windows_dir) {
    if (!windows_dir) return;
    CloseHandle(windows_dir->overlapped.hEvent);
    CloseHandle(windows_dir->handle);
    PageAllocator::Instance().Delete(windows_dir);
}

ErrorCodeOr<DirectoryWatcher> CreateDirectoryWatcher(Allocator& a) {
    ZoneScoped;
    DirectoryWatcher result {
        .allocator = a,
        .watched_dirs = {a},
    };
    return result;
}

void DestoryDirectoryWatcher(DirectoryWatcher& watcher) {
    ZoneScoped;

    for (auto const& dir : watcher.watched_dirs) {
        if (dir.state == DirectoryWatcher::WatchedDirectory::State::Watching ||
            dir.state == DirectoryWatcher::WatchedDirectory::State::NeedsUnwatching) {
            UnwatchDirectory((WindowsWatchedDirectory*)dir.native_data.pointer);
        }
    }

    watcher.watched_dirs.Clear();
}

static ErrorCodeOr<WindowsWatchedDirectory*> WatchDirectory(DirectoryWatcher::WatchedDirectory const& dir,
                                                            ArenaAllocator& scratch_arena) {
    auto wide_path = TRY(path::MakePathForWin32(dir.path, scratch_arena, true));
    auto handle = CreateFileW(wide_path.path.data,
                              FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                              nullptr);
    if (handle == INVALID_HANDLE_VALUE) return FilesystemWin32ErrorCode(GetLastError());

    auto windows_dir = PageAllocator::Instance().NewUninitialised<WindowsWatchedDirectory>();
    windows_dir->handle = handle;
    windows_dir->overlapped = {};

    windows_dir->overlapped.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ASSERT(windows_dir->overlapped.hEvent);

    auto const succeeded = ReadDirectoryChangesW(handle,
                                                 windows_dir->buffer.data,
                                                 (DWORD)windows_dir->buffer.size,
                                                 dir.recursive,
                                                 k_directory_changes_filter,
                                                 nullptr,
                                                 &windows_dir->overlapped,
                                                 nullptr);
    if (!succeeded) {
        UnwatchDirectory(windows_dir);
        auto const error = GetLastError();
        return FilesystemWin32ErrorCode(error);
    }

    return windows_dir;
}

constexpr auto k_log_module = "dirwatch"_log_module;

ErrorCodeOr<Span<DirectoryWatcher::DirectoryChanges const>>
PollDirectoryChanges(DirectoryWatcher& watcher, PollDirectoryChangesArgs args) {
    auto const any_states_changed =
        watcher.HandleWatchedDirChanges(args.dirs_to_watch, args.retry_failed_directories);

    for (auto& dir : watcher.watched_dirs)
        dir.directory_changes.Clear();

    if (any_states_changed) {
        for (auto& dir : watcher.watched_dirs) {
            switch (dir.state) {
                case DirectoryWatcher::WatchedDirectory::State::NeedsWatching: {
                    auto const outcome = WatchDirectory(dir, args.scratch_arena);
                    if (outcome.HasValue()) {
                        dir.state = DirectoryWatcher::WatchedDirectory::State::Watching;
                        dir.native_data.pointer = outcome.Value();
                    } else {
                        dir.state = DirectoryWatcher::WatchedDirectory::State::WatchingFailed;
                        dir.directory_changes.error = outcome.Error();
                        dir.native_data = {};
                    }
                    break;
                }
                case DirectoryWatcher::WatchedDirectory::State::NeedsUnwatching: {
                    UnwatchDirectory((WindowsWatchedDirectory*)dir.native_data.pointer);
                    dir.native_data = {};
                    dir.state = DirectoryWatcher::WatchedDirectory::State::NotWatching;
                    break;
                }
                case DirectoryWatcher::WatchedDirectory::State::Watching:
                case DirectoryWatcher::WatchedDirectory::State::WatchingFailed:
                case DirectoryWatcher::WatchedDirectory::State::NotWatching: break;
            }
        }
    }

    for (auto& dir : watcher.watched_dirs) {
        if (dir.state != DirectoryWatcher::WatchedDirectory::State::Watching) continue;

        auto& windows_dir = *(WindowsWatchedDirectory*)dir.native_data.pointer;

        auto const wait_result = WaitForSingleObjectEx(windows_dir.overlapped.hEvent, 0, TRUE);

        if (wait_result == WAIT_OBJECT_0) {
            DWORD bytes_transferred {};
            if (GetOverlappedResult(windows_dir.handle, &windows_dir.overlapped, &bytes_transferred, FALSE)) {
                auto const* base = windows_dir.buffer.data;
                auto const* end = Min<u8 const*>(base + bytes_transferred, windows_dir.buffer.end());
                auto const min_chunk_size = sizeof(FILE_NOTIFY_INFORMATION);

                bool error = false;

                while (true) {
                    if (base >= end || ((usize)(end - base) < min_chunk_size)) {
                        g_log.Error(k_log_module, "ERROR: invalid data received");
                        error = true;
                        break;
                    }

                    ASSERT(bytes_transferred >= min_chunk_size);

                    DWORD action;
                    DWORD next_entry_offset;
                    Array<wchar_t, 1000> filename_buf;
                    WString filename;

                    {
                        // I've found that it's possible to receive
                        // FILE_NOTIFY_INFORMATION.NextEntryOffset values that result in the next event
                        // being misaligned. Reading unaligned memory is not normally a great idea for
                        // performance. And if you have UBSan enabled it will crash. To work around this,
                        // we copy the given memory into correctly aligned structures. Another option
                        // would be to disable UBSan for this function but I'm not sure of the
                        // consequences of misaligned reads so let's play it safe.

                        ASSERT(bytes_transferred != 1);
                        FILE_NOTIFY_INFORMATION event;
                        __builtin_memcpy_inline(&event, base, sizeof(event));

                        if ((base + event.NextEntryOffset) > end) {
                            g_log.Debug(
                                k_log_module,
                                "ERROR: invalid data received: NextEntryOffset points outside of buffer: FileNameLength: {}, NextEntryOffset: {}",
                                event.FileNameLength,
                                event.NextEntryOffset);
                            error = true;
                            break;
                        }

                        auto const num_wchars = event.FileNameLength / sizeof(wchar_t);
                        if (num_wchars > filename_buf.size) {
                            g_log.Debug(
                                k_log_module,
                                "ERROR: filename too long for buffer ({} chars): FileNameLength: {}, NextEntryOffset: {}, bytes_transferred: {}, min_chunk_size: {}",
                                num_wchars,
                                event.FileNameLength,
                                event.NextEntryOffset,
                                bytes_transferred,
                                min_chunk_size);
                            error = true;
                            break;
                        }
                        CopyMemory(filename_buf.data,
                                   base + offsetof(FILE_NOTIFY_INFORMATION, FileName),
                                   event.FileNameLength);
                        action = event.Action;
                        next_entry_offset = event.NextEntryOffset;
                        filename = {filename_buf.data, num_wchars};
                    }

                    DirectoryWatcher::ChangeTypeFlags changes {};
                    switch (action) {
                        case FILE_ACTION_ADDED: changes |= DirectoryWatcher::ChangeType::Added; break;
                        case FILE_ACTION_REMOVED: changes |= DirectoryWatcher::ChangeType::Deleted; break;
                        case FILE_ACTION_MODIFIED: changes |= DirectoryWatcher::ChangeType::Modified; break;
                        case FILE_ACTION_RENAMED_OLD_NAME:
                            changes |= DirectoryWatcher::ChangeType::RenamedOldName;
                            break;
                        case FILE_ACTION_RENAMED_NEW_NAME:
                            changes |= DirectoryWatcher::ChangeType::RenamedNewName;
                            break;
                    }
                    if (changes) {
                        auto const narrowed = Narrow(args.result_arena, filename);
                        if (narrowed.HasValue()) {
                            g_log.Debug(k_log_module,
                                        "Change: {} {}",
                                        DirectoryWatcher::ChangeType::ToString(changes),
                                        narrowed.Value());
                            dir.directory_changes.Add(
                                {
                                    .subpath = narrowed.Value(),
                                    .file_type = k_nullopt,
                                    .changes = changes,
                                },
                                args.result_arena);
                        }
                    }

                    if (!next_entry_offset) break; // successfully read all events

                    base += next_entry_offset;
                }

                if (error) {
                    dir.directory_changes.Add(
                        {
                            .subpath = {},
                            .file_type = k_nullopt,
                            .changes = DirectoryWatcher::ChangeType::ManualRescanNeeded,
                        },
                        args.result_arena);
                }
            } else {
                dir.directory_changes.error = FilesystemWin32ErrorCode(GetLastError());
            }
        } else if (wait_result != WAIT_TIMEOUT) {
            if constexpr (!PRODUCTION_BUILD) Panic("unexpected result from WaitForSingleObjectEx");
        }

        auto const succeeded = ReadDirectoryChangesW(windows_dir.handle,
                                                     windows_dir.buffer.data,
                                                     (DWORD)windows_dir.buffer.size,
                                                     dir.recursive,
                                                     k_directory_changes_filter,
                                                     nullptr,
                                                     &windows_dir.overlapped,
                                                     nullptr);

        if (!succeeded) {
            auto const error = GetLastError();
            if (error == ERROR_NOTIFY_ENUM_DIR)
                dir.directory_changes.Add(
                    {
                        .subpath = {},
                        .file_type = k_nullopt,
                        .changes = DirectoryWatcher::ChangeType::ManualRescanNeeded,
                    },
                    args.result_arena);
            else
                dir.directory_changes.error = FilesystemWin32ErrorCode(error);
            continue;
        }
    }

    watcher.RemoveAllNotWatching();

    return watcher.AllDirectoryChanges(args.result_arena);
}
