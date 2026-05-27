// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_frame_context.hpp"
#include "gui/core/gui_library_images.hpp"
#include "gui/overlays/gui_confirmation_dialog.hpp"
#include "gui/panels/gui_common_browser.hpp"
#include "gui_framework/gui_builder.hpp"

struct IrBrowserState {
    static constexpr u64 k_panel_id = SourceLocationHash();
    CommonBrowserState common_state = [] {
        CommonBrowserState s {};
        InitCommonFilters(s);
        return s;
    }();
    bool scroll_to_show_selected = false;
};

struct IrBrowserContext {
    sample_lib_server::Server& sample_library_server;
    LibraryImagesTable& library_images;
    Engine& engine;
    prefs::Preferences& prefs;
    Notifications& notifications;
    persistent_store::Store& persistent_store;
    ConfirmationDialogState& confirmation_dialog_state;
    GuiFrameContext const& frame_context;
};

struct IrCursor {
    bool operator==(IrCursor const& o) const = default;
    usize lib_index;
    usize ir_index;
};

void LoadAdjacentIr(IrBrowserContext const& context, IrBrowserState& state, SearchDirection direction);

void LoadRandomIr(IrBrowserContext const& context, IrBrowserState& state);

void DoIrBrowserPopup(GuiBuilder& builder, IrBrowserContext& context, IrBrowserState& state);
