// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "loudness_meter.hpp"

#include <ebur128.h>

#include "foundation/container/dynamic_array.hpp"

// libebur128 recomputes momentary/short-term loudness by summing its entire 400ms/3s window from scratch on
// every query, so we only query this often rather than on every AddBuffer call; the readout is for GUI
// display, which can't redraw anywhere near audio-block rate anyway.
static constexpr f32 k_query_interval_ms = 50.0f;

LufsMeter::~LufsMeter() {
    if (m_state) {
        auto state = (ebur128_state*)m_state;
        ebur128_destroy(&state);
    }
}

void LufsMeter::InitEbur128() {
    if (m_state) {
        auto state = (ebur128_state*)m_state;
        ebur128_destroy(&state);
    }
    m_state = ebur128_init(2, (unsigned long)m_sample_rate, EBUR128_MODE_S);
    ASSERT(m_state);
}

void LufsMeter::PrepareToPlay(f32 sample_rate) {
    ASSERT_HOT(sample_rate > 0);
    if (sample_rate == m_sample_rate && m_state) return;
    m_sample_rate = sample_rate;
    m_query_interval_frames = (u32)(sample_rate * (k_query_interval_ms / 1000.0f));
    m_frames_since_last_query = 0;

    InitEbur128();

    Zero();
}

void LufsMeter::Reset() {
    ASSERT_HOT(m_state);
    m_frames_since_last_query = 0;
    InitEbur128();
    Zero();
}

void LufsMeter::AddBuffer(Span<f32x2> frames) {
    ASSERT_HOT(m_state);
    if (frames.size == 0) return;

    auto state = (ebur128_state*)m_state;
    ebur128_add_frames_float(state, (f32 const*)frames.data, frames.size);

    m_frames_since_last_query += (u32)frames.size;
    if (m_frames_since_last_query < m_query_interval_frames) return;
    m_frames_since_last_query = 0;

    double momentary_lufs = 0;
    double short_term_lufs = 0;
    ebur128_loudness_momentary(state, &momentary_lufs);
    ebur128_loudness_shortterm(state, &short_term_lufs);

    m_snapshot.Store(
        {
            .momentary_lufs = Max((f32)momentary_lufs, k_silence_floor_lufs),
            .short_term_lufs = Max((f32)short_term_lufs, k_silence_floor_lufs),
        },
        StoreMemoryOrder::Relaxed);
}

#include "tests/framework.hpp"

TEST_CASE(TestLufsMeterStartsAtSilenceFloor) {
    LufsMeter meter;
    meter.PrepareToPlay(44100.0f);
    auto const snapshot = meter.GetSnapshot();
    REQUIRE_APPROX_EQ(snapshot.momentary_lufs, LufsMeter::k_silence_floor_lufs, 0.001f);
    REQUIRE_APPROX_EQ(snapshot.short_term_lufs, LufsMeter::k_silence_floor_lufs, 0.001f);
    return k_success;
}

TEST_CASE(TestLufsMeterMeasuresFullScaleSine) {
    constexpr f32 k_sample_rate = 44100.0f;
    LufsMeter meter;
    meter.PrepareToPlay(k_sample_rate);

    // Long enough to fill both the momentary (400ms) and short-term (3s) windows.
    constexpr auto k_num_frames = (u32)(k_sample_rate * 4);
    DynamicArray<f32x2> frames {Malloc::Instance()};
    dyn::Resize(frames, k_num_frames);
    for (auto const i : Range(k_num_frames)) {
        auto const s = Sin(k_tau<f32> * 1000.0f * (f32)i / k_sample_rate);
        frames[i] = {s, s};
    }
    meter.AddBuffer(frames);

    auto const snapshot = meter.GetSnapshot();
    // A full-scale sine, identical on both (summed) channels, lands close to 0 LUFS; just check it's
    // plausibly loud rather than pin an exact figure (the weighting filter's response isn't this test's
    // concern).
    REQUIRE(snapshot.momentary_lufs > -6.0f);
    REQUIRE(snapshot.momentary_lufs < 5.0f);
    REQUIRE(snapshot.short_term_lufs > -6.0f);
    REQUIRE(snapshot.short_term_lufs < 5.0f);
    return k_success;
}

TEST_CASE(TestLufsMeterMomentaryReactsFasterThanShortTerm) {
    constexpr f32 k_sample_rate = 44100.0f;
    LufsMeter meter;
    meter.PrepareToPlay(k_sample_rate);

    auto const feed_sine = [&](u32 num_frames) {
        DynamicArray<f32x2> frames {Malloc::Instance()};
        dyn::Resize(frames, num_frames);
        for (auto const i : Range(num_frames)) {
            auto const s = Sin(k_tau<f32> * 1000.0f * (f32)i / k_sample_rate);
            frames[i] = {s, s};
        }
        meter.AddBuffer(frames);
    };

    feed_sine((u32)(k_sample_rate * 4)); // settle both windows on loud audio

    // 1s of silence: enough for the 400ms momentary window to fall to silence, not the 3s short-term one.
    DynamicArray<f32x2> silence {Malloc::Instance()};
    dyn::Resize(silence, (u32)(k_sample_rate * 1));
    for (auto& f : silence)
        f = {};
    meter.AddBuffer(silence);

    auto const snapshot = meter.GetSnapshot();
    REQUIRE(snapshot.momentary_lufs <= LufsMeter::k_silence_floor_lufs + 0.5f);
    REQUIRE(snapshot.short_term_lufs > LufsMeter::k_silence_floor_lufs + 3.0f);
    return k_success;
}

TEST_REGISTRATION(RegisterLoudnessMeterTests) {
    REGISTER_TEST(TestLufsMeterStartsAtSilenceFloor);
    REGISTER_TEST(TestLufsMeterMeasuresFullScaleSine);
    REGISTER_TEST(TestLufsMeterMomentaryReactsFasterThanShortTerm);
}
