// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/core/gui_fwd.hpp"

struct LayerProcessor;

struct WaveformGuiOptions {
    bool handles_follow_cursor {};
    // If set, show controls relevant for that play mode (loop handles, sample offset, etc).
    // If nullopt, just display the waveform with voice markers.
    Optional<param_values::PlayMode> play_mode {};
};

void DoWaveformElement(GuiState& g,
                       LayerProcessor& layer,
                       Rect viewport_r,
                       WaveformGuiOptions const& options = {});
