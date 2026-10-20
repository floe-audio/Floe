// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/core/gui_fwd.hpp"
#include "processing_utils/arpeggiator.hpp"

struct ArpeggiatorState;

struct ArpStepSequencerAreas {
    Rect velocity_bars; // window coords, visible part only
    Rect step_rows; // window coords, visible part only
    // Each row of step_rows, window coords. Absent in overview mode, or when the row is hidden.
    Optional<Rect> step_number_row;
    Optional<Rect> pitch_row;
    Optional<Rect> tie_row;
    Optional<Rect> gate_row;
};

ArpStepSequencerAreas DoArpStepSequencer(GuiState& g,
                                         ArpeggiatorState& arp_state,
                                         Rect rect,
                                         ArpGuiSnapshot const& snapshot,
                                         u32 playing_step,
                                         bool& show_all);
