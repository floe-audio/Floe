// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

#include "gui/core/gui_fwd.hpp"

enum class MidPanelTab : u8 {
    Perform,
    Layers,
    Effects,
    Count,
};

struct MidPanelState {
    MidPanelTab tab = MidPanelTab::Perform;
};

void MidPanel(GuiState& g, Rect bounds, GuiFrameContext const& frame_context);

void MidPanelLayersContent(GuiBuilder& builder,
                           GuiState& g,
                           GuiFrameContext const& frame_context,
                           Box parent,
                           Box tab_extra_buttons_box);

struct MidBlurredBackgroundOptions {
    f32 opacity = 1;
    u4 rounding_corners = 0b1111;
};

void DrawMidBlurredBackground(GuiState& g,
                              Rect r,
                              Rect clipped_to,
                              sample_lib::LibraryIdRef library_id,
                              MidBlurredBackgroundOptions const& options);
void DrawMidPanelBackgroundImage(GuiState& g, sample_lib::LibraryIdRef library_id);
void DrawMidBlurredPanelSurface(GuiState& g, Rect window_r, Optional<sample_lib::LibraryIdRef> lib_id);
