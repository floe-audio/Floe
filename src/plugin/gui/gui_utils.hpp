// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui_fwd.hpp"

void HandleShowingTextEditorForParams(GuiState& g, Rect r, Span<ParamIndex const> params);

struct TooltipOptions {
    bool ignore_show_tooltips_preference = false;
    bool show_left_or_right = false;
};
bool Tooltip(GuiState& g, imgui::Id id, Rect window_r, String str, TooltipOptions const& options);

void DoParameterTooltipIfNeeded(GuiState& g,
                                DescribedParamValue const& param,
                                imgui::Id imgui_id,
                                Rect window_r);
void DoParameterTooltipIfNeeded(GuiState& g,
                                Span<DescribedParamValue const*> param,
                                imgui::Id imgui_id,
                                Rect window_r);
void ParameterValuePopup(GuiState& g, DescribedParamValue const& param, imgui::Id id, Rect window_r);
void ParameterValuePopup(GuiState& g, Span<DescribedParamValue const*> params, imgui::Id id, Rect window_r);
