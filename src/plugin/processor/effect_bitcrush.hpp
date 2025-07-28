// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "effect.hpp"

struct BitCrushProcessor {
    static s64 IntegerPowerBase2(s64 exponent) {
        s64 i = 1;
        for (int j = 1; j <= exponent; j++)
            i *= 2;
        return i;
    }

    f32x2 BitCrush(f32x2 input, f32 sample_rate, int bit_depth, int bit_rate) {
        auto const resolution = IntegerPowerBase2(bit_depth) - 1;
        auto const step = (int)(sample_rate / (f32)bit_rate);

        if (pos % step == 0) {
            if (bit_depth < 32)
                held_sample = Round((input + 1.0f) * (f32)resolution) / (f32)resolution - 1.0f;
            else
                held_sample = input;
        }

        pos++;
        if (pos >= bit_rate) pos -= bit_rate;
        pos = Clamp(pos, 0, bit_rate - 1);

        return held_sample;
    }

    int pos = 0;
    f32x2 held_sample = 0;
};

class BitCrush final : public Effect {
  public:
    BitCrush() : Effect(EffectType::BitCrush) {}

  private:
    void ProcessChangesInternal(ProcessBlockChanges const& changes, AudioProcessingContext const&) override {
        if (auto p = changes.changed_params.IntValue<int>(ParamIndex::BitCrushBits)) m_bit_depth = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::BitCrushBitRate))
            m_bit_rate = (int)(*p + 0.5f);
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::BitCrushWet)) m_wet_dry.SetWet(*p);
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::BitCrushDry)) m_wet_dry.SetDry(*p);
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> frames, AudioProcessingContext const& context, void*) override {
        return ProcessBlockByFrame(
            frames,
            [&](f32x2 in) {
                auto const v = m_bit_crusher.BitCrush(in, context.sample_rate, m_bit_depth, m_bit_rate);
                f32x2 const wet {v[0], v[1]};
                return m_wet_dry.MixStereo(context, wet, in);
            },
            context);
    }

    void ResetInternal() override { m_wet_dry.Reset(); }

    int m_bit_depth, m_bit_rate;
    BitCrushProcessor m_bit_crusher;
    EffectWetDryHelper m_wet_dry;
};
