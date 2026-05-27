// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

// http://www.musicdsp.org/show_archive_comment.php?ArchiveID=256
// public domain
// 'width' is the stretch factor of the stereo field:
// width < 1: decrease in stereo width
// width = 1: no change
// width > 1: increase in stereo width
// width = 0: mono

inline void DoStereoWiden(f32 width, f32 in_left, f32 in_right, f32& out_left, f32& out_right) {
    auto const coef_s = width * 0.5f;
    auto const m = (in_left + in_right) * 0.5f;
    auto const s = (in_right - in_left) * coef_s;
    out_left = m - s;
    out_right = m + s;
}

inline f32x2 DoStereoWiden(f32 width, f32x2 in) {
    auto const swapped = __builtin_shufflevector(in, in, 1, 0);
    auto const m_pair = (in + swapped) * 0.5f;
    auto const s_signed = (swapped - in) * (width * 0.5f);
    return m_pair - s_signed;
}

ALWAYS_INLINE auto QuadraticPowerFade(ScalarOrVectorFloat auto x) {
    using T = UnderlyingTypeOfVecOrScalar<decltype(x)>;
    return x * (T(1.8284271f) - T(0.8284271f) * x);
}

inline f32x2 DoStereoWidenConstantPower(f32 width, f32x2 in) {
    constexpr f32 k_inv_sqrt_2 = 0.70710678f;
    auto const half_w = width * 0.5f;
    auto const scales = QuadraticPowerFade(f32x2 {1.0f - half_w, half_w}) * k_inv_sqrt_2;
    auto const m = (in.x + in.y) * scales.x;
    auto const s = (in.y - in.x) * scales.y;
    return f32x2 {m - s, m + s};
}

inline f32x4 DoStereoWidenConstantPower2(f32 width, f32x4 in_two_frames) {
    constexpr f32 k_inv_sqrt_2 = 0.70710678f;
    auto const half_w = width * 0.5f;
    auto const scales = QuadraticPowerFade(f32x2 {1.0f - half_w, half_w}) * k_inv_sqrt_2;
    auto const m_scale = scales.x;
    auto const s_scale = scales.y;

    auto const swapped = __builtin_shufflevector(in_two_frames, in_two_frames, 1, 0, 3, 2);
    auto const m_term = (in_two_frames + swapped) * m_scale;
    auto const s_signed = (swapped - in_two_frames) * s_scale;
    return m_term - s_signed;
}
