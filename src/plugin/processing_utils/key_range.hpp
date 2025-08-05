// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

PUBLIC f32 FadeAmpCurve(f32 pos_01) { return trig_table_lookup::SinTurnsPositive(pos_01 * 0.25f); }

PUBLIC f32 KeyRangeFadeIn(s32 note, s32 key_range_low, s32 fade_size) {
    auto const silent_note = key_range_low - 1;
    auto const full_note = key_range_low + fade_size;

    if (note <= silent_note) return 0.0f;
    if (note >= full_note) return 1.0f;

    auto const pos_in_fade = (f32)(full_note - note) / (f32)(full_note - silent_note);
    ASSERT(pos_in_fade >= 0 && pos_in_fade <= 1);
    return 1.0f - pos_in_fade;
}

PUBLIC f32 KeyRangeFadeOut(s32 note, s32 key_range_high, s32 fade_size) {
    auto const silent_note = key_range_high + 1;
    auto const full_note = key_range_high - fade_size;

    if (note >= silent_note) return 0.0f;
    if (note <= full_note) return 1.0f;

    auto const pos_in_fade = (f32)(note - full_note) / (f32)(silent_note - full_note);
    ASSERT(pos_in_fade >= 0 && pos_in_fade <= 1);
    return 1.0f - pos_in_fade;
}

PUBLIC f32 KeyRangeFadeInAmp(s32 note, s32 key_range_low, s32 fade_size) {
    return FadeAmpCurve(KeyRangeFadeIn(note, key_range_low, fade_size));
}

PUBLIC f32 KeyRangeFadeOutAmp(s32 note, s32 key_range_high, s32 fade_size) {
    return FadeAmpCurve(KeyRangeFadeOut(note, key_range_high, fade_size));
}
