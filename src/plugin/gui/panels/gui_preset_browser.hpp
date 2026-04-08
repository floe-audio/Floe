// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common_infrastructure/preferences.hpp"

#include "gui/core/gui_frame_context.hpp"
#include "gui/core/gui_fwd.hpp"
#include "gui/overlays/gui_confirmation_dialog.hpp"
#include "gui/panels/gui_common_browser.hpp"
#include "preset_server/preset_server.hpp"

struct GuiBuilder;
struct PresetServer;

// Ephemeral
struct PresetBrowserContext {
    void Init(ArenaAllocator& arena) {
        if (init++) return;
        auto const [snapshot, handle] = BeginReadFolders(preset_server, arena);
        presets_snapshot = snapshot;
        preset_read_handle = handle;
    }
    void Deinit() {
        if (--init != 0) return;
        EndReadFolders(preset_server, preset_read_handle);
    }

    sample_lib_server::Server& sample_library_server;
    PresetServer& preset_server;
    LibraryImagesTable& library_images;
    prefs::Preferences& prefs;
    Engine& engine;
    Notifications& notifications;
    persistent_store::Store& persistent_store;
    ConfirmationDialogState& confirmation_dialog_state;
    GuiFrameContext const& frame_context;

    u32 init = 0;
    PresetsSnapshot presets_snapshot;
    PresetServerReadHandle preset_read_handle;
};

enum class PresetBrowserFilter : usize {
    Author = (usize)BrowserFilter::CommonCount,
    PresetType,
    Count,
};

static_assert(ToInt(PresetBrowserFilter::Count) <= k_max_browser_filters);

// Persistent
struct PresetBrowserState {
    static constexpr u64 k_panel_id = HashFnv1a("preset-browser");
    bool scroll_to_show_selected = false;

    CommonBrowserState common_state = [] {
        CommonBrowserState s {};
        InitCommonFilters(s);
        dyn::Append(s.filters, FilterSelection::Hashes("Author"_s));
        dyn::Append(s.filters, FilterSelection::Hashes("Preset Type"_s));
        return s;
    }();
};

void LoadAdjacentPreset(PresetBrowserContext const& context,
                        PresetBrowserState& state,
                        SearchDirection direction);

void LoadRandomPreset(PresetBrowserContext const& context, PresetBrowserState& state);

void DoPresetBrowser(GuiBuilder& builder, PresetBrowserContext& context, PresetBrowserState& state);
