// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_knob_widgets.hpp"

#include "foundation/foundation.hpp"

#include "../gui2_macros.hpp"
#include "../gui_drawing_helpers.hpp"
#include "../gui_state.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_widget_helpers.hpp"

namespace knobs {

static void DrawKnob(GuiState& g, imgui::Id id, Rect r, f32 percent, Style const& style) {
    DrawKnob(g.imgui,
             id,
             r,
             percent,
             DrawKnobOptions {
                 .highlight_col = style.highlight_col,
                 .line_col = style.line_col,
                 .overload_position = style.overload_position,
                 .outer_arc_percent = style.outer_arc_percent,
                 .mid_panel_colours = true,
                 .greyed_out = style.greyed_out,
                 .is_fake = style.is_fake,
                 .bidirectional = style.bidirectional,
             });
}

constexpr imgui::SliderConfig k_slider_config = {
    .sensitivity = 256,
    .slower_with_shift = true,
    .default_on_modifer = true,
};

bool Knob(GuiState& g, imgui::Id id, Rect r, f32& fraction, f32 default_fraction, Style const& style) {
    r = g.imgui.RegisterAndConvertRect(r);
    auto const changed = g.imgui.SliderBehaviourFraction({
        .rect_in_window_coords = r,
        .id = id,
        .fraction = fraction,
        .default_fraction = default_fraction,
        .cfg = k_slider_config,
    });
    DrawKnob(g, id, r, fraction, style);
    return changed;
}

bool Knob(GuiState& g, DescribedParamValue const& param, Rect r, Style const& style) {
    return Knob(g, 0, param, r, style);
}

bool Knob(GuiState& g, imgui::Id id, DescribedParamValue const& param, Rect r, Style const& style) {
    id = BeginParameterGUI(g, param, r, id ? Optional<imgui::Id>(id) : k_nullopt);
    Optional<f32> new_val {};
    f32 val = param.LinearValue();

    auto style_copy = style;
    style_copy.outer_arc_percent = MapTo01(AdjustedLinearValue(g.engine.processor.main_params,
                                                               g.engine.processor.main_macro_destinations,
                                                               val,
                                                               param.info.index),
                                           param.info.linear_range.min,
                                           param.info.linear_range.max);

    auto const display_string = param.info.LinearValueToString(val).ReleaseValueOr({});

    if (g.param_text_editor_to_open && *g.param_text_editor_to_open == param.info.index) {
        g.param_text_editor_to_open.Clear();
        g.imgui.SetTextInputFocus(id, display_string, false);
    }

    auto const window_r = g.imgui.RegisterAndConvertRect(r);

    auto const result = g.imgui.DraggerBehaviour({
        .rect_in_window_coords = window_r,
        .id = id,
        .text = display_string,
        .min = param.info.linear_range.min,
        .max = param.info.linear_range.max,
        .value = val,
        .default_value = param.info.default_linear_value,
        .text_input_button_cfg = k_param_text_input_button_flags,
        .text_input_cfg = k_param_text_input_flags,
        .slider_cfg = ({
            auto f = k_slider_config;
            // Sensitivity is based on the pixels needed to change the value by 1. For parameter knobs we want
            // the just have a sensitivity based on the full range of the knob.
            f.sensitivity /= param.info.linear_range.Delta();
            f;
        }),
    });

    if (result.new_string_value) {
        if (auto v = param.info.StringToLinearValue(*result.new_string_value)) {
            new_val = v;
            GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
        }
    }
    if (result.value_changed) new_val = val;

    DrawKnob(g,
             id,
             window_r,
             MapTo01(new_val ? *new_val : val, param.info.linear_range.min, param.info.linear_range.max),
             style_copy);

    if (result.text_input_result && g.imgui.TextInputHasFocus(id))
        DrawParameterTextInput(g.imgui, window_r, *result.text_input_result);

    EndParameterGUI(g, id, param, r, new_val);

    MacroAddDestinationRegion(g, window_r, param.info.index);

    return new_val.HasValue();
}

bool Knob(GuiState& g,
          imgui::Id id,
          layout::Id lay_id,
          f32& percent,
          f32 default_percent,
          Style const& style) {
    return Knob(g, id, layout::GetRect(g.layout, lay_id), percent, default_percent, style);
}
bool Knob(GuiState& g, DescribedParamValue const& param, layout::Id lay_id, Style const& style) {
    return Knob(g, 0, param, layout::GetRect(g.layout, lay_id), style);
}
bool Knob(GuiState& g,
          imgui::Id id,
          DescribedParamValue const& param,
          layout::Id lay_id,
          Style const& style) {
    return Knob(g, id, param, layout::GetRect(g.layout, lay_id), style);
}

void FakeKnob(GuiState& g, Rect r) {
    r = g.imgui.RegisterAndConvertRect(r);
    DrawKnob(g, 99, r, 0, FakeKnobStyle(g.imgui));
}

} // namespace knobs
