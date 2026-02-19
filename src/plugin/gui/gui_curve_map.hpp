// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_fwd.hpp"
#include "processing_utils/curve_map.hpp"

void DoCurveMap(GuiState& g,
                CurveMap& curve_map,
                Rect rect,
                Optional<f32> velocity_marker,
                String additional_tooltip);
