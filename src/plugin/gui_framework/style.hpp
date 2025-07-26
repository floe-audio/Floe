// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

#include "gui_framework/colours.hpp"

namespace style {

// convert from 0xRRGGBB to 0xAABBGGRR
constexpr u32 ToAbgr(u32 rgb) {
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
    Green,
    Red,
    Highlight,
    Blue,

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

    Count,
};

constexpr usize k_colour_bits = NumBitsNeededToStore(ToInt(Colour::Count));
constexpr u32 k_highlight_hue = 47;

constexpr auto k_colours = [] {
    Array<u32, ToInt(Colour::Count)> result {};

    // automatically generate tints from dark to light
    for (auto const col_index : Range<u32>(ToInt(Colour::Background0), ToInt(Colour::Text) + 1)) {
        constexpr auto k_size = ToInt(Colour::Text) - ToInt(Colour::Background0) + 1;
        auto const pos = (f32)(col_index - ToInt(Colour::Background0)) / (f32)(k_size - 1);

        auto const h = (u32)LinearInterpolate(pos, 200.0f, 210.0f);
        auto const s = (u32)LinearInterpolate(constexpr_math::Powf(pos, 0.4f), 21.0f, 8.0f);
        auto const l = (u32)LinearInterpolate(constexpr_math::Powf(pos, 1.2f), 96.0f, 28.0f);
        auto const a = 100u;
        result[col_index] = Hsla(h, s, l, a);
    }

    // check that text is readable on all backgrounds
    for (auto const bg : Array {Colour::Background0, Colour::Background1, Colour::Background2}) {
        for (auto const fg : Array {Colour::Text, Colour::Subtext1})
            if (Contrast(result[ToInt(bg)], result[ToInt(fg)]) < 4.5f) throw "";
    }

    // manually set the rest
    for (auto const i : Range(ToInt(Colour::Count))) {
        switch (Colour(i)) {
            case Colour::None: result[i] = 0; break;
            case Colour::Green: result[i] = ToAbgr(0x40A02B); break;
            case Colour::Red: result[i] = ToAbgr(0xFF8C71); break;
            case Colour::Blue: result[i] = ToAbgr(0x1E88E5); break;
            case Colour::Highlight: result[i] = Hsla(k_highlight_hue, 93, 78, 100); break;

            case Colour::Background0:
            case Colour::Background1:
            case Colour::Background2:
            case Colour::Surface0:
            case Colour::Surface1:
            case Colour::Surface2:
            case Colour::Overlay0:
            case Colour::Overlay1:
            case Colour::Overlay2:
            case Colour::Subtext0:
            case Colour::Subtext1:
            case Colour::Text: break;
            case Colour::Count: break;
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
constexpr f32 k_scrollbar_rhs_space = 2.0f;
constexpr f32 k_scrollbar_lhs_space = 10.0f;
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
