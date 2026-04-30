// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "processing_utils/stereo_widen.hpp"

#include "effect.hpp"

struct StereoWiden final : public Effect {
    StereoWiden() : Effect(EffectType::StereoWiden) {}

    void ProcessChangesInternal(ProcessBlockChanges const& changes, AudioProcessingContext const&) override {
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::StereoWidenWidth)) {
            auto const val = *p;
            f32 v = 0;
            if (val < 0)
                v = 1 - (-(val));
            else
                v = MapFrom01(val, 1, 4);

            width = v;
        }
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> frames, AudioProcessingContext const& context, void*) override {
        return ProcessBlockByFrame(
            frames,
            [&](f32x2 frame) {
                return DoStereoWiden(width_smoother.LowPass(width, context.one_pole_smoothing_cutoff_10ms),
                                     frame);
            },
            context);
    }

    void ResetInternal() override { width_smoother.Reset(); }

    f32 width {};
    OnePoleLowPassFilter<f32> width_smoother {};
};
