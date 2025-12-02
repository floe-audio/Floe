// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sample_processing.hpp"

#include "tests/framework.hpp"

#include "processing_utils/filters.hpp"

struct IntRange {
    int lo;
    int hi;
};

inline int Overlap(IntRange a, IntRange b) { return Max(0, Min(a.hi, b.hi) - Max(a.lo, b.lo) + 1); }

Span<u8> CreateWaveformImage(WaveformAudioSource source,
                             UiSize size,
                             Allocator& a,
                             ArenaAllocator& scratch_allocator) {
    f32x2 normalise_scale = 1.0f;
    if (source.tag == WaveformAudioSourceType::AudioData) {
        auto const& audio_data = *source.Get<AudioData const*>();
        f32 max_amp = 0;
        for (auto const& sample : audio_data.interleaved_samples)
            max_amp = Max(max_amp, Abs(sample));
        if (max_amp > 0) normalise_scale = 1.0f / max_amp;
    }

    auto const px_size = (s32)(size.width * size.height * 4);
    auto px = a.AllocateExactSizeUninitialised<u8>((usize)px_size);
    ZeroMemory(px);

    constexpr s32 k_supersample_scale = 10;
    auto const scaled_width = size.width * k_supersample_scale;
    auto const scaled_height = size.height * k_supersample_scale;

    auto const ranges = scratch_allocator.AllocateExactSizeUninitialised<IntRange>((usize)scaled_width);

    auto const mid_y = scaled_height / 2;
    s32 min_y = scaled_height - 1;
    s32 max_y = 0;

    {
        // Audio data helpers
        sv_filter::CachedHelpers filter_cache {};
        sv_filter::Data<f32x2> filter_data {};
        filter_cache.Update(44100, 2000, 0.5f);
        auto const num_frames =
            source.Is<AudioData const*>() ? source.Get<AudioData const*>()->num_frames : 0;
        auto const samples_per_pixel = (f32)num_frames / ((f32)scaled_width);
        f32 first_sample = 0;

        // Other helpers
        u64 random_seed = 1124;

        for (auto const x : Range(scaled_width)) {
            f32x2 levels {};
            switch (source.tag) {
                case WaveformAudioSourceType::AudioData: {
                    f32 const end_sample = first_sample + samples_per_pixel;
                    int const first_sample_x = RoundPositiveFloat(first_sample);
                    int const end_sample_x = Min((int)num_frames - 1, RoundPositiveFloat(end_sample));
                    first_sample = end_sample;
                    int const window_size = (end_sample_x + 1) - first_sample_x;

                    f32 const max_samples_per_px = 8;
                    int const step = Max(1, (int)((f32)window_size / max_samples_per_px));
                    int num_sampled = 0;

                    for (int i = first_sample_x; i <= end_sample_x; i += step) {
                        auto const& audio_data = *source.Get<AudioData const*>();
                        auto const frame_ptr =
                            audio_data.interleaved_samples.data + (i * audio_data.channels);
                        auto const audio = audio_data.channels == 2 ? LoadUnalignedToType<f32x2>(frame_ptr)
                                                                    : f32x2(frame_ptr[0]);
                        levels += Abs(audio);

                        num_sampled++;
                    }

                    levels /= (f32)Max(1, num_sampled);
                    levels *= normalise_scale;

                    if (x == 0) {
                        // Hard-set the history so that the filter doesn't have to ramp up and therefore
                        // zero-out any initial peak in the audio file.
                        filter_data.z1_a = levels;
                        filter_data.z2_a = levels;
                    }
                    sv_filter::Process(levels, levels, filter_data, sv_filter::Type::Lowpass, filter_cache);

                    levels = Clamp01(levels);

                    // An arbitrary skew to make the waveform a bit more prominent.
                    levels = Pow(levels, f32x2(0.6f));

                    ASSERT(levels.x >= 0 && levels.x <= 1);
                    ASSERT(levels.y >= 0 && levels.y <= 1);
                    break;
                }
                case WaveformAudioSourceType::Sine: {
                    levels = trig_table_lookup::SinTurnsPositive((f32)x / (f32)scaled_width) / 2;
                    break;
                }
                case WaveformAudioSourceType::WhiteNoise: {
                    levels = {RandomFloat01<f32>(random_seed), RandomFloat01<f32>(random_seed)};
                    levels = (0.6f + 0.4f * levels) * 0.8f; // arbitrary scaling to make it look better
                    break;
                }
            }

            auto const fval = levels * (f32)scaled_height;
            auto const val = Min(ConvertVector(fval, s32x2), s32x2(scaled_height));

            auto const start = (int)(mid_y - Abs(val.x / 2));
            // +1 because we always want the centre row of pixels to be filled
            auto end = (int)(mid_y + Abs(val.y / 2)) + 1;
            if (end >= scaled_height) end = scaled_height - 1;

            ranges[(usize)x] = IntRange {start, end};
            min_y = Min(min_y, start / k_supersample_scale);
            max_y = Max(max_y, end / k_supersample_scale);
        }
    }

    {
        min_y = Max(0, min_y - 1);
        max_y = Min(size.height - 1, max_y + 1);
        FillMemory({px.data + (min_y * size.width * 4), (usize)((max_y - min_y + 1) * size.width * 4)}, 0xff);

        int alpha_chan_px_index = (min_y * size.width * 4) + 3;
        for (int y = min_y; y <= max_y; ++y) {
            auto const ss_y = y * k_supersample_scale;
            IntRange const ss_range = {ss_y, ss_y + k_supersample_scale - 1};

            for (auto const x : Range(size.width)) {
                int num_filled_pixels = 0;
                auto const ss_x = x * k_supersample_scale;
                for (int i_x = ss_x; i_x < ss_x + k_supersample_scale; ++i_x)
                    num_filled_pixels += Overlap(ss_range, ranges[(usize)i_x]);

                auto const avg =
                    ((f32)num_filled_pixels * 255.0f) / (k_supersample_scale * k_supersample_scale);
                px[(usize)alpha_chan_px_index] = (u8)(avg + 0.5f);
                alpha_chan_px_index += 4;
            }
        }
    }

    return px;
}

TEST_CASE(TestInterpolation) {
    {
        f32 fm1[2] = {0, 0};
        f32 f0[2] = {1, 1};
        f32 f1[2] = {2, 2};
        f32 f2[2] = {3, 3};
        f32 x = 0;
        InterpolationPoints<f32 const*> points {
            .xm1 = fm1,
            .x0 = f0,
            .x1 = f1,
            .x2 = f2,
        };

        {
            auto const result = DoMonoCubicInterp(points, x);
            CHECK_APPROX_EQ(result, 1.0f, 0.0001f);
        }

        {
            auto const result = DoStereoLagrangeInterp(points, x);
            CHECK_APPROX_EQ(result.x, 1.0f, 0.0001f);
        }
    }

    return k_success;
}

TEST_CASE(TestSamplePlayhead) {
    Array<f32, 10> data;
    for (auto const i : Range(data.size))
        data[i] = (f32)i;

    AudioData const audio {
        .hash = SourceLocationHash(),
        .channels = 1,
        .sample_rate = 44100,
        .num_frames = data.size,
        .interleaved_samples = data,
    };

    PlayHead playhead {};
    ResetPlayhead(playhead, 0.0, k_nullopt, false, audio.num_frames);

    SUBCASE("basic") {
        SUBCASE("forwards") { playhead.inverse_data_lookup = false; }
        SUBCASE("reversed") { playhead.inverse_data_lookup = true; }

        auto const expected_value = [&](f32 index) {
            return playhead.inverse_data_lookup ? (f32)(data.size - 1 - index) : index;
        };

        // Whole steps.
        for (auto const i : Range(data.size)) {
            CAPTURE(i);
            CHECK(!PlaybackEnded(playhead, audio.num_frames));

            auto const frame = GetSampleFrame(audio, playhead);
            CHECK_APPROX_EQ(frame.x, expected_value((f32)i), 0.0001f);
            CHECK(frame.y == frame.x);

            IncrementPlaybackPos(playhead, 1.0, audio.num_frames);
        }

        CHECK(PlaybackEnded(playhead, audio.num_frames));

        ResetPlayhead(playhead, 0.0, k_nullopt, false, audio.num_frames);

        // Fractional steps.
        CHECK_APPROX_EQ(GetSampleFrame(audio, playhead).x, expected_value(0.0f), 0.0001f);

        // Since we're at the boundary of the data, the interpolation algorithm doesn't have all the data
        // to do a 4-point interpolation and so we need to be vague with our approximation here.
        IncrementPlaybackPos(playhead, 0.5, audio.num_frames);
        CHECK_APPROX_EQ(GetSampleFrame(audio, playhead).x, expected_value(0.5f), 0.1f);

        IncrementPlaybackPos(playhead, 0.5, audio.num_frames);
        CHECK_APPROX_EQ(GetSampleFrame(audio, playhead).x, expected_value(1), 0.0001f);
    }

    SUBCASE("whole loop") {
        BoundsCheckedLoop loop {
            .start = 0,
            .end = data.size,
            .crossfade = 0,
            .mode = sample_lib::LoopMode::Standard,
        };

        SUBCASE("standard") { loop.mode = sample_lib::LoopMode::Standard; }
        SUBCASE("ping-pong") { loop.mode = sample_lib::LoopMode::PingPong; }

        ResetPlayhead(playhead, 0.0, loop, false, audio.num_frames);

        for (auto const i : Range(data.size)) {
            CAPTURE(i);
            CHECK(!PlaybackEnded(playhead, audio.num_frames));

            auto const frame = GetSampleFrame(audio, playhead);
            CHECK_APPROX_EQ(frame.x, (f32)i, 0.0001f);
            CHECK(frame.y == frame.x);

            IncrementPlaybackPos(playhead, 1.0, audio.num_frames);
        }

        switch (playhead.loop->mode) {
            case sample_lib::LoopMode::Standard: {
                // With a whole standard loop, we're expecting wrap-around interpolation at the edges, so
                // at 9.5 we should be halfway between the last and first samples.
                ResetPlayhead(playhead, 0.0, loop, false, audio.num_frames);
                IncrementPlaybackPos(playhead, 9.5, audio.num_frames);
                CHECK_APPROX_EQ(GetSampleFrame(audio, playhead).x,
                                LinearInterpolate(0.5f, Last(data), data[0]),
                                0.0001f);

                // Same for reversed.
                ResetPlayhead(playhead, 0.0, loop, true, audio.num_frames);
                IncrementPlaybackPos(playhead, 9.5, audio.num_frames);
                CHECK_APPROX_EQ(GetSampleFrame(audio, playhead).x,
                                LinearInterpolate(0.5f, Last(data), data[0]),
                                0.0001f);
                break;
            }
            case sample_lib::LoopMode::PingPong: {
                // Ping-pong loops do not wrap around; values very near the end should not interpolate with
                // the start values.
                ResetPlayhead(playhead, 0.0, loop, false, audio.num_frames);
                IncrementPlaybackPos(playhead, 9.5, audio.num_frames);
                CHECK_APPROX_EQ(GetSampleFrame(audio, playhead).x, 9.5f, 0.1f);
                break;
            }
            case sample_lib::LoopMode::Count: PanicIfReached();
        }
    }

    SUBCASE("walk through ping-pong loop") {
        BoundsCheckedLoop const loop {
            .start = 0,
            .end = data.size,
            .crossfade = 0,
            .mode = sample_lib::LoopMode::PingPong,
        };
        ResetPlayhead(playhead, 0.0, loop, false, audio.num_frames);

        // Step through most of the loop as normal.
        for (auto const i : Range(data.size - 1)) {
            CAPTURE(i);
            CHECK(!PlaybackEnded(playhead, audio.num_frames));

            auto const frame = GetSampleFrame(audio, playhead);
            CHECK_APPROX_EQ(frame.x, (f32)i, 0.0001f);
            CHECK(frame.y == frame.x);

            IncrementPlaybackPos(playhead, 1.0, audio.num_frames);
        }

        // Check we're on the last sample.
        CHECK(!PlaybackEnded(playhead, audio.num_frames));
        CHECK_APPROX_EQ(playhead.frame_pos, (f64)(data.size - 1), 0.0001);
        CHECK(!playhead.inverse_data_lookup);
        CHECK_APPROX_EQ(GetSampleFrame(audio, playhead).x, data[data.size - 1], 0.0001f);

        // The next increment moves the playhead past the end, causing a bounce.
        IncrementPlaybackPos(playhead, 1.0, audio.num_frames);
        CHECK(!PlaybackEnded(playhead, audio.num_frames));
        CHECK(playhead.inverse_data_lookup);
        CHECK_APPROX_EQ(GetSampleFrame(audio, playhead).x, data[data.size - 1], 0.0001f);

        // Another.
        IncrementPlaybackPos(playhead, 1.0, audio.num_frames);
        CHECK(!PlaybackEnded(playhead, audio.num_frames));
        CHECK(playhead.inverse_data_lookup);
        CHECK_APPROX_EQ(GetSampleFrame(audio, playhead).x, data[data.size - 2], 0.0001f);
    }

    return k_success;
}

struct PlayheadTestOptions {
    String test_name;
    f64 start_pos;
    bool reverse;
    sample_lib::LoopMode loop_mode;

    // Expected values
    f64 expected_frame_pos;
    u32 expected_real_frame_pos;
    bool expected_only_use_frames_within_loop;
    bool expected_inverse_data_lookup;
    bool expected_requested_reverse;
};

static ErrorCodeOr<void> TestPlayheadSetup(tests::Tester& tester,
                                           PlayheadTestOptions options,
                                           BoundsCheckedLoop loop,
                                           u32 num_frames) {
    CAPTURE(options.test_name);

    loop.mode = options.loop_mode;

    PlayHead playhead {};
    ResetPlayhead(playhead, options.start_pos, loop, options.reverse, num_frames);

    CHECK_EQ(playhead.frame_pos, options.expected_frame_pos);
    CHECK_EQ(playhead.RealFramePos(num_frames), options.expected_real_frame_pos);
    REQUIRE(playhead.loop);
    CHECK(playhead.loop->only_use_frames_within_loop == options.expected_only_use_frames_within_loop);
    CHECK(playhead.inverse_data_lookup == options.expected_inverse_data_lookup);
    CHECK_EQ(playhead.requested_reverse, options.expected_requested_reverse);

    return k_success;
}

TEST_CASE(TestStandardLoopSmoothness) {
    constexpr u32 k_num_frames = 32;
    Array<f32, k_num_frames> data;

    // Create one complete period of a sine wave
    for (auto const i : Range(k_num_frames))
        data[i] = Sin(k_two_pi<f32> * (f32)i / (f32)k_num_frames);

    AudioData const audio {
        .hash = SourceLocationHash(),
        .channels = 1,
        .sample_rate = 44100,
        .num_frames = k_num_frames,
        .interleaved_samples = data,
    };

    BoundsCheckedLoop const loop {
        .start = 0,
        .end = k_num_frames,
        .crossfade = 0,
        .mode = sample_lib::LoopMode::Standard,
    };

    PlayHead playhead {};
    ResetPlayhead(playhead, 0.0, loop, false, audio.num_frames);

    constexpr f64 k_increment = 0.66;
    constexpr int k_num_iterations = 145; // ~3 complete loops

    for (auto const i : Range(k_num_iterations)) {
        CAPTURE(i);
        CAPTURE(playhead.frame_pos);

        // Calculate expected sine value at current fractional position
        // Use modulo to wrap position within [0, k_num_frames)
        auto normalized_pos = (f32)playhead.frame_pos;
        while (normalized_pos >= (f32)k_num_frames)
            normalized_pos -= (f32)k_num_frames;
        f32 const expected = Sin(k_two_pi<f32> * normalized_pos / (f32)k_num_frames);

        auto const frame = GetSampleFrame(audio, playhead);

        CHECK_APPROX_EQ(frame.x, expected, 0.001f);

        IncrementPlaybackPos(playhead, k_increment, audio.num_frames);
    }

    return k_success;
}

TEST_CASE(TestPlayheadSetupCases) {
    constexpr u32 k_num_frames = 10;

    BoundsCheckedLoop const loop {
        .start = 2,
        .end = 8,
        .crossfade = 0,
        .mode = sample_lib::LoopMode::Standard,
    };

    for (auto const mode : Array {sample_lib::LoopMode::Standard, sample_lib::LoopMode::PingPong}) {
        TRY(TestPlayheadSetup(tester,
                              {
                                  .test_name = "Forward, start before loop"_s,
                                  .start_pos = 0.0,
                                  .reverse = false,
                                  .loop_mode = mode,
                                  .expected_frame_pos = 0.0,
                                  .expected_real_frame_pos = 0u,
                                  .expected_only_use_frames_within_loop = false,
                                  .expected_inverse_data_lookup = false,
                                  .expected_requested_reverse = false,
                              },
                              loop,
                              k_num_frames));

        TRY(TestPlayheadSetup(tester,
                              {
                                  .test_name = "Forward, start inside loop"_s,
                                  .start_pos = 3.0,
                                  .reverse = false,
                                  .loop_mode = mode,
                                  .expected_frame_pos = 3.0,
                                  .expected_real_frame_pos = 3u,
                                  .expected_only_use_frames_within_loop = true,
                                  .expected_inverse_data_lookup = false,
                                  .expected_requested_reverse = false,
                              },
                              loop,
                              k_num_frames));

        TRY(TestPlayheadSetup(tester,
                              {
                                  .test_name = "Forward, start after loop"_s,
                                  .start_pos = 9.0,
                                  .reverse = false,
                                  .loop_mode = mode,
                                  .expected_frame_pos = (f64)loop.start,
                                  .expected_real_frame_pos = loop.start,
                                  .expected_only_use_frames_within_loop = true,
                                  .expected_inverse_data_lookup = false,
                                  .expected_requested_reverse = false,
                              },
                              loop,
                              k_num_frames));

        TRY(TestPlayheadSetup(tester,
                              {
                                  .test_name = "Reverse, start before loop"_s,
                                  .start_pos = 0.0,
                                  .reverse = true,
                                  .loop_mode = mode,
                                  .expected_frame_pos = 0.0,
                                  .expected_real_frame_pos = k_num_frames - 1u,
                                  .expected_only_use_frames_within_loop = false,
                                  .expected_inverse_data_lookup = true,
                                  .expected_requested_reverse = true,
                              },
                              loop,
                              k_num_frames));

        TRY(TestPlayheadSetup(tester,
                              {
                                  .test_name = "Reverse, start inside loop"_s,
                                  .start_pos = 3.0,
                                  .reverse = true,
                                  .loop_mode = mode,
                                  .expected_frame_pos = 3.0,
                                  .expected_real_frame_pos = k_num_frames - 1 - 3,
                                  .expected_only_use_frames_within_loop = true,
                                  .expected_inverse_data_lookup = true,
                                  .expected_requested_reverse = true,
                              },
                              loop,
                              k_num_frames));

        TRY(TestPlayheadSetup(tester,
                              {
                                  .test_name = "Reverse, start after loop"_s,
                                  .start_pos = 9.0,
                                  .reverse = true,
                                  .loop_mode = mode,
                                  .expected_frame_pos = (f64)loop.start,
                                  .expected_real_frame_pos = k_num_frames - 1 - loop.start,
                                  .expected_only_use_frames_within_loop = true,
                                  .expected_inverse_data_lookup = true,
                                  .expected_requested_reverse = true,
                              },
                              loop,
                              k_num_frames));
    }

    return k_success;
}

TEST_REGISTRATION(RegisterSamplePlayheadTests) {
    REGISTER_TEST(TestSamplePlayhead);
    REGISTER_TEST(TestInterpolation);
    REGISTER_TEST(TestStandardLoopSmoothness);
    REGISTER_TEST(TestPlayheadSetupCases);
}
