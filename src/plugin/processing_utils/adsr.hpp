// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Based on code by Nigel Redmon: https://www.earlevel.com/main/2013/06/02/envelope-generators-adsr-part-2/

#pragma once
#include "foundation/foundation.hpp"

// 'Target ratio' represents the curve of the segment. Smaller values such as 0.0001f will make the curve
// virtually exponential, large values such as 100.0f will make the curve virtually linear.

namespace adsr {

struct Params {
    static ALWAYS_INLINE f32 CalcCoeff(f32 num_samples, f32 one_plus_target_ratio, f32 target_ratio) {
        ASSERT(num_samples > 0.0f);
        ASSERT(target_ratio >= 0.000000001f);
        return Exp(-Log(one_plus_target_ratio / target_ratio) / num_samples);
    }

    void SetAttackSamples(f32 num_samples, f32 target_ratio) {
        auto const one_plus_target_ratio = target_ratio + 1.0f;
        attack_coef = CalcCoeff(num_samples, one_plus_target_ratio, target_ratio);
        attack_base = one_plus_target_ratio * (1.0f - attack_coef);
    }

    void SetDecaySamples(f32 num_samples, f32 target_ratio) {
        decay_coef = CalcCoeff(num_samples, 1 + target_ratio, target_ratio);
        decay_base = (sustain_amount - target_ratio) * (1.0f - decay_coef);
        decay_target_ratio = target_ratio;
    }

    void SetReleaseSamples(f32 num_samples, f32 target_ratio) {
        release_coef = CalcCoeff(num_samples, 1 + target_ratio, target_ratio);
        release_base = -target_ratio * (1.0f - release_coef);
    }

    void SetSustainAmp(f32 volume_amp) {
        sustain_amount = volume_amp;
        decay_base = (sustain_amount - decay_target_ratio) * (1.0f - decay_coef);
    }

    f32 attack_coef {};
    f32 attack_base {};
    f32 decay_coef {};
    f32 decay_base {};
    f32 decay_target_ratio {};
    f32 release_coef {};
    f32 release_base {};
    f32 sustain_amount {};
};

enum class State { Idle, Attack, Decay, Sustain, Release };

struct Processor {
    void Gate(bool set_to_active) {
        if (set_to_active)
            state = State::Attack;
        else if (state != State::Idle)
            state = State::Release;
    }

    void Reset() {
        state = State::Idle;
        output = 0.0f;
        prev_output = 0;
    }

    // TODO(1.0): don't smooth here: it's not the right place
    f32 SmoothOutput() {
        static constexpr f32 k_smoothing_amount = 0.10f;
        f32 const result = prev_output + (k_smoothing_amount * (output - prev_output));
        prev_output = result;
        return result;
    }

    f32 Process(Params const& params) {
        switch (state) {
            case State::Idle: break;
            case State::Attack: {
                output = params.attack_base + output * params.attack_coef;
                if (output >= 1.0f) {
                    output = 1.0f;
                    prev_output = 1;
                    state = State::Decay;
                }
                break;
            }
            case State::Decay: {
                output = params.decay_base + output * params.decay_coef;
                if (output <= params.sustain_amount) {
                    output = params.sustain_amount;
                    state = State::Sustain;
                }
                break;
            }
            case State::Sustain: {
                output = params.sustain_amount;
                break;
            }
            case State::Release: {
                output = params.release_base + output * params.release_coef;
                if (output <= 0.0f) Reset();
                break;
            }
        }
        return Clamp(SmoothOutput(), 0.0f, 1.0f);
    }

    bool IsIdle() { return state == State::Idle; }

    f32 prev_output = 0;
    f32 output = 0;
    State state = State::Idle;
};

} // namespace adsr
