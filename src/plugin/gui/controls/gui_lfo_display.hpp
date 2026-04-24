// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui/core/gui_fwd.hpp"

// Non-interactive visualisation of a layer's LFO shape scaled by its amount.
void DoLfoDisplay(GuiState& g, u8 layer_index, Rect viewport_r, bool greyed_out);
