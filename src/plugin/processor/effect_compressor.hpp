// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <vitfx/wrapper.hpp>

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "dsp_stillwell_majortom.hpp"
#include "effect.hpp"

class Compressor final : public Effect {
  public:
    Compressor() : Effect(EffectType::Compressor), m_vital(vitfx::compressor::Create()) {}
    ~Compressor() override { vitfx::compressor::Destroy(m_vital); }

  private:
    void ProcessChangesInternal(ProcessBlockChanges const& changes,
                                AudioProcessingContext const& context) override {
        if (auto p =
                changes.changed_params.IntValue<param_values::CompressorType>(ParamIndex::CompressorType))
            m_type = *p;

        bool major_tom_changed = false;

        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::CompressorThreshold)) {
            auto const db = AmpToDb(*p);
            m_major_tom.slider_threshold = db;
            m_vital_args.params[ToInt(vitfx::compressor::Params::UpperThresholdDb)] = db;
            major_tom_changed = true;
        }
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::CompressorRatio)) {
            m_major_tom.slider_ratio = *p;
            // Map traditional ratio (1..20) to Vital's 0..1 normalised ratio: 1 - 1/r.
            m_vital_args.params[ToInt(vitfx::compressor::Params::UpperRatio)] = 1.0f - (1.0f / *p);
            major_tom_changed = true;
        }
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::CompressorGain)) {
            m_major_tom.slider_gain = *p;
            m_vital_args.params[ToInt(vitfx::compressor::Params::OutputGainDb)] = *p;
            major_tom_changed = true;
        }
        if (auto p = changes.changed_params.BoolValue(ParamIndex::CompressorAutoGain)) {
            m_major_tom.slider_auto_gain = *p;
            major_tom_changed = true;
        }
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::CompressorAttack))
            m_vital_args.params[ToInt(vitfx::compressor::Params::Attack)] = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::CompressorRelease))
            m_vital_args.params[ToInt(vitfx::compressor::Params::Release)] = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::CompressorMix)) mix_param = *p;

        if (major_tom_changed) m_major_tom.Update(context.sample_rate);
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> io_frames, AudioProcessingContext const& context, void*) override {
        if (!ShouldProcessBlock()) return EffectProcessResult::Done;

        switch (m_type) {
            case param_values::CompressorType::MajorTom:
                return ProcessBlockByFrame(
                    io_frames,
                    [&](f32x2 in) {
                        alignas(f32x2) f32 out[2];
                        m_major_tom.Process(context.sample_rate, in.x, in.y, out[0], out[1]);
                        return LoadAlignedToType<f32x2>(out);
                    },
                    context);

            case param_values::CompressorType::Vital: {
                f32x2 wet[k_block_size_max];
                CopyMemory(wet, io_frames.data, io_frames.size * sizeof(f32x2));

                auto num_frames = (u32)io_frames.size;
                u32 pos = 0;
                while (num_frames) {
                    u32 const chunk_size = Min(num_frames, 64u);

                    m_vital_args.num_frames = (int)chunk_size;
                    m_vital_args.in_interleaved = (f32*)(io_frames.data + pos);
                    m_vital_args.out_interleaved = (f32*)(wet + pos);
                    // Lower threshold/ratio pinned to neutral so this acts as a downward-only
                    // compressor.
                    m_vital_args.params[ToInt(vitfx::compressor::Params::LowerThresholdDb)] = -100.0f;
                    m_vital_args.params[ToInt(vitfx::compressor::Params::LowerRatio)] = 0.0f;
                    // Wet/dry blend is handled outside via the Effect base's user_mix.
                    m_vital_args.params[ToInt(vitfx::compressor::Params::Mix)] = 1.0f;

                    vitfx::compressor::Process(*m_vital, m_vital_args);

                    num_frames -= chunk_size;
                    pos += chunk_size;
                }

                for (auto const frame_index : Range((u32)io_frames.size))
                    io_frames[frame_index] =
                        ApplyBypassCrossfade(context, wet[frame_index], io_frames[frame_index]);

                return EffectProcessResult::Done;
            }

            case param_values::CompressorType::Count: break;
        }
        PanicIfReached();
        return EffectProcessResult::Done;
    }

    void ResetInternal() override {
        m_major_tom.Reset();
        vitfx::compressor::HardReset(*m_vital);
    }

    void PrepareToPlay(AudioProcessingContext const& context) override {
        m_major_tom.SetSampleRate(context.sample_rate);
        vitfx::compressor::SetSampleRate(*m_vital, (int)context.sample_rate);
    }

    param_values::CompressorType m_type {param_values::CompressorType::Vital};
    StillwellMajorTom m_major_tom;
    vitfx::compressor::Compressor* m_vital {};
    vitfx::compressor::ProcessCompressorArgs m_vital_args {};
};
