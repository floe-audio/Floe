// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_bit_crush_display.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/macros.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/processor.hpp"

// One cycle of the tone is drawn, so sample rate reduction can only show as the number of held steps across
// the display. For any fixed tone frequency that number is either already smooth at the top of the
// parameter's range or collapsed to a couple of flat steps over most of it, so the step count is mapped
// logarithmically across the parameter's range instead: the staircase shape is real, the number of steps is
// indicative.
constexpr f32 k_min_steps = 2.5f;
constexpr u32 k_max_steps = 128;

// Below this rate the effect starts to bite; above it, barely. The step-count curve is biased around this
// point rather than spread evenly across the parameter's range, so the display stays close to smooth above it
// and falls away quickly below it - matching what's actually audible rather than the raw parameter shape.
constexpr f32 k_step_curve_breakpoint_hz = 10000.0f;
constexpr f32 k_step_curve_smooth_fraction = 0.15f;

// Amplitude that reaches the top and bottom of the display. A little above the test tone's full-scale peak so
// the trace doesn't touch the edges. Bit depth quantises relative to full scale, exactly as the effect does,
// so above roughly 6 bits the steps are smaller than a pixel and the trace draws as a clean ramp - which is
// an honest picture of how little a loud signal is changed up there.
constexpr f32 k_full_scale_amplitude = 1.12f;

// Grid lines finer than this collapse into a wash rather than reading as a grid, so they're skipped entirely
// below this spacing rather than drawn illegibly - the trace alone already gives an honest picture there.
constexpr f32 k_min_grid_line_spacing_px = 3.0f;

// A straight ramp would make the two parameters draw the identical staircase: amplitude is a linear function
// of time there, so quantising either axis looks the same. Curving the sweep separates them - bit depth gives
// treads of equal height and varying width, sample rate gives equal width and varying height. Half a cosine
// blended with the straight ramp keeps a corner-to-corner sweep while staying about 2.5x steeper in the
// middle than at the ends.
static f32 Sweep(f32 t) { return (t - 0.5f) - (0.5f * Cos(k_pi<> * t)); }

// Bits starts to be audible around here. True resolution there is still hundreds of levels - fine enough
// that both the trace and grid would draw as smooth as they do above it, understating how crushed it already
// sounds. So below the threshold, an indicative curve stands in for the real 2^(bits - 1) steps per unit,
// used for the trace's quantiser as well as the grid, and squared so it stays close to full resolution near
// the threshold then collapses hard toward the floor - matching how little headroom there really is between
// "just audible" and "ruined". Above the threshold the literal resolution is already far beyond pixel
// resolution, so using it there draws the same honest smooth ramp either way.
//
// Jumping straight from the literal resolution to the indicative curve would make the trace snap from smooth
// to visibly stepped in a single knob tick. So over the bit above the curve's top, the step height grows
// linearly from the literal step (sub-pixel, so it draws the same as above) to the curve's, and the stepping
// eases into view as Bits is pulled down.
constexpr f32 k_bits_indicative_min = 1.0f;
constexpr f32 k_bits_indicative_max = 8.0f;
constexpr f32 k_bits_indicative_min_resolution = 1.2f;
constexpr f32 k_bits_indicative_max_resolution = 5.6f;
constexpr f32 k_bits_ease_in_max = 9.0f;

static f32 DisplayResolution(f32 bits) {
    if (bits >= 32.0f) return 0.0f;
    if (bits > k_bits_ease_in_max) return Pow(2.0f, bits - 1.0f);
    if (bits > k_bits_indicative_max) {
        auto const ease_t = (k_bits_ease_in_max - bits) / (k_bits_ease_in_max - k_bits_indicative_max);
        auto const literal_step = 1.0f / Pow(2.0f, k_bits_ease_in_max - 1.0f);
        auto const indicative_step = 1.0f / k_bits_indicative_max_resolution;
        return 1.0f / LinearInterpolate(ease_t, literal_step, indicative_step);
    }
    auto const bits_t =
        Clamp((bits - k_bits_indicative_min) / (k_bits_indicative_max - k_bits_indicative_min), 0.0f, 1.0f);
    auto const shaped_bits_t = bits_t * bits_t;
    return k_bits_indicative_min_resolution *
           Pow(k_bits_indicative_max_resolution / k_bits_indicative_min_resolution, shaped_bits_t);
}

void DoBitCrushDisplay(GuiState& g, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& params = g.engine.processor.main_params;
    auto const& macro_dests = g.engine.processor.main_macro_destinations;

    // Legacy values are remapped onto the modern parameters, so what's drawn matches what's heard either way.
    auto const audible_projected_value = [&](ParamIndex index) {
        auto const linear =
            AdjustedLinearValue(params.values, macro_dests, params.LinearValueLegacyAware(index), index);
        return k_param_descriptors[ToInt(index)].ProjectValue(linear);
    };

    auto const bits = audible_projected_value(ParamIndex::BitCrushBits);

    auto const num_steps = ({
        auto const rate = audible_projected_value(ParamIndex::BitCrushBitRate);
        auto const rate_range = k_param_descriptors[ToInt(ParamIndex::BitCrushBitRate)].ProjectionRange();
        auto const rate_01 = Log2(rate / rate_range.min) / Log2(rate_range.max / rate_range.min);
        auto const breakpoint_01 =
            Log2(k_step_curve_breakpoint_hz / rate_range.min) / Log2(rate_range.max / rate_range.min);
        auto const shaped_01 =
            rate_01 >= breakpoint_01
                ? 1.0f - (((1.0f - rate_01) / (1.0f - breakpoint_01)) * k_step_curve_smooth_fraction)
                : (rate_01 / breakpoint_01) * (1.0f - k_step_curve_smooth_fraction);
        Clamp(k_min_steps * Pow((f32)k_max_steps / k_min_steps, shaped_01), 2.0f, (f32)k_max_steps);
    });

    // The count is kept fractional so the hold boundaries slide continuously with the parameter rather than
    // snapping whenever it crosses a whole number; the last hold is simply clipped at the right edge.
    auto const num_whole_steps = (u32)Ceil(num_steps);

    // Shared by the trace's quantiser and the grid lines, so they always agree.
    auto const resolution = DisplayResolution(bits);

    // The effect's quantiser shape (zero is always a level), fed the display's resolution rather than always
    // the literal one. A fractional indicative resolution can round its top level past full scale, which a
    // real one never does, so the output is clamped.
    auto const quantise = [resolution](f32 value) {
        if (resolution == 0.0f) return value;
        return Clamp(Round(value * resolution) / resolution, -1.0f, 1.0f);
    };

    auto const window_r = imgui.ViewportRectToWindowRect(viewport_r);
    {
        auto const id = imgui.MakeId("bit_crush_display");
        imgui.RegisterRectForMouseTracking(window_r, false);
        imgui.SetHot(window_r, id);
        Tooltip(g,
                id,
                window_r,
                {
                    .tooltip = "An indicative preview of the bit crushing applied to a rising test signal."_s,
                });
    }

    imgui.draw_list->AddRectFilled(window_r, LiveCol(UiColMap::EqBack), WwToPixels(k_corner_rounding));

    // Snapping to pixel centres gives every flat run the same subpixel offset, so they're all equally
    // crisp rather than each blurred by a different amount.
    auto const snap_to_pixel_centre = [](f32 v) { return Floor(v) + 0.5f; };

    auto const zero_y = snap_to_pixel_centre(window_r.y + (window_r.h * 0.5f));
    auto const half_h = window_r.h * 0.5f;

    // The grid is the truncation the signal is being snapped to: rows are the quantiser's output levels,
    // columns are the sample-and-hold boundaries. Each axis only draws once its lines would land more than a
    // pixel or two apart, so the grid fades away exactly where that axis stops doing anything visible.
    auto const grid_col = LiveCol(UiColMap::EqGrid);
    if (resolution > 0.0f) {
        auto const level_spacing_px = (half_h / k_full_scale_amplitude) / resolution;
        if (level_spacing_px >= k_min_grid_line_spacing_px) {
            auto const num_levels_per_side = (s32)Round(resolution);
            for (auto const level : Range(-num_levels_per_side, num_levels_per_side + 1)) {
                auto const value = (f32)level / resolution;
                if (value < -1.0f || value > 1.0f) continue;
                auto const y = snap_to_pixel_centre(zero_y - ((value / k_full_scale_amplitude) * half_h));
                imgui.draw_list->AddLine(f32x2 {window_r.x, y}, f32x2 {window_r.Right(), y}, grid_col);
            }
        }
    }
    {
        auto const step_spacing_px = window_r.w / num_steps;
        if (step_spacing_px >= k_min_grid_line_spacing_px) {
            for (auto const step_index : Range(1u, num_whole_steps)) {
                auto const x = snap_to_pixel_centre(window_r.x + ((f32)step_index / num_steps * window_r.w));
                imgui.draw_list->AddLine(f32x2 {x, window_r.y}, f32x2 {x, window_r.Bottom()}, grid_col);
            }
        }
    }

    imgui.draw_list->AddLine(f32x2 {window_r.x, zero_y},
                             f32x2 {window_r.Right(), zero_y},
                             LiveCol(UiColMap::EqGridZero));

    // Below the grid's own visibility threshold, holding each sample flat draws a near-single-pixel
    // staircase - fuzzy rather than informative, and a fair picture anyway of how close to continuous the
    // signal already is up there. So below it the trace switches from the honest hold to a plain curve
    // through the same sample points, connected diagonally instead of held flat.
    auto const step_width_px = window_r.w / num_steps;
    auto const smooth = step_width_px < k_min_grid_line_spacing_px;

    DynamicArrayBounded<f32x2, (k_max_steps + 1) * 2> points;
    if (smooth) {
        for (auto const step_index : Range(num_whole_steps + 1)) {
            auto const t = Min((f32)step_index / num_steps, 1.0f);
            auto const value = quantise(Sweep(t)) / k_full_scale_amplitude;
            auto const y = snap_to_pixel_centre(zero_y - (value * half_h));
            auto const x = snap_to_pixel_centre(window_r.x + (t * window_r.w));
            dyn::Append(points, f32x2 {x, y});
        }
    } else {
        // Each held sample becomes a flat run: the tone is read at the start of the run, quantised, then
        // held. Neighbouring runs regularly land on the same value, and they're one line rather than
        // several: a point per sample would leave zero-length segments that the anti-aliased polyline draws
        // as fuzz.
        for (auto const step_index : Range(num_whole_steps)) {
            auto const t0 = (f32)step_index / num_steps;
            auto const t1 = Min((f32)(step_index + 1) / num_steps, 1.0f);
            auto const value = quantise(Sweep(t0)) / k_full_scale_amplitude;
            auto const y = snap_to_pixel_centre(zero_y - (value * half_h));
            auto const x0 = snap_to_pixel_centre(window_r.x + (t0 * window_r.w));
            auto const x1 = snap_to_pixel_centre(window_r.x + (t1 * window_r.w));
            if (x1 == x0) continue;
            if (points.size && points[points.size - 1].y == y) {
                points[points.size - 1].x = x1;
            } else {
                dyn::Append(points, f32x2 {x0, y});
                dyn::Append(points, f32x2 {x1, y});
            }
        }
    }

    // Consecutive points are connected the same way whichever mode built them: a flat run's start/end share
    // a y, so its quad is the run's rectangle; a smooth segment's quad is a thin diagonal sliver. Either
    // way, filling between each pair and the zero line traces the same shape the polyline below draws.
    ASSERT(points.size >= 2);
    auto const area_col = LiveCol(UiColMap::EqArea);
    for (auto const point_index : Range(points.size - 1)) {
        auto const p0 = points[point_index];
        auto const p1 = points[point_index + 1];
        f32x2 const verts[] = {
            f32x2 {p0.x, zero_y},
            p0,
            p1,
            f32x2 {p1.x, zero_y},
        };
        imgui.draw_list->AddConvexPolyFilled(verts, area_col, false);
    }
    imgui.draw_list->AddPolyline(points,
                                 LiveCol(greyed_out ? UiColMap::EqLineGreyedOut : UiColMap::EqLine),
                                 false,
                                 1.5f,
                                 true);
}
