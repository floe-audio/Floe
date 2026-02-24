// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/core/gui_fwd.hpp"
#include "gui/panels/gui_layer.hpp"

struct LayerProcessor;

struct WaveformGuiOptions {
    bool handles_follow_cursor {};
};

void DoWaveformElement(GuiState& g,
                       LayerProcessor& layer,
                       Rect viewport_r,
                       WaveformGuiOptions const& options = {});
