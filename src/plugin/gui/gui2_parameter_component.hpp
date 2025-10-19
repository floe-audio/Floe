// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_framework/gui_box_system.hpp"
#include "gui_fwd.hpp"

// Box-system version of gui_widget_compounds.hpp

struct AudioProcessor;

struct ParameterComponentOptions {
    style::Colour knob_highlight_col = style::Colour::Highlight;
    style::Colour knob_line_col = style::Colour::Background0;
    bool greyed_out = false;
    bool is_fake = false;
    bool label = true;
    String override_tooltip {};
    String override_label {};
};

Box DoParameterComponent(Gui* g,
                         Box parent,
                         DescribedParamValue const& param,
                         ParameterComponentOptions const& options = {});
