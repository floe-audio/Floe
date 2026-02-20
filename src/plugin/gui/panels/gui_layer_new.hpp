// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/core/gui_fwd.hpp"
#include "gui_framework/gui_builder.hpp"

struct GuiState;
struct GuiFrameContext;

namespace layer_gui_new {

void DoLayerPanel(GuiState& g, GuiFrameContext const& frame_context, u8 layer_index, Box parent);

} // namespace layer_gui_new
