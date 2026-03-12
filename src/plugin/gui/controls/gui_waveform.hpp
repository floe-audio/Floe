// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "os/misc.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/core/gui_fwd.hpp"

struct LayerProcessor;

struct WaveformHashDebounce {
    u64 displayed_hash {};
    u64 last_raw_hash {};
    TimePoint last_change_time {};
    bool locked {};
};

struct WaveformGuiOptions {
    bool handles_follow_cursor {};
    // If set, show controls relevant for that play mode (loop handles, sample offset, etc).
    // If nullopt, just display the waveform with voice markers.
    Optional<param_values::PlayMode> play_mode {};
    // If true, only draw the background and waveform image. No voice markers, no multisample badge,
    // no loading text, no controls.
    bool waveform_only {};
};

void DoWaveformElement(GuiState& g,
                       LayerProcessor& layer,
                       Rect viewport_r,
                       WaveformGuiOptions const& options = {});
