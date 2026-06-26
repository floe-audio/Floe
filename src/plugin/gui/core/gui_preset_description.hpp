// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "foundation/utils/string.hpp"

#include "common_infrastructure/preset_description.hpp"

#include "gui_framework/fonts.hpp"

struct PresetDescriptionDisplay {
    String top_text {};
    String bottom_text {};
    LongDescriptionKind kind = LongDescriptionKind::Auto;
};

// Computes how to split user_text / auto-description between the single-line top panel and the
// wrapping perform-panel column. If user_text is non-empty it takes the top slot (whole if it fits,
// otherwise split at '\n', sentence boundary, or word boundary) and auto_full_block fills the bottom;
// if user_text is empty, auto_headline takes the top and the bottom is left blank.
PUBLIC PresetDescriptionDisplay SplitPresetDescriptionForDisplay(String user_text,
                                                                  String auto_headline,
                                                                  String auto_full_block,
                                                                  Font const& font,
                                                                  f32 max_top_width) {
    if (!user_text.size)
        return {.top_text = auto_headline, .bottom_text = {}, .kind = LongDescriptionKind::Auto};

    if (max_top_width <= 0)
        return {.top_text = user_text, .bottom_text = auto_full_block, .kind = LongDescriptionKind::Auto};

    auto const measure_width = [&](String s) -> f32 { return font.CalcTextSize(s, {}).x; };

    auto const is_trailing_punct = [](char c) {
        return c == ')' || c == ']' || c == '}' || c == '"' || c == '\'';
    };

    // Returns {split_byte, clean_break} where clean_break is true if the split lands at a '\n' or after
    // a sentence-ending '.'. Returns nullopt if the whole text fits without splitting.
    auto const find_split = [&](String text) -> Optional<Pair<usize, bool>> {
        bool const has_newline = Contains(text, '\n');
        if (!has_newline && measure_width(text) <= max_top_width) return k_nullopt;

        if (has_newline) {
            for (usize i = 0; i < text.size; i++) {
                if (text[i] == '\n') {
                    if (measure_width(text.SubSpan(0, i)) <= max_top_width) return Pair<usize, bool> {i, true};
                    break;
                }
            }
        }

        String const wrap_input = has_newline ? text.SubSpan(0, *Find(text, '\n')) : text;

        auto const wrap_eol =
            font.CalcWordWrapPositionA(1.0f, wrap_input.data, End(wrap_input), max_top_width);
        if (wrap_eol <= wrap_input.data || wrap_eol >= End(wrap_input)) return k_nullopt;
        auto const split_byte = (usize)(wrap_eol - text.data);

        usize tail = split_byte;
        while (tail > 0 && (text[tail - 1] == ' ' || text[tail - 1] == '\t' ||
                            is_trailing_punct(text[tail - 1])))
            tail--;
        bool const clean = tail > 0 && text[tail - 1] == '.';
        return Pair<usize, bool> {split_byte, clean};
    };

    auto const split = find_split(user_text);
    if (!split)
        return {.top_text = user_text,
                .bottom_text = auto_full_block,
                .kind = LongDescriptionKind::Auto};

    auto const top = WhitespaceStripped(user_text.SubSpan(0, split->first));
    auto const bottom = WhitespaceStripped(user_text.SubSpan(split->first));
    if (!top.size || !bottom.size)
        return {.top_text = user_text,
                .bottom_text = auto_full_block,
                .kind = LongDescriptionKind::Auto};

    return {.top_text = top,
            .bottom_text = bottom,
            .kind = split->second ? LongDescriptionKind::User : LongDescriptionKind::UserContinued};
}
