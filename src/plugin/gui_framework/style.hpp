// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

#include "gui_framework/colours.hpp"

struct Col {
    // Colour type.
    // We have this inside the Col struct because it's so frequently used that we want the convenience of
    // being able to use the short Col::Name syntax.
    enum Id : u8 {
        None,

        // These are the core building blocks of the UI, they are used for most things. They respond to the
        // dark mode flag.
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
        // range of tints of this accent varying from near-white (highlight50) to near-black (highlight950).
        // These don't respond to DarkMode.
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
        Yellow,
        Error,
        Black,
        White,

        // Additional vivid colours.
        LimeGreen,
        YellowGreen,
        Orchid,
        HotPink,
        Amber,
        Lilac,
        Cyan,
        Coral,
        SkyBlue,
        Mint,

        Count,

        // Alias.
        Highlight = Highlight200,
    };

    constexpr u8 Index() const {
        static_assert(NumBitsNeededToStore(ToInt(Id::Count) - 1) <= 7);
        return (u8)c | (u8)(dark_mode << 7);
    }

    Id c : 7 = Id::None;
    u8 dark_mode : 1 = false;
    u8 alpha = 255; // 0 (transparent) to 255 (opaque).
};

constexpr u32 k_highlight_hue = 47;

constexpr u32 ToU32(Col colour) {
    static constexpr auto k_base_colour_values = [] {
        Array<u32, 255> result {};

        // Automatically generate tints.
        for (auto const col_index : Range<u32>(ToInt(Col::Background0), ToInt(Col::Text) + 1)) {
            constexpr auto k_size = ToInt(Col::Text) - ToInt(Col::Background0) + 1;
            auto const pos = (f32)(col_index - ToInt(Col::Background0)) / (f32)(k_size - 1);

            auto const h = (u32)LinearInterpolate(pos, 200.0f, 210.0f);

            // Light mode
            {
                auto const s = (u32)LinearInterpolate(constexpr_math::Powf(pos, 0.4f), 21.0f, 8.0f);
                auto const l = (u32)LinearInterpolate(constexpr_math::Powf(pos, 1.2f), 96.0f, 28.0f);
                auto const a = 100u;
                result[Col {.c = (Col::Id)col_index}.Index()] = colour::Hsla(h, s, l, a);
            }

            // Dark mode
            {
                auto const s = (u32)LinearInterpolate(constexpr_math::Powf(pos, 1.2f), 3.0f, 6.0f);
                auto const l = (u32)LinearInterpolate(constexpr_math::Powf(pos, 1.35f), 12.0f, 86.0f);
                auto const a = 100u;
                result[Col {.c = (Col::Id)col_index, .dark_mode = true}.Index()] = colour::Hsla(h, s, l, a);
            }
        }

        // Check that text is readable on all backgrounds.
        for (auto const bg : Array {Col::Background0, Col::Background1, Col::Background2}) {
            for (auto const fg : Array {Col::Text, Col::Subtext1})
                if (colour::Contrast(result[Col {.c = bg}.Index()], result[Col {.c = fg}.Index()]) < 4.5f)
                    throw "";
        }

        // Manually set the rest.
        for (auto const col_id : EnumIterator<Col::Id>()) {
            for (auto const dark_mode : Array {true, false}) {
                auto const idx = Col {.c = col_id, .dark_mode = dark_mode}.Index();
                switch (col_id) {
                    case Col::Green: result[idx] = colour::WebHex(0x40A02B); break;
                    case Col::Red: result[idx] = colour::WebHex(0xFF8C71); break;
                    case Col::Blue: result[idx] = colour::WebHex(0x66a9d4); break;
                    case Col::Yellow: result[idx] = colour::WebHex(0xFBFF3F); break;
                    case Col::Error: result[idx] = colour::WebHex(0xFF0000); break;
                    case Col::Black: result[idx] = colour::WebHex(0x000000); break;
                    case Col::White: result[idx] = colour::WebHex(0xFFFFFF); break;

                    case Col::LimeGreen: result[idx] = colour::WebHex(0x8AFF6D); break;
                    case Col::YellowGreen: result[idx] = colour::WebHex(0xDDFF5E); break;
                    case Col::Orchid: result[idx] = colour::WebHex(0xE891FF); break;
                    case Col::HotPink: result[idx] = colour::WebHex(0xFF5CBD); break;
                    case Col::Amber: result[idx] = colour::WebHex(0xFFC36A); break;
                    case Col::Lilac: result[idx] = colour::WebHex(0xC2B3FF); break;
                    case Col::Cyan: result[idx] = colour::WebHex(0x4AFFFF); break;
                    case Col::Coral: result[idx] = colour::WebHex(0xFF7777); break;
                    case Col::SkyBlue: result[idx] = colour::WebHex(0x89B7FF); break;
                    case Col::Mint: result[idx] = colour::WebHex(0x67FFA5); break;


                    case Col::Highlight50: result[idx] = colour::WebHex(0xfffbeb); break;
                    case Col::Highlight100: result[idx] = colour::WebHex(0xfdf1c8); break;
                    case Col::Highlight200: result[idx] = colour::WebHex(0xfbe595); break;
                    case Col::Highlight300: result[idx] = colour::WebHex(0xf8ce51); break;
                    case Col::Highlight400: result[idx] = colour::WebHex(0xf7ba28); break;
                    case Col::Highlight500: result[idx] = colour::WebHex(0xf09910); break;
                    case Col::Highlight600: result[idx] = colour::WebHex(0xd5740a); break;
                    case Col::Highlight700: result[idx] = colour::WebHex(0xb1500c); break;
                    case Col::Highlight800: result[idx] = colour::WebHex(0x8f3f11); break;
                    case Col::Highlight900: result[idx] = colour::WebHex(0x763411); break;
                    case Col::Highlight950: result[idx] = colour::WebHex(0x441904); break;

                    default: break;
                }
            }
        }

        return result;
    }();

    auto const c = k_base_colour_values[colour.Index()];
    return c ? colour::WithAlphaU8(c, colour.alpha) : 0;
}

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
