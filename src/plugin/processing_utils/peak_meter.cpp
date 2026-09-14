// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "peak_meter.hpp"

#include "tests/framework.hpp"

TEST_CASE(TestPeakMeterHold) {
    constexpr f32 k_sample_rate = 48000;
    constexpr f32 k_hold_seconds = StereoPeakMeter::k_hold_ms / 1000.0f;

    StereoPeakMeter meter;
    meter.PrepareToPlay(k_sample_rate);

    auto const add_silence = [&](f32 seconds) {
        Array<f32x2, 512> silence {};
        auto frames_remaining = (usize)(seconds * k_sample_rate);
        while (frames_remaining) {
            auto const num_frames = Min(frames_remaining, silence.size);
            meter.AddBuffer({silence.data, num_frames});
            frames_remaining -= num_frames;
        }
    };

    auto const hold_db = [&]() { return AmpToDb(meter.GetSnapshot().hold_levels[0]); };

    // A single full-scale spike is captured instantly.
    meter.AddBuffer(Array<f32x2, 1> {f32x2 {1.0f, 1.0f}});
    REQUIRE_APPROX_EQ(hold_db(), 0.0f, 0.01f);

    // It stays there for the hold time.
    add_silence(k_hold_seconds * 0.9f);
    REQUIRE_APPROX_EQ(hold_db(), 0.0f, 0.01f);

    // Then it decays at the expected rate.
    constexpr f32 k_decay_seconds = 0.5f;
    add_silence((k_hold_seconds * 0.1f) + k_decay_seconds);
    REQUIRE_APPROX_EQ(hold_db(), -StereoPeakMeter::k_hold_decay_db_per_second * k_decay_seconds, 0.5f);

    // A new peak recaptures the hold and restarts the wait.
    meter.AddBuffer(Array<f32x2, 1> {f32x2 {0.5f, 0.5f}});
    REQUIRE_APPROX_EQ(hold_db(), AmpToDb(0.5f), 0.01f);
    add_silence(k_hold_seconds * 0.9f);
    REQUIRE_APPROX_EQ(hold_db(), AmpToDb(0.5f), 0.01f);

    // A quieter peak doesn't.
    meter.AddBuffer(Array<f32x2, 1> {f32x2 {0.1f, 0.1f}});
    REQUIRE(hold_db() > AmpToDb(0.1f));

    meter.Zero();
    REQUIRE(meter.GetSnapshot().hold_levels[0] == 0);

    return k_success;
}

TEST_REGISTRATION(RegisterPeakMeterTests) { REGISTER_TEST(TestPeakMeterHold); }
