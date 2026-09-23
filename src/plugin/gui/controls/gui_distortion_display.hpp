// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui/core/gui_fwd.hpp"
#include "processing_utils/distortion.hpp"

struct DistortionDisplayState {
    static constexpr u32 k_samples_per_cycle = 240; // 200 Hz at the preview's 48 kHz
    static constexpr u32 k_cycles_drawn = 2;
    static constexpr u32 k_drawn_samples = k_samples_per_cycle * k_cycles_drawn;

    struct Inputs {
        DistortionType type;
        f32 drive01;
        f32 punish01;
        f32 tilt;
        f32 auto_gain01;
        bool operator==(Inputs const&) const = default;
    };

    // The rendered tone is only recomputed when the inputs change.
    bool valid = false;
    Inputs inputs {};
    Array<f32, k_drawn_samples> wet {};
    DistortionDsp dsp {};
};

// Preview of the distortion's shaping: a fixed test tone rendered through the real distortion DSP at the
// current Type, Drive, Punish and Tilt, with Gain applied to the drawing. Shows the wet signal only. A pure
// function of the parameter values; not interactive.
void DoDistortionDisplay(GuiState& g, Rect viewport_r, bool greyed_out);
