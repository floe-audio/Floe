// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

#include "gui_framework/colours.hpp"

enum class FontType : u32 {
    Body,
    BodyItalic,
    Heading1,
    Heading2,
    Heading3,
    Icons,
    Count,
};

namespace style {

enum class Colour : u8 {
    None,

    // These are the core building blocks of the UI, they are used for most things. They respond to the dark
    // mode flag.
    Background0, // Lightest (or darkest if DarkMode is set).
    Background1,
    Background2,
    Surface0,
    Surface1,
    Surface2,
    Overlay0,
    Overlay1,
    Overlay2,
    Subtext0,
    Subtext1,
    Text, // Darkest (or lightest if DarkMode is set).

    // Our GUI has a primary highlight colour used for accents, selections, etc. We use the Tailwind-style
    // range of tints of this accent varying from near-white (highlight50) to near-black (highlight950). These
    // don't respond to DarkMode.
    Highlight50,
    Highlight100,
    Highlight200,
    Highlight300,
    Highlight400,
    Highlight500,
    Highlight600,
    Highlight700,
    Highlight800,
    Highlight900,
    Highlight950,

    // Additional colours that don't respond to dark mode.
    Red,
    Green,
    Blue,

    Count,

    ColourMask = 0b00011111,
    ModifiersMask = 0b11100000,

    // Specify the dark mode variant of a colour.
    DarkMode = 1 << 5,

    // Percentage alpha variants. Default is 100% alpha.
    Alpha75 = 0b01 << 6,
    Alpha50 = 0b10 << 6,
    Alpha15 = 0b11 << 6,

    // Alias.
    Highlight = Highlight200,
};

static_assert(NumBitsNeededToStore(ToInt(Colour::Count)) <= 5);

constexpr Colour operator|(Colour a, Colour b) { return Colour(ToInt(a) | ToInt(b)); }
constexpr Colour operator&(Colour a, Colour b) { return Colour(ToInt(a) & ToInt(b)); }

constexpr usize k_colour_bits = NumBitsNeededToStore(ToInt(Colour::Count));
constexpr u32 k_highlight_hue = 47;

constexpr auto k_colours = [] {
    Array<u32, LargestRepresentableValue<u8>()> result {};

    // Automatically generate tints.
    for (auto const col_index : Range<u32>(ToInt(Colour::Background0), ToInt(Colour::Text) + 1)) {
        constexpr auto k_size = ToInt(Colour::Text) - ToInt(Colour::Background0) + 1;
        auto const pos = (f32)(col_index - ToInt(Colour::Background0)) / (f32)(k_size - 1);

        auto const h = (u32)LinearInterpolate(pos, 200.0f, 210.0f);

        // Light mode
        {
            auto const s = (u32)LinearInterpolate(constexpr_math::Powf(pos, 0.4f), 21.0f, 8.0f);
            auto const l = (u32)LinearInterpolate(constexpr_math::Powf(pos, 1.2f), 96.0f, 28.0f);
            auto const a = 100u;
            result[col_index] = colour::Hsla(h, s, l, a);
        }

        // Dark mode
        {
            auto const s = (u32)LinearInterpolate(constexpr_math::Powf(pos, 1.2f), 3.0f, 6.0f);
            auto const l = (u32)LinearInterpolate(constexpr_math::Powf(pos, 1.35f), 12.0f, 86.0f);
            auto const a = 100u;
            result[col_index | ToInt(Colour::DarkMode)] = colour::Hsla(h, s, l, a);
        }
    }

    // Check that text is readable on all backgrounds.
    for (auto const bg : Array {Colour::Background0, Colour::Background1, Colour::Background2}) {
        for (auto const fg : Array {Colour::Text, Colour::Subtext1})
            if (colour::Contrast(result[ToInt(bg)], result[ToInt(fg)]) < 4.5f) throw "";
    }

    // Manually set the rest.
    for (auto const i : Range(ToInt(Colour::Count))) {
        auto const dark_i = (usize)i | ToInt(Colour::DarkMode);
        switch (Colour(i)) {
            case Colour::Green: result[i] = result[dark_i] = colour::WebHex(0x40A02B); break;
            case Colour::Red: result[i] = result[dark_i] = colour::WebHex(0xFF8C71); break;
            case Colour::Blue: result[i] = result[dark_i] = colour::WebHex(0x66a9d4); break;

            case Colour::Highlight50: result[i] = result[dark_i] = colour::WebHex(0xfffbeb); break;
            case Colour::Highlight100: result[i] = result[dark_i] = colour::WebHex(0xfdf1c8); break;
            case Colour::Highlight200: result[i] = result[dark_i] = colour::WebHex(0xfbe595); break;
            case Colour::Highlight300: result[i] = result[dark_i] = colour::WebHex(0xf8ce51); break;
            case Colour::Highlight400: result[i] = result[dark_i] = colour::WebHex(0xf7ba28); break;
            case Colour::Highlight500: result[i] = result[dark_i] = colour::WebHex(0xf09910); break;
            case Colour::Highlight600: result[i] = result[dark_i] = colour::WebHex(0xd5740a); break;
            case Colour::Highlight700: result[i] = result[dark_i] = colour::WebHex(0xb1500c); break;
            case Colour::Highlight800: result[i] = result[dark_i] = colour::WebHex(0x8f3f11); break;
            case Colour::Highlight900: result[i] = result[dark_i] = colour::WebHex(0x763411); break;
            case Colour::Highlight950: result[i] = result[dark_i] = colour::WebHex(0x441904); break;

            default: break;
        }
    }

    // Fill in alpha variants.
    for (auto const i : Range(ToInt(Colour::Count))) {
        for (auto const dark_mode_bit : Array {(u8)0, ToInt(Colour::DarkMode)}) {
            u8 const idx = i | dark_mode_bit;
            auto const base_col = result[idx];
            if (base_col == 0) continue;
            result[idx | ToInt(Colour::Alpha75)] = colour::WithAlphaU8(base_col, (u8)(255 * 0.75f));
            result[idx | ToInt(Colour::Alpha50)] = colour::WithAlphaU8(base_col, (u8)(255 * 0.50f));
            result[idx | ToInt(Colour::Alpha15)] = colour::WithAlphaU8(base_col, (u8)(255 * 0.15f));
        }
    }

    return result;
}();

constexpr u32 Col(Colour colour) { return k_colours[ToInt(colour)]; }

constexpr f32 k_spacing = 16.0f;
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

constexpr f32 FontPoint(f32 font_pts) { return font_pts * (16.0f / 13.0f); }

constexpr f32 k_font_body_size = FontPoint(13);
constexpr f32 k_font_body_italic_size = FontPoint(12);
constexpr f32 k_font_heading1_size = FontPoint(18);
constexpr f32 k_font_heading2_size = FontPoint(14);
constexpr f32 k_font_heading3_size = FontPoint(10);
constexpr f32 k_font_icons_size = FontPoint(14);
constexpr f32 k_font_small_icons_size = FontPoint(10);

constexpr f32 k_library_icon_standard_size = 20;

} // namespace style
