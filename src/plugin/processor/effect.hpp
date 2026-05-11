// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/debug/tracy_wrapped.hpp"

#include "common_infrastructure/descriptors/effect_descriptors.hpp"

#include "param.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/filters.hpp"

inline void UpdateSilentSeconds(f32& silent_seconds, Span<f32x2 const> frames, f32 sample_rate) {
    bool all_silent = true;
    for (auto const& f : frames)
        if (!IsSilent(f)) {
            all_silent = false;
            break;
        }
    if (all_silent)
        silent_seconds += (f32)frames.size / sample_rate;
    else
        silent_seconds = 0;
}

struct EffectWetDryHelper {
    void SetWet(f32 amp) { wet = amp; }
    void SetDry(f32 amp) { dry = amp; }

    f32 Mix(AudioProcessingContext const& context, f32 w, f32 d) {
        return (w * wet_smoother.LowPass(wet, context.one_pole_smoothing_cutoff_10ms)) +
               (d * dry_smoother.LowPass(dry, context.one_pole_smoothing_cutoff_10ms));
    }

    f32x2 MixStereo(AudioProcessingContext const& context, f32x2 w, f32x2 d) {
        return (w * wet_smoother.LowPass(wet, context.one_pole_smoothing_cutoff_10ms)) +
               (d * dry_smoother.LowPass(dry, context.one_pole_smoothing_cutoff_10ms));
    }

    void Reset() {
        wet_smoother.Reset();
        dry_smoother.Reset();
    }

    f32 wet;
    OnePoleLowPassFilter<f32> wet_smoother {};
    f32 dry;
    OnePoleLowPassFilter<f32> dry_smoother {};
};

struct EffectiveWetDryAmps {
    f32 wet_amp;
    f32 dry_amp;
};
inline EffectiveWetDryAmps EffectiveWetDryFromMixOutputOrLegacy(Parameters const& p,
                                                                WetDryMapping const& mapping) {
    auto const wet_lin = p.LinearValueIgnoringLegacy(mapping.legacy_wet);
    auto const dry_lin = p.LinearValueIgnoringLegacy(mapping.legacy_dry);
    if (IsWetDryLegacyOverriding(mapping.legacy_wet, mapping.legacy_dry, wet_lin, dry_lin)) {
        return {.wet_amp = k_param_descriptors[ToInt(mapping.legacy_wet)].ProjectValue(wet_lin),
                .dry_amp = k_param_descriptors[ToInt(mapping.legacy_dry)].ProjectValue(dry_lin)};
    }
    auto const mix = p.LinearValue(mapping.modern_mix);
    auto const output_amp = p.ProjectedValue(mapping.modern_output);
    return {.wet_amp = output_amp * mix, .dry_amp = output_amp * (1.0f - mix)};
}

inline bool AnyChanged(auto const& changed_params, WetDryMapping const& mapping) {
    return changed_params.ChangedIgnoringLegacy(mapping.legacy_wet) ||
           changed_params.ChangedIgnoringLegacy(mapping.legacy_dry) ||
           changed_params.ChangedIgnoringLegacy(mapping.modern_mix) ||
           changed_params.ChangedIgnoringLegacy(mapping.modern_output);
}

enum class EffectProcessResult : u8 {
    Done, // no more processing needed
    ProcessingTail, // processing needed
};

struct Effect {
    Effect(EffectType type) : type(type) {}

    virtual ~Effect() {}

    // main-thread but never while any audio-thread function is being called
    virtual void PrepareToPlay(AudioProcessingContext const&) {}

    void ProcessChanges(ProcessBlockChanges const& changes, AudioProcessingContext const& context) {
        if (auto const p = changes.changed_params.BoolValue(k_effect_info[(u32)type].on_param_index))
            bypass_target = *p ? 1.0f : 0.0f;
        ProcessChangesInternal(changes, context);
        bypass_mix = bypass_target * mix_param;
    }

    // audio-thread
    virtual EffectProcessResult
    ProcessBlock(Span<f32x2>, AudioProcessingContext const&, void* extra_context) = 0;

    // Helper function for simple effects that only need to process one frame at a time. Wraps the individual
    // frame processing in the necessary block processing machinery.
    ALWAYS_INLINE EffectProcessResult ProcessBlockByFrame(Span<f32x2> frames,
                                                          auto process_frame_function,
                                                          AudioProcessingContext const& context) {
        if (!ShouldProcessBlock()) return EffectProcessResult::Done;
        for (auto& frame : frames)
            frame = ApplyBypassCrossfade(context, process_frame_function(frame), frame);
        return EffectProcessResult::Done;
    }

    // audio-thread
    void Reset() {
        ZoneScoped;
        if (is_reset) return;
        ResetInternal();
        is_reset = true;
        bypass_smoother.Reset();
    }

    // audio-thread
    bool ShouldProcessBlock() {
        if (bypass_mix == 0 && bypass_smoother.IsStable(bypass_mix, 0.001f)) return false;
        is_reset = false;
        return true;
    }

    // audio-thread
    f32x2 ApplyBypassCrossfade(AudioProcessingContext const& context, f32x2 wet, f32x2 dry) {
        return LinearInterpolate(bypass_smoother.LowPass(bypass_mix, context.one_pole_smoothing_cutoff_10ms),
                                 dry,
                                 wet);
    }

    virtual void ResetInternal() {}
    virtual void ProcessChangesInternal(ProcessBlockChanges const& changes,
                                        AudioProcessingContext const& context) = 0;

    EffectType const type;
    f32 bypass_target = 0;
    f32 mix_param = 1;
    f32 bypass_mix = 0;
    OnePoleLowPassFilter<f32> bypass_smoother {};
    bool is_reset = true;
};
