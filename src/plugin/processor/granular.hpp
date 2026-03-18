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
// The spread region extends from the current playhead position onwards.
// At 1.0, the spread covers the entire remaining sample length.
constexpr f32 k_max_grain_spread_fraction = 1.0f;
constexpr f32 k_min_grain_spread_fraction = 0.005f;

inline f32 GrainSpreadParamToFraction(f32 param_value) {
    return k_min_grain_spread_fraction +
           (param_value * (k_max_grain_spread_fraction - k_min_grain_spread_fraction));
}

constexpr u32 k_max_grains_per_voice = 150;
constexpr f32 k_min_grain_spawn_interval_seconds = 0.001f;
constexpr f32 k_max_grain_spawn_interval_seconds = 0.5f;

inline u32 GrainLengthParamToSamples(f32 length_ms, f32 sample_rate) {
    return Max(1u, (u32)(length_ms * 0.001f * sample_rate));
}

inline u32 GrainsParamToSpawnInterval(f32 param_01, f32 sample_rate) {
    // High param = short interval = more grains. Exponential mapping so that the
    // density change feels even across the knob's range.
    auto const seconds = k_max_grain_spawn_interval_seconds *
                         Exp2(param_01 * (f32)constexpr_math::Log2((f64)k_min_grain_spawn_interval_seconds /
                                                                   (f64)k_max_grain_spawn_interval_seconds));
    return Max(1u, (u32)(seconds * sample_rate));
}

// Computes the bounds of the region where grains can spawn, in frame_pos space.
// Two regions are needed because loop wrapping can create two disjoint segments.
struct GrainSpreadBounds {
    struct Region {
        f64 start; // frame_pos space
        f64 end; // frame_pos space
    };
    Region region_1 {};
    Region region_2 {};
    bool has_region_2 = false;
};

inline GrainSpreadBounds ComputeGrainSpreadBounds(f64 playhead_frame_pos,
                                                  f32 spread_param,
                                                  Optional<PlayHead::Loop> const& loop,
                                                  u32 num_frames,
                                                  bool is_fixed) {
    auto const spread_size = (f64)GrainSpreadParamToFraction(spread_param) * (f64)num_frames;

    GrainSpreadBounds bounds {};

    if (is_fixed) {
        // GranularFixed: no loops, clamp to sample bounds.
        bounds.region_1.start = playhead_frame_pos;
        bounds.region_1.end = Min(playhead_frame_pos + spread_size, (f64)(num_frames - 1));
        return bounds;
    }

    auto const region_end_fp = playhead_frame_pos + spread_size;

    if (loop && loop->only_use_frames_within_loop) {
        auto const loop_start = (f64)loop->start;
        auto const loop_end = (f64)loop->end;
        auto const loop_size = loop_end - loop_start;

        if (loop_size <= 0) {
            bounds.region_1.start = playhead_frame_pos;
            bounds.region_1.end = playhead_frame_pos;
            return bounds;
        }

        if (spread_size >= loop_size) {
            // Spread covers the entire loop.
            bounds.region_1.start = loop_start;
            bounds.region_1.end = loop_end;
            return bounds;
        }

        if (region_end_fp <= loop_end) {
            // No wrapping needed - spread fits within loop.
            bounds.region_1.start = playhead_frame_pos;
            bounds.region_1.end = region_end_fp;
        } else {
            // Spread wraps around loop end -> two disjoint regions.
            auto const wrapped_end = loop_start + (region_end_fp - loop_end);

            bounds.region_1.start = playhead_frame_pos;
            bounds.region_1.end = loop_end;
            bounds.region_2.start = loop_start;
            bounds.region_2.end = wrapped_end;
            bounds.has_region_2 = true;
        }
    } else {
        // Pre-loop or no loop: clamp to sample bounds. When a loop exists but the playhead
        // hasn't entered it yet, also clamp to the loop end so the visual region doesn't
        // extend past where grains will be constrained once looping begins.
        auto const clamp_end = (loop ? (f64)loop->end : (f64)(num_frames - 1));
        bounds.region_1.start = playhead_frame_pos;
        bounds.region_1.end = Min(region_end_fp, clamp_end);
    }

    return bounds;
}

// When the number of active grains exceeds this threshold, the oldest grains that
// aren't already fading out will be faded out to make room for new ones. This is
// analogous to voice stealing with a fade-out threshold.
constexpr u32 k_grain_steal_threshold = k_max_grains_per_voice * 3 / 4;

// Duration of the fade-out applied to stolen grains. This is independent of the
// grain's own envelope smoothing parameter - it's a fixed, short fade to prevent pops.
constexpr f32 k_grain_steal_fadeout_ms = 5.0f;

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
    }

    Array<Grain, k_max_grains_per_voice> grains {};
    Array<u32, k_max_num_voice_sound_sources> spawn_counters {};

    // Per-sample decrement applied to Grain::steal_fade when a grain is being stolen.
    // Pre-computed from k_grain_steal_fadeout_ms and the sample rate.
    f32 steal_fade_dec_value {};

    // Cached count of grains that are active and not being stolen. Maintained
    // incrementally to avoid scanning all grain slots every sample.
    u32 num_active_non_stealing {};
};

// Initialise a grain's playhead based on the given position. Returns false if the grain
// should not be spawned (position is past the sample end in non-looping modes).
inline bool
InitGrainPlayhead(Grain& grain, f64 grain_pos, PlayHead const& main_playhead, u32 num_frames, bool is_fixed) {
    auto& gph = grain.playhead;
    if (is_fixed) {
        if (grain_pos >= (f64)num_frames) return false;

        ResetPlayhead(gph, grain_pos, k_nullopt, main_playhead.inverse_data_lookup, num_frames);
        return true;
    }

    // When the main playhead is inside the loop, wrap the grain position into the loop range.
    auto const& main_loop = main_playhead.loop;
    if (main_loop && main_loop->only_use_frames_within_loop) {
        auto const loop_start = (f64)main_loop->start;
        auto const loop_size = (f64)(main_loop->end - main_loop->start);
        if (loop_size > 0) {
            auto const overshoot = grain_pos - loop_start;
            grain_pos = loop_start + Fmod(overshoot, loop_size);
        }
    } else if (grain_pos >= (f64)num_frames) {
        return false;
    }

    // The main playhead's loop is already in frame_pos space (inverted if reversed).
    // Pass is_reversed=false to avoid double-inverting, then copy inverse_data_lookup manually.
    ResetPlayhead(gph,
                  grain_pos,
                  main_loop ? Optional<BoundsCheckedLoop>(*main_loop) : k_nullopt,
                  false,
                  num_frames);
    gph.inverse_data_lookup = main_playhead.inverse_data_lookup;
    return true;
}
