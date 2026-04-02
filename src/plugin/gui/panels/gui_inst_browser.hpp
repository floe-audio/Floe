// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/engine.hpp"
#include "gui/core/gui_frame_context.hpp"
#include "gui/core/gui_library_images.hpp"
#include "gui/overlays/gui_confirmation_dialog.hpp"
#include "gui/panels/gui_common_browser.hpp"
#include "gui_framework/gui_builder.hpp"
#include "processor/layer_processor.hpp"

struct InstBrowserState {
    imgui::Id const id;
    CommonBrowserState common_state = [] {
        CommonBrowserState s {};
        InitCommonFilters(s);
        return s;
    }();
    bool scroll_to_show_selected = false;
};

// Ephemeral
struct InstBrowserContext {
    LayerProcessor& layer;
    sample_lib_server::Server& sample_library_server;
    LibraryImagesTable& library_images;
    Engine& engine;
    prefs::Preferences& prefs;
    Notifications& notifications;
    persistent_store::Store& persistent_store;
    ConfirmationDialogState& confirmation_dialog_state;
    GuiFrameContext const& frame_context;
};

void LoadAdjacentInstrument(InstBrowserContext const& context,
                            InstBrowserState& state,
                            SearchDirection direction);

void LoadRandomInstrument(InstBrowserContext const& context, InstBrowserState& state);

void DoInstBrowserPopup(GuiBuilder& builder, InstBrowserContext& context, InstBrowserState& state);
