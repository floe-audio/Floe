// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_framework/gui_builder.hpp"
#include "gui_fwd.hpp"

// Builder version of gui_widget_compounds.hpp

struct AudioProcessor;

struct ParameterComponentOptions {
    enum class Size { Small, Medium, Large };
    Size size {Size::Medium};
    Col knob_highlight_col = {Col::Highlight};
    Col knob_line_col = {Col::Background0};
    bool greyed_out = false;
    bool is_fake = false;
    bool label = true;
    String override_tooltip {};
    String override_label {};
};

Box DoKnobParameter(GuiState& g,
                    Box parent,
                    DescribedParamValue const& param,
                    ParameterComponentOptions const& options = {});

struct MenuParameterComponentOptions {
    f32 width = 0; // 0 means auto-size from the widest menu item string.
    bool greyed_out = false;
    bool label = true;
    String override_tooltip {};
    String override_label {};
};

Box DoMenuParameter(GuiState& g,
                    Box parent,
                    DescribedParamValue const& param,
                    MenuParameterComponentOptions const& options = {});

struct ButtonParameterComponentOptions {
    f32 width = 0; // 0 means auto-size from text + icon.
    bool greyed_out = false;
    String override_tooltip {};
    String override_label {};
};

Box DoButtonParameter(GuiState& g,
                      Box parent,
                      DescribedParamValue const& param,
                      ButtonParameterComponentOptions const& options = {});
