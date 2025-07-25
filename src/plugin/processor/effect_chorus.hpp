// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// This effect will be replaced by something much better sounding. We will keep this around though so old
// presets still sound the same. It deserves to be buried away in some 'legacy' folder.

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "effect.hpp"
#include "processing_utils/filters.hpp"
#include "processing_utils/lfo.hpp"

struct ChorusProcessor {
    ChorusProcessor() { chorus_lfo.SetWaveform(LFO::Waveform::Sine); }

    ~ChorusProcessor() {
        if (delay_line.buffer.size) PageAllocator::Instance().Free(delay_line.buffer.ToByteSpan());
    }

    // rate_hz sounds good from 0.001 to 10
    void SetRate(f32 sample_rate, f32 rate_hz) { chorus_lfo.SetRate(sample_rate, rate_hz); }

    void SetDelayTime(f32 sample_rate, f32 new_delay_time_ms) {
        ASSERT_HOT(new_delay_time_ms > 0.0f);
        ASSERT_HOT(sample_rate > 0.0f);

        if (delay_line.buffer.size) PageAllocator::Instance().Free(delay_line.buffer.ToByteSpan());
        delay_line.size_float = (new_delay_time_ms / 1000) * sample_rate;
        delay_line.buffer = PageAllocator::Instance().AllocateExactSizeUninitialised<f32x2>(
            Max((u32)delay_line.size_float, 1u));
        Reset();
    }

    StereoAudioFrame Process(StereoAudioFrame in,
                             f32 depth01,
                             rbj_filter::Coeffs const& lowpass_coeffs,
                             rbj_filter::Coeffs const& highpass_coeffs) {
        ASSERT_HOT(depth01 >= 0.0f && depth01 <= 1.0f);

        constexpr auto k_min_time_multiplier = 0.04f;
        auto const depth = (-(0.5f - (k_min_time_multiplier / 2)) * depth01) + 1.0f;
        auto const time_multiplier =
            (chorus_lfo.Tick() * (1.f - depth)) + depth; // range: [k_min_time_multiplier, 1]

        auto const dl_offset = time_multiplier * delay_line.size_float;
        ASSERT_HOT(dl_offset >= 0.0f);
        auto const dl_offset_int = (u32)dl_offset;

        ASSERT_HOT(delay_line.current >= delay_line.buffer.data);
        ASSERT_HOT(delay_line.current < End(delay_line.buffer));
        ASSERT_HOT(dl_offset_int <= delay_line.buffer.size);

        auto ptr1 = delay_line.current - dl_offset_int;
        if (ptr1 < delay_line.buffer.data) ptr1 += delay_line.buffer.size;
        auto ptr2 = ptr1 - 1;
        if (ptr2 < delay_line.buffer.data) ptr2 += delay_line.buffer.size;

        ASSERT_HOT(ptr1 >= delay_line.buffer.data);
        ASSERT_HOT(ptr2 >= delay_line.buffer.data);
        ASSERT_HOT(ptr1 < End(delay_line.buffer));
        ASSERT_HOT(ptr2 < End(delay_line.buffer));

        auto const frac = dl_offset - (f32)dl_offset_int;
        ASSERT_HOT(frac >= 0.0f);
        ASSERT_HOT(frac <= 1.0f);
        auto out = LinearInterpolate(frac, *ptr1, *ptr2) - (z1 * 0.1f);
        z1 = out;

        StereoAudioFrame out_frame {out[0], out[1]};

        out_frame = rbj_filter::Process(lowpass, lowpass_coeffs, out_frame);
        out_frame = rbj_filter::Process(highpass, highpass_coeffs, out_frame);

        *delay_line.current = {in.l, in.r};
        if (++delay_line.current >= End(delay_line.buffer)) delay_line.current = delay_line.buffer.data;

        return out_frame;
    }

    void SetPhase(u32 val) { chorus_lfo.phase = val; }

    void Reset() {
        highpass = {};
        lowpass = {};
        for (auto& x : delay_line.buffer)
            x = {};
        delay_line.current = delay_line.buffer.data;
        z1 = {};
    }

    f32x2 z1 {};
    LFO chorus_lfo {};
    rbj_filter::StereoData highpass {}, lowpass {};

    struct {
        f32x2* current;
        f32 size_float;
        Span<f32x2> buffer {};
    } delay_line;
};

class Chorus final : public Effect {
  public:
    Chorus() : Effect(EffectType::Chorus) {}

  private:
    void PrepareToPlay(AudioProcessingContext const& context) override {
        m_c[ToInt(ChorusIndexes::First)].SetDelayTime(context.sample_rate, 2);
        m_c[ToInt(ChorusIndexes::First)].SetPhase(0);

        m_c[ToInt(ChorusIndexes::Second)].SetDelayTime(context.sample_rate, 5);
        m_c[ToInt(ChorusIndexes::Second)].SetPhase(1);

        m_lowpass_filter_coeffs = rbj_filter::Coefficients({.type = rbj_filter::Type::LowPass,
                                                            .fs = context.sample_rate,
                                                            .fc = 14000,
                                                            .q = 1,
                                                            .peak_gain = 0});
    }

    void OnParamChangeInternal(ChangedParams changed_params, AudioProcessingContext const& context) override {
        if (auto p = changed_params.Param(ParamIndex::ChorusRate)) {
            auto const val = p->ProjectedValue();
            for (auto const i : Range(ToInt(ChorusIndexes::Count)))
                m_c[i].SetRate(context.sample_rate, val);
        }
        if (auto p = changed_params.Param(ParamIndex::ChorusHighpass)) {
            auto const val = p->ProjectedValue();
            m_highpass_filter_coeffs.Set(rbj_filter::Type::HighPass, context.sample_rate, val, 1, 0);
        }
        if (auto p = changed_params.Param(ParamIndex::ChorusDepth)) m_depth_01 = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ChorusWet)) m_wet_dry.SetWet(p->ProjectedValue());
        if (auto p = changed_params.Param(ParamIndex::ChorusDry)) m_wet_dry.SetDry(p->ProjectedValue());
    }

    StereoAudioFrame ProcessFrame(AudioProcessingContext const& context, StereoAudioFrame in, u32) override {
        auto const depth = m_depth_01_smoother.LowPass(m_depth_01, context.one_pole_smoothing_cutoff_10ms);
        auto const [highpass_coeffs, filter_mix] = m_highpass_filter_coeffs.Value();
        auto chorus_in = in * filter_mix;

        auto out = chorus_in;
        out = m_c[ToInt(ChorusIndexes::First)].Process(chorus_in,
                                                       depth,
                                                       m_lowpass_filter_coeffs,
                                                       highpass_coeffs);
        out += m_c[ToInt(ChorusIndexes::Second)].Process(chorus_in,
                                                         depth,
                                                         m_lowpass_filter_coeffs,
                                                         highpass_coeffs) /
               2;
        return m_wet_dry.MixStereo(context, out, in);
    }

    void ResetInternal() override {
        for (auto const i : Range(ToInt(ChorusIndexes::Count)))
            m_c[i].Reset();
        m_highpass_filter_coeffs.ResetSmoothing();
        m_depth_01_smoother.Reset();
        m_wet_dry.Reset();
    }

    enum class ChorusIndexes : u32 { First, Second, Count };

    rbj_filter::Coeffs m_lowpass_filter_coeffs {};
    rbj_filter::SmoothedCoefficients m_highpass_filter_coeffs {};

    f32 m_depth_01 {};
    OnePoleLowPassFilter<f32> m_depth_01_smoother {};
    EffectWetDryHelper m_wet_dry;
    Array<ChorusProcessor, ToInt(ChorusIndexes::Count)> m_c;
};
