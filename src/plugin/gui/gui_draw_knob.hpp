// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_framework/gui_imgui.hpp"

struct DrawKnobOptions {
    u32 highlight_col;
    u32 line_col;
    Optional<f32> overload_position;
    Optional<f32> outer_arc_percent;
    bool greyed_out;
    bool is_fake;
    bool bidirectional;
};

void DrawKnob(imgui::Context& imgui, imgui::Id id, Rect r, f32 percent, DrawKnobOptions const& style);
