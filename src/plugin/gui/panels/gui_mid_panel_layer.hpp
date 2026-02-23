// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui/core/gui_fwd.hpp"

struct GuiFrameContext;

void MidPanelSingleLayer(GuiState& g, Rect bounds, GuiFrameContext const& frame_context, u8 layer_index);
