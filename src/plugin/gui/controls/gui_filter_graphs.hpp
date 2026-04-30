// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui/core/gui_fwd.hpp"

// Graph for a per-layer filter.
void DoFilterGraph(GuiState& g, u8 layer_index, Rect viewport_r, bool greyed_out);

// Graph for the global effect filter (ParamIndex::FilterCutoff, FilterResonance, FilterGain,
// FilterType). Drag X = cutoff; drag Y = gain (only for gain-using types); wheel = resonance.
void DoEffectFilterGraph(GuiState& g, Rect viewport_r, bool greyed_out);

// Reverb pre-filter graph: combined bandpass formed by the pre low-pass and pre high-pass
// cutoff params. Two horizontal grabbers, one per cutoff (Y pinned to 0 dB).
void DoReverbPreFilterGraph(GuiState& g, Rect viewport_r, bool greyed_out);

// Reverb post-shelf graph: combined low-shelf + high-shelf response. Two grabbers, one per
// shelf, draggable on X (cutoff) and Y (gain, 0 dB to -24 dB).
void DoReverbPostShelfGraph(GuiState& g, Rect viewport_r, bool greyed_out);

// Convolution reverb high-pass graph: single cutoff handle for `ConvolutionReverbHighpass`.
// Drag X = cutoff.
void DoConvolutionReverbHighpassGraph(GuiState& g, Rect viewport_r, bool greyed_out);

// Delay filter graph: bandpass centred on `DelayFilterCutoffSemitones`, width controlled by
// `DelayFilterSpread` (radius in octaves = spread * 8). Single grabber: drag X = cutoff; drag
// Y = spread (handle Y position tracks spread, top of viewport = max spread).
void DoDelayFilterGraph(GuiState& g, Rect viewport_r, bool greyed_out);

// Per-layer 3-band EQ graph. One handle per band (drag X = freq, drag Y = gain when the band uses
// gain, wheel = resonance, right-click for band type).
void DoEqGraph(GuiState& g, u8 layer_index, Rect viewport_r, bool greyed_out);
