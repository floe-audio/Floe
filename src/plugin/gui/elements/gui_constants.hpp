// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

enum class GuiStyleSystem {
    MidPanel, // On top of an image background, uses transparent black/white shades often.
    Overlay, // Modals and popups, often light-mode and bold.
    TopBottomPanels, // Dark-mode, bold.
};

constexpr f32 k_corner_rounding = 3.94f;
constexpr f32 k_default_spacing = 16.0f;
constexpr f32 k_button_padding_x = 5.0f;
constexpr f32 k_button_padding_y = 2.0f;
constexpr f32 k_scrollbar_width = 8.0f;
constexpr f32 k_scrollbar_rhs_space = 1.0f;
constexpr f32 k_panel_rounding = 7.0f;
constexpr f32 k_small_gap = 3.0f;
constexpr f32 k_medium_gap = 10.0f;
constexpr f32 k_icon_button_size = 16.0f;
constexpr f32 k_menu_item_padding_x = 8;
constexpr f32 k_menu_item_padding_y = 3;

constexpr f32 k_library_icon_standard_size = 20;
constexpr f32 k_small_knob_width = 28.90f;
constexpr f32x2 k_tooltip_pad = {7.6f, 6.4f};
