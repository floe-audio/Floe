// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
//
#include <aclapi.h>
#include <dbghelp.h>
#include <fileapi.h>
#include <lmcons.h>
#include <shellapi.h>
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

ErrorCodeOr<bool> File::Lock(FileLockOptions options) {
    DWORD flags = 0;
    switch (options.type) {
        case FileLockOptions::Type::Exclusive: flags = LOCKFILE_EXCLUSIVE_LOCK; break;
        case FileLockOptions::Type::Shared: flags = 0; break;
    }
    if (options.non_blocking) flags |= LOCKFILE_FAIL_IMMEDIATELY;

    OVERLAPPED overlapped {};
    if (!LockFileEx(handle, flags, 0, MAXDWORD, MAXDWORD, &overlapped)) {
        auto const error = GetLastError();
        if (options.non_blocking && (error == ERROR_LOCK_VIOLATION || error == ERROR_SHARING_VIOLATION))
            return false;
        return FilesystemWin32ErrorCode(error, "LockFileEx");
    }
    return true;
}

ErrorCodeOr<void> File::Unlock() {
    OVERLAPPED overlapped {};
    if (!UnlockFileEx(handle, 0, MAXDWORD, MAXDWORD, &overlapped))
        return FilesystemWin32ErrorCode(GetLastError(), "UnlockFileEx");
    return k_success;
}

ErrorCodeOr<void> File::Truncate(u64 new_size) {
    if (!SetFilePointerEx(handle, {.QuadPart = (LONGLONG)new_size}, nullptr, FILE_BEGIN))
        return FilesystemWin32ErrorCode(GetLastError(), "SetFilePointerEx");
    if (!SetEndOfFile(handle)) return FilesystemWin32ErrorCode(GetLastError(), "SetEndOfFile");
    return k_success;
}

ErrorCodeOr<s128> File::LastModifiedTimeNsSinceEpoch() {
    FILETIME file_time;
    if (!GetFileTime(handle, nullptr, nullptr, &file_time))
        return FilesystemWin32ErrorCode(GetLastError(), "GetFileTime");

    ULARGE_INTEGER file_time_int;
    file_time_int.LowPart = file_time.dwLowDateTime;
    file_time_int.HighPart = file_time.dwHighDateTime;

    // The windows epoch starts 1601-01-01T00:00:00Z. It's 11644473600 seconds before the Unix/Linux epoch
    // (1970-01-01T00:00:00Z). Windows ticks are in 100 nanoseconds.
    return ((s128)file_time_int.QuadPart * (s128)100) - ((s128)11644473600ull * (s128)1'000'000'000ull);
}

ErrorCodeOr<void> File::SetLastModifiedTimeNsSinceEpoch(s128 time) {
    ULARGE_INTEGER file_time_int;

    // The windows epoch starts 1601-01-01T00:00:00Z. It's 11644473600 seconds before the Unix/Linux epoch
    // (1970-01-01T00:00:00Z). Windows ticks are in 100 nanoseconds.
    file_time_int.QuadPart = (ULONGLONG)((time + (s128)11644473600ull * (s128)1'000'000'000ull) / (s128)100);

    FILETIME file_time;
    file_time.dwLowDateTime = file_time_int.LowPart;
    file_time.dwHighDateTime = file_time_int.HighPart;

    if (!SetFileTime(handle, nullptr, nullptr, &file_time))
        return FilesystemWin32ErrorCode(GetLastError(), "SetFileTime");
    return k_success;
}

void File::CloseFile() {
    if (handle) CloseHandle(handle);
}

ErrorCodeOr<void> File::Flush() {
    if (!FlushFileBuffers(handle)) return FilesystemWin32ErrorCode(GetLastError(), "Flush");
    return k_success;
}

ErrorCodeOr<u64> File::CurrentPosition() {
    LARGE_INTEGER pos;
    if (!SetFilePointerEx(handle, {.QuadPart = 0}, &pos, FILE_CURRENT))
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
    if (!SetFilePointerEx(handle, {.QuadPart = offset}, nullptr, move_method))
        return FilesystemWin32ErrorCode(GetLastError(), "SetFilePointerEx");
    return k_success;
}

ErrorCodeOr<usize> File::Write(Span<u8 const> data) {
    DWORD num_written;
    if (!WriteFile(handle, data.data, CheckedCast<DWORD>(data.size), &num_written, nullptr))
        return FilesystemWin32ErrorCode(GetLastError(), "WriteFile");
    return CheckedCast<usize>(num_written);
}

ErrorCodeOr<usize> File::Read(void* data, usize num_bytes) {
    DWORD num_read;
    if (!ReadFile(handle, data, CheckedCast<DWORD>(num_bytes), &num_read, nullptr))
        return FilesystemWin32ErrorCode(GetLastError(), "ReadFile");
    return CheckedCast<usize>(num_read);
}

ErrorCodeOr<u64> File::FileSize() {
    LARGE_INTEGER size;
    if (!GetFileSizeEx(handle, &size)) return FilesystemWin32ErrorCode(GetLastError(), "GetFileSize");
    return CheckedCast<u64>(size.QuadPart);
}

ErrorCodeOr<File> OpenFile(String filename, FileMode mode) {
    ASSERT(IsValidUtf8(filename));
    PathArena temp_allocator {Malloc::Instance()};

    auto const w_path =
        TRY(path::MakePathForWin32(filename, temp_allocator, path::IsAbsolute(filename))).path;

    auto const access = ({
        DWORD a {};
        auto const cap = ToInt(mode.capability);
        if ((cap & ToInt(FileMode::Capability::ReadWrite)) == ToInt(FileMode::Capability::ReadWrite))
            a = GENERIC_READ | GENERIC_WRITE;
        else if (cap & ToInt(FileMode::Capability::Write))
            a = GENERIC_WRITE;
        else if (cap & ToInt(FileMode::Capability::Read))
            a = GENERIC_READ;

        if (cap & ToInt(FileMode::Capability::Append)) {
            a |= FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES;
            a &= ~(DWORD)FILE_WRITE_DATA; // FILE_WRITE_DATA overwrides our desired append behaviour
        }
        a;
    });

    auto const share = ({
        DWORD s {};
        auto const share_flags = ToInt(mode.win32_share);
        if (share_flags & ToInt(FileMode::Share::Read)) s |= FILE_SHARE_READ;
        if (share_flags & ToInt(FileMode::Share::Write)) s |= FILE_SHARE_WRITE;
        if (share_flags & ToInt(FileMode::Share::DeleteRename)) s |= FILE_SHARE_DELETE;
        s;
    });

    auto const creation = ({
        DWORD c {};
        switch (mode.creation) {
            case FileMode::Creation::OpenExisting: c = OPEN_EXISTING; break;
            case FileMode::Creation::OpenAlways: c = OPEN_ALWAYS; break;
            case FileMode::Creation::CreateNew: c = CREATE_NEW; break;
            case FileMode::Creation::CreateAlways: c = CREATE_ALWAYS; break;
            case FileMode::Creation::TruncateExisting: c = TRUNCATE_EXISTING; break;
        }
        c;
    });

    PSID everyone_sid = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    DEFER {
        if (everyone_sid) FreeSid(everyone_sid);
        if (acl) LocalFree(acl);
        if (sd) LocalFree(sd);
    };
    SECURITY_ATTRIBUTES sa {};

    if (mode.everyone_read_write) {
        SID_IDENTIFIER_AUTHORITY sid_auth_world = SECURITY_WORLD_SID_AUTHORITY;
        if (AllocateAndInitializeSid(&sid_auth_world,
                                     1,
                                     SECURITY_WORLD_RID,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     &everyone_sid) == 0)
            return Win32ErrorCode(GetLastError(), "AllocateAndInitializeSid");

        EXPLICIT_ACCESSW ea {
            .grfAccessPermissions = SPECIFIC_RIGHTS_ALL | STANDARD_RIGHTS_ALL,
            .grfAccessMode = SET_ACCESS,
            .grfInheritance = NO_INHERITANCE,
            .Trustee {
                .TrusteeForm = TRUSTEE_IS_SID,
                .TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP,
                .ptstrName = (LPWSTR)everyone_sid,
            },
        };

        if (auto r = SetEntriesInAclW(1, &ea, nullptr, &acl) != ERROR_SUCCESS)
            return Win32ErrorCode(r, "SetEntriesInAcl");

        sd = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
        if (InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION) == 0)
            return Win32ErrorCode(GetLastError());
        if (SetSecurityDescriptorDacl(sd, TRUE, acl, FALSE) == 0) return Win32ErrorCode(GetLastError());

        sa = {
            .nLength = sizeof(SECURITY_ATTRIBUTES),
            .lpSecurityDescriptor = sd,
            .bInheritHandle = FALSE,
        };
    }

    auto handle = CreateFileW(w_path.data,
                              access,
                              share,
                              sa.nLength ? &sa : nullptr,
                              creation,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (handle == INVALID_HANDLE_VALUE) return FilesystemWin32ErrorCode(GetLastError(), "CreateFileW");

    return File {handle};
}

EXTERN_C IMAGE_DOS_HEADER __ImageBase; // NOLINT(readability-identifier-naming, bugprone-reserved-identifier)

ErrorCodeOr<void> WindowsSetFileAttributes(String path, Optional<WindowsFileAttributes> attributes) {
    ASSERT(path::IsAbsolute(path));

    DWORD attribute_flags = FILE_ATTRIBUTE_NORMAL;
    if (attributes) {
        attribute_flags = 0;
        if (attributes->hidden) attribute_flags |= FILE_ATTRIBUTE_HIDDEN;
    }

    PathArena temp_path_arena {Malloc::Instance()};
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
    ASSERT_EQ(path[path_size], L'\0');

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
    ASSERT(IsValidUtf8(path));
    ASSERT(path::IsAbsolute(path));

    PathArena temp_path_arena {Malloc::Instance()};
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

ErrorCodeOr<MutableString> CurrentBinaryPath(Allocator& a) {
    PathArena temp_path_arena {Malloc::Instance()};
    auto const full_wide_path = TRY(Win32GetRunningProgramName(temp_path_arena));
    auto const result = Narrow(a, full_wide_path).Value();
    ASSERT(IsValidUtf8(result));
    return result;
}

static ErrorCodeOr<WString> VolumeName(const WCHAR* path, ArenaAllocator& arena) {
    auto buffer = arena.AllocateExactSizeUninitialised<WCHAR>(100);
    if (!GetVolumePathNameW(path, buffer.data, (DWORD)buffer.size))
        return FilesystemWin32ErrorCode(GetLastError(), "GetVolumePathNameW");
    return WString {buffer.data, wcslen(buffer.data)};
}

ErrorCodeOr<MutableString> TemporaryDirectoryOnSameFilesystemAs(String path, Allocator& a) {
    ASSERT(path::IsAbsolute(path));
    PathArena temp_path_arena {Malloc::Instance()};

    // standard temporary directory
    Array<WCHAR, MAX_PATH + 1> standard_temp_dir_buffer;
    auto size = GetTempPathW((DWORD)standard_temp_dir_buffer.size, standard_temp_dir_buffer.data);
    WString standard_temp_dir {};
    if (size && size < standard_temp_dir_buffer.size) {
        standard_temp_dir_buffer[size] = L'\0';
        standard_temp_dir = {standard_temp_dir_buffer.data, (usize)size};
    } else {
        standard_temp_dir = L"C:\\Windows\\Temp\\"_s;
    }
    auto const standard_temp_dir_volume = TRY(VolumeName(standard_temp_dir.data, temp_path_arena));

    //
    auto wide_path = WidenAllocNullTerm(temp_path_arena, path).Value();
    for (auto& c : wide_path)
        if (c == L'/') c = L'\\';
    auto const volume_name = TRY(VolumeName(wide_path.data, temp_path_arena));

    WString base_path {};
    if (volume_name == standard_temp_dir_volume)
        base_path = standard_temp_dir;
    else
        base_path = volume_name;

    WString wide_result {};
    {
        auto random_seed = (u64)NanosecondsSinceEpoch();
        auto const filename =
            Widen(temp_path_arena, UniqueFilename(k_temporary_directory_prefix, "", random_seed)).Value();

        auto wide_result_buffer =
            temp_path_arena.AllocateExactSizeUninitialised<WCHAR>(base_path.size + filename.size + 1);
        usize pos = 0;
        ASSERT_EQ(base_path[base_path.size - 1], L'\\');
        WriteAndIncrement(pos, wide_result_buffer, base_path);
        WriteAndIncrement(pos, wide_result_buffer, filename);
        WriteAndIncrement(pos, wide_result_buffer, L'\0');
        pos -= 1;
        if (!CreateDirectoryW(wide_result_buffer.data, nullptr))
            return FilesystemWin32ErrorCode(GetLastError(), "CreateDirectoryW");
        wide_result = {wide_result_buffer.data, pos};
    }

    auto const result = Narrow(a, wide_result).Value();
    ASSERT(path::IsAbsolute(result));
    ASSERT(IsValidUtf8(result));
    return result;
}

MutableString KnownDirectory(Allocator& a, KnownDirectoryType type, KnownDirectoryOptions options) {
    if (type == KnownDirectoryType::Temporary) {
        WCHAR buffer[MAX_PATH + 1];
        auto size = GetTempPathW((DWORD)ArraySize(buffer), buffer);
        WString wide_path {};
        if (size) {
            if (auto const last = buffer[size - 1]; last == L'\\' || last == L'/') --size;
            wide_path = {buffer, (usize)size};
        } else {
            if (options.error_log) {
                auto _ = fmt::FormatToWriter(*options.error_log,
                                             "Failed to get temp path: {}",
                                             FilesystemWin32ErrorCode(GetLastError(), "GetTempPathW"));
            }
            wide_path = L"C:\\Windows\\Temp"_s;
        }

        if (options.create) {
            if (!CreateDirectoryW(wide_path.data, nullptr)) {
                auto const err = GetLastError();
                if (err != ERROR_ALREADY_EXISTS) {
                    if (options.error_log) {
                        PathArena temp_path_arena {Malloc::Instance()};
                        auto _ = fmt::FormatToWriter(*options.error_log,
                                                     "Failed to create directory '{}': {}",
                                                     Narrow(temp_path_arena, wide_path),
                                                     FilesystemWin32ErrorCode(err, "CreateDirectoryW"));
                    }
                }
            }
        }

        auto result = Narrow(a, wide_path).Value();
        ASSERT(!path::IsDirectorySeparator(Last(result)));
        ASSERT(path::IsAbsolute(result));
        ASSERT(IsValidUtf8(result));
        return result;
    }

    struct KnownDirectoryConfig {
        GUID folder_id;
        Span<WString const> subfolders {};
        String fallback_absolute {};
        String fallback_user {};
    };

    KnownDirectoryConfig config {};
    switch (type) {
        case KnownDirectoryType::Temporary: {
            PanicIfReached();
        }
        case KnownDirectoryType::Logs:
            config.folder_id = FOLDERID_LocalAppData;
            config.fallback_user = "AppData\\Local";
            break;
        case KnownDirectoryType::Documents:
            config.folder_id = FOLDERID_Documents;
            config.fallback_user = "Documents";
            break;
        case KnownDirectoryType::Downloads:
            config.folder_id = FOLDERID_Downloads;
            config.fallback_user = "Downloads";
            break;
        case KnownDirectoryType::GlobalData:
            config.folder_id = FOLDERID_Public;
            config.fallback_absolute = "C:\\Users\\Public";
            break;
        case KnownDirectoryType::UserData:
            config.folder_id = FOLDERID_RoamingAppData;
            config.fallback_user = "AppData\\Roaming";
            break;

        case KnownDirectoryType::GlobalClapPlugins: {
            config.folder_id = FOLDERID_ProgramFilesCommon;
            static constexpr auto k_dirs = Array {L"CLAP"_s};
            config.subfolders = k_dirs;
            config.fallback_absolute = "C:\\Program Files\\Common Files\\CLAP";
            break;
        }
        case KnownDirectoryType::UserClapPlugins: {
            config.folder_id = FOLDERID_LocalAppData;
            static constexpr auto k_dirs = Array {L"CLAP"_s};
            config.subfolders = k_dirs;
            config.fallback_user = "AppData\\Local\\CLAP";
            break;
        }
        case KnownDirectoryType::GlobalVst3Plugins: {
            config.folder_id = FOLDERID_ProgramFilesCommon;
            static constexpr auto k_dirs = Array {L"VST3"_s};
            config.subfolders = k_dirs;
            config.fallback_absolute = "C:\\Program Files\\Common Files\\VST3";
            break;
        }
        case KnownDirectoryType::UserVst3Plugins: {
            config.folder_id = FOLDERID_UserProgramFilesCommon;
            config.fallback_user = "AppData\\Local\\Programs\\Common";
            static constexpr auto k_dirs = Array {L"VST3"_s};
            config.subfolders = k_dirs;
            break;
        }

        case KnownDirectoryType::MirageGlobalPreferences:
            config.folder_id = FOLDERID_ProgramData;
            config.fallback_absolute = "C:\\ProgramData";
            break;
        case KnownDirectoryType::MiragePreferences:
            config.folder_id = FOLDERID_RoamingAppData;
            config.fallback_user = "AppData\\Roaming";
            break;
        case KnownDirectoryType::MiragePreferencesAlternate:
            config.folder_id = FOLDERID_RoamingAppData;
            config.fallback_user = "AppData\\Roaming";
            break;
        case KnownDirectoryType::MirageGlobalData:
            config.folder_id = FOLDERID_Public;
            config.fallback_absolute = "C:\\Users\\Public";
            break;

        case KnownDirectoryType::Count: PanicIfReached(); break;
    }

    PWSTR wide_file_path_null_term = nullptr;
    auto hr = SHGetKnownFolderPath(config.folder_id,
                                   options.create ? KF_FLAG_CREATE : KF_FLAG_DEFAULT,
                                   nullptr,
                                   &wide_file_path_null_term);
    // The API says it should be freed regardless of if SHGetKnownFolderPath succeeded
    DEFER { CoTaskMemFree(wide_file_path_null_term); };

    if (hr != S_OK) {
        if (options.error_log) {
            auto _ = fmt::FormatToWriter(
                *options.error_log,
                "Failed to get known directory {{{08X}-{04X}-{04X}-{02X}{02X}-{02X}{02X}{02X}{02X}{02X}{02X}}}: {}",
                config.folder_id.Data1,
                config.folder_id.Data2,
                config.folder_id.Data3,
                config.folder_id.Data4[0],
                config.folder_id.Data4[1],
                config.folder_id.Data4[2],
                config.folder_id.Data4[3],
                config.folder_id.Data4[4],
                config.folder_id.Data4[5],
                config.folder_id.Data4[6],
                config.folder_id.Data4[7],
                FilesystemWin32ErrorCode(HresultToWin32(hr), "SHGetKnownFolderPath"));
        }
        auto const fallback = ({
            MutableString f {};
            if (config.fallback_absolute.size) {
                f = a.Clone(config.fallback_absolute);
                ASSERT(path::IsAbsolute(f));
                ASSERT(IsValidUtf8(f));
            } else {
                ASSERT(config.fallback_user.size);
                Array<WCHAR, UNLEN + 1> wbuffer {};
                Array<char, MaxNarrowedStringSize(wbuffer.size)> buffer {};
                String username = "User";
                auto size = (DWORD)wbuffer.size;
                if (GetUserNameW(wbuffer.data, &size)) {
                    if (size > 0) {
                        auto const narrow_size = NarrowToBuffer(buffer.data, {wbuffer.data, size - 1});
                        if (narrow_size) username = String {buffer.data, *narrow_size};
                    }
                } else if (options.error_log) {
                    auto _ = fmt::FormatToWriter(*options.error_log,
                                                 "Failed to get username: {}",
                                                 FilesystemWin32ErrorCode(GetLastError(), "GetUserNameW"));
                }

                f = fmt::Join(a,
                              Array {
                                  "C:\\Users\\"_s,
                                  username,
                                  "\\"_s,
                                  config.fallback_user,
                              });
                ASSERT(path::IsAbsolute(f));
                ASSERT(IsValidUtf8(f));
            }
            f;
        });
        if (options.create) {
            auto _ = CreateDirectory(fallback,
                                     {
                                         .create_intermediate_directories = true,
                                         .fail_if_exists = false,
                                         .win32_hide_dirs_starting_with_dot = false,
                                     });
        }
        return fallback;
    }

    auto const wide_path = WString {wide_file_path_null_term, wcslen(wide_file_path_null_term)};

    MutableString result;
    if (config.subfolders.size) {
        PathArena temp_path_arena {Malloc::Instance()};
        DynamicArray<wchar_t> wide_result {wide_path, temp_path_arena};
        for (auto const subfolder : config.subfolders) {
            dyn::Append(wide_result, L'\\');
            dyn::AppendSpan(wide_result, subfolder);
            if (options.create) {
                dyn::Append(wide_result, L'\0');
                DEFER { dyn::Pop(wide_result); };
                if (!CreateDirectoryW(wide_result.data, nullptr)) {
                    auto const err = GetLastError();
                    if (err != ERROR_ALREADY_EXISTS) {
                        if (options.error_log) {
                            auto _ = fmt::FormatToWriter(*options.error_log,
                                                         "Failed to create directory '{}': {}",
                                                         Narrow(temp_path_arena, wide_result),
                                                         FilesystemWin32ErrorCode(err, "CreateDirectoryW"));
                        }
                    }
                }
            }
        }
        result = Narrow(a, wide_result).Value();
    } else {
        result = Narrow(a, wide_path).Value();
    }

    ASSERT(!path::IsDirectorySeparator(Last(result)));
    ASSERT(path::IsAbsolute(result));
    ASSERT(IsValidUtf8(result));

    return result;
}

ErrorCodeOr<FileType> GetFileType(String absolute_path) {
    ASSERT(path::IsAbsolute(absolute_path));
    ASSERT(IsValidUtf8(absolute_path));

    PathArena temp_path_arena {Malloc::Instance()};

    auto const attributes =
        GetFileAttributesW(TRY(path::MakePathForWin32(absolute_path, temp_path_arena, true)).path.data);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return FilesystemWin32ErrorCode(GetLastError(), "GetFileAttributesW");

    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return FileType::Directory;
    return FileType::File;
}

ErrorCodeOr<MutableString> AbsolutePath(Allocator& a, String path) {
    ASSERT(path.size);
    ASSERT(IsValidUtf8(path));

    PathArena temp_path_arena {Malloc::Instance()};
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
    ASSERT(!path::IsDirectorySeparator(Last(result)));
    ASSERT(path::IsAbsolute(result));
    return result;
}

ErrorCodeOr<MutableString> CanonicalizePath(Allocator& a, String path) {
    ASSERT(IsValidUtf8(path));
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

ErrorCodeOr<String> TrashFileOrDirectory(String path, Allocator&) {
    ASSERT(IsValidUtf8(path));
    ASSERT(path::IsAbsolute(path));

    PathArena temp_path_arena {Malloc::Instance()};
    DynamicArray<WCHAR> wide_path {temp_path_arena};
    WidenAppend(wide_path, path);
    dyn::AppendSpan(wide_path, L"\0\0"); // double null terminated
    Replace(wide_path, L'/', L'\\');

    SHFILEOPSTRUCTW file_op {
        .hwnd = nullptr,
        .wFunc = FO_DELETE,
        .pFrom = wide_path.data,
        .pTo = nullptr,
        .fFlags = FOF_ALLOWUNDO | FOF_NO_UI | FOF_WANTNUKEWARNING,
    };

    if (auto const r = SHFileOperationW(&file_op); r != 0)
        return FilesystemWin32ErrorCode((DWORD)r, "SHFileOperationW");

    return path;
}

ErrorCodeOr<void> Delete(String path, DeleteOptions options) {
    ASSERT(IsValidUtf8(path));
    ASSERT(path::IsAbsolute(path));

    PathArena temp_path_arena {Malloc::Instance()};
    auto const wide_path = TRY(path::MakePathForWin32(path, temp_path_arena, true));

    auto const is_error_ok = [&options](DWORD error) {
        if (options.fail_if_not_exists) return false;
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return true;
        return false;
    };

    if (options.type == DeleteOptions::Type::Any) {
        if (DeleteFileW(wide_path.path.data) != 0)
            return k_success;
        else if (is_error_ok(GetLastError()))
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
                if (is_error_ok(GetLastError())) return k_success;
                return FilesystemWin32ErrorCode(GetLastError(), "DeleteW");
            }
            break;
        }
        case DeleteOptions::Type::DirectoryOnlyIfEmpty: {
            if (RemoveDirectoryW(wide_path.path.data) != 0) {
                return k_success;
            } else {
                if (is_error_ok(GetLastError())) return k_success;
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
                if (is_error_ok(GetLastError())) return k_success;
                if (GetLastError() == ERROR_DIR_NOT_EMPTY)
                    return Win32DeleteDirectory(wide_path.path, temp_path_arena);
                return FilesystemWin32ErrorCode(GetLastError(), "RemoveDirectoryW");
            }
            break;
        }
    }
}

ErrorCodeOr<void> CopyFile(String from, String to, ExistingDestinationHandling existing) {
    ASSERT(IsValidUtf8(from));
    ASSERT(IsValidUtf8(to));
    ASSERT(path::IsAbsolute(from));
    ASSERT(path::IsAbsolute(to));
    PathArena temp_path_arena {Malloc::Instance()};

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
    PathArena temp_path_arena {Malloc::Instance()};

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
    ASSERT(IsValidUtf8(from));
    ASSERT(IsValidUtf8(to));
    ASSERT(path::IsAbsolute(from));
    ASSERT(path::IsAbsolute(to));
    PathArena temp_path_arena {Malloc::Instance()};

    auto to_wide = TRY(path::MakePathForWin32(to, temp_path_arena, true)).path;

    // MoveFileExW for directories only succeeds if the destination is an empty directory. Do to make
    // Rename consistent across Windows and POSIX rename() we try to delete the empty dir first.
    RemoveDirectoryW(to_wide.data);

    if (!MoveFileExW(TRY(path::MakePathForWin32(from, temp_path_arena, true)).path.data,
                     to_wide.data,
                     MOVEFILE_REPLACE_EXISTING)) {
        auto err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            // When the destination is a non-empty directory we don't get ERROR_DIR_NOT_EMPTY as we might
            // expect, but instead ERROR_ACCESS_DENIED. Let's try and fix that.
            if (PathIsANonEmptyDirectory(to_wide)) err = ERROR_DIR_NOT_EMPTY;
        }
        return FilesystemWin32ErrorCode(err, "MoveFileExW");
    }
    return k_success;
}

//
// ==========================================================================================================

namespace dir_iterator {

static Entry MakeEntry(WIN32_FIND_DATAW const& data, ArenaAllocator& arena) {
    auto filename = Narrow(arena, FromNullTerminated(data.cFileName)).Value();
    ASSERT(IsValidUtf8(filename));
    return {
        .subpath = filename,
        .type = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FileType::Directory : FileType::File,
        .file_size = (data.nFileSizeHigh * (MAXDWORD + 1)) + data.nFileSizeLow,
    };
}

static bool StringIsDot(WString filename) { return filename == L"."_s || filename == L".."_s; }
static bool CharIsDot(WCHAR c) { return c == L'.'; }
static bool CharIsSlash(WCHAR c) { return c == L'\\'; }

static bool ShouldSkipFile(WString filename, bool skip_dot_files) {
    for (auto c : filename)
        ASSERT(!CharIsSlash(c));
    return StringIsDot(filename) || (skip_dot_files && filename.size && CharIsDot(filename[0]));
}

ErrorCodeOr<Iterator> Create(ArenaAllocator& a, String path, Options options) {
    auto result = TRY(Iterator::InternalCreate(a, path, options));
    return result;
}

void Destroy(Iterator& it) {
    if (it.handle) FindClose(it.handle);
}

ErrorCodeOr<Optional<Entry>> Next(Iterator& it, ArenaAllocator& result_arena) {
    if (it.reached_end) return Optional<Entry> {};

    if (!it.handle) {
        PathArena temp_path_arena {Malloc::Instance()};
        auto const wpath =
            path::MakePathForWin32(ArrayT<WString>({Widen(temp_path_arena, it.base_path).Value(),
                                                    Widen(temp_path_arena, it.options.wildcard).Value()}),
                                   temp_path_arena,
                                   true)
                .path;

        WIN32_FIND_DATAW data {};
        auto handle = FindFirstFileExW(wpath.data,
                                       FindExInfoBasic,
                                       &data,
                                       FindExSearchNameMatch,
                                       nullptr,
                                       FIND_FIRST_EX_LARGE_FETCH);
        if (handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) {
                // The search could not find any files.
                it.reached_end = true;
                return Optional<Entry> {};
            }
            return FilesystemWin32ErrorCode(GetLastError(), "FindFirstFileW");
        }
        it.handle = handle;
        ASSERT(it.handle != nullptr);

        if (ShouldSkipFile(FromNullTerminated(data.cFileName), it.options.skip_dot_files))
            return Next(it, result_arena);

        return MakeEntry(data, result_arena);
    }

    while (true) {
        WIN32_FIND_DATAW data {};
        if (!FindNextFileW(it.handle, &data)) {
            if (GetLastError() == ERROR_NO_MORE_FILES) {
                it.reached_end = true;
                return Optional<Entry> {};
            } else {
                return FilesystemWin32ErrorCode(GetLastError(), "FindNextFileW");
            }
        }

        if (ShouldSkipFile(FromNullTerminated(data.cFileName), it.options.skip_dot_files)) continue;

        return MakeEntry(data, result_arena);
    }

    return Optional<Entry> {};
}

} // namespace dir_iterator

//
// ==========================================================================================================

// Directory watcher
// Jim Beveridge's excellent blog post on the ReadDirectoryChangesW API:
// https://qualapps.blogspot.com/2010/05/understanding-readdirectorychangesw_19.html

constexpr DWORD k_directory_changes_filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                             FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;

constexpr bool k_debug_log_watcher = false && !PRODUCTION_BUILD;

struct WindowsWatchedDirectory {
    alignas(16) Array<u8, Kb(32)> buffer;
    HANDLE handle {};
    OVERLAPPED overlapped {};
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
    ASSERT(IsValidUtf8(dir.path));
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
        switch (error) {
            case ERROR_INVALID_FUNCTION: {
                // If the network redirector or the target file system does not support this operation, the
                // function fails with ERROR_INVALID_FUNCTION.
                return ErrorCode {FilesystemError::NotSupported};
            }
            case ERROR_NOACCESS: {
                Panic(
                    "ReadDirectoryChangesW fails with ERROR_NOACCESS when the buffer is not aligned on a DWORD boundary.");
                case ERROR_INVALID_PARAMETER: {
                    Panic(
                        "ReadDirectoryChangesW fails with ERROR_INVALID_PARAMETER when the buffer length is greater than 64 KB and the application is monitoring a directory over the network. This is due to a packet size limitation with the underlying file sharing protocols.");
                }
            }
        }
        return FilesystemWin32ErrorCode(error);
    }

    return windows_dir;
}

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
                    ASSERT(base < end, "invalid data from ReadDirectoryChangesW");
                    ASSERT((usize)(end - base) >= min_chunk_size, "invalid data from ReadDirectoryChangesW");

                    ASSERT(bytes_transferred >= min_chunk_size);

                    DWORD action;
                    DWORD next_entry_offset;
                    Array<wchar_t, 1000> filename_buf;
                    WString filename;

                    {
                        // I've found that it's possible to receive
                        // FILE_NOTIFY_INFORMATION.NextEntryOffset values that result in the next event
                        // being misaligned. Reading unaligned memory is not normally a great idea for
                        // performance. And if you have UBSan enabled, it will crash. To work around this,
                        // we copy the given memory into correctly aligned structures. Another option
                        // would be to disable UBSan for this function but I'm not sure of the
                        // consequences of misaligned reads so let's play it safe.

                        ASSERT(bytes_transferred != 1);
                        FILE_NOTIFY_INFORMATION event;
                        __builtin_memcpy_inline(&event, base, sizeof(event));

                        ASSERT((base + event.FileNameLength) <= end,
                               "invalid data from ReadDirectoryChangesW");
                        constexpr DWORD k_valid_actions =
                            FILE_ACTION_ADDED | FILE_ACTION_REMOVED | FILE_ACTION_MODIFIED |
                            FILE_ACTION_RENAMED_OLD_NAME | FILE_ACTION_RENAMED_NEW_NAME;
                        ASSERT((event.Action & ~k_valid_actions) == 0,
                               "invalid data from ReadDirectoryChangesW");
                        ASSERT(event.FileNameLength % sizeof(wchar_t) == 0,
                               "invalid data from ReadDirectoryChangesW");

                        auto const num_wchars = event.FileNameLength / sizeof(wchar_t);
                        ASSERT(num_wchars <= filename_buf.size);

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
                            ASSERT(IsValidUtf8(narrowed.Value()));
                            if constexpr (k_debug_log_watcher)
                                LogDebug(ModuleName::Filesystem,
                                         "ReadDirectoryChanges: {} {}",
                                         narrowed.Value(),
                                         DirectoryWatcher::ChangeType::ToString(changes));
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
        } else {
            // For WAIT_IO_COMPLETION, WAIT_ABANDONED, WAIT_TIMEOUT, or any other result just continue to the
            // next directory without processing changes; we'll catch any pending changes in the next poll. We
            // have seen WAIT_IO_COMPLETION in the wild.
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
            else {
                ASSERT(error != ERROR_INVALID_PARAMETER);
                ASSERT(error != ERROR_INVALID_FUNCTION);
                dir.directory_changes.error = FilesystemWin32ErrorCode(error);
            }
            continue;
        }
    }

    watcher.RemoveAllNotWatching();

    return watcher.AllDirectoryChanges(args.result_arena);
}
