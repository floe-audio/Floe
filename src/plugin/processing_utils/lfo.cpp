// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lfo.hpp"

#include "tests/framework.hpp"

constexpr f32 k_sample_rate = 44100;

// Tick the LFO until its phase wraps (post-tick phase < pre-tick phase), returning the tick count.
// Returns 0 if no wrap was observed within `max_ticks`.
static u32 TickUntilCycleWrap(LFO& lfo, u32 max_ticks) {
    for (u32 i = 1; i <= max_ticks; ++i) {
        auto const prev = lfo.phase;
        lfo.Tick();
        if (lfo.phase < prev) return i;
    }
    return 0;
}

TEST_CASE(TestLFO) {
    SUBCASE("Sine waveform stays in [-1, 1] and produces non-zero output") {
        LFO lfo;
        lfo.SetWaveform(LFO::Waveform::Sine);
        lfo.SetRate(k_sample_rate, 5.0f);

        f32 min_val = 1.0f;
        f32 max_val = -1.0f;
        for (auto const _ : Range(20000)) {
            auto const v = lfo.Tick();
            REQUIRE(v >= -1.0f);
            REQUIRE(v <= 1.0f);
            min_val = Min(min_val, v);
            max_val = Max(max_val, v);
        }
        // We should see close to the full range over many cycles.
        REQUIRE(min_val < -0.95f);
        REQUIRE(max_val > 0.95f);
    }

    SUBCASE("Square waveform reaches both +1 and -1 and stays in range") {
        LFO lfo;
        lfo.SetWaveform(LFO::Waveform::Square);
        lfo.SetRate(k_sample_rate, 5.0f);

        bool seen_pos = false;
        bool seen_neg = false;
        for (auto const _ : Range(20000)) {
            auto const v = lfo.Tick();
            REQUIRE(v >= -1.0f);
            REQUIRE(v <= 1.0f);
            if (v == 1.0f) seen_pos = true;
            if (v == -1.0f) seen_neg = true;
        }
        // The table-based square has 1-sample interpolated transitions between +1 and -1, but
        // every cycle should hit both rails.
        REQUIRE(seen_pos);
        REQUIRE(seen_neg);
    }

    SUBCASE("Triangle and Sawtooth stay in range") {
        for (auto const waveform : Array {LFO::Waveform::Triangle, LFO::Waveform::Sawtooth}) {
            LFO lfo;
            lfo.SetWaveform(waveform);
            lfo.SetRate(k_sample_rate, 3.0f);
            for (auto const _ : Range(20000)) {
                auto const v = lfo.Tick();
                REQUIRE(v >= -1.0f);
                REQUIRE(v <= 1.0f);
            }
        }
    }

    SUBCASE("RandomSteps holds a value across each cycle and changes at the wrap") {
        LFO lfo;
        // Match StartVoice's bootstrap: seed random_state, set prev_random=0, draw next_random.
        lfo.random_state = 0x12345678u;
        lfo.next_random = lfo.NextRandomBipolar();
        lfo.SetWaveform(LFO::Waveform::RandomSteps);
        // Use a slow rate so each cycle spans many ticks.
        lfo.SetRate(k_sample_rate, 1.0f);

        // First cycle: prev_random is still 0, so the LFO holds 0 until the first wrap.
        auto const first_value = lfo.Tick();
        REQUIRE_EQ(first_value, 0.0f);

        // Tick through the first cycle, which should hold 0 the entire time.
        u32 ticks_to_wrap = 0;
        for (u32 i = 1; i < 100000; ++i) {
            auto const prev_phase = lfo.phase;
            auto const v = lfo.Tick();
            if (lfo.phase < prev_phase) {
                ticks_to_wrap = i;
                // At the wrap, prev_random becomes the bootstrapped next_random (non-zero).
                REQUIRE_NEQ(v, 0.0f);
                break;
            }
            REQUIRE_EQ(v, 0.0f);
        }
        REQUIRE(ticks_to_wrap > 100); // sanity: a 1 Hz LFO at 44.1 kHz wraps after ~44100 samples

        // After the wrap, the value should remain held until the next wrap.
        auto const held = lfo.Tick();
        for (u32 i = 0; i < 100; ++i)
            REQUIRE_EQ(lfo.Tick(), held);
    }

    SUBCASE("RandomGlide is continuous across cycle wraps") {
        LFO lfo;
        lfo.random_state = 0xCAFEBABEu;
        lfo.next_random = lfo.NextRandomBipolar();
        lfo.SetWaveform(LFO::Waveform::RandomGlide);
        lfo.SetRate(k_sample_rate, 2.0f);

        auto prev_value = lfo.Tick();
        f32 max_jump = 0.0f;
        for (u32 i = 1; i < 200000; ++i) {
            auto const v = lfo.Tick();
            REQUIRE(v >= -1.0f);
            REQUIRE(v <= 1.0f);
            max_jump = Max(max_jump, Fabs(v - prev_value));
            prev_value = v;
        }
        // Per-sample jumps must be tiny — the LFO eases from one random target to the next
        // via a smoothstep across the whole cycle. The smoothstep's max slope is 1.5, so the
        // largest possible per-sample step is ~1.5 * 2.0 / cycle_length. For 2 Hz at 44.1 kHz
        // that's ~3/22050 ≈ 1.4e-4. We give a generous bound.
        REQUIRE(max_jump < 0.001f);
    }

    SUBCASE("Random sequences are reproducible from the same seed") {
        auto run = [](u32 seed) {
            LFO lfo;
            lfo.random_state = seed;
            lfo.next_random = lfo.NextRandomBipolar();
            lfo.SetWaveform(LFO::Waveform::RandomSteps);
            lfo.SetRate(k_sample_rate, 50.0f); // fast so we get many cycles
            Array<f32, 10000> out;
            for (auto& v : out)
                v = lfo.Tick();
            return out;
        };

        auto const a = run(0xDEADBEEFu);
        auto const b = run(0xDEADBEEFu);
        for (auto const i : Range(a.size))
            REQUIRE_EQ(a[i], b[i]);

        auto const c = run(0x01234567u);
        bool any_diff = false;
        for (auto const i : Range(a.size))
            if (a[i] != c[i]) {
                any_diff = true;
                break;
            }
        REQUIRE(any_diff);
    }

    SUBCASE("SetRate produces approximately the expected cycle length") {
        LFO lfo;
        lfo.SetWaveform(LFO::Waveform::Sine);
        lfo.SetRate(k_sample_rate, 10.0f);
        // Expected: 44100 / 10 = 4410 samples per cycle.
        auto const ticks = TickUntilCycleWrap(lfo, 10000);
        REQUIRE(ticks > 4350);
        REQUIRE(ticks < 4470);
    }
    return k_success;
}

TEST_REGISTRATION(RegisterLfoTests) { REGISTER_TEST(TestLFO); }
