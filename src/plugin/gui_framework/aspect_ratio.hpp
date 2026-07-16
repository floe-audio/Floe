// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

constexpr UiSize SizeWithAspectRatio(u16 target_width, UiSize aspect_ratio) {
    u32 const low_index = target_width / aspect_ratio.width;
    u32 const high_index = low_index + 1;
    u32 const low_width = aspect_ratio.width * low_index;
    u32 const high_width = aspect_ratio.width * high_index;

    if (high_width > LargestRepresentableValue<u16>() ||
        (target_width - low_width) < (high_width - target_width))
        return {(u16)low_width, (u16)(low_index * aspect_ratio.height)};
    else
        return {(u16)high_width, (u16)(high_index * aspect_ratio.height)};
}

static_assert(SizeWithAspectRatio(65530, {10, 7}).width == 65530);

constexpr auto GreatestCommonDivisor(auto a, auto b) {
    ASSERT(a >= 0);
    ASSERT(b >= 0);
    while (b != 0) {
        auto const t = b;
        b = a % b;
        a = t;
    }
    return a;
}

constexpr UiSize SimplifyAspectRatio(UiSize aspect_ratio) {
    auto const gcd = GreatestCommonDivisor(aspect_ratio.width, aspect_ratio.height);
    return {(u16)(aspect_ratio.width / gcd), (u16)(aspect_ratio.height / gcd)};
}

constexpr Optional<UiSize32> NearestAspectRatioSizeInsideSize32(UiSize32 size, UiSize aspect_ratio) {
    aspect_ratio = SimplifyAspectRatio(aspect_ratio);

    if (aspect_ratio.width == 0 || aspect_ratio.height == 0) return k_nullopt;
    if (aspect_ratio.width > size.width || aspect_ratio.height > size.height) return k_nullopt;

    u32 const low_index = size.width / aspect_ratio.width;
    u32 const low_width = aspect_ratio.width * low_index;
    auto const height_by_width = low_index * aspect_ratio.height;

    if (height_by_width <= size.height) {
        ASSERT(height_by_width <= LargestRepresentableValue<u32>());
        return UiSize32 {low_width, height_by_width};
    } else {
        u32 const height_low_index = size.height / aspect_ratio.height;
        u32 const low_height = aspect_ratio.height * height_low_index;
        auto const width_by_height = height_low_index * aspect_ratio.width;
        ASSERT(width_by_height <= size.width);
        return UiSize32 {width_by_height, low_height};
    }
}

constexpr Optional<UiSize> NearestAspectRatioSizeInsideSize(UiSize size, UiSize aspect_ratio) {
    auto const result = NearestAspectRatioSizeInsideSize32(size, aspect_ratio);
    if (!result) return k_nullopt;
    if (result->width > LargestRepresentableValue<u16>() || result->height > LargestRepresentableValue<u16>())
        return k_nullopt;
    return UiSize {(u16)result->width, (u16)result->height};
}

constexpr bool IsAspectRatio(UiSize size, UiSize aspect_ratio) {
    auto const simplified_size = SimplifyAspectRatio(size);
    auto const simplified_aspect_ratio = SimplifyAspectRatio(aspect_ratio);
    return simplified_size == simplified_aspect_ratio;
}

constexpr f32 HeightFromWidth(f32 width, UiSize aspect_ratio) {
    return width * (f32)aspect_ratio.height / (f32)aspect_ratio.width;
}
