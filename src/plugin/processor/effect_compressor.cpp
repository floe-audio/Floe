// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "effect_compressor.hpp"

#include "tests/framework.hpp"

// Both compressor engines report their compression envelope so the effect can drive a gain reduction
// meter. The exact readings differ between them (different detectors and envelopes), so these check the
// properties the meter relies on rather than specific dB values.

TEST_CASE(TestVintageCompressorGainReductionReporting) {
    constexpr f32 k_sample_rate = 44100.0f;

    auto const reduction_after_loud_signal = [&](f32 ratio) {
        StillwellMajorTom compressor {};
        compressor.SetSampleRate(k_sample_rate);
        compressor.slider_threshold = -20;
        compressor.slider_ratio = ratio;
        compressor.slider_auto_gain = false;
        compressor.slider_gain = 0;
        compressor.Update(k_sample_rate);

        f32 out_l;
        f32 out_r;
        for (auto _ : Range(4096u))
            compressor.Process(k_sample_rate, 1.0f, 1.0f, out_l, out_r);
        return compressor.gain_reduction_db;
    };

    StillwellMajorTom quiet {};
    quiet.SetSampleRate(k_sample_rate);
    quiet.slider_threshold = -20;
    quiet.slider_ratio = 4;
    quiet.Update(k_sample_rate);
    REQUIRE_APPROX_EQ(quiet.gain_reduction_db, 0.0f, 0.001f);

    f32 out_l;
    f32 out_r;
    for (auto _ : Range(4096u))
        quiet.Process(k_sample_rate, 0.0f, 0.0f, out_l, out_r);
    REQUIRE_APPROX_EQ(quiet.gain_reduction_db, 0.0f, 0.001f);

    auto const gentle = reduction_after_loud_signal(2);
    auto const heavy = reduction_after_loud_signal(16);
    REQUIRE(gentle > 0.0f);
    REQUIRE(heavy > gentle);

    // A signal under the threshold shouldn't report any reduction.
    StillwellMajorTom under_threshold {};
    under_threshold.SetSampleRate(k_sample_rate);
    under_threshold.slider_threshold = -20;
    under_threshold.slider_ratio = 4;
    under_threshold.Update(k_sample_rate);
    for (auto _ : Range(4096u))
        under_threshold.Process(k_sample_rate, 0.001f, 0.001f, out_l, out_r);
    REQUIRE_APPROX_EQ(under_threshold.gain_reduction_db, 0.0f, 0.001f);

    return k_success;
}

TEST_CASE(TestModernCompressorGainReductionReporting) {
    constexpr int k_num_frames = 64;

    auto const min_gain_mult_after_loud_signal = [&](f32 ratio_01, f32 input_amp) {
        auto* compressor = vitfx::compressor::Create();
        DEFER { vitfx::compressor::Destroy(compressor); };
        vitfx::compressor::SetSampleRate(*compressor, 44100);

        Array<f32, k_num_frames * 2> in {};
        Array<f32, k_num_frames * 2> out {};
        for (auto& f : in)
            f = input_amp;

        f32 min_gain_mult = 1;
        vitfx::compressor::ProcessCompressorArgs args {};
        args.num_frames = k_num_frames;
        args.in_interleaved = in.data;
        args.out_interleaved = out.data;
        args.params[ToInt(vitfx::compressor::Params::UpperThresholdDb)] = -20;
        args.params[ToInt(vitfx::compressor::Params::LowerThresholdDb)] = -100;
        args.params[ToInt(vitfx::compressor::Params::UpperRatio)] = ratio_01;
        args.params[ToInt(vitfx::compressor::Params::LowerRatio)] = 0;
        args.params[ToInt(vitfx::compressor::Params::OutputGainDb)] = 0;
        args.params[ToInt(vitfx::compressor::Params::Attack)] = 0.5f;
        args.params[ToInt(vitfx::compressor::Params::Release)] = 0.5f;
        args.params[ToInt(vitfx::compressor::Params::Mix)] = 1;
        args.out_min_gain_mult = &min_gain_mult;

        f32 worst = 1;
        for (auto _ : Range(64u)) {
            vitfx::compressor::Process(*compressor, args);
            worst = Min(worst, min_gain_mult);
        }
        return worst;
    };

    REQUIRE_APPROX_EQ(min_gain_mult_after_loud_signal(0.75f, 0.0f), 1.0f, 0.001f);

    auto const gentle = min_gain_mult_after_loud_signal(0.25f, 1.0f);
    auto const heavy = min_gain_mult_after_loud_signal(1.0f, 1.0f);
    REQUIRE(gentle < 1.0f);
    REQUIRE(heavy < gentle);

    return k_success;
}

TEST_REGISTRATION(RegisterCompressorTests) {
    REGISTER_TEST(TestVintageCompressorGainReductionReporting);
    REGISTER_TEST(TestModernCompressorGainReductionReporting);
}
