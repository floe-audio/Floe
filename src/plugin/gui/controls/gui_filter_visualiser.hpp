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

// Reverb pre-filter visualiser: combined bandpass formed by the pre low-pass and pre high-pass
// cutoff params. Two horizontal grabbers, one per cutoff (Y pinned to 0 dB).
void DoReverbPreFilterVisualizer(GuiState& g, Rect viewport_r, bool greyed_out);

// Reverb post-shelf visualiser: combined low-shelf + high-shelf response. Two grabbers, one per
// shelf, draggable on X (cutoff) and Y (gain, 0 dB to -24 dB).
void DoReverbPostShelfVisualizer(GuiState& g, Rect viewport_r, bool greyed_out);
