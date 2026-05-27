// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/debug/tracy_wrapped.hpp"

#include "effect.hpp"
#include "processing_utils/eq_bands.hpp"

class Eq final : public Effect {
  public:
    Eq() : Effect(EffectType::Eq) { eq_bands.SetOn(true); }

    void ResetInternal() override { eq_bands.Reset(); }

    void ProcessChangesInternal(ProcessBlockChanges const& changes,
                                AudioProcessingContext const& context) override {
        struct BandParams {
            ParamIndex type;
            ParamIndex freq;
            ParamIndex reso;
            ParamIndex gain;
        };
        constexpr Array<BandParams, k_num_eq_bands> k_band_params {{
            {ParamIndex::EqType1, ParamIndex::EqFreq1, ParamIndex::EqResonance1, ParamIndex::EqGain1},
            {ParamIndex::EqType2, ParamIndex::EqFreq2, ParamIndex::EqResonance2, ParamIndex::EqGain2},
            {ParamIndex::EqType3, ParamIndex::EqFreq3, ParamIndex::EqResonance3, ParamIndex::EqGain3},
        }};

        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::EqMix)) mix_param = *p;

        for (auto const band_index : Range(k_num_eq_bands)) {
            auto const& p = k_band_params[band_index];
            eq_bands.OnParamChange(
                band_index,
                {
                    .freq_hz = changes.changed_params.ProjectedValue(p.freq),
                    .resonance_linear = changes.changed_params.LinearValue(p.reso),
                    .gain_db = changes.changed_params.ProjectedValue(p.gain),
                    .new_type = changes.changed_params.IntValue<param_values::EqType>(p.type),
                },
                context.sample_rate);
        }
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> io_frames, AudioProcessingContext const& context, void*) override {
        ZoneNamedN(process_block, "Eq ProcessBlock", true);
        return ProcessBlockByFrame(
            io_frames,
            [&](f32x2 frame) { return eq_bands.Process(context, frame); },
            context);
    }

    EqBands eq_bands {};
};
