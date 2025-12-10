// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "os/misc.hpp"

#include "common_infrastructure/preferences.hpp"

#include "gui/gui2_common_browser.hpp"
#include "gui/gui2_confirmation_dialog_state.hpp"
#include "gui/gui_frame_context.hpp"
#include "gui/gui_fwd.hpp"
#include "preset_server/preset_server.hpp"

struct GuiBoxSystem;
struct PresetServer;

// Ephemeral
struct PresetBrowserContext {
    void Init(ArenaAllocator& arena) {
        if (init++) return;
        presets_snapshot = BeginReadFolders(preset_server, arena);
    }
    void Deinit() {
        if (--init != 0) return;
        EndReadFolders(preset_server);
    }

    sample_lib_server::Server& sample_library_server;
    PresetServer& preset_server;
    LibraryImagesTable& library_images;
    prefs::Preferences& prefs;
    Engine& engine;
    Optional<graphics::ImageID>& unknown_library_icon;
    Notifications& notifications;
    persistent_store::Store& persistent_store;
    ConfirmationDialogState& confirmation_dialog_state;
    GuiFrameContext const& frame_context;

    u32 init = 0;
    PresetsSnapshot presets_snapshot;
};

// Persistent
struct PresetBrowserState {
    SelectedHashes selected_author_hashes {"Author"};
    bool scroll_to_show_selected = false;

    // This is contains PresetFormat as u64. We use a dynamic array of u64 so we can share the same code as
    // the other types of selected_* filters.
    SelectedHashes selected_preset_types {"Preset Type"};

    CommonBrowserState common_state {
        .other_selected_hashes = Array {&selected_author_hashes, &selected_preset_types},
    };
};

void LoadAdjacentPreset(PresetBrowserContext const& context,
                        PresetBrowserState& state,
                        SearchDirection direction);

void LoadRandomPreset(PresetBrowserContext const& context, PresetBrowserState& state);

void DoPresetBrowser(GuiBoxSystem& box_system, PresetBrowserContext& context, PresetBrowserState& state);
