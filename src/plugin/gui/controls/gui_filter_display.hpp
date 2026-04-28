// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/core/gui_fwd.hpp"
#include "processing_utils/filters.hpp"

namespace filter_display {

constexpr f32 k_min_db = -24.0f;
constexpr f32 k_max_db = 24.0f;
constexpr f32 k_sample_rate = 48000.0f;

f32 DbToY(f32 db, Rect viewport_r);

// `freq_param_info` defines the visualiser's X-axis: its projection (now log) maps linear [0,1]
// to Hz. All freq params share the same range so any one of them works as the reference.
void DrawBackground(imgui::Context& imgui, Rect viewport_r, ParamDescriptor const& freq_param_info);

// Draws the area fill and polyline for a frequency response. The caller supplies a function
// that returns the response magnitude in dB at a given frequency in Hz.
void DrawResponseCurve(imgui::Context& imgui,
                       Rect viewport_r,
                       TrivialFunctionRef<f32(f32 freq_hz)> magnitude_db,
                       ParamDescriptor const& freq_param_info,
                       bool greyed_out);

void DrawHandle(imgui::Context& imgui,
                f32x2 pos_in_window,
                f32 radius,
                imgui::Id interaction_id,
                bool greyed_out);

} // namespace filter_display
