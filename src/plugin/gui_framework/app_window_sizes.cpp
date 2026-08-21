// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app_window_sizes.hpp"

#include "tests/framework.hpp"

TEST_CASE(TestClampWindowSizeToScreen) {
    UiSize const screen {1920, 1080};

    // Unknown or degenerate screens: unchanged.
    CHECK(ClampWindowSizeToScreen({2000, 1400}, k_nullopt) == UiSize {2000, 1400});
    CHECK(ClampWindowSizeToScreen({1000, 700}, UiSize {0, 0}) == UiSize {1000, 700});
    CHECK(ClampWindowSizeToScreen({1000, 700}, UiSize {200, 140}) == UiSize {1000, 700});

    // Fits: unchanged.
    CHECK(ClampWindowSizeToScreen({1000, 700}, screen) == UiSize {1000, 700});

    // Oversized: bounded, aspect ratio preserved, never rounded up.
    auto const clamped = ClampWindowSizeToScreen({65520, 45864}, screen);
    CHECK(IsAspectRatio(clamped, k_gui_aspect_ratio));
    CHECK_LTE(clamped.width, (u16)((f32)screen.width * k_max_window_screen_fraction));
    CHECK_LTE(clamped.height, (u16)((f32)screen.height * k_max_window_screen_fraction));
    CHECK_GTE(clamped.width, k_min_gui_width);

    // Idempotent.
    CHECK(ClampWindowSizeToScreen(clamped, screen) == clamped);

    return k_success;
}

TEST_REGISTRATION(RegisterAppWindowSizeTests) { REGISTER_TEST(TestClampWindowSizeToScreen); }
