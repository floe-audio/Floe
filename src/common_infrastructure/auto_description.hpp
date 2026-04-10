// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

// Per-layer info needed by the auto-description generator. Keeps the generator
// decoupled from LayerProcessor and its dependencies so it can be unit-tested
// with a hand-constructed StateSnapshot.
struct AutoDescriptionLayerInfo {
    String inst_name; // instrument display name, empty if unknown
    bool inst_has_loops; // whether the loaded instrument actually has loops
};

using AutoDescriptionString = DynamicArrayBounded<char, 200>;

// Generate a short human-readable description of the preset state. Used when
// a preset has no explicit description. The random_seed is used to pick between
// wording variations so the output is stable for a given preset but varies
// between presets - typically seeded by Hash(preset_name).
// max_items caps the total number of items in the description (instrument name,
// descriptive phrases, FX summary each count as one); pass -1 for no limit.
AutoDescriptionString GenerateAutoDescription(StateSnapshot const& state,
                                              Array<AutoDescriptionLayerInfo, k_num_layers> const& layer_info,
                                              u64 random_seed,
                                              s32 max_items = -1);
