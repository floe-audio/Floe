// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_button_widgets.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/layout.hpp"

namespace labels {

using Style = buttons::Style;

PUBLIC Style FakeMenuItem(imgui::Context const& imgui) { return buttons::MenuItem(imgui, false, true); }

PUBLIC Style CentredLeft(imgui::Context const&, u32 col) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::CentredLeft;
    s.icon_or_text.add_margin_x = true;
    s.main_cols.reg = col;
    return s;
}

PUBLIC Style TopLeft(imgui::Context const& imgui, u32 col) {
    auto s = CentredLeft(imgui, col);
    s.icon_or_text.justification = TextJustification::TopLeft;
    return s;
}

PUBLIC Style Title(imgui::Context const& imgui, u32 col) {
    auto s = TopLeft(imgui, col);
    s.icon_or_text.justification = TextJustification::CentredLeft;
    return s;
}

PUBLIC Style BrowserHeading(imgui::Context const& imgui) {
    return CentredLeft(imgui, LiveCol(imgui, UiColMap::BrowserHeading));
}

PUBLIC Style PresetBrowserFolder(imgui::Context const& imgui) {
    auto s = CentredLeft(imgui, LiveCol(imgui, UiColMap::PresetBrowserFileFolderText));
    s.icon_or_text.justification = TextJustification::Baseline | TextJustification::Left;
    return s;
}

PUBLIC Style PresetBrowserFolderPath(imgui::Context const& imgui) {
    auto s = CentredLeft(imgui, LiveCol(imgui, UiColMap::PresetBrowserFileFolderTextPath));
    s.icon_or_text.justification = TextJustification::Baseline | TextJustification::Right;
    s.icon_or_text.overflow_type = TextOverflowType::ShowDotsOnLeft;
    return s;
}

PUBLIC Style PresetSectionHeading(imgui::Context const& imgui) {
    return CentredLeft(imgui, LiveCol(imgui, UiColMap::BrowserSectionHeading));
}

PUBLIC Style WaveformLabel(imgui::Context const& imgui) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::Centred;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.capitalise = false;
    s.main_cols.reg = LiveCol(imgui, UiColMap::Waveform_Label);
    return s;
}

PUBLIC Style Parameter(imgui::Context const& imgui, bool greyed_out = false) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::CentredLeft;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.capitalise = false;
    s.main_cols.reg = greyed_out ? LiveCol(imgui, UiColMap::ParameterLabelGreyedOut)
                                 : LiveCol(imgui, UiColMap::ParameterLabel);
    return s;
}

PUBLIC Style ParameterCentred(imgui::Context const& imgui, bool greyed_out = false) {
    auto s = Parameter(imgui, greyed_out);
    s.icon_or_text.justification = TextJustification::HorizontallyCentred;
    return s;
}

PUBLIC Style ErrorWindowLabel(imgui::Context const& imgui) {
    auto s = CentredLeft(imgui, LiveCol(imgui, UiColMap::PopupItemText));
    s.icon_or_text.add_margin_x = false;
    return s;
}

PUBLIC Style WaveformLoadingLabel(imgui::Context const& imgui) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::Centred;
    s.main_cols.reg = LiveCol(imgui, UiColMap::Waveform_LoadingText);
    return s;
}

void Label(Gui* g, Rect r, String str, Style const& style);
void Label(Gui* g, ::Parameter const& param, Rect r, Style const& style);

void Label(Gui* g, layout::Id r, String str, Style const& style);
void Label(Gui* g, ::Parameter const& param, layout::Id r, Style const& style);

} // namespace labels
