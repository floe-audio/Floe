// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "true_peak_detector.hpp"

#include "tests/framework.hpp"

TEST_CASE(TestTruePeakDetector) {
    SUBCASE("constant signal is reproduced without added gain") {
        TruePeakDetector detector;
        f32x2 last_estimate {};
        for (auto _ : Range(20))
            last_estimate = detector.EstimatePeak({0.5f, -0.5f});

        CHECK_APPROX_EQ(last_estimate.x, 0.5f, 0.0001f);
        CHECK_APPROX_EQ(last_estimate.y, 0.5f, 0.0001f);
    }

    SUBCASE("silence stays silent") {
        TruePeakDetector detector;
        for (auto _ : Range(20)) {
            auto const estimate = detector.EstimatePeak({0, 0});
            REQUIRE(estimate.x == 0);
            REQUIRE(estimate.y == 0);
        }
    }

    SUBCASE("finds an inter-sample peak that raw sample-peak reading misses") {
        TruePeakDetector detector;

        // A sine whose phase is chosen so its analytic peak falls exactly halfway between sample index
        // 0 and 1 -- the worst case for a raw sample-peak reader.
        constexpr f32 k_sample_rate = 44100.0f;
        constexpr f32 k_freq = 5000.0f;
        constexpr f32 k_phase_offset = k_half_pi<f32> - (k_pi<f32> * k_freq / k_sample_rate);

        auto const sample = [&](int i) {
            auto const t = (f32)i / k_sample_rate;
            return Sin((k_tau<f32> * k_freq * t) + k_phase_offset);
        };

        constexpr auto k_centre = (int)TruePeakDetector::k_centre_tap;
        f32x2 estimate {};
        for (auto i = -k_centre - 1; i <= k_centre + 1; ++i)
            estimate = detector.EstimatePeak({sample(i), sample(i)});

        auto const raw_sample_peak = Max(Abs(sample(0)), Abs(sample(1)));

        // The continuous sine's true peak is 1.0; the true-peak estimate should get closer to it than
        // the raw discrete sample-peak does.
        REQUIRE(estimate.x > raw_sample_peak);
        REQUIRE(estimate.x <= 1.05f); // shouldn't overshoot the analytic peak by much
    }

    return k_success;
}

TEST_REGISTRATION(RegisterTruePeakDetectorTests) { REGISTER_TEST(TestTruePeakDetector); }
