// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "effect.hpp"
#include "processing_utils/audio_processing_context.hpp"

enum DistFunction {
    DistFunctionTubeLog,
    DistFunctionTubeAsym3,
    DistFunctionSinFunc,
    DistFunctionRaph1,
    DistFunctionDecimate,
    DistFunctionAtan,
    DistFunctionClip,
    DistFunctionFoldback,
    DistFunctionRectifier,
    DistFunctionRingMod,

    DistFunctionCount
};

struct DistortionProcessor {
    DistortionProcessor() { Reset(); }

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

                auto const amount = (amount_fraction * 199) + 1;
                decimate_cnt += amount + ((1.0f - amount) * 0.165f);

                if (decimate_cnt >= 1) {
                    decimate_cnt -= 1;
                    decimate_y = Trunc(input * k_m) / k_m;
                }
                output = Tanh(decimate_y);
                break;
            }
            case DistFunctionAtan: {
                auto const amount = (amount_fraction * 59 + 1) / 4;
                output = (1.0f / Atan(amount)) * Atan(input * amount);
                break;
            }
            case DistFunctionClip: {
                output = input >= 0 ? Min(input, f32x2(1.0f)) : Max(input, f32x2(-1.0f));
                break;
            }
            case DistFunctionFoldback: {
                auto const threshold = 0.5f + (amount_fraction * 0.4f);
                auto abs_input = Fabs(input);
                auto sign = Copysign(f32x2(1), input);

                output =
                    abs_input > threshold ? sign * Max(threshold - (abs_input - threshold), f32x2(0)) : input;
                output = Tanh(output * (1 + amount_fraction));
                break;
            }
            case DistFunctionRectifier: {
                auto const mix = amount_fraction;
                auto const rectified = Fabs(input);
                output = input * (1 - mix) + rectified * mix;
                output = Tanh(output * (1 + amount_fraction * 2));
                break;
            }
            case DistFunctionRingMod: {
                auto const freq = 50 + (amount_fraction * 200);
                ring_phase += freq * k_tau<> / 44100.0f;
                if (ring_phase > k_tau<>) ring_phase -= k_tau<>;

                auto const modulator = Sin(ring_phase);
                auto const ring_amount = amount_fraction;
                output = input * (1 - ring_amount + ring_amount * modulator);
                output = Tanh(output * (1 + amount_fraction));
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

    void Reset() {
        decimate_y = 0;
        decimate_cnt = 0;
        ring_phase = 0;
        chaos_state = 0.5f;
    }

    f32x2 decimate_y;
    f32 decimate_cnt;
    f32 ring_phase;
    f32 chaos_state;
};

struct Distortion final : public Effect {
    Distortion() : Effect(EffectType::Distortion) {}

    void ProcessChangesInternal(ProcessBlockChanges const& changes, AudioProcessingContext const&) override {
        if (auto p = changes.changed_params.Param(ParamIndex::DistortionType)) {
            // Remapping enum values like this allows us to separate values that cannot change (the
            // parameter value), with values that we have more control over (DSP code)
            switch (p->ValueAsInt<param_values::DistortionType>()) {
                case param_values::DistortionType::TubeLog: type = DistFunctionTubeLog; break;
                case param_values::DistortionType::TubeAsym3: type = DistFunctionTubeAsym3; break;
                case param_values::DistortionType::Sine: type = DistFunctionSinFunc; break;
                case param_values::DistortionType::Raph1: type = DistFunctionRaph1; break;
                case param_values::DistortionType::Decimate: type = DistFunctionDecimate; break;
                case param_values::DistortionType::Atan: type = DistFunctionAtan; break;
                case param_values::DistortionType::Clip: type = DistFunctionClip; break;
                case param_values::DistortionType::Foldback: type = DistFunctionFoldback; break;
                case param_values::DistortionType::Rectifier: type = DistFunctionRectifier; break;
                case param_values::DistortionType::RingMod: type = DistFunctionRingMod; break;
                case param_values::DistortionType::Count: PanicIfReached(); break;
            }
        }

        if (auto p = changes.changed_params.Param(ParamIndex::DistortionDrive)) amount = p->ProjectedValue();
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> frames, AudioProcessingContext const& context, void *) override {
        return ProcessBlockByFrame(
            frames,
            [&](f32x2 in) {
                return processor.Saturate(
                    in,
                    type,
                    amount_smoother.LowPass(amount, context.one_pole_smoothing_cutoff_10ms));
            },
            context);
    }

    void ResetInternal() override {
        processor.Reset();
        amount_smoother.Reset();
    }

    f32 amount {};
    OnePoleLowPassFilter<f32> amount_smoother {};
    DistFunction type;
    DistortionProcessor processor {};
};
