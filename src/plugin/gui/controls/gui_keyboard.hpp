// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui/core/gui_fwd.hpp"

constexpr s8 k_highect_oct_on_keyboard = 10;
constexpr s8 k_num_octaves_default = 6;
constexpr s8 k_lowest_starting_oct = 0;
constexpr s8 k_octave_default_offset = 2;

constexpr s8 HighestStartingOct(s8 num_octaves) {
    return (s8)((k_highect_oct_on_keyboard + 1) - num_octaves);
}

constexpr s8 k_octave_lowest = (k_lowest_starting_oct - k_octave_default_offset);

constexpr s8 OctaveHighest(s8 num_octaves) {
    return (s8)(HighestStartingOct(num_octaves) - k_octave_default_offset);
}

constexpr s8 k_octave_highest = OctaveHighest(k_num_octaves_default);

struct KeyboardGuiKeyPressed {
    bool is_down;
    u7 note;
    f32 velocity;
};

Optional<KeyboardGuiKeyPressed>
KeyboardGui(GuiState& g, Rect r, int starting_octave, s8 num_octaves = k_num_octaves_default);
