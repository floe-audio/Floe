// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

namespace colours {

struct Col {
    u8 a, b, g, r;
};

PUBLIC constexpr Col FromU32(u32 abgr) {
    return Col {
        (u8)((abgr >> 24) & 0xFF),
        (u8)((abgr >> 16) & 0xFF),
        (u8)((abgr >> 8) & 0xFF),
        (u8)(abgr & 0xFF),
    };
}
PUBLIC constexpr u32 ToU32(Col c) { return ((u32)c.a << 24) | ((u32)c.b << 16) | ((u32)c.g << 8) | (c.r); }

PUBLIC constexpr u32 FromWeb(u32 rgba) {
    auto const r = rgba >> 24;
    auto const g = (rgba >> 16) & 0xff;
    auto const b = (rgba >> 8) & 0xff;
    auto const a = rgba & 0xff;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

PUBLIC constexpr u32 WithAlpha(u32 abgr, u8 alpha) { return (abgr & 0x00ffffff) | ((u32)alpha << 24); }

PUBLIC constexpr u32 ChangeBrightness(u32 abgr, f32 brightness_factor) {
    auto col = FromU32(abgr);
    col.r = (u8)Min((f32)col.r * brightness_factor, 255.0f);
    col.g = (u8)Min((f32)col.g * brightness_factor, 255.0f);
    col.b = (u8)Min((f32)col.b * brightness_factor, 255.0f);
    return ToU32(col);
}

PUBLIC constexpr u32 ChangeAlpha(u32 abgr, f32 scaling) {
    auto const new_val = (u8)Min((f32)((abgr >> 24) & 0xFF) * scaling, 255.0f);
    return (abgr & 0x00FFFFFF) | ((u32)(new_val) << 24);
}

// Convert rgb floats ([0-1],[0-1],[0-1]) to hsv floats ([0-1],[0-1],[0-1]), from Foley & van Dam p592
// Optimized http://lolengine.net/blog/2013/01/13/fast-rgb-to-hsv
// This function is from dear imgui
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT
PUBLIC void ConvertRGBtoHSV(f32 r, f32 g, f32 b, f32& out_h, f32& out_s, f32& out_v) {
    f32 k = 0.f;
    if (g < b) {
        Swap(g, b);
        k = -1.f;
    }
    if (r < g) {
        Swap(r, g);
        k = -2.f / 6.f - k;
    }

    f32 const chroma = r - (g < b ? g : b);
    out_h = Fabs(k + ((g - b) / (6.f * chroma + 1e-20f)));
    out_s = chroma / (r + 1e-20f);
    out_v = r;
}

// Convert hsv floats ([0-1],[0-1],[0-1]) to rgb floats ([0-1],[0-1],[0-1]), from Foley & van Dam p593
// also http://en.wikipedia.org/wiki/HSL_and_HSV
// This function is from dear imgui
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT
PUBLIC void ConvertHSVtoRGB(f32 h, f32 s, f32 v, f32& out_r, f32& out_g, f32& out_b) {
    if (s == 0.0f) {
        // gray
        out_r = out_g = out_b = v;
        return;
    }

    h = Fmod(h, 1.0f) / (60.0f / 360.0f);
    auto const i = (int)h;
    f32 const f = h - (f32)i;
    f32 const p = v * (1.0f - s);
    f32 const q = v * (1.0f - s * f);
    f32 const t = v * (1.0f - s * (1.0f - f));

    switch (i) {
        case 0:
            out_r = v;
            out_g = t;
            out_b = p;
            break;
        case 1:
            out_r = q;
            out_g = v;
            out_b = p;
            break;
        case 2:
            out_r = p;
            out_g = v;
            out_b = t;
            break;
        case 3:
            out_r = p;
            out_g = q;
            out_b = v;
            break;
        case 4:
            out_r = t;
            out_g = p;
            out_b = v;
            break;
        case 5:
        default:
            out_r = v;
            out_g = p;
            out_b = q;
            break;
    }
}

PUBLIC Col WithValue(Col const& c, f32 value) {
    f32 h {};
    f32 s {};
    f32 v {};
    ConvertRGBtoHSV(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, h, s, v);
    f32 r {};
    f32 g {};
    f32 b {};
    ConvertHSVtoRGB(h, s, value, r, g, b);

    auto result = c;
    result.r = (u8)(r * 255.0f);
    result.g = (u8)(g * 255.0f);
    result.b = (u8)(b * 255.0f);
    return result;
}

} // namespace colours
