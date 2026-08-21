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

constexpr f32 k_max_window_screen_fraction = 0.95f;

// Bounds a window size to a fraction of the screen, preserving the aspect ratio (rounding down, never up).
// If the screen is unknown or absurdly small, the size is returned unchanged. Idempotent.
constexpr UiSize ClampWindowSizeToScreen(UiSize size, Optional<UiSize> screen_size) {
    if (!screen_size) return size;

    auto const min_size = SizeWithAspectRatio(k_min_gui_width, k_gui_aspect_ratio);
    UiSize const max_size {
        (u16)((f32)screen_size->width * k_max_window_screen_fraction),
        (u16)((f32)screen_size->height * k_max_window_screen_fraction),
    };
    if (max_size.width < min_size.width || max_size.height < min_size.height) return size;

    if (size.width <= max_size.width && size.height <= max_size.height) return size;

    if (auto const clamped = NearestAspectRatioSizeInsideSize(max_size, k_gui_aspect_ratio);
        clamped && clamped->width >= k_min_gui_width)
        return *clamped;
    return min_size;
}

static_assert(ClampWindowSizeToScreen({2000, 1400}, k_nullopt) == UiSize {2000, 1400});
static_assert(ClampWindowSizeToScreen({1000, 700}, UiSize {1920, 1080}) == UiSize {1000, 700});
static_assert(ClampWindowSizeToScreen({65520, 45864}, UiSize {1920, 1080}) == UiSize {1460, 1022});
static_assert(ClampWindowSizeToScreen(ClampWindowSizeToScreen({65520, 45864}, UiSize {1920, 1080}),
                                      UiSize {1920, 1080}) ==
              ClampWindowSizeToScreen({65520, 45864}, UiSize {1920, 1080}));
static_assert(ClampWindowSizeToScreen({1000, 700}, UiSize {200, 140}) == UiSize {1000, 700});
static_assert(ClampWindowSizeToScreen({1000, 700}, UiSize {0, 0}) == UiSize {1000, 700});

// A step that is a multiple of the GUI aspect ratio width, but large enough that doing +1 step
// feels like a reasonable change.
constexpr u16 k_window_width_step = []() {
    u16 step = k_gui_aspect_ratio.width;
    while (step < 100)
        step += k_gui_aspect_ratio.width;
    return step;
}();
