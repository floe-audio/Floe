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

inline String HarmonyIntervalName(int semitones, ArenaAllocator& arena) {
    // Interval names for semitones 1-12 (positive direction)
    constexpr Array k_interval_names = {
        "Min 2nd"_s, // 1
        "Maj 2nd"_s, // 2
        "Min 3rd"_s, // 3
        "Maj 3rd"_s, // 4
        "Perfect 4th"_s, // 5
        "Tritone"_s, // 6
        "Perfect 5th"_s, // 7
        "Min 6th"_s, // 8
        "Maj 6th"_s, // 9
        "Min 7th"_s, // 10
        "Maj 7th"_s, // 11
        "Octave"_s, // 12
    };

    auto const abs_semi = Abs(semitones);
    auto const sign = semitones > 0 ? "+"_s : ""_s;

    if (abs_semi >= 1 && abs_semi <= 12)
        return fmt::Format(arena, "{}{} {}", sign, semitones, k_interval_names[(usize)(abs_semi - 1)]);
    return fmt::Format(arena, "{}{}", sign, semitones);
}

inline String HarmonyIntervalsLabel(HarmonyIntervalsBitset const& intervals, ArenaAllocator& arena) {
    // Count set bits excluding the unison bit
    auto non_unison = intervals;
    non_unison.Clear(k_harmony_interval_centre_bit);
    auto const num_set = non_unison.NumSet();

    if (num_set == 0) return "None"_s;

    if (num_set >= 5) return fmt::Format(arena, "{} intervals", num_set);

    // 1-4 intervals: list them
    DynamicArrayBounded<char, 64> buf {};
    usize count = 0;
    non_unison.ForEachSetBit([&](usize bit) {
        auto const semitones = HarmonyIntervalSemitones(bit);
        if (count > 0) fmt::Append(buf, ", "_s);
        if (semitones > 0)
            fmt::Append(buf, "+{}"_s, semitones);
        else
            fmt::Append(buf, "{}"_s, semitones);
        ++count;
    });
    return arena.Clone(String(buf));
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
