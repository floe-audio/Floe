// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "processing_utils/filters.hpp"
#include "sample_processing.hpp"

constexpr u32 k_max_grains_per_voice = 150;

constexpr u32 k_grain_steal_threshold = k_max_grains_per_voice * 3 / 4;
constexpr f32 k_grain_steal_fadeout_ms = 5.0f;

inline bool IsGranular(param_values::PlayMode mode) {
    switch (mode) {
        case param_values::PlayMode::Standard: return false;
        case param_values::PlayMode::GranularPlayback: return true;
        case param_values::PlayMode::GranularFixed: return true;
        case param_values::PlayMode::Count: return false;
    }
    return false;
}

struct Grain {
    PlayHead playhead {};
    f64 detune_ratio {1}; // pitch multiplier, assigned randomly at grain spawn

    // Envelope phase: advances from 0→1 over the grain's lifetime. Using f32 avoids
    // per-sample u32→f32 casts in the hot loop and enables branchless processing.
    f32 env_phase {};
    f32 env_phase_inc {}; // = 1.0f / duration_samples, pre-computed at spawn

    f32 pan_pos {}; // -1 (left) to +1 (right), assigned randomly at grain spawn
    u8 source_index {};
    bool active {};

    // Steal fade: starts at 1.0 and decreases towards 0 when the grain is being stolen.
    // steal_fade_dec is 0 for non-stealing grains, making the subtraction a branchless no-op.
    // When steal_fade reaches 0 the grain is deactivated.
    f32 steal_fade {1.0f};
    f32 steal_fade_dec {};

    bool IsStealing() const { return steal_fade_dec > 0.0f; }
};

struct GrainPool {
    void Reset(f32 sample_rate) {
        for (auto& g : grains)
            g.active = false;
        for (auto& c : spawn_counters)
            c = 0;
        steal_fade_dec_value = 1.0f / Max(1.0f, k_grain_steal_fadeout_ms * 0.001f * sample_rate);
        num_active_non_stealing = 0;
        smoothing_smoother.Reset();
    }

    Array<Grain, k_max_grains_per_voice> grains {};
    Array<u32, k_max_num_voice_sound_sources> spawn_counters {};

    // Per-sample decrement applied to Grain::steal_fade when a grain is being stolen.
    // Pre-computed from k_grain_steal_fadeout_ms and the sample rate.
    f32 steal_fade_dec_value {};

    // Cached count of grains that are active and not being stolen. Maintained
    // incrementally to avoid scanning all grain slots every sample.
    u32 num_active_non_stealing {};

    // Low-pass filter to smooth the granular smoothing parameter, preventing clicks when
    // the user adjusts the knob while grains are playing.
    OnePoleLowPassFilter<f32> smoothing_smoother {};
};
