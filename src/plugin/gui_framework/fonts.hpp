// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <IconsFontAwesome6.h>

#include "build_resources/embedded_files.h"
#include "gui_framework/draw_list.hpp"
#include "style.hpp"

enum class FontType : u32 {
    Body,
    Heading1,
    Heading2,
    Heading3,
    Icons,
    Count,
};

using Fonts = Array<graphics::Font*, ToInt(FontType::Count)>;

PUBLIC void LoadFonts(graphics::DrawContext& graphics, Fonts& fonts, f32 pixels_per_point) {
    auto load_font = [&](BinaryData ttf, f32 size, graphics::GlyphRanges ranges) {
        size *= pixels_per_point;
        graphics::FontConfig config {};
        config.font_data_reference_only = true;
        return graphics.fonts.AddFontFromMemoryTTF((void*)ttf.data, (int)ttf.size, size, &config, ranges);
    };

    auto const def_ranges = graphics.fonts.GetGlyphRangesDefaultAudioPlugin();
    auto const roboto_ttf = EmbeddedRoboto();

    fonts[ToInt(FontType::Body)] = load_font(roboto_ttf, style::k_font_body_size, def_ranges);
    // IMPROVE: bold fonts
    fonts[ToInt(FontType::Heading1)] = load_font(roboto_ttf, style::k_font_heading1_size, def_ranges);
    fonts[ToInt(FontType::Heading2)] = load_font(roboto_ttf, style::k_font_heading2_size, def_ranges);
    fonts[ToInt(FontType::Heading3)] = load_font(roboto_ttf, style::k_font_heading3_size, def_ranges);

    auto const icons_ttf = EmbeddedFontAwesome();
    auto constexpr k_icon_ranges = Array {graphics::GlyphRange {ICON_MIN_FA, ICON_MAX_FA}};

    fonts[ToInt(FontType::Icons)] = load_font(icons_ttf, style::k_font_icons_size, k_icon_ranges);
}
