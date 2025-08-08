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

    f32x2 BitCrush(f32x2 input, f32 sample_rate, s32 bit_depth, s32 bit_rate) {
        ASSERT_HOT(sample_rate > 0.0f);
        ASSERT_HOT(bit_depth >= 1);
        ASSERT_HOT(bit_depth <= 32);
        ASSERT_HOT(bit_rate >= 1);

        auto const resolution = IntegerPowerBase2(bit_depth) - 1;
        auto const step = (s32)(sample_rate / (f32)bit_rate);

        if (step > 0 && pos % step == 0) {
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

    s32 pos = 0;
    f32x2 held_sample = 0;
};

class BitCrush final : public Effect {
  public:
    BitCrush() : Effect(EffectType::BitCrush) {}

  private:
    void ProcessChangesInternal(ProcessBlockChanges const& changes, AudioProcessingContext const&) override {
        if (auto p = changes.changed_params.IntValue<s32>(ParamIndex::BitCrushBits)) m_bit_depth = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::BitCrushBitRate)) {
            ASSERT_HOT(*p >= 1.0f && *p <= 1000000.0f);
            m_bit_rate = (s32)(*p + 0.5f);
        }
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

    s32 m_bit_depth {}, m_bit_rate {};
    BitCrushProcessor m_bit_crusher;
    EffectWetDryHelper m_wet_dry;
};
