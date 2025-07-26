// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_knob_widgets.hpp"

#include "foundation/foundation.hpp"

#include "gui.hpp"
#include "gui_draw_knob.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_widget_helpers.hpp"

namespace knobs {

static void DrawKnob(Gui* g, imgui::Id id, Rect r, f32 percent, Style const& style) {
    DrawKnob(g->imgui,
             id,
             r,
             percent,
             DrawKnobOptions {
                 .highlight_col = style.highlight_col,
                 .line_col = style.line_col,
                 .overload_position = style.overload_position,
                 .greyed_out = style.greyed_out,
                 .is_fake = style.is_fake,
                 .bidirectional = style.bidirectional,
             });
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
