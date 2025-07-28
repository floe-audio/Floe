// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/layout.hpp"
#include "gui_fwd.hpp"

namespace knobs {

struct Style {
    Style& GreyedOut(bool state) {
        greyed_out = state;
        return *this;
    }

    bool bidirectional {};
    bool greyed_out {};
    u32 highlight_col {};
    u32 line_col {};
    bool is_fake {};
    Optional<f32> overload_position {};
    Optional<f32> outer_arc_percent {};
};

PUBLIC Style DefaultKnob(imgui::Context const& imgui, Optional<u32> _highlight_col = {}) {
    Style s {};
    s.highlight_col = _highlight_col ? *_highlight_col : LiveCol(imgui, UiColMap::KnobOuterArcBright);
    s.line_col = _highlight_col ? LiveCol(imgui, UiColMap::FXKnobLine) : LiveCol(imgui, UiColMap::KnobLine);
    return s;
}

PUBLIC Style BidirectionalKnob(imgui::Context const& imgui, Optional<u32> _highlight_col = {}) {
    auto s = DefaultKnob(imgui, _highlight_col);
    s.bidirectional = true;
    return s;
}

PUBLIC Style FakeKnobStyle(imgui::Context const& imgui) {
    auto s = DefaultKnob(imgui);
    s.is_fake = true;
    return s;
}

bool Knob(Gui* g, imgui::Id id, Rect r, f32& percent, f32 default_percent, Style const& style);
bool Knob(Gui* g, imgui::Id id, DescribedParamValue const& param, Rect r, Style const& style);
bool Knob(Gui* g, DescribedParamValue const& param, Rect r, Style const& style);

bool Knob(Gui* g, imgui::Id id, layout::Id r, f32& percent, f32 default_percent, Style const& style);
bool Knob(Gui* g, imgui::Id id, DescribedParamValue const& param, layout::Id r, Style const& style);
bool Knob(Gui* g, DescribedParamValue const& param, layout::Id r, Style const& style);

void FakeKnob(Gui* g, Rect r);

} // namespace knobs
