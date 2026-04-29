// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/loop_behaviour.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

// Per-layer info needed by the auto-description generator. Keeps the generator
// decoupled from LayerProcessor and its dependencies so it can be unit-tested
// with a hand-constructed StateSnapshot.
struct AutoDescriptionLayerInfo {
    String inst_name; // instrument display name, empty if unknown
    LoopBehaviour actual_loop_behaviour; // resolved loop behaviour for the layer
};

using AutoDescriptionString = DynamicArrayBounded<char, 200>;

// Generate a short human-readable description of the preset state. Used when a preset has no explicit
// description. The random_seed picks between wording variations so the output is stable for a given preset
// but varies between presets - typically seeded by Hash(preset_name). The output places a full stop after
// the most defining items (instrument name + first descriptive phrase) so SplitPresetDescription can split
// it into a short leading sentence and a longer continuation.
AutoDescriptionString GenerateAutoDescription(StateSnapshot const& state,
                                              Array<AutoDescriptionLayerInfo, k_num_layers> const& layer_info,
                                              u64 random_seed);

struct PresetDescriptionSplit {
    Optional<String> short_part;
    Optional<String> long_part;
};

// Splits a preset description into a short leading part and a longer continuation. The split point is a
// full stop or newline within the first k_short_max_codepoints UTF-8 codepoints. If the description
// already fits in that budget, only short_part is filled. If it's too long and no natural boundary
// exists, neither is filled. Returned views point into the input string.
PUBLIC PresetDescriptionSplit SplitPresetDescription(String description) {
    constexpr usize k_short_max_codepoints = 56;

    if (!description.size) return {};

    Optional<usize> boundary_byte {};
    usize byte_index = 0;
    usize codepoint_count = 0;
    while (byte_index < description.size && codepoint_count < k_short_max_codepoints) {
        auto const c = (u8)description[byte_index];
        if (c == '.' || c == '\n') {
            boundary_byte = byte_index;
            break;
        }
        usize advance = 1;
        if ((c & 0b11111000) == 0b11110000)
            advance = 4;
        else if ((c & 0b11110000) == 0b11100000)
            advance = 3;
        else if ((c & 0b11100000) == 0b11000000)
            advance = 2;
        byte_index += advance;
        codepoint_count++;
    }

    if (byte_index >= description.size) return {.short_part = description};

    if (!boundary_byte) return {};

    auto const short_end = *boundary_byte + 1;
    auto short_part = WhitespaceStripped(description.SubSpan(0, short_end));
    auto long_part = WhitespaceStripped(description.SubSpan(short_end));

    if (!short_part.size || !long_part.size) return {};

    return {.short_part = short_part, .long_part = long_part};
}
