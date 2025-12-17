// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/audio_data.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

template <typename T>
union InterpolationPoints {
    struct {
        T xm1, x0, x1, x2; // NOLINT
    };
};

template <Integral T>
union InterpolationPoints<T> {
    using Vector = __attribute__((ext_vector_type(4))) T;
    static_assert(sizeof(Vector) == sizeof(T[4]));

    Vector vec;
    struct {
        T xm1, x0, x1, x2; // NOLINT
    };
};

inline f32 DoMonoCubicInterp(InterpolationPoints<f32 const*> const& points, f32 const x) {
    return points.x0[0] + ((((points.x2[0] - points.xm1[0] - 3 * points.x1[0] + 3 * points.x0[0]) * x +
                             3 * (points.x1[0] + points.xm1[0] - 2 * points.x0[0])) *
                                x -
                            (points.x2[0] + 2 * points.xm1[0] - 6 * points.x1[0] + 3 * points.x0[0])) *
                           x / 6.0f);
}

inline f32x2 DoStereoLagrangeInterp(InterpolationPoints<f32 const*> const& points, f32 const x) {
    // x is given in the range 0 to 1 but we want the value between f0 and f1, therefore we add 1.
    auto const xf = x + 1;
    auto const xfm1 = x;
    auto const xfm2 = xf - 2;
    auto const xfm3 = xf - 3;

    f32x4 const v0 {xfm1, xf, xf, xf};
    f32x4 const v2 {xfm2, xfm2, xfm1, xfm1};
    f32x4 const v4 {xfm3, xfm3, xfm3, xfm2};

    f32x4 constexpr k_v1 {-1, 1, 2, 3};
    f32x4 constexpr k_v3 {-2, -1, 1, 2};
    f32x4 constexpr k_v5 {-3, -2, -1, 1};

    f32x4 const vd0 = v0 / k_v1;
    f32x4 const vd1 = v2 / k_v3;
    f32x4 const vd2 = v4 / k_v5;

    auto const vt = vd0 * vd1 * vd2;

    return {
        (points.xm1[0] * vt[0]) + (points.x0[0] * vt[1]) + (points.x1[0] * vt[2]) + (points.x2[0] * vt[3]),
        (points.xm1[1] * vt[0]) + (points.x0[1] * vt[1]) + (points.x1[1] * vt[2]) + (points.x2[1] * vt[3]),
    };
}

struct BoundsCheckedLoop {
    u32 start {}; // Inclusive.
    u32 end {}; // Exclusive.
    u32 crossfade {};
    sample_lib::LoopMode mode {};
};

template <typename Type>
inline Type ClampCrossfadeSize(Type crossfade, Type start, Type end, Type total, sample_lib::LoopMode mode) {
    ASSERT(crossfade >= 0);
    ASSERT(start >= 0);
    ASSERT(end >= 0);
    auto const loop_size = end - start;
    ASSERT(loop_size >= 0);
    Type result;
    switch (mode) {
        case sample_lib::LoopMode::Standard: result = Min(crossfade, loop_size, start); break;
        case sample_lib::LoopMode::PingPong:
            if (total < end)
                result = 0;
            else
                result = Min(crossfade, start, total - end, loop_size);
            break;
        case sample_lib::LoopMode::Count: PanicIfReached();
    }
    return result;
}

inline BoundsCheckedLoop CreateBoundsCheckedLoop(sample_lib::BuiltinLoop loop, u32 num_frames) {
    ASSERT_HOT(num_frames != 0);

    auto start = ({
        u32 s;
        if (loop.start_frame < 0) {
            auto const offset_end = -loop.start_frame;
            s = (num_frames >= offset_end) ? (num_frames - (u32)offset_end) : 0;
        } else
            s = loop.start_frame < num_frames ? (u32)loop.start_frame : num_frames - 1;
        s;
    });

    auto end = ({
        u32 e;
        if (loop.end_frame <= 0) {
            auto const offset_end = -loop.end_frame;
            e = (num_frames >= offset_end) ? (num_frames - (u32)offset_end) : 0;
        } else {
            e = loop.end_frame < num_frames ? (u32)loop.end_frame : num_frames;
        }
        e;
    });

    ASSERT_HOT(start < num_frames);
    ASSERT_HOT(end <= num_frames);

    // This strange clamping is unfortunately necessary to maintain backwards compatibility.
    auto const smallest_loop_size_allowed = Max(num_frames / 1000, 32u);

    // If the end is before the start, we try to move it to after the start.
    if (end <= start) {
        u32 new_end;
        if (__builtin_add_overflow(start, smallest_loop_size_allowed, &new_end)) [[unlikely]]
            end = num_frames;
        else
            end = Min(new_end, num_frames);
    }

    ASSERT_HOT(end >= start);

    // It's possible with values close to the end of the sample (or u32 max) that we couldn't move the end to
    // the ideal place after the start. In this case, we move the start back instead.
    if ((end - start) < smallest_loop_size_allowed) {
        u32 new_start;
        if (__builtin_sub_overflow(end, smallest_loop_size_allowed, &new_start)) [[unlikely]]
            start = 0;
        else
            start = new_start;
    }

    ASSERT(end > start);

    return {
        .start = start,
        .end = end,
        .crossfade = ClampCrossfadeSize<u32>(loop.crossfade_frames, start, end, num_frames, loop.mode),
        .mode = loop.mode,
    };
}

[[nodiscard]] inline BoundsCheckedLoop InvertLoop(BoundsCheckedLoop const& l, u32 num_frames) {
    ASSERT_HOT(l.end <= num_frames);
    ASSERT_HOT(l.start < num_frames);
    auto const new_start = num_frames - l.end;
    auto const new_end = num_frames - l.start;

    BoundsCheckedLoop const result {
        .start = new_start,
        .end = new_end,
        .crossfade = ClampCrossfadeSize<u32>(l.crossfade, new_start, new_end, num_frames, l.mode),
        .mode = l.mode,
    };

    ASSERT_HOT(result.end <= num_frames);
    ASSERT_HOT(result.start < num_frames);
    return result;
}

struct PlayHead {
    struct Loop : BoundsCheckedLoop {
        bool only_use_frames_within_loop {};
    };

    Optional<u32> RealFramePos(u32 num_frames) const {
        if (frame_pos >= num_frames) return k_nullopt;
        auto const frame_index = (u32)frame_pos;
        return inverse_data_lookup ? ((num_frames - 1) - frame_index) : frame_index;
    }

    // Change the direction but maintain the same audio data position.
    void Invert(u32 num_frames) {
        inverse_data_lookup = !inverse_data_lookup;
        frame_pos = num_frames - frame_pos;
    }

    // The frame position in the audio data regardless of playback direction. It only ever goes forwards. So
    // even when in reverse playback mode, it starts at 0 and goes to num_frames.
    f64 frame_pos = {};

    // The looping information, if any. The start and end points are in the same dimension as frame_pos.
    Optional<Loop> loop = {};

    // This is the audio playback direction that was requested by the system controlling playback. We use it
    // to determine if the request has changed. It might not be the same as the inverse_data_lookup because of
    // ping-pong loops.
    bool requested_reverse = {};

    // Throughout this system, we handle all playback in a 'forwards only' manner rather than having to do
    // 'ifs' throughout the code to handle reverse vs forwards playback. In order to achieve this, we use this
    // flag to indicate that we need to look up data in reverse rather than forwards. This mode is toggled in
    // a ping-pong loop.
    bool inverse_data_lookup = {};
};

inline bool PlaybackEnded(PlayHead const& playhead, u32 num_frames) {
    return playhead.frame_pos >= num_frames;
}

inline void IncrementPlaybackPos(PlayHead& playhead, f64 increment, u32 num_frames) {
    ASSERT_HOT(!PlaybackEnded(playhead, num_frames));
    ASSERT_HOT(playhead.frame_pos < num_frames);
    ASSERT_HOT(increment >= 0);
    ASSERT_HOT(num_frames);

    playhead.frame_pos += increment;

    if (playhead.loop) {
        // Handle passing the loop end.
        if (playhead.frame_pos >= playhead.loop->end) {
            ASSERT_HOT(playhead.loop->end > playhead.loop->start);

            auto const loop_size = playhead.loop->end - playhead.loop->start;
            auto const overshoot = playhead.frame_pos - playhead.loop->end;
            auto const bounded_overshoot = Fmod(overshoot, (f64)loop_size);

            switch (playhead.loop->mode) {
                case sample_lib::LoopMode::Standard: {
                    // Wrap around to the start.
                    playhead.frame_pos = playhead.loop->start + bounded_overshoot;
                    break;
                }
                case sample_lib::LoopMode::PingPong: {
                    // Bounce the position off the end.
                    playhead.frame_pos = playhead.loop->end - bounded_overshoot;

                    if ((u32)(overshoot / loop_size) % 2 == 0) {
                        playhead.Invert(num_frames);
                        playhead.loop = InvertLoop(*playhead.loop, num_frames);
                    }
                    break;
                }
                case sample_lib::LoopMode::Count: PanicIfReached(); break;
            }

            ASSERT_HOT(playhead.frame_pos >= playhead.loop->start);
            ASSERT_HOT(playhead.frame_pos < playhead.loop->end);

            playhead.loop->only_use_frames_within_loop = true;
        }

        // The start point might have been moved to before the playhead.
        if (playhead.loop->only_use_frames_within_loop && playhead.frame_pos < playhead.loop->start)
            playhead.loop->only_use_frames_within_loop = false;
    }
}

inline void ResetPlayhead(PlayHead& playhead,
                          f64 frame_pos,
                          Optional<BoundsCheckedLoop> const& loop,
                          bool is_reversed,
                          u32 num_frames) {
    ASSERT_HOT(num_frames);
    playhead = {};
    playhead.frame_pos = frame_pos;
    playhead.requested_reverse = is_reversed;
    playhead.inverse_data_lookup = is_reversed;
    if (loop) {
        playhead.loop = is_reversed ? InvertLoop(*loop, num_frames) : *loop;
        if (frame_pos >= playhead.loop->start) playhead.loop->only_use_frames_within_loop = true;
        if (frame_pos >= playhead.loop->end) playhead.frame_pos = playhead.loop->start;
    }
}

inline void UpdatePlayhead(PlayHead& playhead,
                           Optional<BoundsCheckedLoop> const& loop,
                           bool is_reversed,
                           u32 num_frames) {
    ASSERT_HOT(num_frames);
    if (playhead.requested_reverse != is_reversed) {
        playhead.requested_reverse = is_reversed;
        auto const should_invert = ({
            bool v;
            if (loop && loop->mode == sample_lib::LoopMode::PingPong)
                // For ping-pong loops, it feels more natural that changing the reverse state flips the
                // playback so at least something happens. Playback direction is less important in this mode
                // since it's constantly changing.
                v = true;
            else
                // Otherwise, we
                v = playhead.inverse_data_lookup != is_reversed;
            v;
        });
        if (should_invert) playhead.Invert(num_frames);
    }

    if (!loop) {
        playhead.loop = k_nullopt;
    } else {
        // When the loop changes mode, let's reset the inversion state so that for standard loops it always
        // respects the current playback direction.
        if ((!playhead.loop || playhead.loop->mode != loop->mode) &&
            playhead.inverse_data_lookup != is_reversed) {
            playhead.Invert(num_frames);
        }

        playhead.loop = playhead.inverse_data_lookup ? InvertLoop(*loop, num_frames) : *loop;

        if (!PlaybackEnded(playhead, num_frames))
            // Use the increment function to handle loop clamping that we may need to do if the loop changed
            // (using 0 as the step increment).
            IncrementPlaybackPos(playhead, 0, num_frames);
    }
}

ALWAYS_INLINE constexpr u32 DataIndexAtOffset(signed _BitInt(3) steps,
                                              u32 frame_index,
                                              PlayHead::Loop const* loop,
                                              u32 num_frames,
                                              u32 last_frame) {
    ASSERT_HOT(steps != 0);
    using namespace sample_lib;

    // The theoretical new position - may be out of bounds.
    auto const v = (s64)frame_index + steps;

    if (steps < 0) {
        if (loop && loop->only_use_frames_within_loop && v < loop->start) {
            ASSERT_HOT(loop->start < loop->end);
            ASSERT_HOT(loop->end != 0); // This is implicit in the assert above but let's state it anyways.

            auto const overshoot = loop->start - v;
            ASSERT_HOT(overshoot);

            switch (loop->mode) {
                case LoopMode::Standard: {
                    // Wrap around to the end of the loop.

                    u32 result;
                    if (__builtin_sub_overflow(loop->end, overshoot, &result)) [[unlikely]]
                        // We've under-flowed, ideally we'd do some sort of modulo to find the right value
                        // but it's not worth the computational cost. We'd only get to this point if the
                        // loop is absolutely tiny (a few frames long); they're not going to sound good by
                        // any means so we just return the start because we know at least it's a valid
                        // position. anyways.
                        return loop->end - 1;

                    return Max(result, loop->start);
                }
                case LoopMode::PingPong: {
                    // Bounce off the start of the loop.

                    u32 result;
                    if (__builtin_add_overflow(loop->start, overshoot - 1, &result)) [[unlikely]]
                        // Overflowing here means the loop is tiny and is near the max u32 value; we just
                        // return a valid position without much care if it's perfect.
                        return loop->start;

                    return Min(result, loop->end - 1);
                }
                case LoopMode::Count: PanicIfReached();
            }
        } else if (v < 0) {
            return 0;
        }
    } else {
        //
        if (loop && v >= loop->end) {
            ASSERT_HOT(loop->start < loop->end);
            ASSERT_HOT(loop->end != 0);

            auto const overshoot = (v - loop->end) + 1;

            switch (loop->mode) {
                case LoopMode::Standard: {
                    // Wrap around to the start of the loop.

                    u32 result;
                    if (__builtin_add_overflow(loop->start, overshoot - 1, &result)) [[unlikely]]
                        return loop->start; // As above, the loop must be tiny.

                    return Min(result, loop->end - 1);
                }
                case LoopMode::PingPong: {
                    // Bounce off the end of the loop.

                    u32 result;
                    if (__builtin_sub_overflow(loop->end, overshoot, &result)) [[unlikely]]
                        return loop->end - 1; // As above, the loop must be tiny.

                    return Max(result, loop->start);
                }
                case LoopMode::Count: PanicIfReached();
            }
        } else if (v >= num_frames) {
            return last_frame;
        }
    }

    return (u32)v;
}

inline f32x2 GetSampleFrame(AudioData const& s, PlayHead const& playhead) {
    auto const loop = playhead.loop.NullableValue();

    ASSERT_HOT(s.num_frames != 0);
    ASSERT_HOT(playhead.frame_pos >= 0);
    ASSERT_HOT(playhead.frame_pos < s.num_frames);

    if (loop) {
        ASSERT_HOT(loop->end <= s.num_frames);
        ASSERT_HOT(loop->start < s.num_frames);
        ASSERT_HOT(loop->end > loop->start);
    }

    auto const last_frame = s.num_frames - 1;
    auto const frame_index = (u32)playhead.frame_pos;
    auto const x = (f32)(playhead.frame_pos - frame_index);

    InterpolationPoints<u32> const frame_indices = {
        .xm1 = DataIndexAtOffset(-1, frame_index, loop, s.num_frames, last_frame),
        .x0 = frame_index,
        .x1 = DataIndexAtOffset(1, frame_index, loop, s.num_frames, last_frame),
        .x2 = DataIndexAtOffset(2, frame_index, loop, s.num_frames, last_frame),
    };

    ASSERT_HOT(frame_indices.x0 >= 0 && frame_indices.x0 < s.num_frames);

    auto const data_vals = ({
        auto indices = frame_indices;

        // If we're reversed, invert the indices.
        if (playhead.inverse_data_lookup) indices.vec = u32x4(last_frame) - indices.vec;

        // Convert from frame indices to sample indices.
        indices.vec *= s.channels;

        InterpolationPoints<f32 const*> p {
            .xm1 = s.interleaved_samples.data + indices.xm1,
            .x0 = s.interleaved_samples.data + indices.x0,
            .x1 = s.interleaved_samples.data + indices.x1,
            .x2 = s.interleaved_samples.data + indices.x2,
        };
        p;
    });

    auto result = ({
        f32x2 r;
        if (s.channels == 1)
            r = DoMonoCubicInterp(data_vals, x);
        else if (s.channels == 2)
            r = DoStereoLagrangeInterp(data_vals, x);
        else
            PanicIfReached();
        r;
    });

    if (loop && loop->crossfade) {
        f32 crossfade_pos = 0;
        bool is_crossfading = false;
        f32x2 xfade_result = 0;

        switch (loop->mode) {
            case sample_lib::LoopMode::Standard: {
                auto const xfade_fade_out_start = loop->end - loop->crossfade;
                auto const xfade_fade_in_start = loop->start - loop->crossfade;

                if (playhead.frame_pos >= xfade_fade_out_start && playhead.frame_pos < loop->end) {
                    auto const frames_info_fade = playhead.frame_pos - xfade_fade_out_start;

                    xfade_result = GetSampleFrame(s,
                                                  {
                                                      .frame_pos = xfade_fade_in_start + frames_info_fade,
                                                      .inverse_data_lookup = playhead.inverse_data_lookup,
                                                  });
                    crossfade_pos = (f32)(frames_info_fade / loop->crossfade);
                    is_crossfading = true;
                }
                break;
            }
            case sample_lib::LoopMode::PingPong: {
                if (playhead.frame_pos >= (loop->end - loop->crossfade) && playhead.frame_pos < loop->end) {
                    auto const frames_into_fade = loop->end - playhead.frame_pos;
                    auto const fade_pos = loop->end + frames_into_fade;
                    xfade_result = GetSampleFrame(s,
                                                  {
                                                      .frame_pos = s.num_frames - fade_pos,
                                                      .inverse_data_lookup = !playhead.inverse_data_lookup,
                                                  });
                    crossfade_pos = (f32)(1.0 - (frames_into_fade / loop->crossfade));
                    is_crossfading = true;
                }
                break;
            }
            case sample_lib::LoopMode::Count: PanicIfReached();
        }

        if (is_crossfading) {
            ASSERT(crossfade_pos >= 0 && crossfade_pos <= 1);
            f32x4 t {1 - crossfade_pos, crossfade_pos, 1, 1};
            t = Sqrt(t);

            result *= t[0];
            xfade_result *= t[1];

            result += xfade_result;
        }
    }

    return result;
}

enum class WaveformAudioSourceType { AudioData, Sine, WhiteNoise };

using WaveformAudioSource =
    TaggedUnion<WaveformAudioSourceType, TypeAndTag<AudioData const*, WaveformAudioSourceType::AudioData>>;

Span<u8>
CreateWaveformImage(WaveformAudioSource source, UiSize size, Allocator& a, ArenaAllocator& scratch_allocator);
