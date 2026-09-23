// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sample_processing.hpp"

#include "tests/framework.hpp"

#include "benchmarks/framework.hpp"
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
        InterpolationPoints<f32x2> const points {
            .xm1 = {0, 0},
            .x0 = {1, 1},
            .x1 = {2, 2},
            .x2 = {3, 3},
        };
        f32 const x = 0;

        auto const result = DoHermiteInterp(points, x);
        CHECK_APPROX_EQ(result[0], 1.0f, 0.0001f);
        CHECK_APPROX_EQ(result[1], 1.0f, 0.0001f);
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

static PlayHead RandomTestPlayhead(u64& seed, u32 num_frames) {
    PlayHead playhead {
        .frame_pos = RandomFloatInRange<f64>(seed, 0, num_frames - 0.001),
        .inverse_data_lookup = RandomIntInRange<u32>(seed, 0, 1) == 1,
    };
    if (RandomIntInRange<u32>(seed, 0, 2) != 0) {
        auto const start = RandomIntInRange<u32>(seed, 0, num_frames - 8);
        auto const end = RandomIntInRange<u32>(seed, start + 4, num_frames);
        auto const mode = RandomIntInRange<u32>(seed, 0, 1) == 0 ? sample_lib::LoopMode::Standard
                                                                 : sample_lib::LoopMode::PingPong;
        playhead.loop = PlayHead::Loop {
            {
                .start = start,
                .end = end,
                .crossfade =
                    ClampCrossfadeSize(RandomIntInRange<u32>(seed, 0, 16), start, end, num_frames, mode),
                .mode = mode,
            },
            RandomIntInRange<u32>(seed, 0, 1) == 1,
        };
        if (playhead.loop->only_use_frames_within_loop)
            playhead.frame_pos = RandomFloatInRange<f64>(seed, start, end - 0.001);
    }
    return playhead;
}

TEST_CASE(TestContiguousFrames) {
    Array<f32, 128> data;
    u64 seed = SourceLocationHash();
    for (auto& v : data)
        v = RandomFloatInRange<f32>(seed, -1, 1);

    AudioData const audio {
        .hash = SourceLocationHash(),
        .channels = 2,
        .sample_rate = 44100,
        .num_frames = data.size / 2,
        .interleaved_samples = data,
    };

    for (auto const _ : Range(2000)) {
        auto playhead = RandomTestPlayhead(seed, audio.num_frames);
        auto const max_increment = RandomFloatInRange<f64>(seed, 0, 6);
        auto const count =
            ContiguousFramesAvailable(playhead, max_increment, {.upper = audio.num_frames}, 512);

        for (auto const frame : Range(count)) {
            CAPTURE(frame);
            auto const frame_index = (u32)playhead.frame_pos;
            REQUIRE(frame_index >= 1 && frame_index + 2 < audio.num_frames);

            auto const expected = GetSampleFrame(audio, playhead);
            auto const fast =
                InterpolateContiguousFrame(audio, playhead.frame_pos, playhead.inverse_data_lookup);
            CHECK_EQ(fast[0], expected[0]);
            CHECK_EQ(fast[1], expected[1]);

            // Advancing must be equivalent to a plain addition: no wrap and no change to the loop state.
            auto const increment = RandomFloatInRange<f64>(seed, 0, max_increment);
            auto const before = playhead;
            IncrementPlaybackPos(playhead, increment, audio.num_frames);
            REQUIRE(playhead.frame_pos == before.frame_pos + increment);
            REQUIRE(playhead.inverse_data_lookup == before.inverse_data_lookup);
            if (before.loop) {
                REQUIRE(playhead.loop->only_use_frames_within_loop ==
                        before.loop->only_use_frames_within_loop);
                REQUIRE(playhead.loop->start == before.loop->start);
            }
        }
    }

    return k_success;
}

TEST_CASE(TestFetchSampleFrames) {
    Array<f32, 128> data;
    u64 seed = SourceLocationHash();
    for (auto& v : data)
        v = RandomFloatInRange<f32>(seed, -1, 1);

    for (auto const iteration : Range(4000)) {
        u8 const channels = (iteration % 2) ? 2 : 1;
        AudioData const audio {
            .hash = SourceLocationHash(),
            .channels = channels,
            .sample_rate = 44100,
            .num_frames = (u32)data.size / channels,
            .interleaved_samples = data,
        };
        CAPTURE(channels);

        auto playhead = RandomTestPlayhead(seed, audio.num_frames);
        auto reference_playhead = playhead;

        constexpr u32 k_num_frames = 40;
        f64 increments[k_num_frames];
        f64 max_increment = 0;
        for (auto& inc : increments) {
            inc = RandomFloatInRange<f64>(seed, 0, 4);
            max_increment = Max(max_increment, inc);
        }
        SampleFetchOptions const options {
            .increments = increments,
            .max_increment = max_increment,
            .increment_scale = RandomFloatInRange<f64>(seed, 0.5, 1.5),
            .end_frame = RandomIntInRange<u32>(seed, 1, audio.num_frames),
            .contiguous_bounds =
                {
                    .lower = RandomIntInRange<u32>(seed, 0, 10),
                    .upper = RandomIntInRange<u32>(seed, audio.num_frames - 10, audio.num_frames),
                },
        };

        f32x2 expected[k_num_frames];
        u32 expected_count = 0;
        for (auto const frame_index : Range(k_num_frames)) {
            if (PlaybackEnded(reference_playhead, options.end_frame)) break;
            expected[frame_index] = GetSampleFrame(audio, reference_playhead);
            IncrementPlaybackPos(reference_playhead,
                                 increments[frame_index] * options.increment_scale,
                                 audio.num_frames);
            ++expected_count;
        }

        f32x2 out[k_num_frames];
        u32 general_path_frames = 0;
        auto const count = FetchSampleFrames(audio, playhead, out, options, [&](u32, PlayHead const&) {
            ++general_path_frames;
        });

        REQUIRE_EQ(count, expected_count);
        CHECK(general_path_frames <= count);
        for (auto const frame_index : Range(count)) {
            CAPTURE(frame_index);
            CHECK_EQ(out[frame_index][0], expected[frame_index][0]);
            CHECK_EQ(out[frame_index][1], expected[frame_index][1]);
        }
        REQUIRE_EQ(playhead.frame_pos, reference_playhead.frame_pos);
        REQUIRE_EQ(playhead.inverse_data_lookup, reference_playhead.inverse_data_lookup);
        REQUIRE_EQ(playhead.loop.HasValue(), reference_playhead.loop.HasValue());
        if (playhead.loop) {
            REQUIRE_EQ(playhead.loop->start, reference_playhead.loop->start);
            REQUIRE_EQ(playhead.loop->end, reference_playhead.loop->end);
            REQUIRE_EQ(playhead.loop->only_use_frames_within_loop,
                       reference_playhead.loop->only_use_frames_within_loop);
        }
    }

    return k_success;
}

TEST_REGISTRATION(RegisterSamplePlayheadTests) {
    REGISTER_TEST(TestSamplePlayhead);
    REGISTER_TEST(TestContiguousFrames);
    REGISTER_TEST(TestFetchSampleFrames);
    REGISTER_TEST(TestInterpolation);
    REGISTER_TEST(TestStandardLoopSmoothness);
    REGISTER_TEST(TestPlayheadSetupCases);
}

// ======================================================================================
// Benchmarks

BENCHMARK_FN void BenchmarkGetSampleFrameMono() {
    constexpr u32 k_num_frames = 44100; // 1 second at 44.1kHz
    alignas(16) f32 data[k_num_frames];
    for (u32 i = 0; i < k_num_frames; ++i)
        data[i] = Sin(k_two_pi<f32> * (f32)i / (f32)k_num_frames);

    AudioData const audio {
        .hash = 0,
        .channels = 1,
        .sample_rate = 44100,
        .num_frames = k_num_frames,
        .interleaved_samples = {data, k_num_frames},
    };

    constexpr int k_num_iterations = 750;
    constexpr f64 k_increment = 1.0;

    for (int iter = 0; iter < k_num_iterations; ++iter) {
        PlayHead playhead {};
        ResetPlayhead(playhead, 0.0, k_nullopt, false, audio.num_frames);

        f32x2 sum = 0;
        while (!PlaybackEnded(playhead, audio.num_frames)) {
            auto frame = GetSampleFrame(audio, playhead);
            benchmarks::DoNotOptimise(frame);
            sum += frame;
            IncrementPlaybackPos(playhead, k_increment, audio.num_frames);
        }
        benchmarks::DoNotOptimise(sum);
    }
}

BENCHMARK_FN void BenchmarkGetSampleFrameStereo() {
    constexpr u32 k_num_frames = 44100;
    alignas(16) f32 data[k_num_frames * 2];
    for (u32 i = 0; i < k_num_frames; ++i) {
        data[i * 2] = Sin((k_two_pi<f32> * (f32)i) / (f32)k_num_frames);
        data[(i * 2) + 1] = Sin(((k_two_pi<f32> * (f32)i) / (f32)k_num_frames) + 0.5f);
    }

    AudioData const audio {
        .hash = 0,
        .channels = 2,
        .sample_rate = 44100,
        .num_frames = k_num_frames,
        .interleaved_samples = {data, k_num_frames * 2},
    };

    constexpr int k_num_iterations = 750;
    constexpr f64 k_increment = 1.0;

    for (int iter = 0; iter < k_num_iterations; ++iter) {
        PlayHead playhead {};
        ResetPlayhead(playhead, 0.0, k_nullopt, false, audio.num_frames);

        f32x2 sum = 0;
        while (!PlaybackEnded(playhead, audio.num_frames)) {
            auto frame = GetSampleFrame(audio, playhead);
            benchmarks::DoNotOptimise(frame);
            sum += frame;
            IncrementPlaybackPos(playhead, k_increment, audio.num_frames);
        }
        benchmarks::DoNotOptimise(sum);
    }
}

BENCHMARK_FN void BenchmarkGetSampleFrameMonoLooped() {
    constexpr u32 k_num_frames = 44100;
    alignas(16) f32 data[k_num_frames];
    for (u32 i = 0; i < k_num_frames; ++i)
        data[i] = Sin(k_two_pi<f32> * (f32)i / (f32)k_num_frames);

    AudioData const audio {
        .hash = 0,
        .channels = 1,
        .sample_rate = 44100,
        .num_frames = k_num_frames,
        .interleaved_samples = {data, k_num_frames},
    };

    BoundsCheckedLoop const loop {
        .start = 1000,
        .end = 40000,
        .crossfade = 500,
        .mode = sample_lib::LoopMode::Standard,
    };

    constexpr int k_num_iterations = 750;
    constexpr f64 k_increment = 1.0;
    constexpr u32 k_frames_per_iter = 44100;

    for (int iter = 0; iter < k_num_iterations; ++iter) {
        PlayHead playhead {};
        ResetPlayhead(playhead, 0.0, loop, false, audio.num_frames);

        f32x2 sum = 0;
        for (u32 f = 0; f < k_frames_per_iter; ++f) {
            auto frame = GetSampleFrame(audio, playhead);
            benchmarks::DoNotOptimise(frame);
            sum += frame;
            IncrementPlaybackPos(playhead, k_increment, audio.num_frames);
        }
        benchmarks::DoNotOptimise(sum);
    }
}

BENCHMARK_FN void BenchmarkGetSampleFrameFractionalIncrement() {
    constexpr u32 k_num_frames = 44100;
    alignas(16) f32 data[k_num_frames * 2];
    for (u32 i = 0; i < k_num_frames; ++i) {
        data[i * 2] = Sin((k_two_pi<f32> * (f32)i) / (f32)k_num_frames);
        data[(i * 2) + 1] = Sin(((k_two_pi<f32> * (f32)i) / (f32)k_num_frames) + 0.5f);
    }

    AudioData const audio {
        .hash = 0,
        .channels = 2,
        .sample_rate = 44100,
        .num_frames = k_num_frames,
        .interleaved_samples = {data, k_num_frames * 2},
    };

    // Simulate pitch-shifted playback (e.g. 1.5x speed).
    constexpr int k_num_iterations = 750;
    constexpr f64 k_increment = 1.5;

    for (int iter = 0; iter < k_num_iterations; ++iter) {
        PlayHead playhead {};
        ResetPlayhead(playhead, 0.0, k_nullopt, false, audio.num_frames);

        f32x2 sum = 0;
        while (!PlaybackEnded(playhead, audio.num_frames)) {
            auto frame = GetSampleFrame(audio, playhead);
            benchmarks::DoNotOptimise(frame);
            sum += frame;
            IncrementPlaybackPos(playhead, k_increment, audio.num_frames);
        }
        benchmarks::DoNotOptimise(sum);
    }
}

BENCHMARK_REGISTRATION(RegisterSampleProcessingBenchmarks) {
    REGISTER_BENCHMARK(BenchmarkGetSampleFrameMono);
    REGISTER_BENCHMARK(BenchmarkGetSampleFrameStereo);
    REGISTER_BENCHMARK(BenchmarkGetSampleFrameMonoLooped);
    REGISTER_BENCHMARK(BenchmarkGetSampleFrameFractionalIncrement);
}
