// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/macros.hpp"

#include "gui_framework/gui_builder.hpp"

struct GuiState;

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

    DynamicArrayBounded<TrivialFixedSizeFunction<64, void(GuiState&)>, 4> draw_overlays {};

    struct HotDestinationParam {
        Rect r {};
        ParamIndex param_index;
    };
    Optional<HotDestinationParam> hot_destination_param {};

    // If set, open a text editor for this destination knob on the next frame.
    struct DestinationTextEditor {
        u8 macro_index;
        u8 destination_index;
    };
    Optional<DestinationTextEditor> destination_text_editor_to_open {};

    // If set, open the range editor popup for this destination knob on the next frame.
    struct DestinationRangeEditor {
        u8 macro_index;
        u8 destination_index;
    };
    Optional<DestinationRangeEditor> range_editor_to_open {};

    bool range_editor_keeps_current_value = false;

    // Captured when a range knob starts dragging so every frame of the drag solves from the same starting
    // state.
    struct RangeEditDragStart {
        Array<f32, k_num_parameters> param_values;
        MacroDestinations macros;
    };
    Optional<RangeEditDragStart> range_edit_drag_start {};

    // Set each frame by whatever wants to audition a macro position; sent to the audio thread at the end of
    // the frame.
    Optional<MacroPositionOverride> macro_audition_request {};

    imgui::Id open_remove_destination_button_id {0};
};

void DoMacrosEditGui(GuiState& g, Box const& parent);

void OverlayMacroDestinationRegion(GuiState& g, Rect window_r, ParamIndex param_index);

void MacroGuiBeginFrame(GuiState& g);
void MacroGuiEndFrame(GuiState& g);
