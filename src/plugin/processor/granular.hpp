// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "sample_processing.hpp"

constexpr u32 k_max_num_voice_sound_sources = 4;

inline bool IsGranular(param_values::PlayMode mode) {
    switch (mode) {
        case param_values::PlayMode::Standard: return false;
        case param_values::PlayMode::GranularPlayback: return true;
        case param_values::PlayMode::GranularFixed: return true;
        case param_values::PlayMode::Count: return false;
    }
    return false;
}

// The grain spread parameter (0 to 1) maps to this fraction of the total sample length.
// At 1.0, the region covers the entire sample.
constexpr f32 k_max_grain_spread_fraction = 1.0f;
constexpr f32 k_min_grain_spread_fraction = 0.005f;

inline f32 GrainSpreadParamToFraction(f32 param_value) {
    return k_min_grain_spread_fraction +
           (param_value * (k_max_grain_spread_fraction - k_min_grain_spread_fraction));
}

constexpr u32 k_max_grains_per_voice = 64;
constexpr f32 k_min_grain_length_seconds = 0.005f;
constexpr f32 k_max_grain_length_seconds = 1.0f;
constexpr f32 k_min_grain_spawn_interval_seconds = 0.001f;
constexpr f32 k_max_grain_spawn_interval_seconds = 0.5f;

inline u32 GrainLengthParamToSamples(f32 param_01, f32 sample_rate) {
    auto const seconds =
        k_min_grain_length_seconds + (param_01 * (k_max_grain_length_seconds - k_min_grain_length_seconds));
    return Max(1u, (u32)(seconds * sample_rate));
}

inline u32 GrainsParamToSpawnInterval(f32 param_01, f32 sample_rate) {
    // High param = short interval = more grains.
    auto const seconds =
        k_max_grain_spawn_interval_seconds -
        (param_01 * (k_max_grain_spawn_interval_seconds - k_min_grain_spawn_interval_seconds));
    return Max(1u, (u32)(seconds * sample_rate));
}

// Trapezoidal envelope with quarter-sine curves.
// smoothing=0 -> rectangular, smoothing=1 -> full raised-cosine (Hann-like).
inline f32 GrainEnvelope(f32 phase_01, f32 smoothing) {
    if (smoothing < 0.001f) return 1.0f;

    // The fade region is smoothing/2 at each end.
    auto const fade = smoothing * 0.5f;

    if (phase_01 < fade) {
        // Fade in: quarter-sine from 0 to 1.
        auto const t = phase_01 / fade;
        return trig_table_lookup::SinTurnsPositive(t * 0.25f);
    }
    if (phase_01 > (1.0f - fade)) {
        // Fade out: quarter-sine from 1 to 0.
        auto const t = (1.0f - phase_01) / fade;
        return trig_table_lookup::SinTurnsPositive(t * 0.25f);
    }
    return 1.0f;
}

struct Grain {
    PlayHead playhead {};
    u32 duration_samples {};
    u32 samples_elapsed {};
    u8 source_index {};
    bool active {};
};

struct GrainPool {
    Array<Grain, k_max_grains_per_voice> grains {};
    Array<u32, k_max_num_voice_sound_sources> spawn_counters {};

    void Reset() {
        for (auto& g : grains)
            g.active = false;
        for (auto& c : spawn_counters)
            c = 0;
    }
};
