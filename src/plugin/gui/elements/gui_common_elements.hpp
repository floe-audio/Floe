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

// Reusable row with prev/next arrow buttons. Add your content to the row, then call
// DoMidPanelPrevNextButtons.
Box DoMidPanelPrevNextRow(GuiBuilder& builder, Box parent, f32 width);

struct MidPanelPrevNextButtonsResult {
    bool prev_fired;
    bool next_fired;
};
struct MidPanelPrevNextButtonsOptions {
    String prev_tooltip {"Previous"};
    String next_tooltip {"Next"};
};
MidPanelPrevNextButtonsResult
DoMidPanelPrevNextButtons(GuiBuilder& builder, Box row, MidPanelPrevNextButtonsOptions const& options = {});

struct MidPanelShuffleButtonOptions {
    String tooltip {"Shuffle"};
};
Box DoMidPanelShuffleButton(GuiBuilder& builder, Box row, MidPanelShuffleButtonOptions const& options = {});
