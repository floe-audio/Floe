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
#include "processing_utils/synced_timings.hpp"
#include "processor/processor.hpp"

constexpr usize k_lfo_curve_points = 480;
// Display window in seconds at the reference tempo. Picked so that a quarter-note synced rate at
// 120 BPM (2 Hz) shows ~4 cycles, which reads as a moderate rate.
constexpr f32 k_viewport_seconds = 2.0f;
constexpr f64 k_reference_bpm = 120.0;

static f32 Smoothstep(f32 t) { return t * t * (3.0f - (2.0f * t)); }

// Stable hash to [-1, 1] so random previews don't visibly repeat across the viewport.
static f32 RandomStepValue(usize step_index) {
    auto x = (u32)step_index * 2654435761u;
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    return ((f32)(x & 0xffffu) / 32767.5f) - 1.0f;
}

// Returns a waveform value in [-1, 1] for the given shape at phase across all drawn cycles.
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
            constexpr f32 k_steps_per_cycle = 1.0f;
            return RandomStepValue((usize)(phase * k_steps_per_cycle));
        }
        case param_values::LfoShape::RandomGlide: {
            constexpr f32 k_steps_per_cycle = 1.0f;
            auto const scaled = phase * k_steps_per_cycle;
            auto const step_index = (usize)scaled;
            auto const t = scaled - Floor(scaled);
            return RandomStepValue(step_index) +
                   ((RandomStepValue(step_index + 1) - RandomStepValue(step_index)) * Smoothstep(t));
        }
        case param_values::LfoShape::Pluck: return 1.0f - (2.0f * Exp(-cycle_phase / 0.15f));
        case param_values::LfoShape::PluckSharp: return 1.0f - (2.0f * Exp(-cycle_phase / 0.05f));
        case param_values::LfoShape::PulseNarrow: return cycle_phase < 0.25f ? -1.0f : 1.0f;
        case param_values::LfoShape::PulseWide: return cycle_phase < 0.75f ? -1.0f : 1.0f;
        case param_values::LfoShape::Trapezoid: {
            constexpr f32 k_ramp = 16.0f / 256.0f;
            if (cycle_phase < 0.5f - k_ramp) return 1.0f;
            if (cycle_phase < 0.5f) return 1.0f - (2.0f * (cycle_phase - (0.5f - k_ramp)) / k_ramp);
            if (cycle_phase < 1.0f - k_ramp) return -1.0f;
            return -1.0f + (2.0f * (cycle_phase - (1.0f - k_ramp)) / k_ramp);
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
    auto const sync_on = params.BoolValue(layer_index, LayerParamIndex::LfoSyncSwitch);
    auto const rate_param =
        params.DescribedValue(layer_index,
                              sync_on ? LayerParamIndex::LfoRateTempoSynced : LayerParamIndex::LfoRateHz);

    auto const shape = shape_param.IntValue<param_values::LfoShape>();
    auto const amount_linear =
        AdjustedLinearValue(params, macro_dests, amount_param.LinearValue(), amount_param.info.index);
    auto const amount = Clamp(amount_linear, -1.0f, 1.0f);

    auto const rate_adj_linear =
        AdjustedLinearValue(params, macro_dests, rate_param.LinearValue(), rate_param.info.index);

    // Convert rate to Hz at a reference tempo so the display speed scales with the actual rate
    // value rather than its linear position in the param range.
    auto const rate_hz = ({
        f32 hz;
        if (sync_on) {
            auto const synced = (param_values::LfoSyncedRate)Clamp(Round(rate_adj_linear),
                                                                   rate_param.info.linear_range.min,
                                                                   rate_param.info.linear_range.max);
            hz = SyncedTimeToHz(k_reference_bpm, SyncedTimesFromParam(synced));
        } else {
            hz = rate_param.info.ProjectValue(rate_adj_linear);
        }
        hz;
    });

    // Random modes run at 2x rate in the DSP (see lfo.hpp's RecomputePhaseIncrement); mirror here.
    auto const is_random =
        shape == param_values::LfoShape::RandomSteps || shape == param_values::LfoShape::RandomGlide;
    auto const rate_multiplier = is_random ? 2.0f : 1.0f;
    auto const raw_cycles = rate_hz * k_viewport_seconds * rate_multiplier;

    // Above this many cycles the polyline aliases against the pixel grid (moiré on dense
    // near-vertical segments). Compress logarithmically so each higher rate still looks distinct
    // without packing more crossings per pixel.
    constexpr f32 k_cycle_soft_cap = 12.0f;
    auto const cycles_to_draw = raw_cycles <= k_cycle_soft_cap
                                    ? raw_cycles
                                    : k_cycle_soft_cap + (Log2(raw_cycles / k_cycle_soft_cap) * 1.5f);

    auto const half_h = viewport_r.h * 0.5f;

    DynamicArrayBounded<f32x2, k_lfo_curve_points> curve_points;
    for (auto const i : Range(k_lfo_curve_points)) {
        auto const t = (f32)i / (f32)(k_lfo_curve_points - 1);
        auto const phase = t * cycles_to_draw;
        // The DSP negates the raw LFO output before applying to parameters; mirror that here so
        // the visual matches what's audible.
        auto const value = -LfoShapeValue(shape, phase) * amount;
        auto const x = viewport_r.x + (t * viewport_r.w);
        auto const y = centre_y - (value * half_h);
        dyn::Append(curve_points, imgui.ViewportPosToWindowPos({x, y}));
    }

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
