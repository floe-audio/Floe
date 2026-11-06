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

// Returns null on failure. A non-null handle can still have no debug info available (e.g. a stripped binary),
// in which case error_buffer contains the reason and symbol info will only have addresses.
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
int IsAddressInCurrentModule(SelfModuleHandle module_info, size_t address);

// Only gets info for the current module (our shared library or executable, never for any externals)
// Should be thread safe and signal safe.
void SymbolInfo(SelfModuleHandle module_info,
                size_t const* addresses,
                size_t num_addresses,
                void* user_data,
                SymbolInfoCallback callback);

// Identity of our module as loaded in memory, using the conventions of Sentry's debug_meta images. Strings
// are null-terminated, empty if unavailable, and valid for the lifetime of the handle.
struct SelfModuleImageInfo {
    char const* type; // "elf", "macho" or "pe"
    size_t image_addr;
    size_t image_size;
    size_t image_vmaddr;
    char const* debug_id;
    char const* code_id;
    char const* code_file; // filename only, never a full path
    char const* debug_file; // filename only, never a full path
};

// Returns 0 if not available. Thread-safe and signal-safe.
int SelfModuleImage(SelfModuleHandle module_info, struct SelfModuleImageInfo* out);

#ifdef __cplusplus
}
#endif

// NOLINTEND
