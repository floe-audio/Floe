// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

// We aim to ubiquitously use ABGR format colours stored in a u32.

namespace colour {

constexpr u32 k_red_shift = 0;
constexpr u32 k_green_shift = 8;
constexpr u32 k_blue_shift = 16;
constexpr u32 k_alpha_shift = 24;
constexpr u32 k_alpha_mask = 0xFF000000;
constexpr u32 k_alpha_mask_inv = 0x00FFFFFF;

struct Channels {
    u8 a, b, g, r;
};

constexpr Channels FromU32(u32 abgr) {
    return {
        .a = (u8)((abgr >> k_alpha_shift) & 0xFF),
        .b = (u8)((abgr >> k_blue_shift) & 0xFF),
        .g = (u8)((abgr >> k_green_shift) & 0xFF),
        .r = (u8)((abgr >> k_red_shift) & 0xFF),
    };
}

constexpr u32 ToU32(Channels c) {
    return ((u32)c.a << k_alpha_shift) | ((u32)c.b << k_blue_shift) | ((u32)c.g << k_green_shift) |
           ((u32)c.r << k_red_shift);
}

constexpr u32 Rgba(u8 r, u8 g, u8 b, f32 a) { return ToU32({.a = (u8)(a * 255.0f), .b = b, .g = g, .r = r}); }
constexpr u32 Rgb(u8 r, u8 g, u8 b) { return Rgba(r, g, b, 1.0f); }

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
    Channels result {
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

    return ToU32(result);
}
constexpr u32 Hsl(u32 hue_degrees, u32 saturation_percent, u32 lightness_percent) {
    return Hsla(hue_degrees, saturation_percent, lightness_percent, 100);
}

// Convert from 0xRRGGBB to 0xAABBGGRR.
constexpr u32 WebHex(u32 rgb, u8 alpha = 255) {
    return ToU32({
        .a = alpha,
        .b = (u8)(rgb & 0x0000FF),
        .g = (u8)((rgb & 0x00FF00) >> 8),
        .r = (u8)((rgb & 0xFF0000) >> 16),
    });
}

constexpr u32 BlendColours(u32 bg_abgr, u32 fg_abgr) {
    auto const fg_col = FromU32(fg_abgr);
    auto const bg_col = FromU32(bg_abgr);
    auto const alpha = fg_col.a / 255.0f;
    auto const inv_alpha = 1.0f - alpha;
    auto const r = (u8)Min(255.0f, (fg_col.r * alpha) + (bg_col.r * inv_alpha));
    auto const g = (u8)Min(255.0f, (fg_col.g * alpha) + (bg_col.g * inv_alpha));
    auto const b = (u8)Min(255.0f, (fg_col.b * alpha) + (bg_col.b * inv_alpha));
    auto const a = (u8)Min(255.0f, fg_col.a + (bg_col.a * inv_alpha));
    return ToU32(Channels {.a = a, .b = b, .g = g, .r = r});
}

constexpr f32 RelativeLuminance(u32 abgr) {
    auto const col = FromU32(abgr);
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

constexpr u32 WithAlphaU8(u32 abgr, u8 alpha) { return (abgr & k_alpha_mask_inv) | ((u32)alpha << 24); }
constexpr u32 WithAlphaF(u32 abgr, f32 alpha_fraction) {
    return WithAlphaU8(abgr, (u8)(alpha_fraction * 255.0f));
}

constexpr u32 ChangeBrightness(u32 abgr, f32 brightness_factor) {
    auto col = FromU32(abgr);
    col.r = (u8)Min((f32)col.r * brightness_factor, 255.0f);
    col.g = (u8)Min((f32)col.g * brightness_factor, 255.0f);
    col.b = (u8)Min((f32)col.b * brightness_factor, 255.0f);
    return ToU32(col);
}

constexpr u32 ChangeAlpha(u32 abgr, f32 scaling) {
    auto const new_val = (u8)Min((f32)((abgr >> k_alpha_shift) & 0xFF) * scaling, 255.0f);
    return (abgr & k_alpha_mask_inv) | ((u32)(new_val) << k_alpha_shift);
}

union ColourValues {
    struct {
        f32 r, g, b;
    };
    struct {
        f32 h, s, v;
    };
    f32x3 e;
};

// Convert rgb floats ([0-1],[0-1],[0-1]) to hsv floats ([0-1],[0-1],[0-1]), from Foley & van Dam p592
// Optimized http://lolengine.net/blog/2013/01/13/fast-rgb-to-hsv
// This function is from dear imgui
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT
constexpr ColourValues ConvertRgbtoHsv(ColourValues rgb) {
    f32 k = 0.f;
    if (rgb.g < rgb.b) {
        Swap(rgb.g, rgb.b);
        k = -1.f;
    }
    if (rgb.r < rgb.g) {
        Swap(rgb.r, rgb.g);
        k = -2.f / 6.f - k;
    }

    f32 const chroma = rgb.r - (rgb.g < rgb.b ? rgb.g : rgb.b);
    return {
        .h = Fabs(k + ((rgb.g - rgb.b) / (6.f * chroma + 1e-20f))),
        .s = chroma / (rgb.r + 1e-20f),
        .v = rgb.r,
    };
}

// Convert hsv floats ([0-1],[0-1],[0-1]) to rgb floats ([0-1],[0-1],[0-1]), from Foley & van Dam p593
// also http://en.wikipedia.org/wiki/HSL_and_HSV
// This function is from dear imgui
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT
constexpr ColourValues ConvertHsvtoRgb(ColourValues hsv) {
    if (hsv.s == 0.0f) return {.r = hsv.v, .g = hsv.v, .b = hsv.v};

    hsv.h = Fmod(hsv.h, 1.0f) / (60.0f / 360.0f);
    auto const i = (int)hsv.h;
    f32 const f = hsv.h - (f32)i;
    f32 const p = hsv.v * (1.0f - hsv.s);
    f32 const q = hsv.v * (1.0f - hsv.s * f);
    f32 const t = hsv.v * (1.0f - hsv.s * (1.0f - f));

    switch (i) {
        case 0: return {.r = hsv.v, .g = t, .b = p};
        case 1: return {.r = q, .g = hsv.v, .b = p};
        case 2: return {.r = p, .g = hsv.v, .b = t};
        case 3: return {.r = p, .g = q, .b = hsv.v};
        case 4: return {.r = t, .g = p, .b = hsv.v};
        case 5:
        default: return {.r = hsv.v, .g = p, .b = q};
    }
}

constexpr Channels FromFloatRgb(ColourValues rgb, u8 alpha) {
    rgb.e *= 255.0f;
    return {
        .a = alpha,
        .b = (u8)rgb.b,
        .g = (u8)rgb.g,
        .r = (u8)rgb.r,
    };
}

constexpr Channels WithValue(Channels const& c, f32 value) {
    auto hsv = ConvertRgbtoHsv({.r = c.r / 255.0f, .g = c.g / 255.0f, .b = c.b / 255.0f});
    hsv.v = value;
    auto const rgb = ConvertHsvtoRgb(hsv);
    return FromFloatRgb(rgb, c.a);
}

} // namespace colour
