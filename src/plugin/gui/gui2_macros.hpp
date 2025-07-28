// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/macros.hpp"

#include "gui_framework/gui_box_system.hpp"

struct Gui;

struct MacrosGuiState {
    // If set, we're in 'macro destination select mode'. The value is the index of the macro that we want to
    // connect.
    Optional<u8> macro_destination_select_mode {};

    // The destination knob that is currently active. We use this to highlight the parameters that it's linked
    // to.
    struct DestinationKnob {
        MacroDestination& dest;
        Rect r;
    };
    Optional<DestinationKnob> active_destination_knob {};

    DynamicArrayBounded<TrivialFixedSizeFunction<64, void(Gui*)>, 4> draw_overlays {};

    struct HotDestinationParam {
        Rect r {};
        ParamIndex param_index;
    };
    Optional<HotDestinationParam> hot_destination_param {};

    imgui::Id open_remove_destination_button_id {0};
};

void DoMacrosEditGui(Gui* g, Box const& parent);

void MacroAddDestinationRegion(Gui* g, Rect r, ParamIndex param_index);

void MacroGuiBeginFrame(Gui* g);
void MacroGuiEndFrame(Gui* g);
