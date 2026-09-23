// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/core/gui_library_images.hpp"

#include "build_resources/embedded_files.h"
#include "engine/engine.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui_framework/gui_frame.hpp"
#include "gui_framework/image.hpp"

enum class LibraryImageType : u8 { Icon, Background };

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

using ImagePixelsOrFailure = ValueOrError<ImageBytes, LibraryImages::LoadFailure>;

static ImagePixelsOrFailure ImagePixelsFromLibrary(sample_lib::LibraryId lib_id,
                                                   LibraryImageType type,
                                                   sample_lib_server::Server& server,
                                                   ArenaAllocator& scratch_arena,
                                                   Allocator& result_allocator,
                                                   bool log_failure) {
    auto const filename = FilenameForLibraryImageType(type);

    auto lib = sample_lib_server::FindLibraryRetained(server, lib_id);
    DEFER { lib.Release(); };
    if (!lib) {
        if (log_failure)
            Log(ModuleName::Gui,
                LogLevel::Warning,
                "{} not found when loading {}",
                sample_lib::LookupLibraryIdString(lib_id).ValueOr("Unknown library"_s),
                filename);
        return LibraryImages::LoadFailure::Unavailable;
    }

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

    auto const err = [&](String middle,
                         Optional<ErrorCode> error,
                         LibraryImages::LoadFailure failure) -> ImagePixelsOrFailure {
        if (log_failure)
            Log(ModuleName::Gui, LogLevel::Warning, "{} {} {}, code: {}", lib->name, middle, filename, error);
        return failure;
    };

    if (!path_in_lib) return err("does not have", k_nullopt, LibraryImages::LoadFailure::Missing);

    auto reader = TRY_OR(lib->create_file_reader(*lib, *path_in_lib),
                         return err("error opening", error, LibraryImages::LoadFailure::Unavailable));

    auto const file_data =
        TRY_OR(reader.ReadOrFetchAll(scratch_arena),
               return err("error reading", error, LibraryImages::LoadFailure::Unavailable));

    auto pixels = TRY_OR(DecodeImage(file_data, result_allocator),
                         return err("error decoding", error, LibraryImages::LoadFailure::Missing));

    ASSERT(pixels.size.width && pixels.size.height, "ImageBytes cannot be empty");

    return pixels;
}

inline Allocator& ImageBytesAllocator() { return PageAllocator::Instance(); }

// Floe's own built-in resources (IRs and waveforms) don't ship an icon file; they use Floe's icon.
static bool UsesFloeIcon(sample_lib::LibraryId lib_id) {
    return lib_id == sample_lib::k_builtin_library_id || lib_id == sample_lib::k_waveform_library_id;
}

static Optional<ImageBytes> FloeIconPixels(ArenaAllocator& result_allocator) {
    auto const image_data = EmbeddedIconImage();
    if (!image_data.size) return k_nullopt;
    auto outcome = DecodeImage({image_data.data, image_data.size}, result_allocator);
    if (outcome.HasError()) return k_nullopt;
    return outcome.ReleaseValue();
}

static void AsyncLoadIcon(sample_lib::LibraryId lib_id,
                          imgui::Context const&,
                          LibraryImages::FutureIcon& result,
                          sample_lib_server::Server& server,
                          ThreadPool& thread_pool,
                          FloeInstanceIndex instance_index,
                          bool log_failure) {
    thread_pool.Async(
        result,
        [lib_id = lib_id,
         &server,
         instance_index,
         log_failure,
         desired_icon_size = CheckedCast<u16>(Ceil(WwToPixels(k_library_icon_standard_size)) *
                                              2)]() -> LibraryImages::LoadedIcon {
            DEFER { RequestGuiUpdate(instance_index); };

            ArenaAllocator scratch_arena {PageAllocator::Instance()};
            auto const pixels = ({
                ImagePixelsOrFailure p = LibraryImages::LoadFailure::Missing;
                if (UsesFloeIcon(lib_id)) {
                    if (auto floe_icon = FloeIconPixels(scratch_arena)) p = *floe_icon;
                } else {
                    p = ImagePixelsFromLibrary(lib_id,
                                               LibraryImageType::Icon,
                                               server,
                                               scratch_arena,
                                               scratch_arena,
                                               log_failure);
                }
                p;
            });
            if (pixels.HasError()) return {.failure = pixels.Error()};
            auto const source = pixels.ReleaseValue();
            return {.icon = ResizeImage(source, desired_icon_size, ImageBytesAllocator()).OrElse([&] {
                return source.Clone(ImageBytesAllocator());
            })};
        },
        []() {
            // no cleanup
        });
}

static void AsyncLoadBackgrounds(sample_lib::LibraryId lib_id,
                                 imgui::Context const&,
                                 LibraryImages::FutureBackgrounds& result,
                                 bool reload_background,
                                 bool reload_blurred_background,
                                 sample_lib_server::Server& server,
                                 ThreadPool& thread_pool,
                                 FloeInstanceIndex instance_index,
                                 bool log_failure) {
    BlurredImageBackgroundOptions const blur_options {
        .downscale_factor = Clamp01(29.13f / 100.0f),
        .brightness_scaling_exponent = 62.0f / 100.0f,
        .overlay_value = Clamp01(14.77f / 100.0f),
        .overlay_alpha = Clamp01(26.12f / 100.0f),
        .blur1_radius_percent = 55.0f / 100,
        .blur2_radius_percent = 2.47f / 100,
        .blur2_alpha = Clamp01(12.92f / 100.0f),
    };

    thread_pool.Async(
        result,
        [lib_id,
         reload_background,
         reload_blurred_background,
         blur_options,
         &server,
         instance_index,
         log_failure,
         window_width = GuiIo().in.window_size.width]() -> LibraryImages::LoadedBackgrounds {
            DEFER { RequestGuiUpdate(instance_index); };

            ArenaAllocator scratch_arena {PageAllocator::Instance()};

            auto const pixels_or_failure = ({
                ImagePixelsOrFailure p = LibraryImages::LoadFailure::Missing;
                if (lib_id == k_default_background_lib_id) {
                    auto const image_data = EmbeddedDefaultBackground();
                    p = DecodeImage({image_data.data, image_data.size}, scratch_arena).Value();
                } else {
                    p = ImagePixelsFromLibrary(lib_id,
                                               LibraryImageType::Background,
                                               server,
                                               scratch_arena,
                                               scratch_arena,
                                               log_failure);
                }
                p;
            });

            if (pixels_or_failure.HasError()) return {.failure = pixels_or_failure.Error()};
            auto const pixels = pixels_or_failure.ReleaseValue();

            LibraryImages::LoadedBackgrounds result {};

            // If the image is quite a lot larger than we need, resize it down to avoid storing a huge
            // image on the GPU
            auto const background =
                (f32)pixels.size.width > (f32)window_width * 1.3f
                    ? ResizeImage(pixels, window_width, ImageBytesAllocator()).OrElse([&] {
                          return pixels.Clone(ImageBytesAllocator());
                      })
                    : pixels.Clone(ImageBytesAllocator());

            if (reload_background) result.background = background;

            if (reload_blurred_background)
                result.blurred_background = CreateBlurredLibraryBackground(background,
                                                                           ImageBytesAllocator(),
                                                                           scratch_arena,
                                                                           blur_options);

            if (!reload_background) background.Free(ImageBytesAllocator());

            return result;
        },
        []() {
            // no cleanup
        },
        JobPriority::High);
}

constexpr auto k_background_type_bits =
    Array {ToInt(LibraryImages::ImageType::Background), ToInt(LibraryImages::ImageType::BlurredBackground)};

LibraryImages GetLibraryImages(LibraryImagesTable& table,
                               imgui::Context& imgui,
                               sample_lib::LibraryId lib_id,
                               sample_lib_server::Server& server,
                               FloeInstanceIndex instance_index,
                               LibraryImagesTypes needed_types) {
    ASSERT(g_is_logical_main_thread);

    auto e = table.table.FindOrInsertGrowIfNeeded(table.arena, lib_id, {});
    auto& images = e.element.data;

    if (e.inserted) images.needs_reload.SetAll();

    if ((needed_types & LibraryImagesTypes::Icon) &&
        images.needs_reload.Get(ToInt(LibraryImages::ImageType::Icon))) {
        bool load = false;
        if (!images.loading_icon) {
            images.loading_icon = table.arena.New<LibraryImages::FutureIcon>();
            load = true;
        } else if (images.loading_icon->IsInactive()) {
            load = true;
        }

        if (load) {
            images.icon_load.generation_at_start = images.generation;
            AsyncLoadIcon(lib_id,
                          imgui,
                          *images.loading_icon,
                          server,
                          server.thread_pool,
                          instance_index,
                          !images.icon_load.failure.HasValue());
        }

        images.needs_reload.Clear(ToInt(LibraryImages::ImageType::Icon));
    }

    if ((needed_types & LibraryImagesTypes::Backgrounds) &&
        images.needs_reload.AnySetInSpan(k_background_type_bits)) {
        bool load = false;
        if (!images.loading_backgrounds) {
            images.loading_backgrounds = table.arena.New<LibraryImages::FutureBackgrounds>();
            load = true;
        } else if (images.loading_backgrounds->IsInactive()) {
            load = true;
        }

        if (load) {
            images.backgrounds_load.generation_at_start = images.generation;
            AsyncLoadBackgrounds(lib_id,
                                 imgui,
                                 *images.loading_backgrounds,
                                 images.needs_reload.Get(ToInt(LibraryImages::ImageType::Background)),
                                 images.needs_reload.Get(ToInt(LibraryImages::ImageType::BlurredBackground)),
                                 server,
                                 server.thread_pool,
                                 instance_index,
                                 !images.backgrounds_load.failure.HasValue());
        }

        images.needs_reload.ClearBits(k_background_type_bits);
    }

    return images;
}

static void Invalidate(LibraryImages& imgs, Renderer& renderer) {
    ++imgs.generation;
    imgs.icon_load.failure = k_nullopt;
    imgs.backgrounds_load.failure = k_nullopt;
    if (imgs.icon) renderer.DestroyImageID(*imgs.icon);
    if (imgs.background) renderer.DestroyImageID(*imgs.background);
    if (imgs.blurred_background) renderer.DestroyImageID(*imgs.blurred_background);
}

void InvalidateLibraryImages(LibraryImagesTable& table,
                             sample_lib::LibraryId library_id,
                             Renderer& renderer) {
    ASSERT(g_is_logical_main_thread);

    if (auto imgs = table.table.Find(library_id)) Invalidate(*imgs, renderer);
}

void InvalidateAllLibraryImages(LibraryImagesTable& table, Renderer& renderer) {
    ASSERT(g_is_logical_main_thread);

    for (auto [_, imgs, _] : table.table)
        Invalidate(imgs, renderer);
}

static void FreeLoadedBackgrounds(LibraryImages::LoadedBackgrounds const& backgrounds) {
    if (backgrounds.background) backgrounds.background->Free(ImageBytesAllocator());
    if (backgrounds.blurred_background) backgrounds.blurred_background->Free(ImageBytesAllocator());
}

void Shutdown(LibraryImagesTable& table) {
    ASSERT(g_is_logical_main_thread);

    for (auto [_, imgs, _] : table.table) {
        if (imgs.loading_icon) {
            if (auto const loaded = imgs.loading_icon->ShutdownAndRelease(60000u))
                if (loaded->icon) loaded->icon->Free(ImageBytesAllocator());
        }

        if (imgs.loading_backgrounds) {
            if (auto const loaded = imgs.loading_backgrounds->ShutdownAndRelease(60000u))
                FreeLoadedBackgrounds(*loaded);
        }
    }
}

static void
RecordLoadFailure(LibraryImages::LoadState& state, LibraryImages::LoadFailure failure, u64 wakeup_id) {
    state.failure = failure;
    switch (failure) {
        case LibraryImages::LoadFailure::Missing: break;
        case LibraryImages::LoadFailure::Unavailable:
            state.retry_time = TimePoint::Now() + 1.0;
            GuiIo().out.SetTimedWakeup(wakeup_id, state.retry_time);
            break;
    }
}

static bool ShouldLoad(LibraryImages::LoadState const& state) {
    if (!state.failure) return true;
    switch (*state.failure) {
        case LibraryImages::LoadFailure::Missing: return false;
        case LibraryImages::LoadFailure::Unavailable: return TimePoint::Now() >= state.retry_time;
    }
    PanicIfReached();
    return false;
}

void BeginFrame(LibraryImagesTable& table) {
    ASSERT(g_is_logical_main_thread);

    auto& renderer = *GuiIo().in.renderer;

    for (auto [lib_id, imgs, _] : table.table) {
        if (imgs.loading_icon) {
            if (auto result = imgs.loading_icon->TryReleaseResult()) {
                if (imgs.icon_load.generation_at_start != imgs.generation) {
                    if (result->icon) result->icon->Free(ImageBytesAllocator());
                } else if (result->icon) {
                    imgs.icon = CreateImageIdChecked(renderer, *result->icon);
                    result->icon->Free(ImageBytesAllocator());
                    imgs.icon_load.failure = k_nullopt;
                } else {
                    ASSERT(result->failure.HasValue());
                    RecordLoadFailure(imgs.icon_load, *result->failure, Hash(lib_id) ^ SourceLocationHash());
                }
            }
        }

        if (imgs.loading_backgrounds) {
            if (auto result = imgs.loading_backgrounds->TryReleaseResult()) {
                if (imgs.backgrounds_load.generation_at_start != imgs.generation) {
                    FreeLoadedBackgrounds(*result);
                } else if (result->failure) {
                    RecordLoadFailure(imgs.backgrounds_load,
                                      *result->failure,
                                      Hash(lib_id) ^ SourceLocationHash());
                } else {
                    if (result->background) {
                        imgs.background = CreateImageIdChecked(renderer, *result->background);
                        result->background->Free(ImageBytesAllocator());
                    }
                    if (result->blurred_background) {
                        imgs.blurred_background = CreateImageIdChecked(renderer, *result->blurred_background);
                        result->blurred_background->Free(ImageBytesAllocator());
                    }
                    imgs.backgrounds_load.failure = k_nullopt;
                }
            }
        }

        // Check if we need to reload any images. We don't actually do the loading here because we want to
        // defer it to the point where we know that the images are actually needed.
        {
            imgs.needs_reload.ClearAll();

            if (!renderer.ImageIdIsValid(imgs.icon) && ShouldLoad(imgs.icon_load))
                imgs.needs_reload.Set(ToInt(LibraryImages::ImageType::Icon));

            if (ShouldLoad(imgs.backgrounds_load)) {
                if (!renderer.ImageIdIsValid(imgs.background))
                    imgs.needs_reload.Set(ToInt(LibraryImages::ImageType::Background));
                if (!renderer.ImageIdIsValid(imgs.blurred_background))
                    imgs.needs_reload.Set(ToInt(LibraryImages::ImageType::BlurredBackground));
            }
        }
    }
}
