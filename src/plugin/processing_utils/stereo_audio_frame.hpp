// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/audio_utils.hpp"

struct StereoAudioFrame {
    constexpr StereoAudioFrame() : l(0), r(0) {}
    constexpr StereoAudioFrame(f32 _l, f32 _r) : l(_l), r(_r) {}

    constexpr StereoAudioFrame(f32* interleaved_stereo_samples, u32 index) {
        auto const interleaved_index = index * 2;
        l = interleaved_stereo_samples[interleaved_index + 0];
        r = interleaved_stereo_samples[interleaved_index + 1];
    }

    constexpr StereoAudioFrame(f32** stereo_channels, u32 index) {
        l = stereo_channels[0][index];
        r = stereo_channels[1][index];
    }

    constexpr StereoAudioFrame(StaticSpan<f32*, 2> stereo_channels, u32 index) {
        l = stereo_channels[0][index];
        r = stereo_channels[1][index];
    }

    constexpr void Store(f32* interleaved_stereo_samples, u32 index) const {
        auto const interleaved_index = index * 2;
        interleaved_stereo_samples[interleaved_index + 0] = l;
        interleaved_stereo_samples[interleaved_index + 1] = r;
    }

    constexpr void Store(f32** stereo_channels, u32 index) const {
        stereo_channels[0][index] = l;
        stereo_channels[1][index] = r;
    }

    constexpr bool HasValueAboveThreshold(f32 amp) const { return Max(Abs(l), Abs(r)) > amp; }
    constexpr bool IsSilent(f32 silence_threshold = k_silence_amp_80) const {
        return !HasValueAboveThreshold(silence_threshold);
    }

    constexpr f32x2 ToF32x2() const { return LoadUnalignedToType<f32x2>(channel); }
    static constexpr StereoAudioFrame FromF32x2(f32x2 v) {
        StereoAudioFrame f;
        StoreToUnaligned<f32x2>(f.channel, v);
        return f;
    }

    union {
        struct {
            f32 l, r;
        };
        f32 channel[2];
    };
};

constexpr StereoAudioFrame Abs(StereoAudioFrame const f) { return {Abs(f.l), Abs(f.r)}; }

constexpr StereoAudioFrame operator*(StereoAudioFrame const& lhs, f32 const rhs) {
    return {lhs.l * rhs, lhs.r * rhs};
}

constexpr StereoAudioFrame operator*(f32 const a, StereoAudioFrame const& b) { return {b.l * a, b.r * a}; }

constexpr StereoAudioFrame operator/(StereoAudioFrame const& lhs, f32 const rhs) {
    return {lhs.l / rhs, lhs.r / rhs};
}

constexpr StereoAudioFrame operator+(StereoAudioFrame const& lhs, f32 const rhs) {
    return {lhs.l + rhs, lhs.r + rhs};
}

constexpr StereoAudioFrame operator+(StereoAudioFrame const& lhs, StereoAudioFrame const& rhs) {
    return {lhs.l + rhs.l, lhs.r + rhs.r};
}

constexpr StereoAudioFrame operator-(StereoAudioFrame const& lhs, StereoAudioFrame const& rhs) {
    return {lhs.l - rhs.l, lhs.r - rhs.r};
}

constexpr StereoAudioFrame& operator*=(StereoAudioFrame& lhs, f32 const rhs) {
    lhs.l *= rhs;
    lhs.r *= rhs;
    return lhs;
}

constexpr StereoAudioFrame& operator/=(StereoAudioFrame& lhs, f32 const rhs) {
    lhs.l /= rhs;
    lhs.r /= rhs;
    return lhs;
}

constexpr StereoAudioFrame& operator+=(StereoAudioFrame& lhs, StereoAudioFrame const& rhs) {
    lhs.l += rhs.l;
    lhs.r += rhs.r;
    return lhs;
}

static inline StereoAudioFrame
Clamp(StereoAudioFrame const& f, StereoAudioFrame const& lo, StereoAudioFrame const& hi) {
    return {Clamp(f.l, lo.l, hi.l), Clamp(f.r, lo.r, hi.r)};
}

PUBLIC Span<StereoAudioFrame> ToStereoFramesSpan(f32* interleaved_stereo_samples, u32 num_frames) {
    return {(StereoAudioFrame*)interleaved_stereo_samples, (usize)num_frames};
}

inline void CopyFramesToSeparateChannels(f32** stereo_channels_destination, Span<StereoAudioFrame> frames) {
    for (auto const i : Range(frames.size))
        stereo_channels_destination[0][i] = frames[i].l;
    for (auto const i : Range(frames.size))
        stereo_channels_destination[1][i] = frames[i].r;
}

inline void CopyFramesToSeparateChannels(StaticSpan<f32*, 2> stereo_channels_destination,
                                         Span<StereoAudioFrame> frames) {
    for (auto const i : Range(frames.size))
        stereo_channels_destination[0][i] = frames[i].l;
    for (auto const i : Range(frames.size))
        stereo_channels_destination[1][i] = frames[i].r;
}
