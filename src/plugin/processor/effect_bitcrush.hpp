// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "effect.hpp"

struct BitCrushProcessor {
    // Recomputing the step count is a transcendental call, so it's cached here rather than redone every
    // sample. The quantiser has 2^(bit_depth - 1) steps per unit and always keeps zero as a level.
    void SetBitDepth(f32 bit_depth) {
        ASSERT_HOT(bit_depth >= 1.0f);
        ASSERT_HOT(bit_depth <= 32.0f);
        m_bit_depth = bit_depth;
        m_half_levels = Pow(2.0f, bit_depth - 1.0f);
        m_legacy_resolution = 0.0f;
    }

    // The quantiser of the legacy Bits parameter, reproduced exactly so that existing DAW automation sounds
    // identical: 2^bit_depth - 1 steps per unit, applied to the input shifted up by one.
    void SetLegacyBitDepth(f32 bit_depth) {
        ASSERT_HOT(bit_depth >= 1.0f);
        ASSERT_HOT(bit_depth <= 32.0f);
        m_bit_depth = bit_depth;
        m_legacy_resolution = Pow(2.0f, bit_depth) - 1.0f;
    }

    void SetSampleRate(f32 rate_hz, f32 host_sample_rate) {
        ASSERT_HOT(rate_hz > 0.0f);
        ASSERT_HOT(host_sample_rate > 0.0f);
        m_legacy_rate = 0;
        m_phase_increment = Min(rate_hz / host_sample_rate, 1.0f);
    }

    // The timing of the legacy Sample Rate parameter, reproduced exactly so that existing DAW automation
    // sounds identical, quirks included: the hold length is a whole number of host samples, and the counter
    // wraps at the rate value rather than at the hold length.
    void SetLegacySampleRate(f32 rate_hz, f32 host_sample_rate) {
        ASSERT_HOT(rate_hz >= 1.0f);
        ASSERT_HOT(host_sample_rate > 0.0f);
        m_legacy_rate = (s32)(rate_hz + 0.5f);
        m_legacy_step = (s32)(host_sample_rate / (f32)m_legacy_rate);
    }

    void Reset() {
        m_phase = 1.0f;
        m_legacy_pos = 0;
        m_held_sample = 0;
    }

    f32x2 BitCrush(f32x2 input) {
        if (ShouldResample()) m_held_sample = Quantise(input);
        return m_held_sample;
    }

  private:
    f32x2 Quantise(f32x2 input) const {
        if (m_bit_depth >= 32.0f) return input;
        if (m_legacy_resolution != 0.0f)
            return Round((input + 1.0f) * m_legacy_resolution) / m_legacy_resolution - 1.0f;
        return Round(input * m_half_levels) / m_half_levels;
    }

    bool ShouldResample() {
        if (m_legacy_rate) {
            auto const resample = m_legacy_step > 0 && m_legacy_pos % m_legacy_step == 0;
            m_legacy_pos++;
            if (m_legacy_pos >= m_legacy_rate) m_legacy_pos -= m_legacy_rate;
            m_legacy_pos = Clamp(m_legacy_pos, 0, m_legacy_rate - 1);
            return resample;
        }

        // A fractional phase accumulator rather than an integer sample counter, so any rate is reachable
        // rather than only integer divisions of the host rate, and the result is the same at every host rate.
        m_phase += m_phase_increment;
        if (m_phase < 1.0f) return false;
        m_phase -= 1.0f;
        return true;
    }

    f32 m_phase = 1.0f;
    f32 m_phase_increment = 1.0f;
    s32 m_legacy_rate = 0;
    s32 m_legacy_step = 0;
    s32 m_legacy_pos = 0;
    f32x2 m_held_sample = 0;
    f32 m_bit_depth = 32.0f;
    f32 m_half_levels = 0.0f;
    f32 m_legacy_resolution = 0.0f;
};

class BitCrush final : public Effect {
  public:
    BitCrush() : Effect(EffectType::BitCrush) {}

  private:
    void ProcessChangesInternal(ProcessBlockChanges const& changes,
                                AudioProcessingContext const& context) override {
        if (auto p = changes.changed_params.ProjectedValueLegacyAware(ParamIndex::BitCrushBits)) {
            if (IsAnyLegacyOverriding(ParamIndex::BitCrushBits, changes.changed_params.params.values))
                m_bit_crusher.SetLegacyBitDepth(
                    changes.changed_params.params.ProjectedValue(ParamIndex::LegacyBitCrushBits));
            else
                m_bit_crusher.SetBitDepth(*p);
        }
        auto const rate_hz = changes.changed_params.ProjectedValueLegacyAware(ParamIndex::BitCrushBitRate);
        if (rate_hz) {
            ASSERT_HOT(*rate_hz >= 1.0f && *rate_hz <= 1000000.0f);
            m_rate_hz = *rate_hz;
            m_legacy_rate_overriding =
                IsAnyLegacyOverriding(ParamIndex::BitCrushBitRate, changes.changed_params.params.values);
        }
        if (rate_hz || m_host_sample_rate_at_timing != context.sample_rate) {
            m_host_sample_rate_at_timing = context.sample_rate;
            if (m_legacy_rate_overriding)
                m_bit_crusher.SetLegacySampleRate(m_rate_hz, context.sample_rate);
            else
                m_bit_crusher.SetSampleRate(m_rate_hz, context.sample_rate);
        }
        if (AnyChanged(changes.changed_params, k_bitcrush_wet_dry_mapping)) {
            auto const e = EffectiveWetDryFromMixOutputOrLegacy(changes.changed_params.params,
                                                                k_bitcrush_wet_dry_mapping);
            m_wet_dry.SetWet(e.wet_amp);
            m_wet_dry.SetDry(e.dry_amp);
        }
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> frames, AudioProcessingContext const& context, void*) override {
        return ProcessBlockByFrame(
            frames,
            [&](f32x2 in) {
                auto const v = m_bit_crusher.BitCrush(in);
                f32x2 const wet {v[0], v[1]};
                return m_wet_dry.MixStereo(context, wet, in);
            },
            context);
    }

    void ResetInternal() override {
        m_wet_dry.Reset();
        m_bit_crusher.Reset();
    }

    f32 m_rate_hz = 44100.0f;
    bool m_legacy_rate_overriding = false;
    f32 m_host_sample_rate_at_timing = 0.0f;
    BitCrushProcessor m_bit_crusher;
    EffectWetDryHelper m_wet_dry;
};
