// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

constexpr f32 FontSizeWw(f32 font_pts) { return font_pts * (16.0f / 13.0f); }

constexpr f32 k_default_spacing = 16.0f;
constexpr f32 k_button_rounding = 3.0f;
constexpr f32 k_button_padding_x = 5.0f;
constexpr f32 k_button_padding_y = 2.0f;
constexpr f32 k_scrollbar_width = 6.0f;
constexpr f32 k_scrollbar_rhs_space = 1.0f;
constexpr f32 k_panel_rounding = 7.0f;
constexpr f32 k_small_gap = 3.0f;
constexpr f32 k_medium_gap = 10.0f;
constexpr f32 k_large_gap = 28.0f;
constexpr f32 k_icon_button_size = 16.0f;
constexpr f32 k_menu_item_padding_x = 8;
constexpr f32 k_menu_item_padding_y = 3;

constexpr f32 k_font_body_size = FontSizeWw(13);
constexpr f32 k_font_body_italic_size = FontSizeWw(12);
constexpr f32 k_font_heading1_size = FontSizeWw(18);
constexpr f32 k_font_heading2_size = FontSizeWw(14);
constexpr f32 k_font_heading3_size = FontSizeWw(10);
constexpr f32 k_font_icons_size = FontSizeWw(14);
constexpr f32 k_font_small_icons_size = FontSizeWw(10);

constexpr f32 k_library_icon_standard_size = 20;

enum class FontType : u8 {
    Body,
    BodyItalic,
    Heading1,
    Heading2,
    Heading3,
    Icons,
    Count,
};
