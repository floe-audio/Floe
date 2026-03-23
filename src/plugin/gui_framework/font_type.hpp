// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

enum class FontType : u8 {
    Body,
    BodyItalic,
    Heading1,
    Heading2,
    Heading3, // Typically always capitalise all text with this font.
    Icons,
    Count,
};

constexpr f32 FontSizeWw(f32 font_pts) { return font_pts * (16.0f / 13.0f); }

constexpr f32 k_font_body_size = FontSizeWw(13);
constexpr f32 k_font_body_italic_size = FontSizeWw(12);
constexpr f32 k_font_heading1_size = FontSizeWw(18);
constexpr f32 k_font_heading2_size = FontSizeWw(14);
constexpr f32 k_font_heading3_size = FontSizeWw(10);
constexpr f32 k_font_icons_size = FontSizeWw(14);
