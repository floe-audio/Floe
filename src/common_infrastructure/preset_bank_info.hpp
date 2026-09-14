// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

constexpr auto k_preset_bank_filename = "floe-preset-bank.ini"_s;

// This ID represents a bank that is not user-defined, but rather automatically generated.
constexpr auto k_misc_bank_id = HashFnv1a("misc-preset-bank");

struct PresetBank {
    bool operator==(PresetBank const& other) const = default;
    u64 id {};
    String subtitle {};
    u16 revision {};
    Optional<u64> library_for_visuals_id {};

    // Relative path (posix separators) of a preset within the bank folder, including the extension. Empty if
    // the bank doesn't suggest one.
    String default_preset {};
};

PresetBank ParsePresetBankFile(String file_data, ArenaAllocator& arena);
