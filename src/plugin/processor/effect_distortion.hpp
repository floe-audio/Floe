// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "effect.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/smoothed_value_system.hpp"

enum DistFunction {
    DistFunctionTubeLog,
    DistFunctionTubeAsym3,
    DistFunctionSinFunc,
    DistFunctionRaph1,
    DistFunctionDecimate,
    DistFunctionAtan,
    DistFunctionClip,

    DistFunctionCount
};

struct DistortionProcessor {
    f32x2 Saturate(f32x2 input, DistFunction type, f32 amount_fraction) {
        f32x2 output = 0;

        auto const input_gain = (amount_fraction * 59) + 1;
        input *= input_gain;

        switch (type) {
            case DistFunctionTubeLog: {
                output = Copysign(Log(1 + Fabs(input)), input);
                break;
            }
            case DistFunctionTubeAsym3: {
                auto const a = Exp(input - 1);
                auto const b = Exp(-input);
                auto const num = a - b - (1 / Exp(1.0f)) + 1;
                auto const denom = a + b;

                output = (num / denom);
                break;
            }
            case DistFunctionSinFunc: {
                output = Sin(input);
                break;
            }
            case DistFunctionRaph1: {
                output = (input < 0) ? (Exp(input) - 1.0f - Sinc(3.0f + input))
                                     : (1.0f - Exp(-input) + Sinc(input - 3.0f));
                break;
            }
            case DistFunctionDecimate: {
                constexpr int k_decimate_bits = 16;
                constexpr f32 k_m = 1 << (k_decimate_bits - 1);

                auto const amount = (amount_fraction * 59) + 1;
                m_decimate_cnt += amount + ((1.0f - amount) * 0.165f);

                if (m_decimate_cnt >= 1) {
                    m_decimate_cnt -= 1;
                    m_decimate_y = Trunc(input * k_m) / k_m;
                }
                output = Tanh(m_decimate_y);
                break;
            }
            case DistFunctionAtan: {
                auto const amount = (amount_fraction * 59 + 1) / 8;
                output = (1.0f / Atan(amount)) * Atan(input * amount);
                break;
            }
            case DistFunctionClip: {
                output = input >= 0 ? Min(input, f32x2(1.0f)) : Max(input, f32x2(-1.0f));
                break;
            }
            case DistFunctionCount: PanicIfReached(); break;
        }

        auto const abs = Fabs(output);
        output = abs > 20.0f ? (output / abs) : output;

        output /= input_gain;
        output *= MapFrom01(amount_fraction, 1, 2);

        return output;
    }

    static f32x2 Sinc(f32x2 x) {
        auto initial_x = x;
        x = x == 0.0f ? 1.0f : x; // Avoid division by zero
        x *= k_pi<>;
        return initial_x == 0.0f ? f32x2(1) : Sin(x) / x;
    }

  private:
    f32x2 m_decimate_y = 0;
    f32 m_decimate_cnt = 0;
};

class Distortion final : public Effect {
  public:
    Distortion(FloeSmoothedValueSystem& s)
        : Effect(s, EffectType::Distortion)
        , m_amount_smoother_id(s.CreateSmoother()) {}

  private:
    StereoAudioFrame
    ProcessFrame(AudioProcessingContext const&, StereoAudioFrame in, u32 frame_index) override {
        auto const amt = smoothed_value_system.Value(m_amount_smoother_id, frame_index);
        return StereoAudioFrame::FromF32x2(m_processor.Saturate(in.ToF32x2(), m_type, amt));
    }

    void OnParamChangeInternal(ChangedParams changed_params, AudioProcessingContext const&) override {
        if (auto p = changed_params.Param(ParamIndex::DistortionType)) {
            // Remapping enum values like this allows us to separate values that cannot change (the
            // parameter value), with values that we have more control over (DSP code)
            switch (p->ValueAsInt<param_values::DistortionType>()) {
                case param_values::DistortionType::TubeLog: m_type = DistFunctionTubeLog; break;
                case param_values::DistortionType::TubeAsym3: m_type = DistFunctionTubeAsym3; break;
                case param_values::DistortionType::Sine: m_type = DistFunctionSinFunc; break;
                case param_values::DistortionType::Raph1: m_type = DistFunctionRaph1; break;
                case param_values::DistortionType::Decimate: m_type = DistFunctionDecimate; break;
                case param_values::DistortionType::Atan: m_type = DistFunctionAtan; break;
                case param_values::DistortionType::Clip: m_type = DistFunctionClip; break;
                case param_values::DistortionType::Count: PanicIfReached(); break;
            }
        }

        if (auto p = changed_params.Param(ParamIndex::DistortionDrive)) {
            constexpr f32 k_smoothing_ms = 10;
            smoothed_value_system.Set(m_amount_smoother_id, p->ProjectedValue(), k_smoothing_ms);
        }
    }

    FloeSmoothedValueSystem::FloatId const m_amount_smoother_id;
    DistFunction m_type;
    DistortionProcessor m_processor {};
};
