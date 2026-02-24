// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/container/function.hpp"
#include "foundation/utils/geometry.hpp"

#include "gui/core/gui_fwd.hpp"
#include "gui_framework/gui_builder.hpp"

struct GuiState;
struct GuiFrameContext;
struct LayerProcessor;

void DoLoopModeSelector(GuiState& g, Box parent, LayerProcessor& layer);

// Called to draw the background of the selector row. Receives the window-space rect.
// If not set, draws the default coloured background with timbre-layer highlight.
using InstSelectorDrawBackground = FunctionRef<void(Rect window_r)>;

void DoInstSelector(GuiState& g,
                    GuiFrameContext const& frame_context,
                    u8 layer_index,
                    Box root,
                    InstSelectorDrawBackground draw_background = {});

enum class LayerPageType : u8 {
    Main,
    Engine,
    Filter,
    Lfo,
    Eq,
    Play,
    Count,
};

struct LayerPanelState {
    LayerPageType selected_page {};
};

void DoInstrumentInfoStrip(GuiState& g, u8 layer_index, Box parent);
