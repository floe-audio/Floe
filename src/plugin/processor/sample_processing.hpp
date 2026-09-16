// Copyright 2018-2025 Sam Windell
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

// 4-point, 3rd-order Hermite interpolation (Laurent de Soras' form). Lanes are independent, so a vector can
// pack any mix of channels and frames as long as x is the fractional position of each lane.
template <typename Vec, typename X>
ALWAYS_INLINE inline Vec DoHermiteInterp(InterpolationPoints<Vec> const& p, X const x) {
    Vec const c = (p.x1 - p.xm1) * 0.5f;
    Vec const v = p.x0 - p.x1;
    Vec const w = c + v;
    Vec const a = w + v + ((p.x2 - p.x0) * 0.5f);
    Vec const b_neg = w + a;

    return (((((a * x) - b_neg) * x) + c) * x) + p.x0;
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

    void InvertLoop(u32 num_frames) {
        if (loop) (BoundsCheckedLoop&)* loop = ::InvertLoop(*loop, num_frames);
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

ALWAYS_INLINE NO_UBSAN inline void IncrementPlaybackPos(PlayHead& playhead, f64 increment, u32 num_frames) {
    ASSERT_HOT(!PlaybackEnded(playhead, num_frames));
    ASSERT_HOT(playhead.frame_pos < num_frames);
    ASSERT_HOT(increment >= 0);
    ASSERT_HOT(num_frames);

    playhead.frame_pos += increment;

    if (auto loop = playhead.loop.NullableValue()) {
        // Handle passing the loop end. We only wrap if either:
        // - The playhead was already committed to the loop region (only_use_frames_within_loop), or
        // - The increment genuinely moved the playhead past the loop end (the position before the increment
        //   was still within/before the end).
        // This distinction matters after a reverse toggle: the inverted frame_pos can land past the inverted
        // loop's end even though the playhead hasn't entered the loop yet. Without this check, it would
        // incorrectly clamp into the loop.
        if (playhead.frame_pos >= loop->end &&
            (loop->only_use_frames_within_loop || (playhead.frame_pos - increment) < loop->end)) {
            ASSERT_HOT(loop->end > loop->start);

            auto const loop_size = loop->end - loop->start;
            auto const overshoot = playhead.frame_pos - loop->end;
            auto const bounded_overshoot = Fmod(overshoot, (f64)loop_size);

            switch (loop->mode) {
                case sample_lib::LoopMode::Standard: {
                    // Wrap around to the start.
                    playhead.frame_pos = loop->start + bounded_overshoot;
                    break;
                }
                case sample_lib::LoopMode::PingPong: {
                    // Bounce the position off the end.
                    playhead.frame_pos = loop->end - bounded_overshoot;

                    if ((u32)(overshoot / loop_size) % 2 == 0) {
                        playhead.Invert(num_frames);
                        (BoundsCheckedLoop&)* loop = InvertLoop(*loop, num_frames);
                    }
                    break;
                }
                case sample_lib::LoopMode::Count: PanicIfReached();
            }

            ASSERT_HOT(playhead.frame_pos >= loop->start);
            ASSERT_HOT(playhead.frame_pos < loop->end);

            loop->only_use_frames_within_loop = true;
        }

        // The start point might have been moved to before the playhead.
        if (loop->only_use_frames_within_loop && playhead.frame_pos < loop->start)
            loop->only_use_frames_within_loop = false;
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

        // Remember whether the playhead was already committed to the loop region. If it wasn't (e.g. the
        // playhead is still in the pre-loop portion of the audio), we must not clamp the position into the
        // loop. This matters when the reverse direction changes: the inverted frame_pos can end up past the
        // inverted loop's end even though the playhead hasn't entered the loop yet.
        auto const was_within_loop = playhead.loop && playhead.loop->only_use_frames_within_loop;

        if (!playhead.loop) playhead.loop.Emplace();
        (BoundsCheckedLoop&)* playhead.loop =
            playhead.inverse_data_lookup ? InvertLoop(*loop, num_frames) : *loop;

        if (!PlaybackEnded(playhead, num_frames)) {
            if (was_within_loop) {
                // Use the increment function to handle loop clamping that we may need to do if the loop
                // changed (using 0 as the step increment).
                IncrementPlaybackPos(playhead, 0, num_frames);
            } else if (playhead.frame_pos >= playhead.loop->start &&
                       playhead.frame_pos < playhead.loop->end) {
                // The playhead has naturally entered the loop region.
                playhead.loop->only_use_frames_within_loop = true;
            }
        }
    }
}

ALWAYS_INLINE NO_UBSAN constexpr u32 DataIndexAtOffset(signed _BitInt(3) steps,
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
        if (loop && loop->only_use_frames_within_loop && v >= loop->end) {
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

ALWAYS_INLINE inline InterpolationPoints<u32> ContiguousTapIndices(u32 frame_index) {
    ASSERT_HOT(frame_index >= 1);
    return {.vec = u32x4(frame_index) + u32x4 {(u32)-1, 0, 1, 2}};
}

ALWAYS_INLINE NO_UBSAN inline InterpolationPoints<f32x2>
LoadInterpolationPoints(InterpolationPoints<f32 const*> const& p, u8 channels) {
    ASSERT_HOT(channels > 0);
    ASSERT_HOT(channels <= 2);
    InterpolationPoints<f32x2> result;
    if (channels == 1) {
        result = {
            .xm1 = {p.xm1[0], p.xm1[0]},
            .x0 = {p.x0[0], p.x0[0]},
            .x1 = {p.x1[0], p.x1[0]},
            .x2 = {p.x2[0], p.x2[0]},
        };
    } else {
        __builtin_memcpy_inline(&result.xm1, p.xm1, sizeof(f32x2));
        __builtin_memcpy_inline(&result.x0, p.x0, sizeof(f32x2));
        __builtin_memcpy_inline(&result.x1, p.x1, sizeof(f32x2));
        __builtin_memcpy_inline(&result.x2, p.x2, sizeof(f32x2));
    }
    return result;
}

// 4-point Hermite interpolation using the audio data at the given frame indices.
ALWAYS_INLINE NO_UBSAN inline f32x2 InterpolateAtFrameIndices(AudioData const& s,
                                                              InterpolationPoints<u32> frame_indices,
                                                              f32 x,
                                                              bool inverse_data_lookup) {
    ASSERT_HOT(s.num_frames != 0);
    ASSERT_HOT(s.channels > 0);
    ASSERT_HOT(s.channels <= 2);
    ASSERT_HOT(frame_indices.x0 < s.num_frames);

    auto const data_vals = ({
        auto indices = frame_indices;

        // If we're reversed, invert the indices.
        if (inverse_data_lookup) indices.vec = u32x4(s.num_frames - 1) - indices.vec;

        // Convert from frame indices to sample indices (channels is 1 or 2 so we can use a bit shift for
        // speed).
        indices.vec <<= s.channels - 1;

        InterpolationPoints<f32 const*> p {
            .xm1 = s.interleaved_samples.data + indices.xm1,
            .x0 = s.interleaved_samples.data + indices.x0,
            .x1 = s.interleaved_samples.data + indices.x1,
            .x2 = s.interleaved_samples.data + indices.x2,
        };
        p;
    });

    return DoHermiteInterp(LoadInterpolationPoints(data_vals, s.channels), x);
}

// 4-point interpolation at the playhead position. Doesn't apply the loop crossfade.
ALWAYS_INLINE NO_UBSAN inline f32x2 InterpolateSampleFrame(AudioData const& s, PlayHead const& playhead) {
    auto const loop = playhead.loop.NullableValue();

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

    InterpolationPoints<u32> const frame_indices = ({
        InterpolationPoints<u32> indices;
        if (!loop && frame_index >= 1 && frame_index + 2 < s.num_frames) [[likely]] {
            indices = ContiguousTapIndices(frame_index);
        } else {
            indices = {
                .xm1 = DataIndexAtOffset(-1, frame_index, loop, s.num_frames, last_frame),
                .x0 = frame_index,
                .x1 = DataIndexAtOffset(1, frame_index, loop, s.num_frames, last_frame),
                .x2 = DataIndexAtOffset(2, frame_index, loop, s.num_frames, last_frame),
            };
        }
        indices;
    });

    return InterpolateAtFrameIndices(s, frame_indices, x, playhead.inverse_data_lookup);
}

struct ContiguousFrameBounds {
    u32 lower = 0; // Inclusive.
    u32 upper = LargestRepresentableValue<u32>(); // Exclusive.
};

// How many frames the playhead can be advanced (by at most max_increment each time) such that every fetch at
// those positions has all 4 interpolation taps contiguous and within the bounds, and no loop wrap, loop
// crossfade or end-of-data handling is needed.
ALWAYS_INLINE inline u32 ContiguousFramesAvailable(PlayHead const& playhead,
                                                   f64 max_increment,
                                                   ContiguousFrameBounds bounds,
                                                   u32 max_frames) {
    ASSERT_HOT(max_increment >= 0);

    // The taps are frame_index - 1 to frame_index + 2.
    auto lower_bound = Max(bounds.lower, 1u);
    auto upper_bound = bounds.upper;
    if (auto const loop = playhead.loop.NullableValue()) {
        if (loop->only_use_frames_within_loop) {
            lower_bound = Max(lower_bound, loop->start + 1);
            upper_bound = Min(upper_bound, loop->end);
        }
        // Crossing the loop end must go through the wrap/bounce logic, and positions within the crossfade
        // region need the crossfade applied.
        upper_bound = Min(upper_bound, loop->end - loop->crossfade);
    }

    if (playhead.frame_pos < lower_bound) return 0;

    // Positions are frame_pos + k * max_increment for k in [0, count]; the final one is where the playhead
    // ends up. All must be < upper_bound - 2.
    auto const headroom = ((f64)upper_bound - 3) - playhead.frame_pos;
    if (headroom <= 0) return 0;
    if (max_increment == 0) return max_frames;
    return (u32)Min((f64)max_frames, headroom / max_increment);
}

// Addressing for frames that ContiguousFramesAvailable() has already validated: tap k of frame i is at
// origin + (i + k) * tap_stride. Reversed lookup means frame 0 is the last frame of the data and the stride
// is negative.
struct ContiguousTapLayout {
    f32 const* origin;
    s64 tap_stride;
    u8 channels;
};

ALWAYS_INLINE inline ContiguousTapLayout ContiguousTapLayoutFor(AudioData const& s,
                                                                bool inverse_data_lookup) {
    return {
        .origin =
            s.interleaved_samples.data + (inverse_data_lookup ? (usize)(s.num_frames - 1) * s.channels : 0),
        .tap_stride = inverse_data_lookup ? -(s64)s.channels : (s64)s.channels,
        .channels = s.channels,
    };
}

ALWAYS_INLINE NO_UBSAN inline f32x2 InterpolateContiguousFrame(ContiguousTapLayout layout, f64 frame_pos) {
    auto const frame_index = (u32)frame_pos;
    auto const x0 = layout.origin + ((s64)frame_index * layout.tap_stride);

    return DoHermiteInterp(LoadInterpolationPoints(
                               {
                                   .xm1 = x0 - layout.tap_stride,
                                   .x0 = x0,
                                   .x1 = x0 + layout.tap_stride,
                                   .x2 = x0 + (2 * layout.tap_stride),
                               },
                               layout.channels),
                           (f32)(frame_pos - frame_index));
}

ALWAYS_INLINE NO_UBSAN inline f32x2
InterpolateContiguousFrame(AudioData const& s, f64 frame_pos, bool inverse_data_lookup) {
    auto const frame_index = (u32)frame_pos;
    ASSERT_HOT(frame_index >= 1);
    ASSERT_HOT(frame_index + 2 < s.num_frames);
    return InterpolateContiguousFrame(ContiguousTapLayoutFor(s, inverse_data_lookup), frame_pos);
}

ALWAYS_INLINE NO_UBSAN inline f32x4 LoadStereoFramePair(f32 const* frame_0, f32 const* frame_1) {
    f32x2 a;
    f32x2 b;
    __builtin_memcpy_inline(&a, frame_0, sizeof(f32x2));
    __builtin_memcpy_inline(&b, frame_1, sizeof(f32x2));
    return __builtin_shufflevector(a, b, 0, 1, 2, 3);
}

// Two stereo frames interpolated in one go, packed as {L0, R0, L1, R1}. Lane for lane it's the same
// arithmetic as the single-frame version, so the output is identical.
ALWAYS_INLINE NO_UBSAN inline f32x4
InterpolateContiguousStereoFramePair(ContiguousTapLayout layout, f64 frame_pos_0, f64 frame_pos_1) {
    ASSERT_HOT(layout.channels == 2);
    auto const stride = layout.tap_stride;
    auto const frame_index_0 = (u32)frame_pos_0;
    auto const frame_index_1 = (u32)frame_pos_1;
    auto const x0_0 = layout.origin + ((s64)frame_index_0 * stride);
    auto const x0_1 = layout.origin + ((s64)frame_index_1 * stride);

    InterpolationPoints<f32x4> const points {
        .xm1 = LoadStereoFramePair(x0_0 - stride, x0_1 - stride),
        .x0 = LoadStereoFramePair(x0_0, x0_1),
        .x1 = LoadStereoFramePair(x0_0 + stride, x0_1 + stride),
        .x2 = LoadStereoFramePair(x0_0 + (2 * stride), x0_1 + (2 * stride)),
    };
    f32x2 const fractions {(f32)(frame_pos_0 - frame_index_0), (f32)(frame_pos_1 - frame_index_1)};
    return DoHermiteInterp(points, __builtin_shufflevector(fractions, fractions, 0, 0, 1, 1));
}

// Four mono frames interpolated in one go, packed as {F0, F1, F2, F3}. As above, identical output to the
// single-frame version.
ALWAYS_INLINE NO_UBSAN inline f32x4 InterpolateContiguousMonoFrameQuad(ContiguousTapLayout layout,
                                                                       f64 frame_pos_0,
                                                                       f64 frame_pos_1,
                                                                       f64 frame_pos_2,
                                                                       f64 frame_pos_3) {
    ASSERT_HOT(layout.channels == 1);
    auto const stride = layout.tap_stride;
    auto const frame_index_0 = (u32)frame_pos_0;
    auto const frame_index_1 = (u32)frame_pos_1;
    auto const frame_index_2 = (u32)frame_pos_2;
    auto const frame_index_3 = (u32)frame_pos_3;
    auto const x0_0 = layout.origin + ((s64)frame_index_0 * stride);
    auto const x0_1 = layout.origin + ((s64)frame_index_1 * stride);
    auto const x0_2 = layout.origin + ((s64)frame_index_2 * stride);
    auto const x0_3 = layout.origin + ((s64)frame_index_3 * stride);

    InterpolationPoints<f32x4> const points {
        .xm1 = {x0_0[-stride], x0_1[-stride], x0_2[-stride], x0_3[-stride]},
        .x0 = {x0_0[0], x0_1[0], x0_2[0], x0_3[0]},
        .x1 = {x0_0[stride], x0_1[stride], x0_2[stride], x0_3[stride]},
        .x2 = {x0_0[2 * stride], x0_1[2 * stride], x0_2[2 * stride], x0_3[2 * stride]},
    };
    f32x4 const x {(f32)(frame_pos_0 - frame_index_0),
                   (f32)(frame_pos_1 - frame_index_1),
                   (f32)(frame_pos_2 - frame_index_2),
                   (f32)(frame_pos_3 - frame_index_3)};
    return DoHermiteInterp(points, x);
}

ALWAYS_INLINE NO_UBSAN inline f32x2 GetSampleFrame(AudioData const& s, PlayHead const& playhead) {
    auto result = InterpolateSampleFrame(s, playhead);

    auto const loop = playhead.loop.NullableValue();
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

                    xfade_result =
                        InterpolateSampleFrame(s,
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
                    xfade_result =
                        InterpolateSampleFrame(s,
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
            ASSERT_HOT(crossfade_pos >= 0 && crossfade_pos <= 1);
            f32x4 t {1 - crossfade_pos, crossfade_pos, 1, 1};
            t = Sqrt(t);

            result *= t[0];
            xfade_result *= t[1];

            result += xfade_result;
        }
    }

    return result;
}

struct SampleFetchOptions {
    Span<f64 const> increments; // Per output frame. Multiplied by increment_scale.
    f64 max_increment; // The largest value in increments (unscaled).
    f64 increment_scale = 1;
    u32 end_frame; // Playback has ended when the playhead reaches this.
    ContiguousFrameBounds contiguous_bounds {};
};

// Fetches interpolated frames at successive playhead positions, advancing the playhead after each. Returns
// the number of frames written; fewer than out.size means playback ended. Frames within contiguous_bounds are
// fetched via a fast path. All others go through the general path, where general_frame_hook(frame_index,
// playhead) is called after the fetch and before the playhead is advanced.
template <typename GeneralFrameHook>
ALWAYS_INLINE NO_UBSAN inline u32 FetchSampleFrames(AudioData const& s,
                                                    PlayHead& playhead,
                                                    Span<f32x2> out,
                                                    SampleFetchOptions const& options,
                                                    GeneralFrameHook&& general_frame_hook) {
    ASSERT_HOT(options.increments.size >= out.size);
    ASSERT_HOT(options.end_frame <= s.num_frames);

    auto const audio_data = s; // A local copy so that the fields aren't re-read from memory every frame.
    auto const bounds = ContiguousFrameBounds {
        .lower = options.contiguous_bounds.lower,
        .upper = Min(options.contiguous_bounds.upper, options.end_frame),
    };
    auto const max_increment = options.max_increment * options.increment_scale;

    u32 frame_index = 0;
    while (frame_index < out.size) {
        if (PlaybackEnded(playhead, options.end_frame)) break;

        auto const contiguous_end =
            frame_index +
            ContiguousFramesAvailable(playhead, max_increment, bounds, (u32)out.size - frame_index);
        if (contiguous_end != frame_index) {
            auto const layout = ContiguousTapLayoutFor(audio_data, playhead.inverse_data_lookup);
            auto const increments = options.increments.data;
            auto const increment_scale = options.increment_scale;
            auto frame_pos = playhead.frame_pos;

            // Several frames per iteration so that one vector holds a few frames' worth of Hermite
            // arithmetic. The positions are accumulated in the same order as the single-frame loop below.
            if (layout.channels == 2) {
                for (; frame_index + 2 <= contiguous_end; frame_index += 2) {
                    auto const frame_pos_0 = frame_pos;
                    auto const frame_pos_1 = frame_pos_0 + (increments[frame_index] * increment_scale);
                    frame_pos = frame_pos_1 + (increments[frame_index + 1] * increment_scale);
                    auto const frames =
                        InterpolateContiguousStereoFramePair(layout, frame_pos_0, frame_pos_1);
                    __builtin_memcpy_inline(out.data + frame_index, &frames, sizeof(frames));
                }
            } else {
                for (; frame_index + 4 <= contiguous_end; frame_index += 4) {
                    auto const frame_pos_0 = frame_pos;
                    auto const frame_pos_1 = frame_pos_0 + (increments[frame_index] * increment_scale);
                    auto const frame_pos_2 = frame_pos_1 + (increments[frame_index + 1] * increment_scale);
                    auto const frame_pos_3 = frame_pos_2 + (increments[frame_index + 2] * increment_scale);
                    frame_pos = frame_pos_3 + (increments[frame_index + 3] * increment_scale);
                    auto const frames = InterpolateContiguousMonoFrameQuad(layout,
                                                                           frame_pos_0,
                                                                           frame_pos_1,
                                                                           frame_pos_2,
                                                                           frame_pos_3);
                    f32x4 const frames_01 = __builtin_shufflevector(frames, frames, 0, 0, 1, 1);
                    f32x4 const frames_23 = __builtin_shufflevector(frames, frames, 2, 2, 3, 3);
                    __builtin_memcpy_inline(out.data + frame_index, &frames_01, sizeof(frames_01));
                    __builtin_memcpy_inline(out.data + frame_index + 2, &frames_23, sizeof(frames_23));
                }
            }

            for (; frame_index < contiguous_end; ++frame_index) {
                out.data[frame_index] = InterpolateContiguousFrame(layout, frame_pos);
                frame_pos += increments[frame_index] * increment_scale;
            }
            playhead.frame_pos = frame_pos;
        } else {
            out.data[frame_index] = GetSampleFrame(audio_data, playhead);
            general_frame_hook(frame_index, (PlayHead const&)playhead);
            IncrementPlaybackPos(playhead,
                                 options.increments.data[frame_index] * options.increment_scale,
                                 s.num_frames);
            ++frame_index;
        }
    }
    return frame_index;
}

enum class WaveformAudioSourceType : u8 { AudioData, Sine, WhiteNoise };

using WaveformAudioSource =
    TaggedUnion<WaveformAudioSourceType, TypeAndTag<AudioData const*, WaveformAudioSourceType::AudioData>>;

Span<u8>
CreateWaveformImage(WaveformAudioSource source, UiSize size, Allocator& a, ArenaAllocator& scratch_allocator);
