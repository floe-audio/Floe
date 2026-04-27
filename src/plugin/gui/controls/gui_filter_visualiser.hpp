// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui/core/gui_fwd.hpp"

// Visualiser for a per-layer filter.
void DoFilterVisualizer(GuiState& g, u8 layer_index, Rect viewport_r, bool greyed_out);

// Visualiser for the global effect filter (ParamIndex::FilterCutoff, FilterResonance, FilterGain,
// FilterType). Drag X = cutoff; drag Y = gain (only for gain-using types); wheel = resonance.
void DoEffectFilterVisualizer(GuiState& g, Rect viewport_r, bool greyed_out);
