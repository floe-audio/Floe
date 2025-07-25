// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/audio_utils.hpp"

inline bool IsSilent(f32x2 f, f32 silence_threshold = k_silence_amp_80) {
    return All(Abs(f) < silence_threshold);
}

PUBLIC Span<f32x2> ToStereoFramesSpan(f32* interleaved_stereo_samples, u32 num_frames) {
    ASSERT_HOT(IsAligned(interleaved_stereo_samples, alignof(f32x2)));
    return {(f32x2*)(void*)interleaved_stereo_samples, (usize)num_frames};
}

inline void CopyFramesToSeparateChannels(f32** stereo_channels_destination, Span<f32x2> frames) {
    for (auto const i : Range(frames.size))
        stereo_channels_destination[0][i] = frames[i].x;
    for (auto const i : Range(frames.size))
        stereo_channels_destination[1][i] = frames[i].y;
}

inline void CopyFramesToSeparateChannels(StaticSpan<f32*, 2> stereo_channels_destination,
                                         Span<f32x2> frames) {
    for (auto const i : Range(frames.size))
        stereo_channels_destination[0][i] = frames[i].x;
    for (auto const i : Range(frames.size))
        stereo_channels_destination[1][i] = frames[i].y;
}
