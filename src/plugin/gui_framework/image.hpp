// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stb_image.h>
#include <stb_image_resize2.h>

#include "foundation/foundation.hpp"

#include "gui_framework/graphics.hpp"

constexpr u16 k_rgba_channels = 4;

struct ImageBytes {
    usize NumPixels() const { return (usize)(size.width * size.height); }
    usize NumBytes() const { return NumPixels() * k_rgba_channels; }
    Span<u8> Bytes() const { return {rgba, NumBytes()}; }
    void Free(Allocator& a) const {
        if (rgba) a.Free(Span {rgba, NumBytes()});
    }
    ImageBytes Clone(Allocator& a) const {
        auto const num_bytes = NumBytes();
        auto new_rgba = a.AllocateExactSizeUninitialised<u8>(num_bytes).data;
        CopyMemory(new_rgba, rgba, num_bytes);
        return {.rgba = new_rgba, .size = size};
    }
    u8* rgba {};
    UiSize size {};
};

struct ImageF32 {
    usize NumPixels() const { return (usize)(size.width * size.height); }
    usize NumBytes() const { return NumPixels() * sizeof(f32x4); }
    Span<f32x4> rgba;
    UiSize size;
};

ErrorCodeOr<ImageBytes> DecodeImage(Span<u8 const> image_data, Allocator& allocator);

ErrorCodeOr<ImageBytes>
DecodeImageFromFile(String filename, ArenaAllocator& scratch_arena, Allocator& allocator);

Optional<ImageBytes> ResizeImage(ImageBytes image, u16 new_width, Allocator& allocator);

struct BlurredImageBackgroundOptions {
    f32 downscale_factor; // 0-1, 0.5 is half the size
    f32 brightness_scaling_exponent;
    f32 overlay_value; // 0-1, 0 is black, 1 is white
    f32 overlay_alpha; // 0-1
    f32 blur1_radius_percent; // 0-1
    f32 blur2_radius_percent; // 0-1
    f32 blur2_alpha; // 0-1, blur2 is layered on top of blur1
};

ImageBytes CreateBlurredLibraryBackground(ImageBytes original,
                                          Allocator& allocator,
                                          ArenaAllocator& scratch_arena,
                                          BlurredImageBackgroundOptions options);

graphics::ImageID CreateImageIdChecked(graphics::Renderer& renderer, ImageBytes const& px);

f32x2 GetMaxUVToMaintainAspectRatio(graphics::ImageID img, f32x2 container_size);
