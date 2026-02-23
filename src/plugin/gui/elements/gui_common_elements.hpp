// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/core/gui_fwd.hpp"
#include "gui_framework/gui_builder.hpp"

struct TooltipOptions {
    Optional<Rect> avoid_r {}; // If nullopt, uses the window_r.
    bool ignore_show_tooltips_preference = false;
    TooltipJustification justification = TooltipJustification::AboveOrBelow;
};
bool Tooltip(GuiState& g, imgui::Id id, Rect window_r, String str, TooltipOptions const& options);

f32 TextButtonHeight();

// Reusable row with prev/next arrow buttons. Add your content to the row, then call
// DoMidPanelPrevNextButtons.
Box DoMidPanelPrevNextRow(GuiBuilder& builder, Box parent, f32 width);

struct MidPanelPrevNextButtonsResult {
    bool prev_fired;
    bool next_fired;
};
struct MidPanelPrevNextButtonsOptions {
    bool greyed_out = false;
    String prev_tooltip {"Previous"};
    String next_tooltip {"Next"};
};
MidPanelPrevNextButtonsResult
DoMidPanelPrevNextButtons(GuiBuilder& builder, Box row, MidPanelPrevNextButtonsOptions const& options = {});

struct MidPanelShuffleButtonOptions {
    bool greyed_out = false;
    String tooltip {"Shuffle"};
};
Box DoMidPanelShuffleButton(GuiBuilder& builder, Box row, MidPanelShuffleButtonOptions const& options = {});

// Toggle icon for use inside a parent container with button_behaviour.
struct ToggleIconOptions {
    bool state;
    bool greyed_out = false;
    bool parent_dictates_hot_and_active = true;
    f32 width = 0; // 0 means use default icon width
    TextJustification justify = TextJustification::CentredLeft;
    Optional<Col> on_colour {}; // Custom colour for the "on" state icon.
};
Box DoToggleIcon(GuiBuilder& builder, Box parent, ToggleIconOptions const& options);
