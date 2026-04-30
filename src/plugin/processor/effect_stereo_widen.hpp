// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "effect.hpp"
#include "processing_utils/filters.hpp"
#include "processing_utils/stereo_widen.hpp"

struct StereoWiden final : public Effect {
    StereoWiden() : Effect(EffectType::StereoWiden) {}

    void ProcessChangesInternal(ProcessBlockChanges const& changes,
                                AudioProcessingContext const& context) override {
        bool crossover_changed = false;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::StereoWidenWidth)) width_param = *p;
        if (auto p =
                changes.changed_params.IntValue<param_values::StereoWidenMode>(ParamIndex::StereoWidenMode))
            mode = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::StereoWidenBassMono)) {
            bass_mono_hz = *p;
            crossover_changed = true;
        }

        if (crossover_changed || sample_rate_at_coeffs != context.sample_rate) {
            sample_rate_at_coeffs = context.sample_rate;
            // Linkwitz-Riley 24dB = two cascaded Butterworth biquads, Q = 1/sqrt(2).
            rbj_filter::Params params {
                .fs = context.sample_rate,
                .fc = bass_mono_hz,
                .q = 0.70710678f,
            };
            params.type = rbj_filter::Type::LowPass;
            lp_coeffs = rbj_filter::Coefficients(params);
            params.type = rbj_filter::Type::HighPass;
            hp_coeffs = rbj_filter::Coefficients(params);
        }
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> frames, AudioProcessingContext const& context, void*) override {
        return ProcessBlockByFrame(
            frames,
            [&](f32x2 frame) {
                auto const smoothed_w =
                    width_smoother.LowPass(width_param, context.one_pole_smoothing_cutoff_10ms);
                switch (mode) {
                    case param_values::StereoWidenMode::Balanced: {
                        // Linear val [-1, +1] → width [0, 2].
                        return DoStereoWidenConstantPower(smoothed_w + 1.0f, frame);
                    }
                    case param_values::StereoWidenMode::Legacy: {
                        // Original musicdsp formulation: val<0 → [0,1], val≥0 → [1,4].
                        auto const w = smoothed_w < 0 ? 1 - (-smoothed_w) : MapFrom01(smoothed_w, 1, 4);
                        return DoStereoWiden(w, frame);
                    }
                    case param_values::StereoWidenMode::BassMono: {
                        // Linkwitz-Riley split: low becomes mono, high passes through (and is widened
                        // by the width control using the constant-power algorithm).
                        auto low = rbj_filter::Process(lp_data[0], lp_coeffs, frame);
                        low = rbj_filter::Process(lp_data[1], lp_coeffs, low);
                        auto high = rbj_filter::Process(hp_data[0], hp_coeffs, frame);
                        high = rbj_filter::Process(hp_data[1], hp_coeffs, high);

                        auto const low_mono = (low.x + low.y) * 0.5f;
                        auto const widened_high = DoStereoWidenConstantPower(smoothed_w + 1.0f, high);
                        return f32x2 {low_mono, low_mono} + widened_high;
                    }
                    case param_values::StereoWidenMode::Count: PanicIfReached();
                }
                return frame;
            },
            context);
    }

    void ResetInternal() override {
        width_smoother.Reset();
        for (auto& d : lp_data)
            d = {};
        for (auto& d : hp_data)
            d = {};
    }

    f32 width_param {};
    param_values::StereoWidenMode mode = param_values::StereoWidenMode::Balanced;
    f32 bass_mono_hz = 120;
    OnePoleLowPassFilter<f32> width_smoother {};

    f32 sample_rate_at_coeffs = 0;
    rbj_filter::Coeffs lp_coeffs {};
    rbj_filter::Coeffs hp_coeffs {};
    Array<rbj_filter::StereoData, 2> lp_data {};
    Array<rbj_filter::StereoData, 2> hp_data {};
};
