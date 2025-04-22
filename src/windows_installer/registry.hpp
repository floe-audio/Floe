// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <shlobj.h>
#include <windows.h>
//
#include "os/misc_windows.hpp"
#include "os/undef_windows_macros.h"
//

#include "foundation/foundation.hpp"

#include "common_infrastructure/error_reporting.hpp"

// Never change this.
constexpr auto k_uninstall_key =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{1395024D-2B55-4B81-88CA-26DF09D175B1}";

PUBLIC Optional<String> UninstallerPath(ArenaAllocator& arena, bool create) {
    PWSTR wide_dir {};
    auto const hr = SHGetKnownFolderPath(FOLDERID_ProgramFiles,
                                         create ? KF_FLAG_CREATE : KF_FLAG_DEFAULT,
                                         nullptr,
                                         &wide_dir);
    DEFER { CoTaskMemFree(wide_dir); };
    if (hr != S_OK) {
        ReportError(ErrorLevel::Warning, k_nullopt, "Failed to get Program Files directory: {}", hr);
        return k_nullopt;
    }

    auto path = *Narrow(arena, FromNullTerminated(wide_dir));
    path = path::JoinAppendResizeAllocation(arena, path, Array {"Floe"_s});

    if (create) {
        TRY_OR(CreateDirectory(path, {.create_intermediate_directories = false, .fail_if_exists = false}), {
            ReportError(ErrorLevel::Warning, k_nullopt, "Failed to create directory: {}", error);
            return k_nullopt;
        });
    }

    path = path::JoinAppendResizeAllocation(arena,
                                            path,
                                            Array {path::Filename(UNINSTALLER_PATH_RELATIVE_BUILD_ROOT)});

    return path;
}

static bool TryOrReportError(LONG rc, String action) {
    if (rc == ERROR_SUCCESS) return true;
    ReportError(ErrorLevel::Warning, k_nullopt, "Failed to {}: {}", action, Win32ErrorCode((DWORD)rc));
    return false;
}

static bool SetRegString(HKEY h_key, wchar_t const* name, WString value, bool required) {
    // We're expecting a null terminator at the end of the string, but not included in the size.
    ASSERT(Last(value) != 0);
    ASSERT(*(value.data + value.size) == 0);
    auto const rc = RegSetValueExW(h_key,
                                   name,
                                   0,
                                   REG_SZ,
                                   (const BYTE*)value.data,
                                   (DWORD)(value.size + 1) * sizeof(wchar_t));
    if (required) return TryOrReportError(rc, "set registry string"_s);

    return true;
}

PUBLIC void CreateUninstallRegistryKey(ArenaAllocator& arena, String uninstaller_exe_path) {
    // Create the uninstall registry key
    HKEY h_key;
    if (!TryOrReportError(RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                                          k_uninstall_key,
                                          0,
                                          nullptr,
                                          REG_OPTION_NON_VOLATILE,
                                          KEY_WRITE,
                                          nullptr,
                                          &h_key,
                                          nullptr),
                          "create uninstall registry key")) {
        return;
    }
    DEFER { RegCloseKey(h_key); };

    // Set the uninstall string (path to the uninstaller).
    // wide_path has a null terminator but it's not included in the size.
    auto const uninstall_path_wide = *WidenAllocNullTerm(arena, uninstaller_exe_path);
    if (!SetRegString(h_key, L"UninstallString", uninstall_path_wide, true)) return;

    if (!SetRegString(h_key, L"DisplayName", L"Floe Audio Plugin"_s, true)) return;

    // We can use the uninstaller as the icon source
    SetRegString(h_key, L"DisplayIcon", uninstall_path_wide, false);

    {
        DWORD const no_modify = 1;
        RegSetValueExW(h_key, L"NoModify", 0, REG_DWORD, (const BYTE*)&no_modify, sizeof(no_modify));
    }

    {
        DWORD const no_repair = 1;
        RegSetValueExW(h_key, L"NoRepair", 0, REG_DWORD, (const BYTE*)&no_repair, sizeof(no_repair));
    }

    SetRegString(h_key, L"DisplayVersion", L"" FLOE_VERSION_STRING, false);

    auto const floe_version = ParseVersionString(FLOE_VERSION_STRING).Value();

    {
        DWORD const major = floe_version.major;
        RegSetValueExW(h_key, L"VersionMajor", 0, REG_DWORD, (const BYTE*)&major, sizeof(major));
    }

    {
        DWORD const minor = floe_version.minor;
        RegSetValueExW(h_key, L"VersionMinor", 0, REG_DWORD, (const BYTE*)&minor, sizeof(minor));
    }

    {
        auto const version = ((DWORD)floe_version.major << 16) | (DWORD)floe_version.minor;
        RegSetValueExW(h_key, L"Version", 0, REG_DWORD, (const BYTE*)&version, sizeof(version));
    }

    SetRegString(h_key, L"URLInfoAbout", L"" FLOE_HOMEPAGE_URL, false);
}

PUBLIC void RemoveUninstallRegistryKey() {
    if (auto const rc = RegDeleteKeyW(HKEY_LOCAL_MACHINE, k_uninstall_key); rc != ERROR_SUCCESS)
        ReportError(ErrorLevel::Warning,
                    k_nullopt,
                    "Failed to delete uninstall registry key: {}",
                    Win32ErrorCode((DWORD)rc));
}

PUBLIC void RemoveFileOnReboot(String path, ArenaAllocator& arena) {
    auto const wide_path = *WidenAllocNullTerm(arena, path);
    if (!MoveFileExW(wide_path.data, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
        ReportError(ErrorLevel::Warning,
                    k_nullopt,
                    "Failed to schedule file for deletion on reboot: {}: {}",
                    path,
                    Win32ErrorCode(GetLastError()));
}
