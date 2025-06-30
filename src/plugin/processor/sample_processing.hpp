// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "foundation/utils/random.hpp"

#include "common_infrastructure/audio_data.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

#include "processing_utils/filters.hpp"

inline f32 DoMonoCubicInterp(f32 const* f0, f32 const* f1, f32 const* f2, f32 const* fm1, f32 const x) {
    return f0[0] + ((((f2[0] - fm1[0] - 3 * f1[0] + 3 * f0[0]) * x + 3 * (f1[0] + fm1[0] - 2 * f0[0])) * x -
                     (f2[0] + 2 * fm1[0] - 6 * f1[0] + 3 * f0[0])) *
                    x / 6.0f);
}

inline f32x2
DoStereoLagrangeInterp(f32 const* f0, f32 const* f1, f32 const* f2, f32 const* fm1, f32 const x) {
    auto xf =
        x + 1; // x is given in the range 0 to 1 but we want the value between f0 and f1, therefore add 1
    auto xfm1 = x;
    auto xfm2 = xf - 2;
    auto xfm3 = xf - 3;

    // IMPROVE: use shuffle instructions instead of manually setting up the vectors

    f32x4 const v0 {xfm1, xf, xf, xf};
    f32x4 const v1 {-1, 1, 2, 3};
    f32x4 const v2 {xfm2, xfm2, xfm1, xfm1};
    f32x4 const v3 {-2, -1, 1, 2};
    f32x4 const v4 {xfm3, xfm3, xfm3, xfm2};
    f32x4 const v5 {-3, -2, -1, 1};

    f32x4 const vd0 = v0 / v1;
    f32x4 const vd1 = v2 / v3;
    f32x4 const vd2 = v4 / v5;

    auto vt = (vd0 * vd1) * vd2;

    alignas(16) f32 t[4];
    StoreToAligned(t, vt);

    return {
        (fm1[0] * t[0]) + (f0[0] * t[1]) + (f1[0] * t[2]) + (f2[0] * t[3]),
        (fm1[1] * t[0]) + (f0[1] * t[1]) + (f1[1] * t[2]) + (f2[1] * t[3]),
    };
}

struct BoundsCheckedLoop {
    u32 start {};
    u32 end {};
    u32 crossfade {};
    sample_lib::LoopMode mode {};
};

template <typename Type>
inline Type ClampCrossfadeSize(Type crossfade, Type start, Type end, Type total, sample_lib::LoopMode mode) {
    ASSERT(crossfade >= 0);
    ASSERT(start >= 0);
    ASSERT(end >= 0);
    auto loop_size = end - start;
    ASSERT(loop_size >= 0);
    Type result;
    switch (mode) {
        case sample_lib::LoopMode::Standard: result = Min(crossfade, loop_size, start); break;
        case sample_lib::LoopMode::PingPong:
            result = Max<Type>(0, Min(crossfade, start, total - end, loop_size));
            break;
        case sample_lib::LoopMode::Count: PanicIfReached(); break;
    }
    return result;
}

inline BoundsCheckedLoop CreateBoundsCheckedLoop(sample_lib::BuiltinLoop loop, usize utotal_frame_count) {
    // This is a bit weird? But I think it's probably important to some already-existing patches.
    u32 const smallest_loop_size_allowed = Max((u32)((f64)utotal_frame_count * 0.001), 32u);

    auto const total_frame_count = CheckedCast<s64>(utotal_frame_count);
    BoundsCheckedLoop result {
        .mode = loop.mode,
    };

    result.start = ({
        s64 s;
        if (loop.start_frame < 0)
            s = Max<s64>(0, total_frame_count + loop.start_frame);
        else
            s = loop.start_frame;
        CheckedCast<u32>(s);
    });

    result.end = ({
        s64 e;
        if (loop.end_frame <= 0)
            e = Max<s64>(0, total_frame_count + loop.end_frame);
        else {
            e = Clamp<s64>(loop.end_frame,
                           Min(loop.start_frame + smallest_loop_size_allowed, total_frame_count),
                           total_frame_count);
        }

        CheckedCast<u32>(e);
    });

    ASSERT(result.end >= result.start);

    result.crossfade = (u32)ClampCrossfadeSize<s64>(loop.crossfade_frames,
                                                    result.start,
                                                    result.end,
                                                    total_frame_count,
                                                    loop.mode);

    return result;
}

namespace loop_and_reverse_flags {

enum Bits : u32 {
    CurrentlyReversed = 1 << 0,
    InFirstLoop = 1 << 1,
    LoopedManyTimes = 1 << 2,

    InLoopingRegion = InFirstLoop | LoopedManyTimes,
};

[[nodiscard]] inline u32 CorrectLoopFlagsIfNeeded(u32 flags, BoundsCheckedLoop const loop, f64 frame_pos) {
    auto const end = (f64)loop.end;
    auto const start = (f64)loop.start;

    if (frame_pos >= start && frame_pos < end) {
        if (!(flags & InLoopingRegion)) flags |= InFirstLoop;
    } else {
        flags &= ~InLoopingRegion;
    }
    return flags;
}

} // namespace loop_and_reverse_flags

inline bool IncrementSamplePlaybackPos(Optional<BoundsCheckedLoop> const& loop,
                                       u32& playback_mode,
                                       f64& frame_pos,
                                       f64 pitch_ratio,
                                       f64 num_frames) {
    using namespace loop_and_reverse_flags;

    bool const going_forward = !(playback_mode & CurrentlyReversed);

    if (going_forward)
        frame_pos += pitch_ratio;
    else
        frame_pos -= pitch_ratio;

    if (loop) {
        auto const end = (f64)loop->end;
        auto const start = (f64)loop->start;

        if (going_forward) {
            if (frame_pos >= start && !(playback_mode & InLoopingRegion)) playback_mode |= InFirstLoop;

            if ((playback_mode & InLoopingRegion) && frame_pos >= end) {
                playback_mode &= ~InFirstLoop;
                playback_mode |= LoopedManyTimes;
                switch (loop->mode) {
                    case sample_lib::LoopMode::Standard: {
                        frame_pos = start + (frame_pos - end);
                        break;
                    }
                    case sample_lib::LoopMode::PingPong: {
                        frame_pos = end - Fmod(frame_pos - end, end);
                        playback_mode ^= CurrentlyReversed;
                        break;
                    }
                    case sample_lib::LoopMode::Count: PanicIfReached(); break;
                }
            }
        } else {
            if (frame_pos < end && !(playback_mode & InLoopingRegion)) playback_mode |= InFirstLoop;

            if ((playback_mode & InLoopingRegion) && frame_pos < start) {
                playback_mode &= ~InFirstLoop;
                playback_mode |= LoopedManyTimes;
                switch (loop->mode) {
                    case sample_lib::LoopMode::Standard: {
                        frame_pos = end - (start - frame_pos);
                        break;
                    }
                    case sample_lib::LoopMode::PingPong: {
                        frame_pos = start + (start - frame_pos);
                        playback_mode ^= CurrentlyReversed;
                        break;
                    }
                    case sample_lib::LoopMode::Count: PanicIfReached(); break;
                }
            }
        }
    }

    if (frame_pos < 0 || frame_pos >= num_frames) return false;
    return true;
}

inline f32x2 SampleGetData(AudioData const& s,
                           Optional<BoundsCheckedLoop> const opt_loop,
                           u32 loop_and_reverse_flags,
                           f64 frame_pos,
                           bool recurse = false) {
    using namespace loop_and_reverse_flags;
    auto const loop = opt_loop.NullableValue();

    auto const frames_in_sample = s.num_frames;
    ASSERT(s.num_frames != 0);
    auto const last_frame = frames_in_sample - 1;

    auto const forward = !(loop_and_reverse_flags & CurrentlyReversed);

    if (loop) {
        ASSERT(loop->end <= frames_in_sample);
        ASSERT(loop->start < frames_in_sample);
        ASSERT(loop->end > loop->start);
    }
    ASSERT(frame_pos < frames_in_sample);

    auto const frame_index = (int)frame_pos;
    auto x = (f32)frame_pos - (f32)frame_index;

    s64 xm1;
    s64 x0;
    s64 x1;
    s64 x2;

    if (forward) {
        xm1 = frame_index - 1;
        x0 = frame_index;
        x1 = frame_index + 1;
        x2 = frame_index + 2;

        if (loop && loop->mode == sample_lib::LoopMode::PingPong &&
            (loop_and_reverse_flags & InLoopingRegion)) {
            if (loop_and_reverse_flags & LoopedManyTimes && xm1 < loop->start)
                xm1 = loop->start;
            else if (xm1 < 0)
                xm1 = 0;
            if (x1 >= loop->end) x1 = loop->end - 1;
            if (x2 >= loop->end) x2 = (loop->end - 1) - (x2 - loop->end);
        } else if (loop && loop->mode == sample_lib::LoopMode::Standard &&
                   (loop_and_reverse_flags & InLoopingRegion) && loop->crossfade == 0) {
            if (xm1 < 0) xm1 = loop->end + xm1;
            if (x1 >= loop->end) x1 = loop->start + (x1 - loop->end);
            if (x2 >= loop->end) x2 = loop->start + (x2 - loop->end);
        } else {
            if (xm1 < 0) xm1 = 0;
            if (x1 >= frames_in_sample) x1 = last_frame;
            if (x2 >= frames_in_sample) x2 = last_frame;
        }
    } else {
        x = 1 - x;
        xm1 = frame_index + 1;
        x0 = frame_index;
        x1 = frame_index - 1;
        x2 = frame_index - 2;

        if (loop && loop->mode == sample_lib::LoopMode::PingPong &&
            (loop_and_reverse_flags & InLoopingRegion)) {
            if (loop_and_reverse_flags & LoopedManyTimes) {
                if (xm1 >= loop->end) xm1 = loop->end - 1;
            }
            if (x1 < loop->start) x1 = loop->start;
            if (x2 < loop->start) x2 = loop->start + ((loop->start - x2) - 1);
        } else if (loop && loop->mode == sample_lib::LoopMode::Standard &&
                   (loop_and_reverse_flags & InLoopingRegion) && loop->crossfade == 0) {
            if (xm1 >= loop->end) xm1 = loop->start;
            if (x1 < 0) x1 = loop->end + x1;
            if (x2 < 0) x2 = loop->end + x2;
        } else {
            if (xm1 >= frames_in_sample) xm1 = last_frame;
            if (x1 < 0) x1 = 0;
            if (x2 < 0) x2 = 0;
        }
    }

    ASSERT(x0 >= 0 && x0 < frames_in_sample);

    f32 const* sample_data = s.interleaved_samples.data;
    auto* f0 = sample_data + (x0 * s.channels);
    auto* f1 = sample_data + (x1 * s.channels);
    auto* f2 = sample_data + (x2 * s.channels);
    auto* fm1 = sample_data + (xm1 * s.channels);
    f32x2 result = 0;
    if (s.channels == 1)
        result = DoMonoCubicInterp(f0, f1, f2, fm1, x);
    else if (s.channels == 2)
        result = DoStereoLagrangeInterp(f0, f1, f2, fm1, x);
    else
        PanicIfReached();

    if (loop && loop->crossfade) {
        f32 crossfade_pos = 0;
        bool is_crossfading = false;
        f32x2 xfade_result = 0;
        if (loop->mode == sample_lib::LoopMode::Standard) {
            auto const xfade_fade_out_start =
                loop->end - loop->crossfade; // The bit before the loop end point.
            auto const xfade_fade_in_start =
                loop->start - loop->crossfade; // The bit before the loop start point.

            if (frame_pos >= xfade_fade_out_start && frame_pos < loop->end) {
                if (forward || (!forward && (loop_and_reverse_flags & LoopedManyTimes))) {
                    auto frames_info_fade = frame_pos - xfade_fade_out_start;

                    xfade_result = SampleGetData(s,
                                                 opt_loop,
                                                 loop_and_reverse_flags & CurrentlyReversed,
                                                 xfade_fade_in_start + frames_info_fade,
                                                 true);
                    crossfade_pos = (f32)frames_info_fade / (f32)loop->crossfade;
                    ASSERT(crossfade_pos >= 0 && crossfade_pos <= 1);

                    is_crossfading = true;
                }
            }
        } else if (loop_and_reverse_flags & LoopedManyTimes) { // Ping-pong
            ASSERT(!recurse);
            ASSERT(loop->mode == sample_lib::LoopMode::PingPong);

            if (forward && (frame_pos <= (loop->start + loop->crossfade)) && frame_pos >= loop->start) {
                auto frames_into_fade = frame_pos - loop->start;
                auto fade_pos = (f64)loop->start - frames_into_fade;
                xfade_result = SampleGetData(s, opt_loop, CurrentlyReversed, fade_pos, true);
                crossfade_pos = 1.0f - ((f32)frames_into_fade / (f32)loop->crossfade);
                ASSERT(crossfade_pos >= 0 && crossfade_pos <= 1);

                is_crossfading = true;
            } else if (!forward && frame_pos >= (loop->end - loop->crossfade) && frame_pos < loop->end) {
                auto frames_into_fade = loop->end - frame_pos;
                auto fade_pos = loop->end + frames_into_fade;
                xfade_result = SampleGetData(s, opt_loop, 0, fade_pos, true);
                crossfade_pos = 1.0f - ((f32)frames_into_fade / (f32)loop->crossfade);
                ASSERT(crossfade_pos >= 0 && crossfade_pos <= 1);

                is_crossfading = true;
            }
        }

        if (is_crossfading) {
            f32x4 t {1 - crossfade_pos, crossfade_pos, 1, 1};
            t = Sqrt(t);

            result *= t[0];
            xfade_result *= t[1];

            result += xfade_result;
        }
    }

    return result;
}

struct IntRange {
    int lo;
    int hi;
};

inline int Overlap(IntRange a, IntRange b) { return Max(0, Min(a.hi, b.hi) - Max(a.lo, b.lo) + 1); }

enum class WaveformAudioSourceType { AudioData, Sine, WhiteNoise };

using WaveformAudioSource =
    TaggedUnion<WaveformAudioSourceType, TypeAndTag<AudioData const*, WaveformAudioSourceType::AudioData>>;

PUBLIC Span<u8> CreateWaveformImage(WaveformAudioSource source,
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
        // audio data helpers
        sv_filter::CachedHelpers filter_cache {};
        sv_filter::Data<f32x2> filter_data {};
        filter_cache.Update(44100, 2000, 0.5f);
        auto const num_frames =
            source.Is<AudioData const*>() ? source.Get<AudioData const*>()->num_frames : 0;
        auto const samples_per_pixel = (f32)num_frames / ((f32)scaled_width);
        f32 first_sample = 0;

        // other helpers
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
                        // hard-set the history so that the filter doesn't have to ramp up and therefore
                        // zero-out any initial peak in the audio file
                        filter_data.z1_a = levels;
                        filter_data.z2_a = levels;
                    }
                    sv_filter::Process(levels, levels, filter_data, sv_filter::Type::Lowpass, filter_cache);

                    levels = Clamp01(levels);

                    // arbitrary skew to make the waveform a bit more prominent
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
