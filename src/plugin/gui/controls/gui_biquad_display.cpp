// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_biquad_display.hpp"

#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_live_edit.hpp"

namespace biquad_display {

constexpr usize k_curve_points = 160;

f32 MagnitudeDb(rbj_filter::Coeffs const& c, f32 frequency_hz) {
    auto const w = 2.0 * k_pi<f64> * (f64)frequency_hz / (f64)k_sample_rate;
    auto const cos_w = Cos(w);
    auto const cos_2w = Cos(2.0 * w);
    auto const b0 = (f64)c.b0;
    auto const b1 = (f64)c.b1;
    auto const b2 = (f64)c.b2;
    auto const a1 = (f64)c.a1;
    auto const a2 = (f64)c.a2;
    auto const num = (b0 * b0) + (b1 * b1) + (b2 * b2) + (2.0 * ((b0 * b1) + (b1 * b2)) * cos_w) +
                     (2.0 * b0 * b2 * cos_2w);
    auto const den = 1.0 + (a1 * a1) + (a2 * a2) + (2.0 * (a1 + (a1 * a2)) * cos_w) + (2.0 * a2 * cos_2w);
    if (den <= 0) return 0;
    auto const mag2 = num / den;
    if (mag2 <= 0) return k_min_db;
    return (f32)(10.0 * Log10(mag2));
}

f32 DbToY(f32 db, Rect viewport_r) {
    auto const t = MapTo01(Clamp(db, k_min_db, k_max_db), k_min_db, k_max_db);
    return viewport_r.Bottom() - (t * viewport_r.h);
}

void DrawBackground(imgui::Context& imgui, Rect viewport_r, ParamDescriptor const& freq_param_info) {
    auto const window_rect = imgui.ViewportRectToWindowRect(viewport_r);
    imgui.draw_list->AddRectFilled(window_rect, LiveCol(UiColMap::EqBack), WwToPixels(k_corner_rounding));

    auto const grid_col = LiveCol(UiColMap::EqGrid);
    auto const zero_col = LiveCol(UiColMap::EqGridZero);
    for (auto const db : Array {-18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f}) {
        auto const y = DbToY(db, viewport_r);
        auto const p0 = imgui.ViewportPosToWindowPos({viewport_r.x, y});
        auto const p1 = imgui.ViewportPosToWindowPos({viewport_r.Right(), y});
        imgui.draw_list->AddLine(p0, p1, db == 0.0f ? zero_col : grid_col);
    }

    for (auto const f_hz : Array {100.0f, 1000.0f, 10000.0f}) {
        auto const linear = freq_param_info.LineariseValue(f_hz, true);
        if (!linear) continue;
        auto const x = viewport_r.x + (*linear * viewport_r.w);
        auto const p0 = imgui.ViewportPosToWindowPos({x, viewport_r.y});
        auto const p1 = imgui.ViewportPosToWindowPos({x, viewport_r.Bottom()});
        imgui.draw_list->AddLine(p0, p1, grid_col);
    }
}

void DrawResponseCurve(imgui::Context& imgui,
                       Rect viewport_r,
                       Span<rbj_filter::Coeffs const> coeffs,
                       ParamDescriptor const& freq_param_info,
                       bool greyed_out) {
    struct CurvePoint {
        f32x2 pos_window;
        bool in_range; // true if the unclamped dB value falls within [k_min_db, k_max_db]
    };
    DynamicArrayBounded<CurvePoint, k_curve_points> curve_points;

    for (auto const i : Range(k_curve_points)) {
        auto const t = (f32)i / (f32)(k_curve_points - 1);
        auto const x = viewport_r.x + (t * viewport_r.w);
        auto const freq_hz = freq_param_info.ProjectValue(t);

        f32 total_db = 0;
        for (auto const& c : coeffs)
            total_db += MagnitudeDb(c, freq_hz);

        bool const in_range = total_db >= k_min_db && total_db <= k_max_db;
        auto const y = DbToY(total_db, viewport_r);
        dyn::Append(curve_points, CurvePoint {imgui.ViewportPosToWindowPos({x, y}), in_range});
    }

    // Area fill: per-segment quads between zero line and curve, using the clamped Y so the
    // fill still covers the out-of-range regions up to the display edges. AA disabled to
    // avoid seams between the translucent quads.
    auto const zero_y_window = imgui.ViewportPosToWindowPos({viewport_r.x, DbToY(0.0f, viewport_r)}).y;
    auto const area_col = LiveCol(UiColMap::EqArea);
    for (auto const i : Range(curve_points.size - 1)) {
        auto const p0 = curve_points[i].pos_window;
        auto const p1 = curve_points[i + 1].pos_window;
        f32x2 const verts[] = {
            f32x2 {p0.x, zero_y_window},
            p0,
            p1,
            f32x2 {p1.x, zero_y_window},
        };
        imgui.draw_list->AddConvexPolyFilled(verts, area_col, false);
    }

    // Polyline: draw consecutive in-range runs as separate polylines.
    auto const line_col = greyed_out ? LiveCol(UiColMap::EqLineGreyedOut) : LiveCol(UiColMap::EqLine);
    DynamicArrayBounded<f32x2, k_curve_points> run;
    auto flush_run = [&]() {
        if (run.size >= 2) imgui.draw_list->AddPolyline(run, line_col, false, 1.5f, true);
        dyn::Clear(run);
    };
    for (auto const& p : curve_points)
        if (p.in_range)
            dyn::Append(run, p.pos_window);
        else
            flush_run();
    flush_run();
}

void DrawHandle(imgui::Context& imgui,
                f32x2 pos_in_window,
                f32 radius,
                imgui::Id interaction_id,
                bool greyed_out) {
    auto col = greyed_out ? LiveCol(UiColMap::EqHandleGreyedOut) : LiveCol(UiColMap::EqHandle);
    if (imgui.IsHot(interaction_id) || imgui.IsActive(interaction_id, MouseButton::Left)) {
        auto halo = FromU32(col);
        halo.a /= 2;
        imgui.draw_list->AddCircleFilled(pos_in_window, radius * 1.8f, ToU32(halo));
        col = LiveCol(UiColMap::EqHandleHover);
    }
    imgui.draw_list->AddCircleFilled(pos_in_window, radius, col);
}

} // namespace biquad_display
