// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "limiter.hpp"

#include "tests/framework.hpp"

TEST_CASE(TestSlidingWindowMin) {
    auto const brute_force_min = [](Span<f32 const> values, usize i, u32 window_size) {
        f32 result = values[i];
        auto const start = (i >= window_size) ? (i - window_size) : 0uz;
        for (auto j = start; j <= i; ++j)
            result = Min(result, values[j]);
        return result;
    };

    auto const test_sequence = [&](Span<f32 const> values, u32 window_size) {
        SlidingWindowMin w;
        w.PrepareToPlay(window_size);
        for (auto const i : Range(values.size)) {
            auto const result = w.Push(values[i]);
            auto const expected = brute_force_min(values, i, window_size);
            REQUIRE_APPROX_EQ(result, expected, 0.0001f);
        }
    };

    SUBCASE("constant") {
        Array<f32, 10> values {};
        for (auto& v : values)
            v = 3.0f;
        test_sequence(values, 4);
    }

    SUBCASE("single spike") { test_sequence(Array<f32, 10> {1, 1, 1, 1, 0, 1, 1, 1, 1, 1}, 3); }

    SUBCASE("monotonic ramp up") {
        Array<f32, 10> values {};
        for (auto const i : Range(10u))
            values[i] = (f32)i;
        test_sequence(values, 3);
    }

    SUBCASE("monotonic ramp down") {
        Array<f32, 10> values {};
        for (auto const i : Range(10u))
            values[i] = (f32)(10 - i);
        test_sequence(values, 3);
    }

    SUBCASE("window size 1") { test_sequence(Array<f32, 8> {5, 3, 8, 1, 9, 2, 7, 4}, 1); }

    SUBCASE("varied values, wider window") {
        test_sequence(Array<f32, 20> {5, 3, 8, 1, 9, 2, 7, 4, 6, 0, 10, 3, 2, 8, 5, 1, 9, 4, 7, 6}, 5);
    }

    SUBCASE("random values, window longer than the sequence and shorter") {
        u64 seed = 12345;
        Array<f32, 500> values {};
        for (auto& v : values)
            v = RandomFloat01<f32>(seed);
        for (auto const window_size : Array {1u, 7u, 24u, 48u, 600u})
            test_sequence(values, window_size);
    }

    return k_success;
}

static void
SetUpLimiter(LimiterDsp& limiter, f32 sample_rate, f32 ceiling_db, f32 gain_db = 0.0f, f32 mix_01 = 1.0f) {
    limiter.PrepareToPlay(sample_rate);
    limiter.OnParamChange({
        .gain_db = gain_db,
        .ceiling_db = ceiling_db,
        .mix_01 = mix_01,
    });
}

// Runs silence through for long enough that the param smoothers settle at their targets.
static void SettleParams(LimiterDsp& limiter) {
    for (auto _ : Range(2000u))
        limiter.Process({0, 0});
}

TEST_CASE(TestLimiterNeverExceedsCeiling) {
    constexpr f32 k_ceiling_db = -3.0f;
    auto const k_ceiling_lin = DbToAmp(k_ceiling_db);
    // Purely for float rounding; the algorithm itself is designed to never need the hard clamp.
    constexpr f32 k_tolerance = 0.0005f;

    auto const require_within_ceiling = [&](f32x2 out) {
        REQUIRE(Abs(out.x) <= k_ceiling_lin + k_tolerance);
        REQUIRE(Abs(out.y) <= k_ceiling_lin + k_tolerance);
    };

    for (auto const sample_rate : Array {44100.0f, 96000.0f}) {
        LimiterDsp limiter;
        SetUpLimiter(limiter, sample_rate, k_ceiling_db);

        // Hard step: silence -> full-scale square wave.
        for (auto _ : Range(200))
            require_within_ceiling(limiter.Process({0, 0}));
        for (auto _ : Range(2000))
            require_within_ceiling(limiter.Process({1.0f, -1.0f}));

        // Single-sample impulses at various positions.
        limiter.Reset();
        for (auto const impulse_pos : Array {0u, 17u, 300u}) {
            for (auto const i : Range(500u)) {
                auto const in = (i == impulse_pos) ? f32x2 {5.0f, -5.0f} : f32x2 {0, 0};
                require_within_ceiling(limiter.Process(in));
            }
        }

        // Full-scale sine near Nyquist, driven hard: its inter-sample peaks exceed its sample
        // peaks, so this exercises the true-peak path.
        limiter.Reset();
        limiter.OnParamChange({.gain_db = 6.0f});
        for (auto const i : Range(4000u)) {
            auto const s = Sin(k_tau<f32> * (sample_rate * 0.23f) * (f32)i / sample_rate);
            require_within_ceiling(limiter.Process({s, s}));
        }
    }

    return k_success;
}

// The gain envelope must already be at (or below) the required reduction when a peak reaches the
// delayed output, having ramped down over the lookahead window beforehand. This checks the envelope
// directly, since the final hard clamp would otherwise hide a misaligned delay line.
TEST_CASE(TestLimiterPreEmptiveAttack) {
    constexpr f32 k_ceiling_db = -3.0f;
    auto const k_ceiling_lin = DbToAmp(k_ceiling_db);
    constexpr f32 k_step_level = 5.0f;

    for (auto const sample_rate : Array {44100.0f, 96000.0f}) {
        LimiterDsp limiter;
        SetUpLimiter(limiter, sample_rate, k_ceiling_db);
        SettleParams(limiter);

        auto const latency = limiter.LatencySamples();
        auto const lookahead = latency - TruePeakDetector::k_group_delay_samples;
        constexpr u32 k_step_pos = 400;

        f32 prev_gain = 1.0f;
        for (auto const i : Range(k_step_pos + latency + 200)) {
            auto const in = (i >= k_step_pos) ? f32x2 {k_step_level, k_step_level} : f32x2 {0, 0};
            auto const out = limiter.Process(in);
            auto const gain = limiter.CurrentGain();

            if (i < k_step_pos) {
                REQUIRE_APPROX_EQ(gain, 1.0f, 0.0001f); // nothing to react to yet
            } else if (i < k_step_pos + latency) {
                // The ramp down happens during the lookahead window before the step arrives.
                REQUIRE(gain <= prev_gain + 0.0001f);
                if (i == k_step_pos + latency - lookahead) REQUIRE(gain < 1.0f);
            } else {
                // The step is now at the output: the envelope alone must hold it within the ceiling.
                // The hard edge has a genuine inter-sample overshoot, so the output sits a little
                // below the ceiling while the release recovers from that, but it's still limiting
                // rather than crushing the signal.
                REQUIRE(k_step_level * gain <= k_ceiling_lin * 1.001f);
                REQUIRE(Abs(out.x) >= k_ceiling_lin * 0.85f);
            }
            prev_gain = gain;
        }
    }

    return k_success;
}

TEST_CASE(TestLimiterQuietSignalPassesThroughDelayed) {
    constexpr f32 k_sample_rate = 44100.0f;
    LimiterDsp limiter;
    SetUpLimiter(limiter, k_sample_rate, -1.0f);
    SettleParams(limiter);

    auto const latency = limiter.LatencySamples();
    REQUIRE(latency >= 20 && latency <= 30); // ~0.5ms plus the true-peak group delay

    // A quiet sine, always well below the ceiling: the output should be the input delayed by exactly
    // the latency, at unity gain.
    constexpr f32 k_amplitude = 0.1f;
    auto const sample = [&](u32 i) {
        return k_amplitude * Sin(k_tau<f32> * 440.0f * (f32)i / k_sample_rate);
    };
    for (auto const i : Range(2000u)) {
        auto const s = sample(i);
        auto const out = limiter.Process({s, s});
        if (i >= latency) {
            REQUIRE_APPROX_EQ(out.x, sample(i - latency), 0.0001f);
            REQUIRE_APPROX_EQ(out.y, sample(i - latency), 0.0001f);
        } else {
            REQUIRE(out.x == 0);
        }
    }

    return k_success;
}

TEST_CASE(TestLimiterReleaseRecovers) {
    constexpr f32 k_sample_rate = 44100.0f;
    constexpr f32 k_ceiling_db = -3.0f;
    auto const k_ceiling_lin = DbToAmp(k_ceiling_db);

    LimiterDsp limiter;
    SetUpLimiter(limiter, k_sample_rate, k_ceiling_db);
    SettleParams(limiter);

    for (auto _ : Range(500u))
        limiter.Process({1.0f, 1.0f});
    auto const settled = limiter.Process({1.0f, 1.0f});
    REQUIRE(Abs(settled.x) <= k_ceiling_lin + 0.001f);
    REQUIRE(Abs(settled.x) >= k_ceiling_lin * 0.9f);
    REQUIRE(limiter.CurrentGain() < 0.8f);

    // Drop to a quiet probe: the gain should recover monotonically back towards unity over a few
    // release time-constants, rather than instantly or with glitches.
    constexpr f32 k_probe_amplitude = 0.01f;
    Array<f32, 6> gains {};
    auto const release_samples = (u32)(k_limiter_release_ms * 0.001f * k_sample_rate);
    for (auto const step : Range(gains.size)) {
        for (auto _ : Range(release_samples))
            limiter.Process({k_probe_amplitude, k_probe_amplitude});
        gains[step] = limiter.CurrentGain();
    }
    for (auto const i : Range(gains.size - 1))
        REQUIRE(gains[i] <= gains[i + 1] + 0.0001f);
    REQUIRE(gains[0] < 0.95f);
    REQUIRE(gains[gains.size - 1] > 0.99f);

    return k_success;
}

TEST_CASE(TestLimiterMixBlendsAgainstDelayedDry) {
    constexpr f32 k_sample_rate = 44100.0f;
    constexpr f32 k_ceiling_db = -3.0f;
    auto const k_ceiling_lin = DbToAmp(k_ceiling_db);
    constexpr f32 k_loud = 2.0f;

    SUBCASE("mix 0 is the dry signal, time-aligned with the wet path") {
        LimiterDsp limiter;
        SetUpLimiter(limiter, k_sample_rate, k_ceiling_db, 0.0f, 0.0f);
        SettleParams(limiter);
        auto const latency = limiter.LatencySamples();
        for (auto const i : Range(300u)) {
            auto const in = (i >= 100) ? f32x2 {k_loud, k_loud} : f32x2 {0, 0};
            auto const out = limiter.Process(in);
            if (i >= 100 + latency)
                REQUIRE_APPROX_EQ(out.x, k_loud, 0.0001f);
            else
                REQUIRE(out.x == 0);
        }
    }

    SUBCASE("mix 0.5 is halfway between limited and dry") {
        LimiterDsp limiter;
        SetUpLimiter(limiter, k_sample_rate, k_ceiling_db, 0.0f, 0.5f);
        SettleParams(limiter);
        // Long enough for the release to fully recover from the initial edge's overshoot.
        for (auto _ : Range(30000u))
            limiter.Process({k_loud, k_loud});
        auto const out = limiter.Process({k_loud, k_loud});
        REQUIRE_APPROX_EQ(out.x, (k_loud + k_ceiling_lin) * 0.5f, 0.001f);
    }

    return k_success;
}

TEST_CASE(TestLimiterGainReductionReporting) {
    constexpr f32 k_sample_rate = 44100.0f;
    constexpr f32 k_ceiling_db = -3.0f;
    constexpr u32 k_block_size = 256;

    LimiterDsp limiter;
    SetUpLimiter(limiter, k_sample_rate, k_ceiling_db);

    REQUIRE_APPROX_EQ(limiter.GainReductionDb(), 0.0f, 0.001f);

    // Drive it hard enough to trigger meaningful gain reduction, then publish stats as would happen
    // once per processed block in real use.
    for (auto _ : Range(k_block_size * 4))
        limiter.Process({1.0f, 1.0f});
    limiter.PublishGuiStats(k_block_size);
    auto const driven_db = limiter.GainReductionDb();
    REQUIRE(driven_db > 0.5f);

    // The readout falls back steadily once the signal stops, rather than snapping to zero.
    auto const process_silent_blocks = [&](u32 num_blocks) {
        for (auto _ : Range(num_blocks)) {
            for (auto _ : Range(k_block_size))
                limiter.Process({0, 0});
            limiter.PublishGuiStats(k_block_size);
        }
    };
    process_silent_blocks((u32)(k_sample_rate * 0.1f) / k_block_size);
    auto const shortly_after_db = limiter.GainReductionDb();
    REQUIRE(shortly_after_db < driven_db);
    REQUIRE(shortly_after_db > 0.0f);
    process_silent_blocks(200);
    REQUIRE_APPROX_EQ(limiter.GainReductionDb(), 0.0f, 0.001f);

    // Reset should also clear the published stat.
    for (auto _ : Range(k_block_size))
        limiter.Process({1.0f, 1.0f});
    limiter.PublishGuiStats(k_block_size);
    REQUIRE(limiter.GainReductionDb() > 0.5f);
    limiter.Reset();
    REQUIRE_APPROX_EQ(limiter.GainReductionDb(), 0.0f, 0.001f);

    return k_success;
}

TEST_CASE(TestLimiterResetClearsDelayLine) {
    constexpr f32 k_sample_rate = 44100.0f;
    LimiterDsp limiter;
    SetUpLimiter(limiter, k_sample_rate, -3.0f);
    SettleParams(limiter);

    for (auto _ : Range(100u))
        limiter.Process({0.5f, 0.5f});
    REQUIRE(limiter.HasPendingAudio());

    limiter.Reset();
    REQUIRE(!limiter.HasPendingAudio());
    for (auto const i : Range(limiter.LatencySamples())) {
        auto const out = limiter.Process({0, 0});
        REQUIRE(out.x == 0);
        REQUIRE(out.y == 0);
        (void)i;
    }

    // Silence flushes the delay line so the effect can report it has nothing pending.
    for (auto _ : Range(100u))
        limiter.Process({0.5f, 0.5f});
    for (auto _ : Range(limiter.LatencySamples()))
        limiter.Process({0, 0});
    REQUIRE(!limiter.HasPendingAudio());

    return k_success;
}

// Under sustained heavy reduction the envelope is already well below unity when the next crest
// arrives. The attack ramp must still spread that drop over the lookahead window rather than
// collapsing into a one-sample step, which is audible as crackle on drones with sharp crests.
TEST_CASE(TestLimiterAttackRampsUnderSustainedReduction) {
    constexpr f32 k_sample_rate = 48000.0f;
    LimiterDsp limiter;
    SetUpLimiter(limiter, k_sample_rate, -1.0f, 20.0f);
    SettleParams(limiter);

    // In-phase harmonics: one sharp pulse per cycle with quiet gaps between, so the release recovers
    // between crests and each crest has to pull the envelope back down.
    auto const sample = [&](u32 i) {
        auto const t = (f32)i / k_sample_rate;
        f32 s = 0;
        for (auto const harmonic : Range(1u, 25u))
            s += Cos(k_tau<f32> * 55.0f * (f32)harmonic * t) * (1.0f - (f32)harmonic / 25.0f);
        return s * 0.08f;
    };

    // With the release stage after the moving average, this signal produced ~1.4dB single-sample
    // drops; with it before, ~0.5dB.
    f32 prev_gain = 1.0f;
    f32 max_single_sample_drop_db = 0;
    f32 min_gain = 1.0f;
    for (auto const i : Range(96000u)) {
        auto const s = sample(i);
        limiter.Process({s, s});
        auto const gain = limiter.CurrentGain();
        if (i > 48000) {
            max_single_sample_drop_db = Max(max_single_sample_drop_db, AmpToDb(prev_gain / gain));
            min_gain = Min(min_gain, gain);
        }
        prev_gain = gain;
    }
    REQUIRE(min_gain < DbToAmp(-18.0f)); // sanity: it's actually limiting hard
    REQUIRE(max_single_sample_drop_db < 0.75f);

    return k_success;
}

TEST_REGISTRATION(RegisterLimiterTests) {
    REGISTER_TEST(TestSlidingWindowMin);
    REGISTER_TEST(TestLimiterNeverExceedsCeiling);
    REGISTER_TEST(TestLimiterPreEmptiveAttack);
    REGISTER_TEST(TestLimiterAttackRampsUnderSustainedReduction);
    REGISTER_TEST(TestLimiterQuietSignalPassesThroughDelayed);
    REGISTER_TEST(TestLimiterReleaseRecovers);
    REGISTER_TEST(TestLimiterMixBlendsAgainstDelayedDry);
    REGISTER_TEST(TestLimiterGainReductionReporting);
    REGISTER_TEST(TestLimiterResetClearsDelayLine);
}
