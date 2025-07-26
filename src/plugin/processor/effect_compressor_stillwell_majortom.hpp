// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "dsp_stillwell_majortom.hpp"
#include "effect.hpp"

class Compressor final : public Effect {
  public:
    Compressor() : Effect(EffectType::Compressor) {}

  private:
    void ProcessChangesInternal(ProcessBlockChanges const& changes,
                                AudioProcessingContext const& context) override {
        bool params_changed = false;

        if (auto p = changes.changed_params.Param(ParamIndex::CompressorThreshold)) {
            m_compressor.slider_threshold = AmpToDb(p->ProjectedValue());
            params_changed = true;
        }
        if (auto p = changes.changed_params.Param(ParamIndex::CompressorRatio)) {
            m_compressor.slider_ratio = p->ProjectedValue();
            params_changed = true;
        }
        if (auto p = changes.changed_params.Param(ParamIndex::CompressorGain)) {
            m_compressor.slider_gain = p->ProjectedValue();
            params_changed = true;
        }
        if (auto p = changes.changed_params.Param(ParamIndex::CompressorAutoGain)) {
            m_compressor.slider_auto_gain = p->ValueAsBool();
            params_changed = true;
        }

        if (params_changed) m_compressor.Update(context.sample_rate);
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> frames, AudioProcessingContext const& context, void *) override {
        return ProcessBlockByFrame(
            frames,
            [&](f32x2 in) {
                alignas(f32x2) f32 out[2];
                m_compressor.Process(context.sample_rate, in.x, in.y, out[0], out[1]);
                return LoadAlignedToType<f32x2>(out);
            },
            context);
    }

    void ResetInternal() override { m_compressor.Reset(); }

    void PrepareToPlay(AudioProcessingContext const& context) override {
        m_compressor.SetSampleRate(context.sample_rate);
    }

    StillwellMajorTom m_compressor;
};
