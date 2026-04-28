// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "effect.hpp"
#include "param.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/filters.hpp"

constexpr u32 EffectFilterStageCount(param_values::EffectFilterType t) {
    switch (t) {
        case param_values::EffectFilterType::LowPass12:
        case param_values::EffectFilterType::HighPass12: return 1;
        case param_values::EffectFilterType::LowPass24:
        case param_values::EffectFilterType::HighPass24:
        case param_values::EffectFilterType::BandPass:
        case param_values::EffectFilterType::Notch:
        case param_values::EffectFilterType::Peak:
        case param_values::EffectFilterType::LowShelf:
        case param_values::EffectFilterType::HighShelf: return 2;
        case param_values::EffectFilterType::Count: break;
    }
    PanicIfReached();
    return 2;
}

constexpr rbj_filter::Type EffectFilterTypeToRbjType(param_values::EffectFilterType t) {
    switch (t) {
        case param_values::EffectFilterType::LowPass12:
        case param_values::EffectFilterType::LowPass24: return rbj_filter::Type::LowPass;
        case param_values::EffectFilterType::HighPass12:
        case param_values::EffectFilterType::HighPass24: return rbj_filter::Type::HighPass;
        case param_values::EffectFilterType::BandPass: return rbj_filter::Type::BandPassCzpg;
        case param_values::EffectFilterType::Notch: return rbj_filter::Type::Notch;
        case param_values::EffectFilterType::Peak: return rbj_filter::Type::Peaking;
        case param_values::EffectFilterType::LowShelf: return rbj_filter::Type::LowShelf;
        case param_values::EffectFilterType::HighShelf: return rbj_filter::Type::HighShelf;
        case param_values::EffectFilterType::Count: break;
    }
    PanicIfReached();
    return rbj_filter::Type::LowPass;
}

class FilterEffect final : public Effect {
  public:
    FilterEffect()
        : Effect(EffectType::FilterEffect)
        , m_filter_type({
              .current_idx = ParamIndex::FilterType,
              .legacies = {{{
                  .idx = ParamIndex::LegacyFilterType,
                  .remap_table = k_legacy_effect_filter_type_to_current,
              }}},
          }) {}

    static bool IsUsingGainParam(Parameters const& params) {
        return param_values::EffectFilterTypeUsesGain(
            params.IntValue<param_values::EffectFilterType>(ParamIndex::FilterType));
    }

  private:
    void PrepareToPlay(AudioProcessingContext const& context) override {
        m_filter_params.fs = context.sample_rate;
        m_smoothed_coeffs.Set(m_filter_params);
    }

    void ProcessChangesInternal(ProcessBlockChanges const& changes, AudioProcessingContext const&) override {
        bool set_params = false;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::FilterCutoff)) {
            m_filter_params.fc = *p;
            set_params = true;
        }
        if (changes.changed_params.Changed(ParamIndex::LegacyFilterResonance) ||
            changes.changed_params.Changed(ParamIndex::FilterResonance)) {
            auto const legacy_idx = ParamIndex::LegacyFilterResonance;
            auto const legacy_linear = changes.changed_params.params.LinearValue(legacy_idx);
            auto const legacy_default = k_param_descriptors[ToInt(legacy_idx)].default_linear_value;
            m_filter_params.q =
                (legacy_linear != legacy_default)
                    ? MapFrom01Skew(legacy_linear, 0.5f, 2, 5)
                    : rbj_filter::EffectFilterResonanceToQ(
                          changes.changed_params.params.LinearValue(ParamIndex::FilterResonance));
            set_params = true;
        }
        if (changes.changed_params.Changed(ParamIndex::LegacyFilterGain) ||
            changes.changed_params.Changed(ParamIndex::FilterGain)) {
            auto const legacy_idx = ParamIndex::LegacyFilterGain;
            auto const legacy_linear = changes.changed_params.params.LinearValue(legacy_idx);
            auto const legacy_default = k_param_descriptors[ToInt(legacy_idx)].default_linear_value;
            m_filter_params.peak_gain =
                (legacy_linear != legacy_default)
                    ? k_param_descriptors[ToInt(legacy_idx)].ProjectValue(legacy_linear)
                    : k_param_descriptors[ToInt(ParamIndex::FilterGain)].ProjectValue(
                          changes.changed_params.params.LinearValue(ParamIndex::FilterGain)) /
                          2;
            set_params = true;
        }
        if (auto p = m_filter_type.Poll(changes.changed_params)) {
            m_filter_params.type = EffectFilterTypeToRbjType(*p);
            m_num_stages = EffectFilterStageCount(*p);
            set_params = true;
        }

        if (set_params) m_smoothed_coeffs.Set(m_filter_params);
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> frames, AudioProcessingContext const& context, void*) override {
        return ProcessBlockByFrame(
            frames,
            [&](f32x2 in) {
                auto const [coeffs, filter_mix] = m_smoothed_coeffs.Value();
                f32x2 out = in * filter_mix;
                out = Process(m_filter1, coeffs, out);
                if (m_num_stages == 2) out = Process(m_filter2, coeffs, out);
                return out;
            },
            context);
    }

    void ResetInternal() override {
        m_filter1 = {};
        m_filter2 = {};
        m_smoothed_coeffs.ResetSmoothing();
    }

    EnumParamWithLegacies<param_values::EffectFilterType> const m_filter_type;
    rbj_filter::SmoothedCoefficients m_smoothed_coeffs {};
    rbj_filter::StereoData m_filter1 {};
    rbj_filter::StereoData m_filter2 {};
    rbj_filter::Params m_filter_params = {};
    u32 m_num_stages = 2;
};
