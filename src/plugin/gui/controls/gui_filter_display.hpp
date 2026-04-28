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

// X-axis is log-frequency (equal pixels per octave) over this fixed range, regardless of which
// parameter drives the visualiser. Picked to put C5 (≈ 523 Hz) close to the centre.
constexpr f32 k_min_hz = 13.75f; // A-1
constexpr f32 k_max_hz = 22000.0f;

f32 HzToX01(f32 hz);
f32 X01ToHz(f32 t);

f32 DbToY(f32 db, Rect viewport_r);

void DrawBackground(imgui::Context& imgui, Rect viewport_r);

// Draws the area fill and polyline for a frequency response. The caller supplies a function
// that returns the response magnitude in dB at a given frequency in Hz.
void DrawResponseCurve(imgui::Context& imgui,
                       Rect viewport_r,
                       TrivialFunctionRef<f32(f32 freq_hz)> magnitude_db,
                       bool greyed_out);

void DrawHandle(imgui::Context& imgui,
                f32x2 pos_in_window,
                f32 radius,
                imgui::Id interaction_id,
                bool greyed_out);

} // namespace filter_display
