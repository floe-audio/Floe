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
    EffectProcessResult ProcessBlock(Span<StereoAudioFrame> frames,
                                     AudioProcessingContext const& context,
                                     ExtraProcessingContext) override {
        return ProcessBlockByFrame(
            frames,
            [&](StereoAudioFrame in) {
                StereoAudioFrame out;
                m_compressor.Process(context.sample_rate, in.l, in.r, out.l, out.r);
                return out;
            },
            context);
    }

    void OnParamChangeInternal(ChangedParams changed_params, AudioProcessingContext const& context) override {
        if (auto p = changed_params.Param(ParamIndex::CompressorThreshold))
            m_compressor.slider_threshold = AmpToDb(p->ProjectedValue());
        if (auto p = changed_params.Param(ParamIndex::CompressorRatio))
            m_compressor.slider_ratio = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::CompressorGain))
            m_compressor.slider_gain = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::CompressorAutoGain))
            m_compressor.slider_auto_gain = p->ValueAsBool();

        m_compressor.Update(context.sample_rate);
    }

    void ResetInternal() override { m_compressor.Reset(); }

    void PrepareToPlay(AudioProcessingContext const& context) override {
        m_compressor.SetSampleRate(context.sample_rate);
    }

    StillwellMajorTom m_compressor;
};
