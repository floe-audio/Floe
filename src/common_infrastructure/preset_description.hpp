// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "foundation/utils/string.hpp"

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

struct AutoDescription {
    AutoDescriptionString short_text; // instrument name + lead phrase + folder
    AutoDescriptionString long_text; // remaining phrases and FX summary
};

// Generate a short human-readable description of the preset state. Used when a preset has no explicit
// description. The random_seed picks between wording variations so the output is stable for a given preset
// but varies between presets - typically seeded by Hash(preset_name).
AutoDescription GenerateAutoDescription(StateSnapshot const& state,
                                        Array<AutoDescriptionLayerInfo, k_num_layers> const& layer_info,
                                        String folder_name,
                                        u64 random_seed);

struct PresetDescriptionSplit {
    Optional<String> short_part;
    Optional<String> long_part;
    bool mid_sentence_chop = false;
};

PUBLIC PresetDescriptionSplit SplitPresetDescription(String description) {
    constexpr usize k_short_max_codepoints = 56;

    if (!description.size) return {};

    auto const is_word_boundary = [&](usize i) {
        if (i >= description.size) return true;
        auto const c = description[i];
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    };
    auto const is_trailing_punct = [](char c) {
        return c == ')' || c == ']' || c == '}' || c == '"' || c == '\'';
    };

    Optional<usize> newline_end_byte {};
    Optional<usize> fullstop_end_byte {};
    Optional<usize> last_word_break_byte {};
    usize byte_index = 0;
    usize codepoint_count = 0;
    while (byte_index < description.size && codepoint_count < k_short_max_codepoints) {
        auto const c = (u8)description[byte_index];
        if (c == '\n') {
            newline_end_byte = byte_index + 1;
            break;
        }
        if (c == '.' && !fullstop_end_byte) {
            usize end = byte_index + 1;
            while (end < description.size && is_trailing_punct(description[end]))
                end++;
            if (is_word_boundary(end)) fullstop_end_byte = end;
        }
        if (is_word_boundary(byte_index)) last_word_break_byte = byte_index;
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

    if (byte_index >= description.size && !newline_end_byte) return {.short_part = description};

    auto const short_end_byte = newline_end_byte    ? newline_end_byte
                                : fullstop_end_byte ? fullstop_end_byte
                                                    : last_word_break_byte;

    if (!short_end_byte) return {};

    bool const mid_sentence_chop = !newline_end_byte && !fullstop_end_byte;

    auto const short_end = *short_end_byte;
    auto short_part = WhitespaceStripped(description.SubSpan(0, short_end));
    auto long_part = WhitespaceStripped(description.SubSpan(short_end));

    if (!short_part.size || !long_part.size) return {};

    return {.short_part = short_part, .long_part = long_part, .mid_sentence_chop = mid_sentence_chop};
}
