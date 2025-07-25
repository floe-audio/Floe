// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "effect.hpp"
#include "processing_utils/stereo_audio_frame.hpp"

// http://www.musicdsp.org/show_archive_comment.php?ArchiveID=256
// public domain
// 'width' is the stretch factor of the stereo field:
// width < 1: decrease in stereo width
// width = 1: no change
// width > 1: increase in stereo width
// width = 0: mono
inline void DoStereoWiden(f32 width, f32 in_left, f32 in_right, f32& out_left, f32& out_right) {
    auto const coef_s = width * 0.5f;
    auto const m = (in_left + in_right) * 0.5f;
    auto const s = (in_right - in_left) * coef_s;
    out_left = m - s;
    out_right = m + s;
}

inline f32x2 DoStereoWiden(f32 width, f32x2 in) {
    alignas(f32x2) f32 result[2];
    DoStereoWiden(width, in.x, in.y, result[0], result[1]);
    return LoadAlignedToType<f32x2>(result);
}

struct StereoWiden final : public Effect {
    StereoWiden() : Effect(EffectType::StereoWiden) {}

    void ProcessChangesInternal(ProcessBlockChanges const& changes, AudioProcessingContext const&) override {
        if (auto p = changes.changed_params.Param(ParamIndex::StereoWidenWidth)) {
            auto const val = p->ProjectedValue();
            f32 v = 0;
            if (val < 0)
                v = 1 - (-(val));
            else
                v = MapFrom01(val, 1, 4);

            width = v;
        }
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> frames, AudioProcessingContext const& context, ExtraProcessingContext) override {
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
