// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_lfo_display.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/processor.hpp"

constexpr usize k_lfo_curve_points = 240;
constexpr usize k_cycles_to_draw = 2;

// Deterministic "random" sequence so the random-mode previews are stable rather than flickering.
constexpr Array<f32, 8> k_random_steps_preview {
    0.45f,
    -0.72f,
    0.15f,
    -0.30f,
    0.88f,
    -0.55f,
    0.10f,
    -0.92f,
};

static f32 Smoothstep(f32 t) { return t * t * (3.0f - (2.0f * t)); }

// Returns a waveform value in [-1, 1] for the given shape at phase in [0, k_cycles_to_draw).
static f32 LfoShapeValue(param_values::LfoShape shape, f32 phase) {
    auto const cycle_phase = phase - Floor(phase);

    switch (shape) {
        case param_values::LfoShape::Sine: return Sin(2.0f * k_pi<f32> * cycle_phase);
        case param_values::LfoShape::Triangle: {
            if (cycle_phase < 0.25f) return cycle_phase * 4.0f;
            if (cycle_phase < 0.5f) return 2.0f - (cycle_phase * 4.0f);
            if (cycle_phase < 0.75f) return 2.0f - (cycle_phase * 4.0f);
            return -4.0f + (cycle_phase * 4.0f);
        }
        case param_values::LfoShape::Sawtooth: return (cycle_phase * 2.0f) - 1.0f;
        case param_values::LfoShape::Square: return cycle_phase < 0.5f ? 1.0f : -1.0f;
        case param_values::LfoShape::RandomSteps: {
            auto const step_index =
                (usize)(cycle_phase * (f32)k_random_steps_preview.size) % k_random_steps_preview.size;
            return k_random_steps_preview[step_index];
        }
        case param_values::LfoShape::RandomGlide: {
            auto const scaled = cycle_phase * (f32)k_random_steps_preview.size;
            auto const step_index = (usize)scaled % k_random_steps_preview.size;
            auto const next_index = (step_index + 1) % k_random_steps_preview.size;
            auto const t = scaled - Floor(scaled);
            auto const s = Smoothstep(t);
            return k_random_steps_preview[step_index] +
                   ((k_random_steps_preview[next_index] - k_random_steps_preview[step_index]) * s);
        }
        case param_values::LfoShape::Pluck: return 1.0f - (2.0f * Exp(-cycle_phase / 0.15f));
        case param_values::LfoShape::PluckSharp: return 1.0f - (2.0f * Exp(-cycle_phase / 0.05f));
        case param_values::LfoShape::PulseNarrow: return cycle_phase < 0.25f ? -1.0f : 1.0f;
        case param_values::LfoShape::PulseWide: return cycle_phase < 0.75f ? -1.0f : 1.0f;
        case param_values::LfoShape::Trapezoid: {
            constexpr f32 ramp = 16.0f / 256.0f;
            if (cycle_phase < 0.5f - ramp) return 1.0f;
            if (cycle_phase < 0.5f) return 1.0f - (2.0f * (cycle_phase - (0.5f - ramp)) / ramp);
            if (cycle_phase < 1.0f - ramp) return -1.0f;
            return -1.0f + (2.0f * (cycle_phase - (1.0f - ramp)) / ramp);
        }
        case param_values::LfoShape::Count: break;
    }
    return 0;
}

void DoLfoDisplay(GuiState& g, u8 layer_index, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;

    auto const& macro_dests = engine.processor.main_macro_destinations;

    // Background.
    auto const window_rect = imgui.ViewportRectToWindowRect(viewport_r);
    imgui.draw_list->AddRectFilled(window_rect, LiveCol(UiColMap::EqBack), WwToPixels(k_corner_rounding));

    // Centre line.
    auto const centre_y = viewport_r.y + (viewport_r.h * 0.5f);
    {
        auto const p0 = imgui.ViewportPosToWindowPos({viewport_r.x, centre_y});
        auto const p1 = imgui.ViewportPosToWindowPos({viewport_r.Right(), centre_y});
        imgui.draw_list->AddLine(p0, p1, LiveCol(UiColMap::EqGridZero));
    }

    auto const shape_param = params.DescribedValue(layer_index, LayerParamIndex::LfoShape);
    auto const amount_param = params.DescribedValue(layer_index, LayerParamIndex::LfoAmount);

    auto const shape = shape_param.IntValue<param_values::LfoShape>();
    auto const amount_linear =
        AdjustedLinearValue(params, macro_dests, amount_param.LinearValue(), amount_param.info.index);

    // Amount param is a BidirectionalPercent with linear range [-1, 1]; use directly.
    auto const amount = Clamp(amount_linear, -1.0f, 1.0f);

    // Half of the viewport height is the full +/-1 amplitude.
    auto const half_h = viewport_r.h * 0.5f;

    DynamicArrayBounded<f32x2, k_lfo_curve_points> curve_points;
    for (auto const i : Range(k_lfo_curve_points)) {
        auto const t = (f32)i / (f32)(k_lfo_curve_points - 1);
        auto const phase = t * (f32)k_cycles_to_draw;
        // The DSP negates the raw LFO output before applying to parameters; mirror that here so
        // the visual matches what's audible.
        auto const value = -LfoShapeValue(shape, phase) * amount;
        auto const x = viewport_r.x + (t * viewport_r.w);
        auto const y = centre_y - (value * half_h);
        dyn::Append(curve_points, imgui.ViewportPosToWindowPos({x, y}));
    }

    // Area fill between curve and centre line (per-segment quads, no AA to avoid seams).
    auto const zero_y_window = imgui.ViewportPosToWindowPos({viewport_r.x, centre_y}).y;
    auto const area_col = LiveCol(UiColMap::EqArea);
    for (auto const i : Range(curve_points.size - 1)) {
        auto const p0 = curve_points[i];
        auto const p1 = curve_points[i + 1];
        f32x2 const verts[] = {
            f32x2 {p0.x, zero_y_window},
            p0,
            p1,
            f32x2 {p1.x, zero_y_window},
        };
        imgui.draw_list->AddConvexPolyFilled(verts, area_col, false);
    }

    auto const line_col = greyed_out ? LiveCol(UiColMap::EqLineGreyedOut) : LiveCol(UiColMap::EqLine);
    imgui.draw_list->AddPolyline(curve_points, line_col, false, 1.5f, true);
}
