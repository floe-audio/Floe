// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/core/gui_fwd.hpp"
#include "processing_utils/curve_map.hpp"

// description: what the curve as a whole does. Shown on every part of the graph, followed by a line about
// whichever part is hovered.
void DoCurveMap(GuiState& g,
                CurveMap& curve_map,
                u8 layer_index,
                Rect rect,
                Optional<f32> velocity_marker,
                String description);
