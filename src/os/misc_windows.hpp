// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <objbase.h>
#include <windows.h>

#include "foundation/foundation.hpp"

ErrorCode Win32ErrorCode(DWORD error_code,
                         char const* info_for_developer = nullptr,
                         SourceLocation source_location = SourceLocation::Current());

// https://devblogs.microsoft.com/oldnewthing/20061103-07/?p=29133
constexpr DWORD HresultToWin32(HRESULT hr) {
    if ((HRESULT)(hr & (long)0xFFFF0000) == MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, 0))
        return HRESULT_CODE(hr);
    if (hr == S_OK) return ERROR_SUCCESS;
    return ERROR_CAN_NOT_COMPLETE; // Not a Win32 HRESULT so return a generic error code.
}

inline ErrorCode HresultErrorCode(HRESULT hr,
                                  char const* info_for_developer = nullptr,
                                  SourceLocation source_location = SourceLocation::Current()) {
    return Win32ErrorCode(HresultToWin32(hr), info_for_developer, source_location);
}
