// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

constexpr auto k_preset_bank_filename = "floe-preset-bank.ini"_s;

// This ID represents a bank that is not user-defined, but rather automatically generated.
constexpr auto k_misc_bank_id = HashComptime("misc-preset-bank");

struct PresetBank {
    bool operator==(PresetBank const& other) const = default;
    u64 id {};
    String subtitle {};
    u16 minor_version {};
};

PresetBank ParsePresetBankFile(String file_data, ArenaAllocator& arena);
