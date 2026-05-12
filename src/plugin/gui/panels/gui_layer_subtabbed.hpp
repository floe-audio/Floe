// Copyright 2024-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/core/gui_fwd.hpp"
#include "gui/core/gui_subsystem.hpp"
#include "gui_framework/gui_builder.hpp"

enum class LayerPageType : u8 {
    Playback,
    Main,
    Lfo,
    Eq,
    Arp,
    Config,
    Count,
};

struct LayerPanelState {
    u32 const layer_index;
    LayerPageType selected_page {};
    bool arp_step_sequencer_show_all {};
};

void DoLayerPanel(GuiState& g, GuiFrameContext const& frame_context, u8 layer_index, Box parent);

extern GuiSubsystem<LayerPanelState> const g_layer_panel_subsystem;
