// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_button_widgets.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/layout.hpp"

namespace labels {

using Style = buttons::Style;

PUBLIC Style FakeMenuItem(imgui::Context const& imgui) { return buttons::MenuItem(imgui, false, true); }

PUBLIC Style WaveformLabel(imgui::Context const&) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::Centred;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.capitalise = false;
    s.main_cols.reg = LiveCol(UiColMap::MidText);
    return s;
}

PUBLIC Style Parameter(imgui::Context const&, bool greyed_out = false) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::CentredLeft;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.capitalise = false;
    s.main_cols.reg = greyed_out ? LiveCol(UiColMap::MidTextDimmed) : LiveCol(UiColMap::MidText);
    return s;
}

PUBLIC Style ParameterCentred(imgui::Context const& imgui, bool greyed_out = false) {
    auto s = Parameter(imgui, greyed_out);
    s.icon_or_text.justification = TextJustification::HorizontallyCentred;
    return s;
}

PUBLIC Style WaveformLoadingLabel(imgui::Context const&) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::Centred;
    s.main_cols.reg = LiveCol(UiColMap::WaveformLoadingText);
    return s;
}

void Label(GuiState& g, Rect r, String str, Style const& style);
void Label(GuiState& g, DescribedParamValue const& param, Rect r, Style const& style);

void Label(GuiState& g, layout::Id r, String str, Style const& style);
void Label(GuiState& g, DescribedParamValue const& param, layout::Id r, Style const& style);

} // namespace labels
