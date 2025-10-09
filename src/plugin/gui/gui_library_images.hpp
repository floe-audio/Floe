// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_framework/gui_imgui.hpp"
#include "sample_lib_server/sample_library_server.hpp"

struct LibraryImages {
    Optional<graphics::ImageID> icon {};
    Optional<graphics::ImageID> background {};
    Optional<graphics::ImageID> blurred_background {};
    bool icon_missing {};
    bool background_missing {};
};

using LibraryImagesTable = DynamicHashTable<sample_lib::LibraryId, LibraryImages>;

enum class LibraryImagesNeeded : u8 {
    Icon = 1 << 0,
    Backgrounds = 1 << 1,
    All = Icon | Backgrounds,
};
BITWISE_OPERATORS(LibraryImagesNeeded)

LibraryImages LibraryImagesFromLibraryId(LibraryImagesTable& table,
                                         imgui::Context& imgui,
                                         sample_lib::LibraryIdRef const& library_id,
                                         sample_lib_server::Server& server,
                                         ArenaAllocator& scratch_arena,
                                         LibraryImagesNeeded needed = LibraryImagesNeeded::All);

void InvalidateLibraryImages(LibraryImagesTable& table,
                             sample_lib::LibraryIdRef library_id,
                             graphics::DrawContext& ctx);
