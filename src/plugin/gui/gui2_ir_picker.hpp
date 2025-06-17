// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/engine.hpp"
#include "gui/gui2_common_picker.hpp"
#include "gui/gui2_ir_picker_state.hpp"
#include "gui/gui_library_images.hpp"
#include "gui_framework/gui_box_system.hpp"

struct IrPickerContext {
    void Init(ArenaAllocator& arena) {
        libraries = sample_lib_server::AllLibrariesRetained(sample_library_server, arena);
        Sort(libraries, [](auto const& a, auto const& b) { return a->name < b->name; });
    }
    void Deinit() { sample_lib_server::ReleaseAll(libraries); }

    sample_lib_server::Server& sample_library_server;
    LibraryImagesArray& library_images;
    Engine& engine;
    Optional<graphics::ImageID>& unknown_library_icon;
    Span<sample_lib_server::RefCounted<sample_lib::Library>> libraries;
    sample_lib::ImpulseResponse const* hovering_ir {};
};

struct IrCursor {
    bool operator==(IrCursor const& o) const = default;
    usize lib_index;
    usize ir_index;
};

void LoadAdjacentIr(IrPickerContext const& context, IrPickerState& state, SearchDirection direction);

void LoadRandomIr(IrPickerContext const& context, IrPickerState& state);

void DoIrPickerPopup(GuiBoxSystem& box_system,
                     imgui::Id popup_id,
                     Rect absolute_button_rect,
                     IrPickerContext& context,
                     IrPickerState& state);
