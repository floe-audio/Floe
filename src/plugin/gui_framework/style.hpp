// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

#include "gui_framework/colours.hpp"

namespace style {

// convert from 0xRRGGBB to 0xAABBGGRR
constexpr u32 FromWebColour(u32 rgb) {
    auto const r = (rgb & 0xFF0000) >> 16;
    auto const g = (rgb & 0x00FF00) >> 8;
    auto const b = (rgb & 0x0000FF);
    auto const a = 0xFFu;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

constexpr u32 Hsla(u32 hue_degrees, u32 saturation_percent, u32 lightness_percent, u32 alpha_percent) {
    auto const hue_to_rgb = [](float p, float q, float t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0f / 6) return p + ((q - p) * 6 * t);
        if (t < 1.0f / 2) return q;
        if (t < 2.0f / 3) return p + ((q - p) * (2.0f / 3 - t) * 6);
        return p;
    };

    auto const h = (f32)hue_degrees / 360.0f;
    auto const s = (f32)saturation_percent / 100.0f;
    auto const l = (f32)lightness_percent / 100.0f;
    auto const a = (f32)alpha_percent / 100.0f;
    colours::Col result {
        .a = (u8)(a * 255),
    };
    if (s == 0) {
        result.r = result.g = result.b = (u8)(l * 255); // grey
    } else {
        auto const q = l < 0.5f ? l * (1 + s) : l + s - (l * s);
        auto const p = (2 * l) - q;
        result.r = (u8)(hue_to_rgb(p, q, h + (1.0f / 3)) * 255);
        result.g = (u8)(hue_to_rgb(p, q, h) * 255);
        result.b = (u8)(hue_to_rgb(p, q, h - (1.0f / 3)) * 255);
    }

    return colours::ToU32(result);
}

constexpr u32 BlendColours(u32 bg, u32 fg) {
    auto const fg_col = colours::FromU32(fg);
    auto const bg_col = colours::FromU32(bg);
    auto const alpha = fg_col.a / 255.0f;
    auto const inv_alpha = 1.0f - alpha;
    auto const r = (u8)Min(255.0f, (fg_col.r * alpha) + (bg_col.r * inv_alpha));
    auto const g = (u8)Min(255.0f, (fg_col.g * alpha) + (bg_col.g * inv_alpha));
    auto const b = (u8)Min(255.0f, (fg_col.b * alpha) + (bg_col.b * inv_alpha));
    auto const a = (u8)Min(255.0f, fg_col.a + (bg_col.a * inv_alpha));
    return colours::ToU32(colours::Col {.a = a, .b = b, .g = g, .r = r});
}

constexpr f32 RelativeLuminance(u32 abgr) {
    auto const col = colours::FromU32(abgr);
    f32 rgb[3] {};
    rgb[0] = col.r / 255.0f;
    rgb[1] = col.g / 255.0f;
    rgb[2] = col.b / 255.0f;

    for (auto& c : rgb)
        if (c <= 0.03928f)
            c = c / 12.92f;
        else
            c = constexpr_math::Powf((c + 0.055f) / 1.055f, 2.4f);

    return (0.2126f * rgb[0]) + (0.7152f * rgb[1]) + (0.0722f * rgb[2]);
}

constexpr f32 Contrast(u32 abgr1, u32 abgr2) {
    auto const l1 = RelativeLuminance(abgr1);
    auto const l2 = RelativeLuminance(abgr2);
    return (Max(l1, l2) + 0.05f) / (Min(l1, l2) + 0.05f);
}

enum class Colour : u8 {
    None,

    // These are the core building blocks of the UI, they are used for most things. They respond to the dark
    // mode flag.
    Background0,
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
    Text,

    // Our GUI has a primary highlight colour used for accents, selections, etc. We use the Tailwind-style
    // range of tints of this accent varying from near-white (highlight50) to near-black (highlight950). These
    // don't respond to dark mode.
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
            result[col_index] = Hsla(h, s, l, a);
        }

        // Dark mode
        {
            auto const s = (u32)LinearInterpolate(constexpr_math::Powf(pos, 1.2f), 3.0f, 6.0f);
            auto const l = (u32)LinearInterpolate(constexpr_math::Powf(pos, 1.35f), 12.0f, 86.0f);
            auto const a = 100u;
            result[col_index | ToInt(Colour::DarkMode)] = Hsla(h, s, l, a);
        }
    }

    // Check that text is readable on all backgrounds.
    for (auto const bg : Array {Colour::Background0, Colour::Background1, Colour::Background2}) {
        for (auto const fg : Array {Colour::Text, Colour::Subtext1})
            if (Contrast(result[ToInt(bg)], result[ToInt(fg)]) < 4.5f) throw "";
    }

    // Manually set the rest.
    for (auto const i : Range(ToInt(Colour::Count))) {
        switch (Colour(i)) {
            case Colour::Green:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0x40A02B);
                break;
            case Colour::Red:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0xFF8C71);
                break;
            case Colour::Blue:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0x66a9d4);
                break;

            case Colour::Highlight50:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0xfffbeb);
                break;
            case Colour::Highlight100:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0xfdf1c8);
                break;
            case Colour::Highlight200:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0xfbe595);
                break;
            case Colour::Highlight300:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0xf8ce51);
                break;
            case Colour::Highlight400:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0xf7ba28);
                break;
            case Colour::Highlight500:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0xf09910);
                break;
            case Colour::Highlight600:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0xd5740a);
                break;
            case Colour::Highlight700:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0xb1500c);
                break;
            case Colour::Highlight800:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0x8f3f11);
                break;
            case Colour::Highlight900:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0x763411);
                break;
            case Colour::Highlight950:
                result[i] = result[i | ToInt(Colour::DarkMode)] = FromWebColour(0x441904);
                break;

            default: break;
        }
    }

    // Fill in alpha variants.
    for (auto const i : Range(ToInt(Colour::Count))) {
        for (auto const dark_mode_bit : Array {(u8)0, ToInt(Colour::DarkMode)}) {
            u8 const idx = i | dark_mode_bit;
            auto const base_col = result[idx];
            if (base_col == 0) continue;
            result[idx | ToInt(Colour::Alpha75)] = colours::WithAlpha(base_col, (u8)(255 * 0.75f));
            result[idx | ToInt(Colour::Alpha50)] = colours::WithAlpha(base_col, (u8)(255 * 0.50f));
            result[idx | ToInt(Colour::Alpha15)] = colours::WithAlpha(base_col, (u8)(255 * 0.15f));
        }
    }

    return result;
}();

constexpr u32 Col(Colour colour) { return k_colours[ToInt(colour)]; }

// TODO: use a strong type for viewport width
// struct Vw {
//     explicit constexpr Vw(f32 value) : value(value) {}
//     explicit constexpr operator f32() const { return value; }
//     f32 value;
// };
// constexpr Vw operator""_vw(long double value) { return Vw((f32)(value)); }

constexpr f32 k_spacing = 16.0f;
constexpr f32 k_button_rounding = 3.0f;
constexpr f32 k_button_padding_x = 5.0f;
constexpr f32 k_button_padding_y = 2.0f;
constexpr f32 k_scrollbar_rhs_space = 1.0f;
constexpr f32 k_panel_rounding = 7.0f;
constexpr f32 k_prefs_lhs_width = 190.0f;
constexpr f32 k_prefs_small_gap = 3.0f;
constexpr f32 k_prefs_medium_gap = 10.0f;
constexpr f32 k_prefs_large_gap = 28.0f;
constexpr f32 k_prefs_icon_button_size = 16.0f;
constexpr f32 k_menu_item_padding_x = 8;
constexpr f32 k_menu_item_padding_y = 3;
constexpr f32 k_notification_panel_width = 300;
constexpr f32 k_install_dialog_width = 400;
constexpr f32 k_install_dialog_height = 300;
constexpr f32 k_prefs_dialog_width = 625;
constexpr f32 k_prefs_dialog_height = 443;
constexpr f32 k_info_dialog_width = k_prefs_dialog_width;
constexpr f32 k_info_dialog_height = k_prefs_dialog_height;
constexpr f32 k_feedback_dialog_width = 400;
constexpr f32 k_feedback_dialog_height = k_prefs_dialog_height;

constexpr f64 k_tooltip_open_delay = 0.5;

constexpr f32 k_tooltip_max_width = 200;
constexpr f32 k_tooltip_pad_x = 5;
constexpr f32 k_tooltip_pad_y = 2;
constexpr f32 k_tooltip_rounding = k_button_rounding;

constexpr u32 k_auto_hot_white_overlay = Hsla(k_highlight_hue, 35, 70, 20);
constexpr u32 k_auto_active_white_overlay = Hsla(k_highlight_hue, 35, 70, 38);

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
