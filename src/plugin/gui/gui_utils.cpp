// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_utils.hpp"

#include "gui/gui_drawing_helpers.hpp"
#include "gui/gui_prefs.hpp"
#include "gui_state.hpp"

constexpr imgui::ButtonConfig k_param_text_input_button_flags = {
    .mouse_button = MouseButton::Left,
    .event = MouseButtonEvent::DoubleClick,
};

constexpr imgui::TextInputConfig k_param_text_input_flags = {
    .centre_align = true,
    .escape_unfocuses = true,
    .select_all_when_opening = true,
};

void HandleShowingTextEditorForParams(GuiState& g, Rect r, Span<ParamIndex const> params) {
    if (g.param_text_editor_to_open) {
        for (auto const p : params) {
            if (p == *g.param_text_editor_to_open) {
                auto const id = g.imgui.MakeId("text input");

                auto const p_obj = g.engine.processor.main_params.DescribedValue(p);
                auto const str = p_obj.info.LinearValueToString(p_obj.LinearValue());
                ASSERT(str.HasValue());

                g.imgui.SetTextInputFocus(id, *str, false);

                auto const text_input = ({
                    auto const input_r = g.imgui.RegisterAndConvertRect(r);
                    auto const o = g.imgui.TextInputBehaviour({
                        .rect_in_window_coords = input_r,
                        .id = id,
                        .text = *str,
                        .input_cfg = k_param_text_input_flags,
                        .button_cfg = k_param_text_input_button_flags,
                    });
                    DrawParameterTextInput(g.imgui, input_r, o);
                    o;
                });

                if (text_input.enter_pressed || g.imgui.TextInputJustUnfocused(id)) {
                    if (auto val = p_obj.info.StringToLinearValue(text_input.text)) {
                        SetParameterValue(g.engine.processor, p, *val, {});
                        GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
                    }
                    g.param_text_editor_to_open.Clear();
                }
                break;
            }
        }
    }
}

bool Tooltip(GuiState& g, imgui::Id id, Rect window_r, String str, TooltipOptions const& options) {
    if (!options.ignore_show_tooltips_preference &&
        !prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::ShowTooltips)))
        return false;

    if (g.imgui.TooltipBehaviour(window_r, id)) {
        DrawOverlayTooltipForRect(g.imgui,
                                  g.fonts,
                                  str,
                                  {
                                      .r = window_r,
                                      .avoid_r = window_r,
                                      .show_left_or_right = false,
                                  });
        return true;
    }

    return false;
}

void ParameterValuePopup(GuiState& g, DescribedParamValue const& param, imgui::Id id, Rect window_r) {
    auto param_ptr = &param;
    ParameterValuePopup(g, {&param_ptr, 1}, id, window_r);
}

void ParameterValuePopup(GuiState& g, Span<DescribedParamValue const*> params, imgui::Id id, Rect window_r) {
    if (!g.imgui.IsActive(id, imgui::SliderConfig::k_activation_cfg)) return;

    DrawOverlayTooltipForRect(g.imgui,
                              g.fonts,
                              ({
                                  String s = {};
                                  if (params.size == 1)
                                      s = g.scratch_arena.Clone(
                                          *params[0]->info.LinearValueToString(params[0]->LinearValue()));
                                  else {
                                      DynamicArray<char> buf {g.scratch_arena};
                                      for (auto param : params) {
                                          fmt::Append(buf,
                                                      "{}: {}",
                                                      param->info.gui_label,
                                                      *param->info.LinearValueToString(param->LinearValue()));
                                          if (param != Last(params)) dyn::Append(buf, '\n');
                                      }
                                      s = buf.ToOwnedSpan();
                                  }
                                  s;
                              }),
                              {
                                  .r = window_r,
                                  .avoid_r = window_r,
                                  .show_left_or_right = false,
                              });
}

void DoParameterTooltipIfNeeded(GuiState& g,
                                DescribedParamValue const& param,
                                imgui::Id imgui_id,
                                Rect param_rect_in_window_coords) {
    auto param_ptr = &param;
    DoParameterTooltipIfNeeded(g, {&param_ptr, 1}, imgui_id, param_rect_in_window_coords);
}

void DoParameterTooltipIfNeeded(GuiState& g,
                                Span<DescribedParamValue const*> params,
                                imgui::Id imgui_id,
                                Rect param_rect_in_window_coords) {
    DynamicArray<char> buf {g.scratch_arena};
    for (auto param : params) {
        auto const str = param->info.LinearValueToString(param->LinearValue());
        ASSERT(str);

        fmt::Append(buf, "{}: {}\n{}", param->info.name, str.Value(), param->info.tooltip);

        if (param->info.value_type == ParamValueType::Int)
            fmt::Append(buf, ". Drag to edit or double-click to type a value");

        if (params.size != 1 && param != Last(params)) fmt::Append(buf, "\n\n");
    }
    Tooltip(g, imgui_id, param_rect_in_window_coords, buf, {});
}
