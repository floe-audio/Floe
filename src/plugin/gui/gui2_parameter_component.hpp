// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_framework/gui_box_system.hpp"

// Box-system version of gui_widget_compounds.hpp

struct AudioProcessor;

struct ParameterComponentOptions {
    Optional<UiSizeId> size_index_for_gapx = {};
    bool set_gapx_independent_of_size = false;
    bool set_bottom_gap_independent_of_size = false;
    style::Colour knob_highlight_col = style::Colour::Highlight;
    style::Colour knob_line_col = style::Colour::Background0;
    bool greyed_out = false;
    bool is_fake = false;
    String override_tooltip = {};
};

Box DoParameterComponent(GuiBoxSystem& builder,
                         Box parent,
                         Parameter const& param,
                         AudioProcessor& processor,
                         ParameterComponentOptions const& options = {});
