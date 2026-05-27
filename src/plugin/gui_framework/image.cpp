// Copyright 2024-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "image.hpp"

#include <stb_image.h>
#include <stb_image_resize2.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/common_errors.hpp"

#include "gui_framework/renderer.hpp"

static ErrorCodeOr<ImageBytes> DecodeJpgOrPng(Span<u8 const> image_data, Allocator& allocator) {
    if (!image_data.size) return ErrorCode(CommonError::InvalidFileFormat);

    // Always returns rgba because we specify k_rgba_channels as the output channels.
    int actual_number_channels;
    int width;
    int height;
    auto const rgba = stbi_load_from_memory(image_data.data,
                                            CheckedCast<int>(image_data.size),
                                            &width,
                                            &height,
                                            &actual_number_channels,
                                            k_rgba_channels);

    if (!rgba) return ErrorCode(CommonError::InvalidFileFormat);

    if (!width || !height) {
        stbi_image_free(rgba);
        return ErrorCode(CommonError::InvalidFileFormat);
    }

    // It's a bit silly, but stb_image doesn't let us allocate the image data in an arena allocator, so we
    // just copy it here.
    auto const num_bytes = CheckedCast<usize>(width) * CheckedCast<usize>(height) * k_rgba_channels;
    auto rgba_arena = allocator.AllocateExactSizeUninitialised<u8>(num_bytes);
    CopyMemory(rgba_arena.data, rgba, num_bytes);
    stbi_image_free(rgba);

    return ImageBytes {
        .rgba = rgba_arena.data,
        .size = {CheckedCast<u16>(width), CheckedCast<u16>(height)},
    };
}

ErrorCodeOr<ImageBytes> DecodeImage(Span<u8 const> image_data, Allocator& allocator) {
    return DecodeJpgOrPng(image_data, allocator);
}

ErrorCodeOr<ImageBytes>
DecodeImageFromFile(String filename, ArenaAllocator& scratch_arena, Allocator& allocator) {
    auto const file_data = TRY(ReadEntireFile(filename, scratch_arena));
    return DecodeImage(file_data.ToByteSpan(), allocator);
}

Optional<ImageBytes> ResizeImage(ImageBytes image, u16 new_width, Allocator& allocator) {
    ASSERT(image.size.width && image.size.height);

    UiSize const new_size = {
        new_width,
        CheckedCast<u16>((f32)image.size.height * ((f32)new_width / (f32)image.size.width)),
    };

    if (!new_size.width || !new_size.height) return k_nullopt;

    Stopwatch stopwatch;
    DEFER {
        LogDebug(ModuleName::Gui,
                 "Shrinking image {}x{} to {}x{} took {} ms",
                 image.size.width,
                 image.size.height,
                 new_size.width,
                 new_size.height,
                 stopwatch.MillisecondsElapsed());
    };

    ImageBytes result {
        .rgba =
            allocator.AllocateExactSizeUninitialised<u8>(new_size.width * new_size.height * k_rgba_channels)
                .data,
        .size = new_size,
    };

    stbir_resize_uint8_linear(image.rgba,
                              image.size.width,
                              image.size.height,
                              0,
                              result.rgba,
                              result.size.width,
                              result.size.height,
                              0,
                              STBIR_RGBA);

    return result;
}

struct BlurAxisArgs {
    f32x4 const* in_data;
    f32x4* out_data;
    usize data_size;
    u16 radius;
    u16 line_data_stride;
    u16 element_data_stride;
    u16 num_lines;
    u16 num_elements;
};

static inline void BlurAxis(BlurAxisArgs const args) {
    ASSERT(args.in_data);
    ASSERT(args.out_data);
    ASSERT(args.data_size);
    ASSERT(args.radius);
    ASSERT(args.line_data_stride);
    ASSERT(args.element_data_stride);
    ASSERT(args.num_lines);
    ASSERT(args.num_elements);

    u16 const radius_p1 = args.radius + 1;
    u16 const last_element_index = args.num_elements - 1;
    auto const rhs_edge_element_index = CheckedCast<u16>(args.num_elements - radius_p1);
    auto const box_size = (f32)(args.radius + radius_p1);
    for (auto const line_number : Range(args.num_lines)) {
        auto const line_data_offset = line_number * args.line_data_stride;

        auto data_index = [&](u16 element_index) ALWAYS_INLINE {
            return line_data_offset + (element_index * args.element_data_stride);
        };

        // Rather than calculate the average for every pixel, we can just keep a running average. For each
        // pixel we just add to the running average the next pixel in view, and subtract the pixel that went
        // out of view. This means the performance will not be worse for larger radii.
        f32x4 avg = 0;

        // calculate the initial average so that we can do a moving average from here onwards
        for (int element_index = -(int)args.radius; element_index < args.radius; ++element_index)
            avg += args.in_data[data_index((u16)Clamp<int>(element_index, 0, last_element_index))];

        auto write_ptr = args.out_data + data_index(0);

        // So as to avoid doing the Min/Max check for the edges, we break the loop into 3 sections, where
        // the middle section (probably the largest section) does not have to do bounds checks.

        auto write_checked = [&](u16 start, u16 end) {
            for (int element_index = start; element_index < end; ++element_index) {
                *write_ptr = Clamp01<f32x4>(avg / box_size);
                write_ptr += args.element_data_stride;

                auto const lhs = args.in_data[data_index((u16)Max<int>(element_index - args.radius, 0))];
                auto const rhs =
                    args.in_data[data_index((u16)Min<int>(element_index + args.radius, last_element_index))];
                avg += rhs - lhs;
            }
        };

        write_checked(0, args.radius);

        // write_unchecked:
        // We don't have to check the edge cases for this middle section - which works out much faster
        auto lhs_ptr = args.in_data + data_index(0);
        auto rhs_ptr = args.in_data + data_index(args.radius + radius_p1);
        for (u16 element_index = args.radius; element_index < rhs_edge_element_index; ++element_index) {
            *write_ptr = Clamp01<f32x4>(avg / box_size);
            write_ptr += args.element_data_stride;

            avg += *rhs_ptr - *lhs_ptr;
            lhs_ptr += args.element_data_stride;
            rhs_ptr += args.element_data_stride;
        }

        write_checked(rhs_edge_element_index, args.num_elements);
    }
}

static bool BoxBlur(ImageF32 in, f32x4* out, u16 radius) {
    radius = Min<u16>(radius, in.size.width / 2, in.size.height / 2);
    if (radius == 0) return false;

    Stopwatch stopwatch;
    DEFER {
        LogDebug(ModuleName::Gui,
                 "Box blur {}x{}, radius {} took {} ms",
                 in.size.width,
                 in.size.height,
                 radius,
                 stopwatch.MillisecondsElapsed());
    };

    // You can do a box blur by first blurring in one direction, and then in the other. This is quicker
    // because we only need to work in 1 dimension at a time, and the memory access is probably more
    // sequential and therefore more cache-friendly.

    BlurAxisArgs args {
        .in_data = in.rgba.data,
        .out_data = out,
        .data_size = (usize)(in.size.width * in.size.height),
        .radius = radius,
    };

    // vertical blur, a 'line' is a column
    args.num_lines = in.size.width;
    args.num_elements = in.size.height;
    args.line_data_stride = 1;
    args.element_data_stride = in.size.width;
    BlurAxis(args);

    args.in_data = out;

    // horizontal blur, a 'line' is a row
    args.num_lines = in.size.height;
    args.num_elements = in.size.width;
    args.line_data_stride = in.size.width;
    args.element_data_stride = 1;
    BlurAxis(args);

    return true;
}

static f32x4* CreateBlurredImage(Allocator& arena, ImageF32 original, u16 blur_radius) {
    auto const result = arena.AllocateExactSizeUninitialised<f32x4>(original.NumPixels()).data;
    if (!BoxBlur(original, result, blur_radius)) CopyMemory(result, original.rgba.data, original.NumBytes());
    return result;
}

static ImageF32 ImageBytesToImageF32(ImageBytes image, Allocator& arena) {
    auto const result = arena.AllocateExactSizeUninitialised<f32x4>(image.NumPixels());
    for (auto [pixel_index, pixel] : Enumerate(result)) {
        auto const bytes = LoadUnalignedToType<u8x4>(image.rgba + (pixel_index * k_rgba_channels));
        pixel = ConvertVector(bytes, f32x4) / 255.0f;
    }
    return {.rgba = result, .size = image.size};
}

static inline f32x4 MakeOpaque(f32x4 pixel) {
    pixel[3] = 1;
    return pixel;
}

static void WriteImageF32AsBytesNoAlpha(ImageF32 image, u8* out) {
    for (auto [pixel_index, pixel] : Enumerate(image.rgba)) {
        auto bytes = ConvertVector(MakeOpaque(pixel) * 255.0f, u8x4);
        StoreToUnaligned(out + (pixel_index * k_rgba_channels), bytes);
    }
}

static f32 CalculateBrightnessAverage(ImageF32 image) {
    auto const num_pixels = image.NumPixels();
    ASSERT(num_pixels);

    f32x4 brightness_sum {};
    for (auto const& pixel : image.rgba)
        brightness_sum += pixel;

    f32 brightness_average = 0;
    brightness_average += brightness_sum[0];
    brightness_average += brightness_sum[1];
    brightness_average += brightness_sum[2];
    brightness_average /= (f32)num_pixels * 3;

    ASSERT(brightness_average >= 0 && brightness_average <= 1);
    return brightness_average;
}

ImageBytes CreateBlurredLibraryBackground(ImageBytes original,
                                          Allocator& allocator,
                                          ArenaAllocator& scratch_arena,
                                          BlurredImageBackgroundOptions options) {
    ASSERT(options.downscale_factor > 0 && options.downscale_factor <= 1);
    ASSERT(options.brightness_scaling_exponent >= 0);
    ASSERT(options.overlay_value >= 0 && options.overlay_value <= 1);
    ASSERT(options.overlay_alpha >= 0 && options.overlay_alpha <= 1);
    ASSERT(options.blur2_alpha >= 0 && options.blur2_alpha <= 1);
    ASSERT(options.blur1_radius_percent >= 0 && options.blur1_radius_percent <= 1);
    ASSERT(options.blur2_radius_percent >= 0 && options.blur2_radius_percent <= 1);
    ASSERT(original.size.width);
    ASSERT(original.size.height);

    Stopwatch stopwatch;
    DEFER {
        LogDebug(ModuleName::Gui, "Blurred image generation took {} ms", stopwatch.MillisecondsElapsed());
    };

    // Shrink the image down for better speed. We are about to blur it, we don't need detail.
    auto const shrunk_width = Max(CheckedCast<u16>(original.size.width * options.downscale_factor),
                                  Min((u16)16, original.size.width));
    auto const result =
        ResizeImage(original, shrunk_width, allocator).OrElse([&] { return original.Clone(allocator); });

    // For ease-of-use and performance, we convert the image to f32x4 format
    auto const pixels = ImageBytesToImageF32(result, scratch_arena);

    // Make the blurred image more of a mid-brightness, instead of very light or very dark. We adjust the
    // brightness relative to the average brightness of the image.
    {
        auto const exponent = MapFrom01(1 - CalculateBrightnessAverage(pixels),
                                        -options.brightness_scaling_exponent,
                                        options.brightness_scaling_exponent);
        auto const multiplier = MakeOpaque(Pow(2.0f, exponent));

        for (auto& pixel : pixels.rgba)
            pixel = Clamp01(pixel * multiplier);
    }

    // Blend on top a dark colour to achieve a more consistently dark background.
    {
        f32x4 const overlay_pixel = options.overlay_value;
        f32x4 const overlay_alpha = options.overlay_alpha;

        for (auto& pixel : pixels.rgba)
            pixel = LinearInterpolate(overlay_alpha, pixel, overlay_pixel);
    }

    // Do a pair of blurs with different radii, and blend them together. 2 is enough to get a nice effect with
    // minimal performance cost.
    {
        auto const blur1 = CreateBlurredImage(scratch_arena,
                                              pixels,
                                              (u16)(options.blur1_radius_percent * pixels.size.width));
        auto const blur2 = CreateBlurredImage(scratch_arena,
                                              pixels,
                                              (u16)(options.blur2_radius_percent * pixels.size.width));

        for (auto const pixel_index : Range(pixels.NumPixels()))
            pixels.rgba[pixel_index] =
                LinearInterpolate(options.blur2_alpha, blur1[pixel_index], blur2[pixel_index]);
    }

    // Convert the f32x4 back to bytes
    WriteImageF32AsBytesNoAlpha(pixels, result.rgba);

    return result;
}

ImageID CreateImageIdChecked(Renderer& renderer, ImageBytes const& px) {
    ASSERT(px.rgba);
    auto const outcome = renderer.CreateImageID(px.rgba, px.size, k_rgba_channels);
    if (outcome.HasError()) {
        LogError(ModuleName::Gui,
                 "Failed to create a texture (size {}x{}): {}",
                 px.size.width,
                 px.size.height,
                 outcome.Error());
        return {};
    }
    return outcome.Value();
}

f32x2 GetMaxUVToMaintainAspectRatio(ImageID img, f32x2 container_size) {
    if (!img.size.width || !img.size.height) return {1, 1};
    auto const img_w = (f32)img.size.width;
    auto const img_h = (f32)img.size.height;
    auto const window_ratio = container_size.x / container_size.y;
    auto const image_ratio = img_w / img_h;

    f32x2 uv {1, 1};
    if (image_ratio > window_ratio)
        uv.x = window_ratio / image_ratio;
    else
        uv.y = image_ratio / window_ratio;
    return uv;
}

// Tests
// ============================================================

#include "tests/framework.hpp"

TEST_CASE(TestBlurZeroHeight) {
    ArenaAllocator result_arena {PageAllocator::Instance()};

    constexpr u16 k_width = 100;
    constexpr u16 k_height = 1;
    auto const num_pixels = k_width * k_height;
    auto rgba_data = tester.scratch_arena.AllocateExactSizeUninitialised<u8>(num_pixels * k_rgba_channels);

    for (auto const [i, byte] : Enumerate(rgba_data))
        byte = (u8)(i % 256);

    ImageBytes const original {.rgba = rgba_data.data, .size = {k_width, k_height}};

    auto const resized = ResizeImage(original, 50, result_arena);
    CHECK(!resized.HasValue());

    BlurredImageBackgroundOptions const options {
        .downscale_factor = 0.5f,
        .brightness_scaling_exponent = 1.0f,
        .overlay_value = 0.2f,
        .overlay_alpha = 0.5f,
        .blur1_radius_percent = 0.1f,
        .blur2_radius_percent = 0.2f,
        .blur2_alpha = 0.5f,
    };

    auto const blurred =
        CreateBlurredLibraryBackground(original, result_arena, tester.scratch_arena, options);
    CHECK(blurred.size.width > 0);
    CHECK(blurred.size.height > 0);

    return k_success;
}

TEST_CASE(TestBlurTallNarrowImage) {
    ArenaAllocator result_arena {PageAllocator::Instance()};

    constexpr u16 k_width = 2;
    constexpr u16 k_height = 100;
    auto const num_pixels = k_width * k_height;
    auto rgba_data = tester.scratch_arena.AllocateExactSizeUninitialised<u8>(num_pixels * k_rgba_channels);

    for (auto const [i, byte] : Enumerate(rgba_data))
        byte = (u8)(i % 256);

    ImageBytes const original {.rgba = rgba_data.data, .size = {k_width, k_height}};
    auto const resized = ResizeImage(original, 1, result_arena);

    CHECK(resized.HasValue());
    if (resized) {
        CHECK(resized->size.width > 0);
        CHECK(resized->size.height > 0);
    }

    return k_success;
}

TEST_CASE(TestBlurExtremeAspectRatio) {
    ArenaAllocator result_arena {PageAllocator::Instance()};

    constexpr u16 k_width = 20;
    constexpr u16 k_height = 1;
    auto const num_pixels = k_width * k_height;
    auto rgba_data = tester.scratch_arena.AllocateExactSizeUninitialised<u8>(num_pixels * k_rgba_channels);

    for (auto const [i, byte] : Enumerate(rgba_data))
        byte = (u8)((i * 17 + 42) % 256);

    ImageBytes const original {.rgba = rgba_data.data, .size = {k_width, k_height}};

    BlurredImageBackgroundOptions const options {
        .downscale_factor = 0.5f,
        .brightness_scaling_exponent = 1.0f,
        .overlay_value = 0.2f,
        .overlay_alpha = 0.5f,
        .blur1_radius_percent = 0.15f,
        .blur2_radius_percent = 0.3f,
        .blur2_alpha = 0.5f,
    };

    auto const blurred =
        CreateBlurredLibraryBackground(original, result_arena, tester.scratch_arena, options);
    CHECK(blurred.size.width > 0);
    CHECK(blurred.size.height > 0);

    return k_success;
}

TEST_CASE(TestBlurMaxRadius) {
    ArenaAllocator result_arena {PageAllocator::Instance()};

    constexpr u16 k_width = 64;
    constexpr u16 k_height = 64;
    auto const num_pixels = k_width * k_height;
    auto rgba_data = tester.scratch_arena.AllocateExactSizeUninitialised<u8>(num_pixels * k_rgba_channels);

    for (auto const [i, byte] : Enumerate(rgba_data))
        byte = (u8)((i * 7) % 256);

    ImageBytes const original {.rgba = rgba_data.data, .size = {k_width, k_height}};

    BlurredImageBackgroundOptions const options {
        .downscale_factor = 1.0f,
        .brightness_scaling_exponent = 1.0f,
        .overlay_value = 0.5f,
        .overlay_alpha = 0.5f,
        .blur1_radius_percent = 0.5f,
        .blur2_radius_percent = 0.5f,
        .blur2_alpha = 0.5f,
    };

    auto const blurred =
        CreateBlurredLibraryBackground(original, result_arena, tester.scratch_arena, options);
    CHECK(blurred.size.width == k_width);
    CHECK(blurred.size.height == k_height);

    return k_success;
}

TEST_CASE(TestBlurPowerOfTwo) {
    ArenaAllocator result_arena {PageAllocator::Instance()};

    for (auto const size : Array<u16, 5> {2, 4, 8, 16, 32}) {
        auto const num_pixels = (usize)size * size;
        auto rgba_data =
            tester.scratch_arena.AllocateExactSizeUninitialised<u8>(num_pixels * k_rgba_channels);

        for (auto const [i, byte] : Enumerate(rgba_data))
            byte = (u8)(i % 256);

        ImageBytes const original {.rgba = rgba_data.data, .size = {size, size}};

        BlurredImageBackgroundOptions const options {
            .downscale_factor = 0.7f,
            .brightness_scaling_exponent = 1.0f,
            .overlay_value = 0.2f,
            .overlay_alpha = 0.5f,
            .blur1_radius_percent = 0.2f,
            .blur2_radius_percent = 0.3f,
            .blur2_alpha = 0.5f,
        };

        auto const blurred =
            CreateBlurredLibraryBackground(original, result_arena, tester.scratch_arena, options);
        CHECK(blurred.size.width > 0);
        CHECK(blurred.size.height > 0);
    }

    return k_success;
}

TEST_CASE(TestBlurRealisticSizes) {
    struct TestSize {
        u16 w, h;
    };
    for (auto const size : Array {
             TestSize {800, 600},
             TestSize {1920, 1080},
             TestSize {1024, 1024},
             TestSize {640, 480},
             TestSize {3840, 2160},
             TestSize {1280, 720},
         }) {
        ArenaAllocator result_arena {PageAllocator::Instance()};

        auto const num_pixels = (usize)size.w * size.h;
        auto rgba_data =
            tester.scratch_arena.AllocateExactSizeUninitialised<u8>(num_pixels * k_rgba_channels);

        for (auto const [i, byte] : Enumerate(rgba_data))
            byte = (u8)((i * 13 + 7) % 256);

        ImageBytes const original {.rgba = rgba_data.data, .size = {size.w, size.h}};

        BlurredImageBackgroundOptions const options {
            .downscale_factor = 0.3f,
            .brightness_scaling_exponent = 1.5f,
            .overlay_value = 0.2f,
            .overlay_alpha = 0.6f,
            .blur1_radius_percent = 0.15f,
            .blur2_radius_percent = 0.25f,
            .blur2_alpha = 0.4f,
        };

        auto const blurred =
            CreateBlurredLibraryBackground(original, result_arena, tester.scratch_arena, options);
        CHECK(blurred.size.width > 0);
        CHECK(blurred.size.height > 0);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterImageTests) {
    REGISTER_TEST(TestBlurZeroHeight);
    REGISTER_TEST(TestBlurTallNarrowImage);
    REGISTER_TEST(TestBlurExtremeAspectRatio);
    REGISTER_TEST(TestBlurMaxRadius);
    REGISTER_TEST(TestBlurPowerOfTwo);
    REGISTER_TEST(TestBlurRealisticSizes);
}
