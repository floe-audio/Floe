// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/core/gui_fwd.hpp"
#include "processing_utils/arpeggiator.hpp"

struct ArpeggiatorState;

void DoArpStepSequencer(GuiState& g,
                        ArpeggiatorState& arp_state,
                        Rect rect,
                        ArpGuiSnapshot const& snapshot,
                        u32 playing_step,
                        bool& show_all);
