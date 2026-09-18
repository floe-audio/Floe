// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "effect_bitcrush.hpp"

#include "tests/framework.hpp"

#include "common_infrastructure/state/legacy_param_logic.hpp"

// Feeds a ramp (each sample equals its index) through the crusher and counts how many distinct held values
// appear, which is the number of times the crusher resampled.
static u32 NumResamples(f32 rate_hz, f32 host_sample_rate, u32 num_frames) {
    BitCrushProcessor crusher {};
    crusher.SetSampleRate(rate_hz, host_sample_rate);
    u32 num_resamples = 0;
    f32 previous_held = -1.0f;
    for (auto frame_index : Range(num_frames)) {
        auto const held = crusher.BitCrush(f32x2 {(f32)frame_index, (f32)frame_index})[0];
        if (held != previous_held) ++num_resamples;
        previous_held = held;
    }
    return num_resamples;
}

TEST_CASE(TestBitCrushSampleRateReduction) {
    // Any rate is reachable, not just integer divisions of the host rate.
    REQUIRE_APPROX_EQ((f32)NumResamples(30000.0f, 44100.0f, 44100), 30000.0f, 2.0f);
    REQUIRE_APPROX_EQ((f32)NumResamples(1000.0f, 44100.0f, 44100), 1000.0f, 2.0f);
    REQUIRE_APPROX_EQ((f32)NumResamples(100.0f, 44100.0f, 44100), 100.0f, 2.0f);
    REQUIRE_APPROX_EQ((f32)NumResamples(1.0f, 44100.0f, 44100), 1.0f, 1.0f);

    // Same result regardless of the host sample rate.
    REQUIRE_APPROX_EQ((f32)NumResamples(1000.0f, 96000.0f, 96000), 1000.0f, 2.0f);
    REQUIRE_APPROX_EQ((f32)NumResamples(1000.0f, 192000.0f, 192000), 1000.0f, 2.0f);

    // At or above the host rate the signal passes through unchanged.
    REQUIRE_EQ(NumResamples(44100.0f, 44100.0f, 1000), 1000u);
    REQUIRE_EQ(NumResamples(100000.0f, 44100.0f, 1000), 1000u);

    // Holds are evenly spaced: every hold is either floor or ceil of the ideal length.
    {
        BitCrushProcessor crusher {};
        crusher.SetSampleRate(1000.0f, 44100.0f);
        u32 hold_length = 0;
        f32 previous_held = -1.0f;
        for (auto frame_index : Range(44100u)) {
            auto const held = crusher.BitCrush(f32x2 {(f32)frame_index, (f32)frame_index})[0];
            if (held != previous_held && frame_index != 0) {
                REQUIRE(hold_length == 44 || hold_length == 45);
                hold_length = 0;
            }
            ++hold_length;
            previous_held = held;
        }
    }

    // The first sample after a reset is captured immediately rather than holding silence.
    {
        BitCrushProcessor crusher {};
        crusher.SetSampleRate(100.0f, 44100.0f);
        crusher.Reset();
        REQUIRE_EQ(crusher.BitCrush(f32x2 {0.5f, 0.5f})[0], 0.5f);
    }

    return k_success;
}

// The legacy parameter must reproduce the original integer-hold timing exactly, quirks and all, so DAW
// automation of it sounds identical.
TEST_CASE(TestBitCrushLegacySampleRateTiming) {
    // Runs a ramp through the legacy timing and calls expected_hold_length(hold_index) for each completed
    // hold, checking the hold length matches.
    auto const check_hold_lengths =
        [&](f32 rate_hz, f32 host_sample_rate, u32 num_frames, auto expected_hold_length) {
            BitCrushProcessor crusher {};
            crusher.SetLegacySampleRate(rate_hz, host_sample_rate);
            u32 hold_index = 0;
            u32 hold_length = 0;
            f32 previous_held = -1.0f;
            for (auto frame_index : Range(num_frames)) {
                auto const held = crusher.BitCrush(f32x2 {(f32)frame_index, (f32)frame_index})[0];
                if (held != previous_held && frame_index != 0) {
                    REQUIRE_EQ(hold_length, expected_hold_length(hold_index));
                    ++hold_index;
                    hold_length = 0;
                }
                ++hold_length;
                previous_held = held;
            }
            REQUIRE(hold_index > 0);
        };

    // 44100 / 1000 truncates to a 44-sample hold, and the counter wrapping at 1000 leaves a 32-sample hold
    // once per 1000 samples: 22 holds of 44 then one of 32.
    check_hold_lengths(1000.0f, 44100.0f, 3000, [](u32 hold_index) {
        return (hold_index % 23 == 22) ? 32u : 44u;
    });

    // Below the square root of the host rate the wrap happens before the hold ends, so 100 Hz holds for 100
    // samples rather than 441.
    check_hold_lengths(100.0f, 44100.0f, 1000, [](u32) { return 100u; });

    // At the host rate every sample passes through.
    check_hold_lengths(44100.0f, 44100.0f, 100, [](u32) { return 1u; });

    // Switching back to the modern timing takes effect.
    {
        BitCrushProcessor crusher {};
        crusher.SetLegacySampleRate(100.0f, 44100.0f);
        crusher.SetSampleRate(1000.0f, 44100.0f);
        u32 num_resamples = 0;
        f32 previous_held = -1.0f;
        for (auto frame_index : Range(44100u)) {
            auto const held = crusher.BitCrush(f32x2 {(f32)frame_index, (f32)frame_index})[0];
            if (held != previous_held) ++num_resamples;
            previous_held = held;
        }
        REQUIRE_APPROX_EQ((f32)num_resamples, 1000.0f, 2.0f);
    }

    return k_success;
}

TEST_CASE(TestBitCrushBitDepth) {
    BitCrushProcessor crusher {};
    crusher.SetSampleRate(44100.0f, 44100.0f);

    crusher.SetBitDepth(32.0f);
    REQUIRE_EQ(crusher.BitCrush(f32x2 {0.123456f, -0.654321f})[0], 0.123456f);

    // 2 bits: steps of a half, zero is a level.
    crusher.SetBitDepth(2.0f);
    REQUIRE_EQ(crusher.BitCrush(f32x2 {0.0f, 0.0f})[0], 0.0f);
    REQUIRE_EQ(crusher.BitCrush(f32x2 {0.2f, 0.2f})[0], 0.0f);
    REQUIRE_EQ(crusher.BitCrush(f32x2 {0.3f, 0.3f})[0], 0.5f);
    REQUIRE_EQ(crusher.BitCrush(f32x2 {-0.3f, -0.3f})[0], -0.5f);
    REQUIRE_EQ(crusher.BitCrush(f32x2 {0.9f, 0.9f})[0], 1.0f);

    // 1 bit: -1, 0, 1.
    crusher.SetBitDepth(1.0f);
    REQUIRE_EQ(crusher.BitCrush(f32x2 {0.3f, 0.3f})[0], 0.0f);
    REQUIRE_EQ(crusher.BitCrush(f32x2 {0.6f, 0.6f})[0], 1.0f);

    // Fractional bit depths keep silence at exactly zero.
    crusher.SetBitDepth(1.5f);
    REQUIRE_EQ(crusher.BitCrush(f32x2 {0.0f, 0.0f})[0], 0.0f);
    crusher.SetBitDepth(3.3f);
    REQUIRE_EQ(crusher.BitCrush(f32x2 {0.0f, 0.0f})[0], 0.0f);

    // The legacy quantiser has one more step per unit: 2 legacy bits gives thirds.
    crusher.SetLegacyBitDepth(2.0f);
    REQUIRE_EQ(crusher.BitCrush(f32x2 {0.0f, 0.0f})[0], 0.0f);
    REQUIRE_APPROX_EQ(crusher.BitCrush(f32x2 {0.3f, 0.3f})[0], 1.0f / 3.0f, 0.0001f);
    REQUIRE_APPROX_EQ(crusher.BitCrush(f32x2 {-0.3f, -0.3f})[0], -1.0f / 3.0f, 0.0001f);
    crusher.SetLegacyBitDepth(32.0f);
    REQUIRE_EQ(crusher.BitCrush(f32x2 {0.123456f, 0.123456f})[0], 0.123456f);

    return k_success;
}

// Modernising a legacy Bits value must land on the modern bit depth that produces the same levels.
TEST_CASE(TestBitCrushLegacyBitsRemap) {
    auto const& legacy_desc = k_param_descriptors[ToInt(ParamIndex::LegacyBitCrushBits)];
    auto const& modern_desc = k_param_descriptors[ToInt(ParamIndex::BitCrushBits)];

    auto const modern_bits_for = [&](s32 legacy_bits) {
        auto const legacy_linear = *legacy_desc.LineariseValue((f32)legacy_bits, false);
        auto const successor = SuccessorOfLegacyValue(ParamIndex::LegacyBitCrushBits, legacy_linear);
        REQUIRE(successor.HasValue());
        REQUIRE(successor->successor_param == ParamIndex::BitCrushBits);
        return modern_desc.ProjectValue(successor->successor_linear);
    };

    // Both quantisers must have the same number of steps per unit.
    for (auto const legacy_bits : Range(2, 32)) {
        auto const legacy_steps = Pow(2.0f, (f32)legacy_bits) - 1.0f;
        auto const modern_steps = Pow(2.0f, modern_bits_for(legacy_bits) - 1.0f);
        REQUIRE_APPROX_EQ(modern_steps / legacy_steps, 1.0f, 0.00001f);
    }

    // Legacy 32 is a bypass, and so must be its modern equivalent.
    REQUIRE_EQ(modern_bits_for(32), 32.0f);

    // Inputs at the centre of every legacy level land on the same level through both quantisers. Level
    // centres are used because a remap that is only float-accurate can legitimately pick the neighbouring
    // level for an input sat exactly on a boundary.
    for (auto const legacy_bits : Range(2, 13)) {
        BitCrushProcessor legacy {};
        legacy.SetSampleRate(44100.0f, 44100.0f);
        legacy.SetLegacyBitDepth((f32)legacy_bits);
        BitCrushProcessor modern {};
        modern.SetSampleRate(44100.0f, 44100.0f);
        modern.SetBitDepth(modern_bits_for(legacy_bits));

        auto const num_steps = (s32)(Pow(2.0f, (f32)legacy_bits) - 1.0f);
        for (auto const level : Range(-num_steps, num_steps + 1)) {
            auto const input = (f32)level / (f32)num_steps;
            auto const from_legacy = legacy.BitCrush(f32x2 {input, input})[0];
            auto const from_modern = modern.BitCrush(f32x2 {input, input})[0];
            REQUIRE_APPROX_EQ(from_modern, from_legacy, 0.00001f);
        }
    }

    return k_success;
}

TEST_REGISTRATION(RegisterBitCrushTests) {
    REGISTER_TEST(TestBitCrushSampleRateReduction);
    REGISTER_TEST(TestBitCrushLegacySampleRateTiming);
    REGISTER_TEST(TestBitCrushBitDepth);
    REGISTER_TEST(TestBitCrushLegacyBitsRemap);
}
