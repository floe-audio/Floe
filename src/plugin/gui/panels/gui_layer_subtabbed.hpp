// Copyright 2024-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/core/gui_fwd.hpp"
#include "gui_framework/gui_builder.hpp"

enum class LayerPageType : u8 {
    Engine,
    Envelope,
    Filter,
    Lfo,
    Eq,
    Play,
    Count,
};

struct LayerPanelState {
    LayerPageType selected_page {};
};

void DoLayerPanel(GuiState& g, GuiFrameContext const& frame_context, u8 layer_index, Box parent);
