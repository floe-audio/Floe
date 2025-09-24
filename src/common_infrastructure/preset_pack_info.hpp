#pragma once
#include "foundation/foundation.hpp"

constexpr auto k_metadata_filename = "floe-preset-pack.ini"_s;

struct PresetPackInfo {
    u64 id {};
    String subtitle {};
    u16 minor_version {};
};

PresetPackInfo ParseMetadataFile(String file_data, ArenaAllocator& arena);
