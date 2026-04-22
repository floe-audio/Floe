// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/arpeggiator.hpp"
#include "gui/core/gui_fwd.hpp"

struct ArpeggiatorState;

void DoArpStepSequencer(GuiState& g,
                        ArpeggiatorState& arp_state,
                        Rect rect,
                        ArpGuiSnapshot const& snapshot,
                        u32 playing_step,
                        bool& show_all);
