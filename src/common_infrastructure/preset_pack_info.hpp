// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

constexpr auto k_metadata_filename = "floe-preset-pack.ini"_s;

struct PresetPackInfo {
    u64 id {};
    String subtitle {};
    u16 minor_version {};
};

PresetPackInfo ParsePresetPackInfoFile(String file_data, ArenaAllocator& arena);
