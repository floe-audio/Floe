// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/engine.hpp"
#include "gui/gui2_common_picker.hpp"
#include "gui/gui2_confirmation_dialog_state.hpp"
#include "gui/gui2_inst_picker_state.hpp"
#include "gui/gui_library_images.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "processor/layer_processor.hpp"

// Ephemeral
struct InstPickerContext {
    void Init(ArenaAllocator& arena) {
        libraries = sample_lib_server::AllLibrariesRetained(sample_library_server, arena);
        Sort(libraries, [](auto const& a, auto const& b) { return a->name < b->name; });
    }
    void Deinit() { sample_lib_server::ReleaseAll(libraries); }

    LayerProcessor& layer;
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

void LoadAdjacentInstrument(InstPickerContext const& context,
                            InstPickerState& state,
                            SearchDirection direction);

void LoadRandomInstrument(InstPickerContext const& context, InstPickerState& state);

void DoInstPickerPopup(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state);
