// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_label_widgets.hpp"

#include "../gui_state.hpp"
#include "processor/param.hpp"

namespace labels {

//
//
//
void Label(GuiState& g, Rect r, String str, Style const& style) { buttons::FakeButton(g, r, str, style); }

void Label(GuiState& g, ::DescribedParamValue const& param, Rect r, Style const& style) {
    Label(g, r, param.info.gui_label, style);
}

void Label(GuiState& g, layout::Id id, String str, Style const& style) {
    Label(g, layout::GetRect(g.layout, id), str, style);
}

void Label(GuiState& g, ::DescribedParamValue const& param, layout::Id id, Style const& style) {
    Label(g, param, layout::GetRect(g.layout, id), style);
}

} // namespace labels
