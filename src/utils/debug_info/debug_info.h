// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
// NOLINTBEGIN

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// This is used in the Zig code to back into our C code.
void PanicHandler(char const* message, size_t message_length);

typedef void* SelfModuleHandle;

SelfModuleHandle CreateSelfModuleInfo(char* error_buffer, size_t error_buffer_size);
void DestroySelfModuleInfo(SelfModuleHandle module_info);

struct SymbolInfoData {
    size_t address;
    char const* name; // always valid
    char const* compile_unit_name; // always valid
    char const* file; // nullptr if not available
    int line; // -1 if not available
    int column; // -1 if not available
    int address_in_self_module; // bool: if the filename is in the current module
};

typedef void (*SymbolInfoCallback)(void* user_data, struct SymbolInfoData const* symbol_info);

// Fast, thread-safe and signal-safe.
// Checks if any of the given address are in the current module.
int HasAddressesInCurrentModule(SelfModuleHandle module_info, size_t const* addresses, size_t num_addresses);

// Only gets info for the current module (our shared library or executable, never for any externals)
// Should be thread safe and signal safe.
void SymbolInfo(SelfModuleHandle module_info,
                size_t const* addresses,
                size_t num_addresses,
                void* user_data,
                SymbolInfoCallback callback);

#ifdef __cplusplus
}
#endif

// NOLINTEND
