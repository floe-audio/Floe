// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/core/gui_fwd.hpp"
#include "gui_framework/gui_builder.hpp"

struct GuiState;
struct LayerProcessor;

void DoLoopModeSelector(GuiState& g, Box parent, LayerProcessor& layer);

enum class LayerPageType : u8 {
    Main,
    Filter,
    Lfo,
    Eq,
    Play,
    Count,
};

struct LayerPanelState {
    LayerPageType selected_page {};
};
