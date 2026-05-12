// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

#include "gui/core/gui_fwd.hpp"
#include "gui/core/gui_subsystem.hpp"

enum class MidPanelTab : u8 {
    Perform,
    Layers,
    Effects,
    Count,
};

struct MidPanelState {
    MidPanelTab tab = MidPanelTab::Perform;
    f32 last_random_variation_amount = 0;
    f32 last_strip_fire_x = 0;
};

void MidPanel(GuiState& g, Rect bounds, GuiFrameContext const& frame_context);

extern GuiSubsystem<MidPanelState> const g_mid_panel_subsystem;

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
                              sample_lib::LibraryId library_id,
                              MidBlurredBackgroundOptions const& options);
void DrawMidPanelBackgroundImage(GuiState& g, sample_lib::LibraryId library_id);
void DrawMidBlurredPanelSurface(GuiState& g, Rect window_r, Optional<sample_lib::LibraryId> lib_id);
