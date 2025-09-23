#pragma once
#include "foundation/foundation.hpp"

constexpr auto k_metadata_filename = "floe-preset-pack.ini"_s;

struct PresetPackMetadata {
    u64 id {};
    String subtitle {};
    u16 minor_version {};
};

PresetPackMetadata ParseMetadataFile(String file_data, ArenaAllocator& arena);
