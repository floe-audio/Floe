// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/engine.hpp"
#include "gui/gui2_common_browser.hpp"
#include "gui/gui2_confirmation_dialog_state.hpp"
#include "gui/gui2_inst_browser_state.hpp"
#include "gui/gui_frame_context.hpp"
#include "gui/gui_library_images.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "processor/layer_processor.hpp"

// Ephemeral
struct InstBrowserContext {
    LayerProcessor& layer;
    sample_lib_server::Server& sample_library_server;
    LibraryImagesTable& library_images;
    Engine& engine;
    prefs::Preferences& prefs;
    Optional<graphics::ImageID>& unknown_library_icon;
    Notifications& notifications;
    persistent_store::Store& persistent_store;
    ConfirmationDialogState& confirmation_dialog_state;
    GuiFrameContext const& frame_context;
};

void LoadAdjacentInstrument(InstBrowserContext const& context,
                            InstBrowserState& state,
                            SearchDirection direction);

void LoadRandomInstrument(InstBrowserContext const& context, InstBrowserState& state);

void DoInstBrowserPopup(GuiBoxSystem& box_system, InstBrowserContext& context, InstBrowserState& state);
