// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

#include "aspect_ratio.hpp"

constexpr UiSize k_gui_aspect_ratio = {10, 7};

constexpr u16 k_min_gui_width = SizeWithAspectRatio(300, k_gui_aspect_ratio).width;
constexpr u32 k_max_gui_width =
    SizeWithAspectRatio(LargestRepresentableValue<u16>() - k_gui_aspect_ratio.width, k_gui_aspect_ratio)
        .width;

// A step that is a multiple of the GUI aspect ratio width, but large enough that doing +1 step
// feels like a reasonable change.
constexpr u16 k_window_width_step = []() {
    u16 step = k_gui_aspect_ratio.width;
    while (step < 100)
        step += k_gui_aspect_ratio.width;
    return step;
}();
