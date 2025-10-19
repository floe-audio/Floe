// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_library_images.hpp"

#include "build_resources/embedded_files.h"
#include "engine/engine.hpp"
#include "gui_framework/image.hpp"
#include "gui_framework/style.hpp"

enum class LibraryImageType { Icon, Background };

static String FilenameForLibraryImageType(LibraryImageType type) {
    switch (type) {
        case LibraryImageType::Icon: return "icon.png";
        case LibraryImageType::Background: return "background.jpg";
    }
    PanicIfReached();
    return {};
}

static Optional<sample_lib::LibraryPath> LibraryImagePath(sample_lib::Library const& lib,
                                                          LibraryImageType type) {
    switch (type) {
        case LibraryImageType::Icon: return lib.icon_image_path;
        case LibraryImageType::Background: return lib.background_image_path;
    }
    PanicIfReached();
    return {};
}

static Optional<ImageBytes> ImagePixelsFromLibrary(sample_lib::LibraryIdRef const& lib_id,
                                                   LibraryImageType type,
                                                   sample_lib_server::Server& server,
                                                   ArenaAllocator& scratch_arena,
                                                   Allocator& result_allocator) {
    auto lib = sample_lib_server::FindLibraryRetained(server, lib_id);
    DEFER { lib.Release(); };
    if (!lib) return {};

    auto const filename = FilenameForLibraryImageType(type);

    if (lib->file_format_specifics.tag == sample_lib::FileFormat::Mdata) {
        // Back in the Mirage days, some libraries didn't embed their own images, but instead got them from a
        // shared pool. We replicate that behaviour here.
        auto mirage_compat_lib =
            sample_lib_server::FindLibraryRetained(server, sample_lib::k_mirage_compat_library_id);
        DEFER { mirage_compat_lib.Release(); };

        if (mirage_compat_lib) {
            if (auto const dir = path::Directory(mirage_compat_lib->path); dir) {
                String const library_subdir = lib->name == "Wraith Demo" ? "Wraith" : lib->name;
                auto const path =
                    path::Join(scratch_arena, Array {*dir, "Images"_s, library_subdir, filename});
                auto outcome = DecodeImageFromFile(path, scratch_arena, result_allocator);
                if (outcome.HasValue()) return outcome.ReleaseValue();
            }
        }
    }

    auto const path_in_lib = LibraryImagePath(*lib, type);

    auto const err = [&](String middle, Optional<ErrorCode> error) -> Optional<ImageBytes> {
        Log(ModuleName::Gui, LogLevel::Warning, "{} {} {}, code: {}", lib->name, middle, filename, error);
        return k_nullopt;
    };

    if (!path_in_lib) return err("does not have", k_nullopt);

    auto reader = TRY_OR(lib->create_file_reader(*lib, *path_in_lib), return err("error opening", error));

    auto const file_data = TRY_OR(reader.ReadOrFetchAll(scratch_arena), return err("error reading", error));

    auto pixels = TRY_OR(DecodeImage(file_data, result_allocator), return err("error decoding", error));

    ASSERT(pixels.size.width && pixels.size.height, "ImageBytes cannot be empty");

    return pixels;
}

inline Allocator& ImageBytesAllocator() { return PageAllocator::Instance(); }

static void FreeLoadingBytes(Future<Optional<ImageBytes>>& future) {
    if (auto const bytes_opt_ptr = future.ShutdownAndRelease(10000u)) {
        if (auto const bytes_optional = *bytes_opt_ptr) bytes_optional->Free(ImageBytesAllocator());
    }
}

static void FreeLoadingBytes(Future<Optional<LibraryImages::LoadingBackgrounds>>& future) {
    if (auto const bgs_ptr = future.ShutdownAndRelease(10000u)) {
        if (auto const bgs = *bgs_ptr) {
            if (bgs->background) bgs->background->Free(ImageBytesAllocator());
            if (bgs->blurred_background) bgs->blurred_background->Free(ImageBytesAllocator());
        }
    }
}

static void AsyncLoadIcon(sample_lib::LibraryIdRef const& lib_id_ref,
                          imgui::Context const& imgui,
                          Future<Optional<ImageBytes>>& result,
                          sample_lib_server::Server& server,
                          ThreadPool& thread_pool) {
    thread_pool.Async(
        result,
        [lib_id = sample_lib::LibraryId(lib_id_ref),
         &request_gui_update = imgui.frame_input.request_update,
         &server,
         desired_icon_size = CheckedCast<u16>(Ceil(imgui.VwToPixels(style::k_library_icon_standard_size)) *
                                              2)]() -> Optional<ImageBytes> {
            DEFER { request_gui_update.Store(true, StoreMemoryOrder::Release); };

            ArenaAllocator scratch_arena {PageAllocator::Instance()};
            auto pixels =
                ImagePixelsFromLibrary(lib_id, LibraryImageType::Icon, server, scratch_arena, scratch_arena);
            if (!pixels) return k_nullopt;
            auto const result = ShrinkImageIfNeeded(*pixels,
                                                    desired_icon_size,
                                                    desired_icon_size,
                                                    ImageBytesAllocator(),
                                                    true);
            request_gui_update.Store(true, StoreMemoryOrder::Release);
            return result;
        },
        []() {
            // no cleanup
        });
}

static void AsyncLoadBackgrounds(sample_lib::LibraryIdRef const& lib_id_ref,
                                 imgui::Context const& imgui,
                                 Future<Optional<LibraryImages::LoadingBackgrounds>>& result,
                                 bool reload_background,
                                 bool reload_blurred_background,
                                 sample_lib_server::Server& server,
                                 ThreadPool& thread_pool) {
    BlurredImageBackgroundOptions const blur_options {
        .downscale_factor = Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringDownscaleFactor) / 100.0f),
        .brightness_scaling_exponent =
            LiveSize(imgui, UiSizeId::BackgroundBlurringBrightnessExponent) / 100.0f,
        .overlay_value = Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringOverlayColour) / 100.0f),
        .overlay_alpha = Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringOverlayIntensity) / 100.0f),
        .blur1_radius_percent = LiveSize(imgui, UiSizeId::BackgroundBlurringBlur1Radius) / 100,
        .blur2_radius_percent = LiveSize(imgui, UiSizeId::BackgroundBlurringBlur2Radius) / 100,
        .blur2_alpha = Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringBlur2Alpha) / 100.0f),
    };

    thread_pool.Async(
        result,
        [lib_id = sample_lib::LibraryId(lib_id_ref),
         reload_background,
         reload_blurred_background,
         &request_gui_update = imgui.frame_input.request_update,
         blur_options,
         &server,
         window_width =
             imgui.frame_input.window_size.width]() -> Optional<LibraryImages::LoadingBackgrounds> {
            DEFER { request_gui_update.Store(true, StoreMemoryOrder::Release); };

            ArenaAllocator scratch_arena {PageAllocator::Instance()};

            Optional<ImageBytes> pixels;
            if (lib_id == k_default_background_lib_id) {
                auto const image_data = EmbeddedDefaultBackground();
                pixels = DecodeImage({image_data.data, image_data.size}, scratch_arena).Value();
            } else {
                pixels = ImagePixelsFromLibrary(lib_id,
                                                LibraryImageType::Background,
                                                server,
                                                scratch_arena,
                                                scratch_arena);
            }

            if (!pixels) return k_nullopt;

            LibraryImages::LoadingBackgrounds result {};

            auto const scaled_width = CheckedCast<u16>(window_width * 1.3f);
            ASSERT(scaled_width);

            // If the image is quite a lot larger than we need, resize it down to avoid storing a huge
            // image on the GPU
            auto const scaled_background =
                ShrinkImageIfNeeded(*pixels, scaled_width, window_width, ImageBytesAllocator(), true);

            if (reload_background) result.background = scaled_background;

            if (reload_blurred_background)
                result.blurred_background = CreateBlurredLibraryBackground(scaled_background,
                                                                           ImageBytesAllocator(),
                                                                           scratch_arena,
                                                                           blur_options);
            return result;
        },
        []() {
            // no cleanup
        });
}

constexpr auto k_background_type_bits =
    Array {ToInt(LibraryImages::ImageType::Background), ToInt(LibraryImages::ImageType::BlurredBackground)};

LibraryImages GetLibraryImages(LibraryImagesTable& table,
                               imgui::Context& imgui,
                               sample_lib::LibraryIdRef const& lib_id,
                               sample_lib_server::Server& server,
                               LibraryImagesTypes needed_types) {
    auto e = table.table.FindOrInsertGrowIfNeeded(table.arena, lib_id, {});
    auto& images = e.element.data;

    if (e.inserted) {
        images.loading_icon = table.arena.New<Future<Optional<ImageBytes>>>();
        images.loading_backgrounds = table.arena.New<Future<Optional<LibraryImages::LoadingBackgrounds>>>();
        images.needs_reload.SetAll();
    }

    if ((needed_types & LibraryImagesTypes::Icon) &&
        images.needs_reload.Get(ToInt(LibraryImages::ImageType::Icon))) {
        FreeLoadingBytes(*images.loading_icon);
        AsyncLoadIcon(lib_id, imgui, *images.loading_icon, server, server.thread_pool);
        images.needs_reload.Clear(ToInt(LibraryImages::ImageType::Icon));
    }

    if ((needed_types & LibraryImagesTypes::Backgrounds) &&
        images.needs_reload.AnySetInSpan(k_background_type_bits)) {
        FreeLoadingBytes(*images.loading_backgrounds);
        AsyncLoadBackgrounds(lib_id,
                             imgui,
                             *images.loading_backgrounds,
                             images.needs_reload.Get(ToInt(LibraryImages::ImageType::Background)),
                             images.needs_reload.Get(ToInt(LibraryImages::ImageType::BlurredBackground)),
                             server,
                             server.thread_pool);
        images.needs_reload.ClearBits(k_background_type_bits);
    }

    return images;
}

void InvalidateLibraryImages(LibraryImagesTable& table,
                             sample_lib::LibraryIdRef library_id,
                             graphics::DrawContext& ctx) {
    ASSERT(g_is_logical_main_thread);
    if (auto imgs = table.table.Find(library_id)) {
        imgs->icon_missing = false;
        imgs->background_missing = false;
        if (imgs->icon) ctx.DestroyImageID(*imgs->icon);
        if (imgs->background) ctx.DestroyImageID(*imgs->background);
        if (imgs->blurred_background) ctx.DestroyImageID(*imgs->blurred_background);
    }
}

void Shutdown(LibraryImagesTable& table) {
    for (auto [_, imgs, _] : table.table) {
        FreeLoadingBytes(*imgs.loading_icon);
        FreeLoadingBytes(*imgs.loading_backgrounds);
    }
}

void BeginFrame(LibraryImagesTable& table, imgui::Context& imgui) {
    for (auto [_, imgs, _] : table.table) {
        if (auto const result = imgs.loading_icon->TryReleaseResult()) {
            auto const icon_pixels = *result;
            if (icon_pixels) {
                imgs.icon = CreateImageIdChecked(*imgui.frame_input.graphics_ctx, *icon_pixels);
                icon_pixels->Free(ImageBytesAllocator());
            } else
                imgs.icon_missing = true;
        }

        if (auto const result = imgs.loading_backgrounds->TryReleaseResult()) {
            auto const backgrounds = *result;
            if (backgrounds) {
                if (backgrounds->background) {
                    imgs.background =
                        CreateImageIdChecked(*imgui.frame_input.graphics_ctx, *backgrounds->background);
                    backgrounds->background->Free(ImageBytesAllocator());
                }
                if (backgrounds->blurred_background) {
                    imgs.blurred_background = CreateImageIdChecked(*imgui.frame_input.graphics_ctx,
                                                                   *backgrounds->blurred_background);
                    backgrounds->blurred_background->Free(ImageBytesAllocator());
                }
            } else {
                imgs.background_missing = true;
            }
        }

        // Check if we need to reload any images. We don't actually do the loading here because we want to
        // defer it to the point where we know that the images are actually needed.
        {
            imgs.needs_reload.ClearAll();
            auto& graphics = *imgui.frame_input.graphics_ctx;

            if (!graphics.ImageIdIsValid(imgs.icon) && !imgs.icon_missing && imgs.loading_icon->IsInactive())
                imgs.needs_reload.Set(ToInt(LibraryImages::ImageType::Icon));

            if (!imgs.background_missing && imgs.loading_backgrounds->IsInactive()) {
                if (!graphics.ImageIdIsValid(imgs.background))
                    imgs.needs_reload.Set(ToInt(LibraryImages::ImageType::Background));
                if (!graphics.ImageIdIsValid(imgs.blurred_background))
                    imgs.needs_reload.Set(ToInt(LibraryImages::ImageType::BlurredBackground));
            }
        }
    }
}
