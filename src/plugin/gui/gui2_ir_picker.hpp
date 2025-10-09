// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/engine.hpp"
#include "gui/gui2_common_picker.hpp"
#include "gui/gui2_confirmation_dialog_state.hpp"
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
    LibraryImagesTable& library_images;
    Engine& engine;
    prefs::Preferences& prefs;
    Optional<graphics::ImageID>& unknown_library_icon;
    Notifications& notifications;
    persistent_store::Store& persistent_store;
    ConfirmationDialogState& confirmation_dialog_state;

    Span<sample_lib_server::RefCounted<sample_lib::Library>> libraries;
};

struct IrCursor {
    bool operator==(IrCursor const& o) const = default;
    usize lib_index;
    usize ir_index;
};

void LoadAdjacentIr(IrPickerContext const& context, IrPickerState& state, SearchDirection direction);

void LoadRandomIr(IrPickerContext const& context, IrPickerState& state);

void DoIrPickerPopup(GuiBoxSystem& box_system, IrPickerContext& context, IrPickerState& state);
