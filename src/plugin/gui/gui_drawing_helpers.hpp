// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/style.hpp"
#include "processing_utils/peak_meter.hpp"

void DrawDropShadow(imgui::Context const& imgui, Rect r, Optional<f32> rounding = {});

void DrawVoiceMarkerLine(imgui::Context const& imgui,
                         f32x2 pos,
                         f32 height,
                         f32 left_min,
                         Optional<Line> upper_line,
                         f32 opacity = 1);

void DrawParameterTextInput(imgui::Context const& imgui, Rect r, imgui::TextInputResult const& result);

struct DrawTextInputConfig {
    style::Colour text_col = style::Colour::Text;
    style::Colour cursor_col = style::Colour::Text;
    style::Colour selection_col = style::Colour::Highlight | style::Colour::Alpha50;
};

void DrawTextInput(imgui::Context const& imgui,
                   imgui::TextInputResult const& result,
                   DrawTextInputConfig const& config = {});

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

void DrawPeakMeter(imgui::Context& imgui, Rect r, StereoPeakMeter const& level, bool flash_when_clipping);

void DrawOverlayTooltipForRect(imgui::Context const& imgui, Fonts& fonts, String str, Rect window_r);
