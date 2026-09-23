// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/sample_library/server/sample_library_server.hpp"

#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/image.hpp"

// Images for a particular sample library.
struct LibraryImages {
    enum class LoadFailure : u8 {
        Missing, // The library doesn't have this image. Not retried.
        Unavailable, // The library couldn't be found or read at the time, e.g. mid-rescan. Retried later.
    };

    struct LoadedIcon {
        Optional<ImageBytes> icon {};
        Optional<LoadFailure> failure {};
    };

    struct LoadedBackgrounds {
        Optional<ImageBytes> background {};
        Optional<ImageBytes> blurred_background {};
        Optional<LoadFailure> failure {};
    };

    using FutureIcon = Future<LoadedIcon>;
    using FutureBackgrounds = Future<LoadedBackgrounds>;

    struct LoadState {
        Optional<LoadFailure> failure {};
        TimePoint retry_time {};
        u32 generation_at_start {};
    };

    enum class ImageType : u8 { Icon, Background, BlurredBackground, Count };

    Optional<ImageID> icon {};
    Optional<ImageID> background {};
    Optional<ImageID> blurred_background {};
    LoadState icon_load {};
    LoadState backgrounds_load {};

    // Incremented on invalidation. Results from loads that started before it are discarded.
    u32 generation {};

    // Futures cannot be moved around (for example when a hash table resizes), so they are allocated elsewhere
    // and we have pointers to them.
    FutureIcon* loading_icon;
    FutureBackgrounds* loading_backgrounds;

    // Per-frame state.
    Bitset<ToInt(ImageType::Count)> needs_reload {};
};

struct LibraryImagesTable {
    // Memory for library images is never freed until Shutdown. We do free the pixel data and GPU resources,
    // but the Futures and table is never freed - they are small and very infrequently changing - simplifying
    // lifetime management.
    ArenaAllocator arena {PageAllocator::Instance()}; // Never reset.
    HashTable<sample_lib::LibraryId, LibraryImages, NoHash> table;
};

enum class LibraryImagesTypes : u8 {
    Icon = 1 << 0,
    Backgrounds = 1 << 1,
    All = Icon | Backgrounds,
};
BITWISE_OPERATORS(LibraryImagesTypes)

// Very efficiently retrieves library images for a given library - starting any asynchronous loading if
// needed. Use needed_types to trigger loading only for the images you need.
LibraryImages GetLibraryImages(LibraryImagesTable& table,
                               imgui::Context& imgui,
                               sample_lib::LibraryId library_id,
                               sample_lib_server::Server& server,
                               FloeInstanceIndex instance_index,
                               LibraryImagesTypes needed_types = LibraryImagesTypes::All);

void BeginFrame(LibraryImagesTable& table);
void Shutdown(LibraryImagesTable& table);
void InvalidateLibraryImages(LibraryImagesTable& table, sample_lib::LibraryId library_id, Renderer& renderer);
void InvalidateAllLibraryImages(LibraryImagesTable& table, Renderer& renderer);
