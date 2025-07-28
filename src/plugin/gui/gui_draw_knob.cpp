// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_draw_knob.hpp"

void DrawKnob(imgui::Context& imgui, imgui::Id id, Rect r, f32 percent, DrawKnobOptions const& options) {
    auto const c = f32x2 {r.CentreX(), r.y + (r.w / 2)};
    auto const outer_arc_percent = options.outer_arc_percent.ValueOr(percent);
    auto const start_radians = (3 * k_pi<>) / 4;
    auto const end_radians = k_tau<> + (k_pi<> / 4);
    auto const delta = end_radians - start_radians;
    auto const angle = start_radians + ((1 - percent) * delta);
    auto const angle2 = start_radians + (outer_arc_percent * delta);
    ASSERT(percent >= 0 && percent <= 1);
    ASSERT(outer_arc_percent >= 0 && outer_arc_percent <= 1);
    ASSERT(angle >= start_radians && angle <= end_radians);

    auto inner_arc_col = LiveCol(imgui, UiColMap::KnobInnerArc);
    auto bright_arc_col = options.highlight_col;
    if (options.greyed_out) {
        bright_arc_col = LiveCol(imgui, UiColMap::KnobOuterArcGreyedOut);
        inner_arc_col = LiveCol(imgui, UiColMap::KnobInnerArcGreyedOut);
    }
    auto line_col = options.line_col;
    if (!options.is_fake && (imgui.IsHot(id) || imgui.IsActive(id))) {
        inner_arc_col = LiveCol(imgui, UiColMap::KnobInnerArcHover);
        line_col = LiveCol(imgui, UiColMap::KnobLineHover);
    }

    // outer arc
    auto const outer_arc_thickness = LiveSize(imgui, UiSizeId::KnobOuterArcWeight);
    auto const outer_arc_radius_mid = r.w * 0.5f;
    if (!options.overload_position) {
        imgui.graphics->PathArcTo(c,
                                  outer_arc_radius_mid - (outer_arc_thickness / 2),
                                  start_radians,
                                  end_radians,
                                  32);
        imgui.graphics->PathStroke(LiveCol(imgui, UiColMap::KnobOuterArcEmpty), false, outer_arc_thickness);
    } else {
        auto const overload_radians = start_radians + (delta * *options.overload_position);
        auto const radians_per_px = k_tau<> * r.w / 2;
        auto const desired_px_width = 15;
        auto const overload_radians_end = overload_radians + (desired_px_width / radians_per_px);

        {
            imgui.graphics->PathArcTo(c,
                                      outer_arc_radius_mid - (outer_arc_thickness / 2),
                                      start_radians,
                                      overload_radians,
                                      32);
            imgui.graphics->PathStroke(LiveCol(imgui, UiColMap::KnobOuterArcEmpty),
                                       false,
                                       outer_arc_thickness);
        }

        {
            auto const gain_thickness = outer_arc_thickness;
            imgui.graphics->PathArcTo(c,
                                      outer_arc_radius_mid - (gain_thickness / 2) +
                                          (gain_thickness - outer_arc_thickness),
                                      overload_radians_end,
                                      end_radians,
                                      32);
            imgui.graphics->PathStroke(LiveCol(imgui, UiColMap::KnobOuterArcOverload), false, gain_thickness);
        }
    }

    if (!options.is_fake) {
        if (!options.bidirectional) {
            imgui.graphics->PathArcTo(c,
                                      outer_arc_radius_mid - (outer_arc_thickness / 2),
                                      start_radians,
                                      angle2,
                                      32);
        } else {
            auto const mid_radians = start_radians + (delta / 2);
            imgui.graphics->PathArcTo(c,
                                      outer_arc_radius_mid - (outer_arc_thickness / 2),
                                      Min(mid_radians, angle2),
                                      Max(mid_radians, angle2),
                                      32);
        }
        imgui.graphics->PathStroke(bright_arc_col, false, outer_arc_thickness);
    }

    // inner arc
    auto inner_arc_radius_mid = outer_arc_radius_mid - LiveSize(imgui, UiSizeId::KnobInnerArc);
    auto inner_arc_thickness = LiveSize(imgui, UiSizeId::KnobInnerArcWeight);
    imgui.graphics->PathArcTo(c, inner_arc_radius_mid, start_radians, end_radians, 32);
    imgui.graphics->PathStroke(inner_arc_col, false, inner_arc_thickness);

    // cursor
    if (!options.is_fake) {
        auto const line_weight = LiveSize(imgui, UiSizeId::KnobLineWeight);

        auto const inner_arc_radius_outer = inner_arc_radius_mid + (inner_arc_thickness / 2);
        auto const inner_arc_radius_inner = inner_arc_radius_mid - (inner_arc_thickness / 2);

        f32x2 offset;
        offset.x = Sin(angle - (k_pi<> / 2));
        offset.y = Cos(angle - (k_pi<> / 2));
        auto const outer_point = c + (offset * f32x2 {inner_arc_radius_outer, inner_arc_radius_outer});
        auto const inner_point = c + (offset * f32x2 {inner_arc_radius_inner, inner_arc_radius_inner});

        imgui.graphics->AddLine(inner_point, outer_point, line_col, line_weight);
    }
}
