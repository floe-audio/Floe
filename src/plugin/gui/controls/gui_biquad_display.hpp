// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/core/gui_fwd.hpp"
#include "processing_utils/filters.hpp"

namespace biquad_display {

constexpr f32 k_min_db = -24.0f;
constexpr f32 k_max_db = 24.0f;
constexpr f32 k_sample_rate = 48000.0f;

f32 MagnitudeDb(rbj_filter::Coeffs const& c, f32 frequency_hz);

f32 DbToY(f32 db, Rect viewport_r);

void DrawBackground(imgui::Context& imgui, Rect viewport_r, ParamDescriptor const& freq_param_info);

// Draws the area fill and polyline for the combined frequency response of the given coefficients.
void DrawResponseCurve(imgui::Context& imgui,
                       Rect viewport_r,
                       Span<rbj_filter::Coeffs const> coeffs,
                       ParamDescriptor const& freq_param_info,
                       bool greyed_out);

void DrawHandle(imgui::Context& imgui,
                f32x2 pos_in_window,
                f32 radius,
                imgui::Id interaction_id,
                bool greyed_out);

} // namespace biquad_display
