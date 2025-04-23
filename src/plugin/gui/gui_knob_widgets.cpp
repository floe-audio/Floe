// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_knob_widgets.hpp"

#include "foundation/foundation.hpp"

#include "gui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_widget_helpers.hpp"

namespace knobs {

static void DrawKnob(Gui* g, imgui::Id id, Rect r, f32 percent, Style const& style) {
    auto const& imgui = g->imgui;
    auto const c = f32x2 {r.CentreX(), r.y + r.w / 2};
    auto const start_radians = (3 * k_pi<>) / 4;
    auto const end_radians = k_tau<> + k_pi<> / 4;
    auto const delta = end_radians - start_radians;
    auto const angle = start_radians + (1 - percent) * delta;
    auto const angle2 = start_radians + percent * delta;
    ASSERT(percent >= 0 && percent <= 1);
    ASSERT(angle >= start_radians && angle <= end_radians);

    auto inner_arc_col = LiveCol(imgui, UiColMap::KnobInnerArc);
    auto bright_arc_col = style.highlight_col;
    if (style.greyed_out) {
        bright_arc_col = LiveCol(imgui, UiColMap::KnobOuterArcGreyedOut);
        inner_arc_col = LiveCol(imgui, UiColMap::KnobInnerArcGreyedOut);
    }
    auto line_col = style.line_col;
    if (imgui.IsHot(id) || imgui.IsActive(id)) {
        inner_arc_col = LiveCol(imgui, UiColMap::KnobInnerArcHover);
        line_col = LiveCol(imgui, UiColMap::KnobLineHover);
    }

    // outer arc
    auto const outer_arc_thickness = LiveSize(imgui, UiSizeId::KnobOuterArcWeight);
    auto const outer_arc_radius_mid = r.w * 0.5f;
    if (!style.overload_position) {
        imgui.graphics->PathArcTo(c,
                                  outer_arc_radius_mid - outer_arc_thickness / 2,
                                  start_radians,
                                  end_radians,
                                  32);
        imgui.graphics->PathStroke(LiveCol(imgui, UiColMap::KnobOuterArcEmpty), false, outer_arc_thickness);
    } else {
        auto const overload_radians = start_radians + delta * *style.overload_position;
        auto const radians_per_px = k_tau<> * r.w / 2;
        auto const desired_px_width = 15;
        auto const overload_radians_end = overload_radians + desired_px_width / radians_per_px;

        {
            imgui.graphics->PathArcTo(c,
                                      outer_arc_radius_mid - outer_arc_thickness / 2,
                                      start_radians,
                                      overload_radians,
                                      32);
            imgui.graphics->PathStroke(LiveCol(imgui, UiColMap::KnobOuterArcEmpty),
                                       false,
                                       outer_arc_thickness);
        }

        if constexpr (0) {
            auto const gain_thickness = outer_arc_thickness * 1.6f;
            imgui.graphics->PathArcTo(c,
                                      outer_arc_radius_mid - (gain_thickness / 2) +
                                          (gain_thickness - outer_arc_thickness),
                                      overload_radians,
                                      overload_radians_end,
                                      32);
            imgui.graphics->PathStroke(LiveCol(imgui, UiColMap::KnobOuterArcOverload), false, gain_thickness);
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

        if constexpr (0) {
            auto const line_weight = LiveSize(imgui, UiSizeId::KnobLineWeight);
            auto const line_height = outer_arc_thickness * 1.4f;

            auto const arc_radius_outer = outer_arc_radius_mid + line_height / 2;
            auto const arc_radius_inner = outer_arc_radius_mid - outer_arc_thickness / 2;

            // NOTE: the x is using cos and y is using sin, I'm just following what PathArcTo() does. I don't
            // know why PathToArc() does it that way.
            f32x2 offset;
            offset.x = Cos(overload_radians);
            offset.y = Sin(overload_radians);
            auto const outer_point = c + (offset * f32x2 {arc_radius_outer, arc_radius_outer});
            auto const inner_point = c + (offset * f32x2 {arc_radius_inner, arc_radius_inner});

            imgui.graphics->AddLine(inner_point,
                                    outer_point,
                                    LiveCol(imgui, UiColMap::KnobOuterArcOverload),
                                    line_weight);
        }
    }

    if (!style.is_fake) {
        if (!style.bidirectional) {
            imgui.graphics->PathArcTo(c,
                                      outer_arc_radius_mid - outer_arc_thickness / 2,
                                      start_radians,
                                      angle2,
                                      32);
        } else {
            auto const mid_radians = start_radians + delta / 2;
            imgui.graphics->PathArcTo(c,
                                      outer_arc_radius_mid - outer_arc_thickness / 2,
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
    if (!style.is_fake) {
        auto const line_weight = LiveSize(imgui, UiSizeId::KnobLineWeight);

        auto const inner_arc_radius_outer = inner_arc_radius_mid + inner_arc_thickness / 2;
        auto const inner_arc_radius_inner = inner_arc_radius_mid - inner_arc_thickness / 2;

        f32x2 offset;
        offset.x = Sin(angle - k_pi<> / 2);
        offset.y = Cos(angle - k_pi<> / 2);
        auto const outer_point = c + (offset * f32x2 {inner_arc_radius_outer, inner_arc_radius_outer});
        auto const inner_point = c + (offset * f32x2 {inner_arc_radius_inner, inner_arc_radius_inner});

        imgui.graphics->AddLine(inner_point, outer_point, line_col, line_weight);
    }
}

static imgui::SliderSettings KnobSettings(Gui* g, Style const& style) {
    auto settings = imgui::DefSlider();
    settings.flags = {.slower_with_shift = true, .default_on_modifer = true};
    settings.draw = [g, &style](IMGUI_DRAW_SLIDER_ARGS) { DrawKnob(g, id, r, percent, style); };
    return settings;
}

bool Knob(Gui* g, imgui::Id id, Rect r, f32& percent, f32 default_percent, Style const& style) {
    return g->imgui.Slider(KnobSettings(g, style), r, id, percent, default_percent);
}

bool Knob(Gui* g, Parameter const& param, Rect r, Style const& style) { return Knob(g, 0, param, r, style); }

bool Knob(Gui* g, imgui::Id id, Parameter const& param, Rect r, Style const& style) {
    id = BeginParameterGUI(g, param, r, id ? Optional<imgui::Id>(id) : k_nullopt);
    Optional<f32> new_val {};
    f32 val = param.LinearValue();

    auto settings = imgui::DefTextInputDraggerFloat();
    settings.slider_settings = KnobSettings(g, style);
    settings.text_input_settings = GetParameterTextInputSettings();

    auto const display_string = param.info.LinearValueToString(val).ReleaseValueOr({});

    if (g->param_text_editor_to_open && *g->param_text_editor_to_open == param.info.index) {
        g->param_text_editor_to_open.Clear();
        g->imgui.SetTextInputFocus(id, display_string, false);
    }

    auto const result = g->imgui.TextInputDraggerCustom(settings,
                                                        r,
                                                        id,
                                                        display_string,
                                                        param.info.linear_range.min,
                                                        param.info.linear_range.max,
                                                        val,
                                                        param.DefaultLinearValue());
    if (result.new_string_value) {
        if (auto v = param.info.StringToLinearValue(*result.new_string_value)) {
            new_val = v;
            g->imgui.frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
        }
    }

    if (result.value_changed) new_val = val;

    EndParameterGUI(g, id, param, r, new_val);
    return new_val.HasValue();
}

bool Knob(Gui* g, imgui::Id id, layout::Id lay_id, f32& percent, f32 default_percent, Style const& style) {
    return Knob(g, id, layout::GetRect(g->layout, lay_id), percent, default_percent, style);
}
bool Knob(Gui* g, Parameter const& param, layout::Id lay_id, Style const& style) {
    return Knob(g, 0, param, layout::GetRect(g->layout, lay_id), style);
}
bool Knob(Gui* g, imgui::Id id, Parameter const& param, layout::Id lay_id, Style const& style) {
    return Knob(g, id, param, layout::GetRect(g->layout, lay_id), style);
}

void FakeKnob(Gui* g, Rect r) {
    g->imgui.RegisterAndConvertRect(&r);
    DrawKnob(g, 99, r, 0, FakeKnobStyle(g->imgui));
}

} // namespace knobs
