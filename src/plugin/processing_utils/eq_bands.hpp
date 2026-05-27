// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "audio_processing_context.hpp"
#include "filters.hpp"

constexpr u32 k_num_eq_bands = 3;
constexpr u32 k_max_eq_band_stages = 2;

constexpr u32 EqTypeStageCount(param_values::EqType t) {
    switch (t) {
        case param_values::EqType::LowPass24:
        case param_values::EqType::HighPass24: return 2;
        case param_values::EqType::Peak:
        case param_values::EqType::LowShelf:
        case param_values::EqType::HighShelf:
        case param_values::EqType::Notch:
        case param_values::EqType::LowPass12:
        case param_values::EqType::HighPass12: return 1;
        case param_values::EqType::Count: break;
    }
    PanicIfReached();
    return 1;
}

constexpr rbj_filter::Type EqTypeToRbjType(param_values::EqType t) {
    switch (t) {
        case param_values::EqType::Peak: return rbj_filter::Type::Peaking;
        case param_values::EqType::LowShelf: return rbj_filter::Type::LowShelf;
        case param_values::EqType::HighShelf: return rbj_filter::Type::HighShelf;
        case param_values::EqType::Notch: return rbj_filter::Type::Notch;
        case param_values::EqType::LowPass12:
        case param_values::EqType::LowPass24: return rbj_filter::Type::LowPass;
        case param_values::EqType::HighPass12:
        case param_values::EqType::HighPass24: return rbj_filter::Type::HighPass;
        case param_values::EqType::Count: break;
    }
    PanicIfReached();
    return rbj_filter::Type::Peaking;
}

struct EqBand {
    struct ParamUpdate {
        Optional<f32> freq_hz;
        Optional<f32> resonance_linear;
        Optional<f32> gain_db;
        Optional<param_values::EqType> new_type;
    };

    f32x2 Process(f32x2 in) {
        auto const [coeffs, mix] = eq_coeffs.Value();
        f32x2 out = in * mix;
        for (auto const stage_index : Range(num_stages))
            out = rbj_filter::Process(eq_data[stage_index], coeffs, out);
        return out;
    }

    void OnParamChange(ParamUpdate const& update, f32 sample_rate) {
        bool changed = false;
        if (update.freq_hz) {
            eq_params.fs = sample_rate;
            eq_params.fc = *update.freq_hz;
            changed = true;
        }
        if (update.resonance_linear) {
            eq_params.fs = sample_rate;
            eq_params.q = rbj_filter::EqResonanceToQ(*update.resonance_linear);
            changed = true;
        }
        if (update.gain_db) {
            eq_params.fs = sample_rate;
            eq_params.peak_gain = *update.gain_db;
            changed = true;
        }
        if (update.new_type) {
            eq_params.fs = sample_rate;
            eq_params.type = EqTypeToRbjType(*update.new_type);
            num_stages = EqTypeStageCount(*update.new_type);
            changed = true;
        }
        if (changed) eq_coeffs.Set(eq_params);
    }

    void Reset() {
        eq_coeffs.ResetSmoothing();
        for (auto& d : eq_data)
            d = {};
    }

    Array<rbj_filter::StereoData, k_max_eq_band_stages> eq_data {};
    rbj_filter::Params eq_params {};
    rbj_filter::SmoothedCoefficients eq_coeffs {};
    u32 num_stages = 1;
};

struct EqBands {
    void OnParamChange(u32 band_num, EqBand::ParamUpdate const& update, f32 sample_rate) {
        eq_bands[band_num].OnParamChange(update, sample_rate);
    }

    void SetOn(bool on) { eq_mix = on ? 1.0f : 0.0f; }

    f32x2 Process(AudioProcessingContext const& context, f32x2 in) {
        f32x2 result = in;
        if (auto mix = eq_mix_smoother.LowPass(eq_mix, context.one_pole_smoothing_cutoff_10ms); mix != 0) {
            for (auto& eq_band : eq_bands)
                result = eq_band.Process(result);
            if (mix != 1) result = LinearInterpolate(mix, in, result);
        }
        return result;
    }

    void Reset() {
        for (auto& eq_band : eq_bands)
            eq_band.Reset();
        eq_mix_smoother.Reset();
    }

    Array<EqBand, k_num_eq_bands> eq_bands;
    f32 eq_mix {};
    OnePoleLowPassFilter<f32> eq_mix_smoother {};
};
