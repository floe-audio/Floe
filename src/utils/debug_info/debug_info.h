// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
// NOLINTBEGIN

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* CreateSelfModuleInfo();
void DestroySelfModuleInfo(void* module_info);

void SymbolInfo(void* module_info,
                size_t const* addresses,
                size_t num_addresses,
                void* user_data,
                void (*callback)(void* user_data,
                                 size_t address,
                                 char const* name,
                                 char const* compile_unit_name,
                                 char const* file,
                                 int line,
                                 int column));

#ifdef __cplusplus
}
#endif

// NOLINTEND
