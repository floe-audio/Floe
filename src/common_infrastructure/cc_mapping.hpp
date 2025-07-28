// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

struct MappedCcToParam {
    ParamIndex param;
    u7 cc;
};

constexpr auto const k_default_cc_to_param_mapping = Array {
    MappedCcToParam {ParamIndex::MasterTimbre, 11},
    MappedCcToParam {ParamIndex::MasterVolume, 7},
    MappedCcToParam {ParamIndex::Macro1, 1},
};
