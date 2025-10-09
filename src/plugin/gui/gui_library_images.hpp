// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/image.hpp"
#include "sample_lib_server/sample_library_server.hpp"

struct LibraryImages {
    struct LoadingBackgrounds {
        Optional<ImageBytes> background {};
        Optional<ImageBytes> blurred_background {};
    };

    Optional<graphics::ImageID> icon {};
    Optional<graphics::ImageID> background {};
    Optional<graphics::ImageID> blurred_background {};
    bool icon_missing {};
    bool background_missing {};

    // Futures cannot be moved around (for example when a hash table resizes), so they are allocated elsewhere
    // and we have pointers to them.
    Future<Optional<ImageBytes>>* loading_icon;
    Future<Optional<LoadingBackgrounds>>* loading_backgrounds;

    // Per-frame state.
    bool needs_icon_reload {};
    bool needs_background_reload {};
    bool needs_blurred_background_reload {};
};

struct LibraryImagesTable {
    ArenaAllocator arena {PageAllocator::Instance()};
    HashTable<sample_lib::LibraryId, LibraryImages> table;
};

enum class LibraryImagesNeeded : u8 {
    Icon = 1 << 0,
    Backgrounds = 1 << 1,
    All = Icon | Backgrounds,
};
BITWISE_OPERATORS(LibraryImagesNeeded)

void BeginFrame(LibraryImagesTable& table, imgui::Context& imgui);
void Shutdown(LibraryImagesTable& table);

LibraryImages LibraryImagesFromLibraryId(LibraryImagesTable& table,
                                         imgui::Context& imgui,
                                         sample_lib::LibraryIdRef const& library_id,
                                         sample_lib_server::Server& server,
                                         LibraryImagesNeeded needed = LibraryImagesNeeded::All);

void InvalidateLibraryImages(LibraryImagesTable& table,
                             sample_lib::LibraryIdRef library_id,
                             graphics::DrawContext& ctx);
