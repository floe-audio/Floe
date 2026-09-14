// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "effect.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/distortion.hpp"

struct Distortion final : public Effect {
    Distortion() : Effect(EffectType::Distortion) {}

    void ProcessChangesInternal(ProcessBlockChanges const& changes, AudioProcessingContext const&) override {
        auto settings = dsps[active_dsp_index].settings;
        bool settings_changed = false;
        bool type_changed = false;

        if (auto p = changes.changed_params.IntValueLegacyAware<DistortionType>(ParamIndex::DistortionType)) {
            settings.type = *p;
            type_changed = true;
            settings_changed = true;
        }

        if (auto p = changes.changed_params.BoolValue(ParamIndex::DistortionAutoGain)) auto_gain_param = *p;

        // Faded so the level doesn't step, including when a type change flips the target (only Legacy types
        // honour the param being off).
        auto_gain = (IsLegacyDistortionType(settings.type) && !auto_gain_param) ? 0.0f : 1.0f;

        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::DistortionDrive)) amount = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::DistortionMix)) mix = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::DistortionPunish)) punish = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::DistortionTilt)) {
            settings.tilt = *p;
            settings_changed = true;
        }
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::DistortionGain))
            output_gain = DbToAmp(*p);

        if (settings_changed) {
            // A type change crossfades to the other DistortionDsp instance rather than snapping the wave
            // shape. The new type takes over whichever instance carries less of the blend, so a change
            // arriving mid-fade disturbs the output as little as possible. The incoming instance starts
            // from reset and is primed with the recent input so the fade blends in continuous signal
            // rather than a hard onset ringing through its oversampler.
            if (type_changed) {
                auto const active_weight = type_fade_smoother.prev_output;
                if (active_weight >= 0.5f) {
                    active_dsp_index ^= 1;
                    type_fade_smoother.prev_output = 1 - active_weight;
                }
                dsps[active_dsp_index].SetSettings(settings);
                dsps[active_dsp_index].Reset();
                for (auto const i : Range(k_dry_delay_size))
                    dsps[active_dsp_index].Process(dry_delay[(dry_delay_pos + i) & k_dry_delay_mask],
                                                   {
                                                       .drive01 = amount,
                                                       .punish01 = punish,
                                                       .auto_gain01 = auto_gain,
                                                   });
            } else {
                dsps[active_dsp_index].SetSettings(settings);
            }
        }
    }

    // The oversampled wet path lags the input, so this doesn't use the base class's ProcessBlockByFrame
    // helper:
    // - Mix is blended here against the delayed dry signal (blending against the undelayed input would
    //   comb-filter), so the base class's mix_param is left at 1 and bypass_mix tracks only the on/off state.
    // - The on/off transition is crossfaded against the undelayed input. The delay entering or leaving the
    //   signal path is an unavoidable time-shift; spreading it over the 10ms crossfade is far less audible
    //   than the hard cut you'd get from switching instantly.
    // - Once fully bypassed the DSP is reset so the delay lines don't hold stale audio that would otherwise
    //   replay when the effect is next enabled.
    EffectProcessResult
    ProcessBlock(Span<f32x2> frames, AudioProcessingContext const& context, void*) override {
        auto const starting_from_reset = is_reset;
        if (!ShouldProcessBlock()) {
            Reset();
            return EffectProcessResult::Done;
        }
        if (starting_from_reset) bypass_smoother.prev_output = 0;

        for (auto& frame : frames) {
            auto const delayed_dry = DelayDry(frame);
            auto const smoothing = context.one_pole_smoothing_cutoff_10ms;
            DistortionDsp::Controls const controls {
                .drive01 = amount_smoother.LowPass(amount, smoothing),
                .punish01 = punish_smoother.LowPass(punish, smoothing),
                .auto_gain01 = auto_gain_smoother.LowPass(auto_gain, smoothing),
            };
            auto wet = dsps[active_dsp_index].Process(frame, controls);
            auto const type_fade = type_fade_smoother.LowPass(1, smoothing);
            if (type_fade < 0.999f)
                wet = LinearInterpolate(type_fade, dsps[active_dsp_index ^ 1].Process(frame, controls), wet);
            wet *= output_gain_smoother.LowPass(output_gain, smoothing);
            auto const mixed = LinearInterpolate(mix_smoother.LowPass(mix, smoothing), delayed_dry, wet);
            frame = ApplyBypassCrossfade(context, mixed, frame);
        }
        return EffectProcessResult::Done;
    }

    void ResetInternal() override {
        for (auto& dsp : dsps)
            dsp.Reset();
        amount_smoother.Reset();
        punish_smoother.Reset();
        auto_gain_smoother.Reset();
        output_gain_smoother.Reset();
        mix_smoother.Reset();
        type_fade_smoother.Reset();
        for (auto& d : dry_delay)
            d = 0;
        dry_delay_pos = 0;
    }

    void PrepareToPlay(AudioProcessingContext const& context) override {
        for (auto& dsp : dsps)
            dsp.SetSampleRate(context.sample_rate);
    }

    // Returns the input delayed by the wet path's latency so it lines up with Process()'s output.
    f32x2 DelayDry(f32x2 input) {
        dry_delay[dry_delay_pos & k_dry_delay_mask] = input;
        auto const delayed =
            dry_delay[(dry_delay_pos - DistortionDsp::k_latency_base_samples) & k_dry_delay_mask];
        ++dry_delay_pos;
        return delayed;
    }

    static constexpr u32 k_dry_delay_size = NextPowerOf2(DistortionDsp::k_latency_base_samples + 1);
    static constexpr u32 k_dry_delay_mask = k_dry_delay_size - 1;

    f32 amount {};
    f32 punish {};
    f32 output_gain = 1;
    f32 mix = 1;
    bool auto_gain_param = true;
    f32 auto_gain = 1;
    OnePoleLowPassFilter<f32> amount_smoother {};
    OnePoleLowPassFilter<f32> punish_smoother {};
    OnePoleLowPassFilter<f32> auto_gain_smoother {};
    OnePoleLowPassFilter<f32> output_gain_smoother {};
    OnePoleLowPassFilter<f32> mix_smoother {};
    OnePoleLowPassFilter<f32> type_fade_smoother {};
    Array<DistortionDsp, 2> dsps {};
    u32 active_dsp_index = 0;
    Array<f32x2, k_dry_delay_size> dry_delay {};
    u32 dry_delay_pos = 0;
};
