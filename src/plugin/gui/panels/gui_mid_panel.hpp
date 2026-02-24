// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"
#include "gui/core/gui_fwd.hpp"

enum class MidPanelTab : u8 {
    All,
    Layer1,
    Layer2,
    Layer3,
    Effects,
    Count,
};

struct MidPanelState {
    MidPanelTab tab = MidPanelTab::All;
};

void MidPanel(GuiState& g, Rect bounds, GuiFrameContext const& frame_context);

// Internal: called from MidPanel, accepting a parent box instead of creating their own viewport
struct GuiBuilder;
struct Box;
void MidPanelCombinedContent(GuiBuilder& builder, GuiState& g, GuiFrameContext const& frame_context, Box parent);

namespace imgui {
struct Context;
}

void DrawMidBlurredBackground(GuiState& g,
                              Rect r,
                              Rect clipped_to,
                              sample_lib::LibraryIdRef library_id,
                              f32 opacity);
void DoMidOverlayGradient(imgui::Context const& imgui, Rect r);
void DrawMidPanelBackgroundImage(GuiState& g, sample_lib::LibraryIdRef library_id);
void DrawMidBlurredPanelSurface(GuiState& g, Rect window_r, Optional<sample_lib::LibraryIdRef> lib_id);
