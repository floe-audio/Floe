// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui/core/gui_fwd.hpp"

// Preview of the bit crushing: a full-scale sine wave quantised to the current Bits and sample-and-held at
// the current Sample Rate. Drawn directly from the two parameter values rather than from the effect's DSP. A
// pure function of those values; not interactive.
void DoBitCrushDisplay(GuiState& g, Rect viewport_r, bool greyed_out);
