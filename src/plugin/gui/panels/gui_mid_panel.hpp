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

void MidPanelTabs(GuiState& g, Rect bounds);

void MidPanel(GuiState& g, Rect bounds, GuiFrameContext const& frame_context);

// Internal
void MidPanelCombined(GuiState& g, Rect bounds, GuiFrameContext const& frame_context);

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
