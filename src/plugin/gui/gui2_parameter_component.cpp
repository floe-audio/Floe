// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_parameter_component.hpp"

#include "gui.hpp"
#include "gui/gui_draw_knob.hpp"
#include "gui/gui_widget_helpers.hpp"
#include "processor/param.hpp"

// IMPROVE: this is too complicated and messy. It's code pasted from the old GUI system. We will want to
// remove lots of the dependency on Gui*.

Box DoParameterComponent(Gui* g,
                         Box parent,
                         Parameter const& param,
                         ParameterComponentOptions const& options) {
    auto& builder = g->box_system;

    auto live_size = [&](UiSizeId id) { return builder.imgui.PixelsToVw(LiveSize(builder.imgui, id)); };

    auto const type = param.info.IsLayerParam()
                          ? LayoutType::Layer
                          : (param.info.IsEffectParam() ? LayoutType::Effect : LayoutType::Generic);

    auto width = type == LayoutType::Layer
                     ? live_size(UiSizeId::ParamComponentLargeWidth)
                     : (type == LayoutType::Effect ? live_size(UiSizeId::ParamComponentSmallWidth)
                                                   : live_size(UiSizeId::ParamComponentExtraSmallWidth));
    auto height = width - live_size(UiSizeId::ParamComponentHeightOffset);

    auto const index_for_menu_items =
        param.info.value_type == ParamValueType::Menu ? Optional<ParamIndex> {param.info.index} : k_nullopt;

    auto const param_popup_button_height = live_size(UiSizeId::ParamPopupButtonHeight);

    if (index_for_menu_items) {
        auto const menu_items = ParameterMenuItems(*index_for_menu_items);
        auto strings_width =
            MaxStringLength(g, menu_items) + (live_size(UiSizeId::MenuButtonTextMarginL) * 2);
        auto const btn_w = live_size(UiSizeId::NextPrevButtonSize);
        auto const margin_r = live_size(UiSizeId::ParamIntButtonMarginR);
        strings_width += btn_w * 2 + margin_r;
        width = strings_width;
        height = param_popup_button_height;
    }

    layout::Margins margins {.b = live_size(UiSizeId::ParamComponentLabelGapY)};

    if (param.info.value_type == ParamValueType::Int) {
        width = live_size(UiSizeId::FXDraggerWidth);
        height = live_size(UiSizeId::FXDraggerHeight);
        margins.t += live_size(UiSizeId::FXDraggerMarginT);
        margins.b += live_size(UiSizeId::FXDraggerMarginB);
    }

    auto val = param.NormalisedLinearValue();

    auto const display_string = param.info.LinearValueToString(val).ReleaseValueOr({});

    auto const container =
        DoBox(builder,
              {
                  .parent = parent,
                  .text = display_string,
                  .text_align_x = TextAlignX::Centre,
                  .text_align_y = TextAlignY::Centre,
                  .layout {
                      .size = layout::k_hug_contents,
                      .contents_direction = layout::Direction::Column,
                      .contents_align = layout::Alignment::Start,
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String {
                      if (options.override_tooltip.size) return options.override_tooltip;
                      auto const str = param.info.LinearValueToString(param.LinearValue());
                      ASSERT(str);

                      DynamicArray<char> buf {builder.arena};
                      fmt::Append(buf, "{}: {}\n{}", param.info.name, str.Value(), param.info.tooltip);
                      if (param.info.value_type == ParamValueType::Int)
                          fmt::Append(buf, ". Drag to edit or double-click to type a value");

                      return buf.ToOwnedSpan();
                  }},
                  .activate_on_double_click = true,
                  .activation_click_event = ActivationClickEvent::Down,
                  .text_input_behaviour = TextInputBox::SingleLine,
                  .knob_behaviour = !options.is_fake,
                  .knob_percent = val,
              });

    Optional<f32> new_val {};

    if (container.text_input_result &&
        (container.text_input_result->buffer_changed || container.text_input_result->enter_pressed)) {
        if (auto v = param.info.StringToLinearValue(container.text_input_result->text)) {
            new_val = v;
            g->imgui.frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
        }
    }

    if (builder.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender) {
        if (g->param_text_editor_to_open && *g->param_text_editor_to_open == param.info.index) {
            g->param_text_editor_to_open.Clear();
            g->imgui.SetTextInputFocus(container.imgui_id, display_string, false);
            g->frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);

            // This is a nasty hack. We have to do run this code after the DoBox because we need to get the
            // imgui_id from it. But when we set the text input focus running the text input, the system
            // thinks discards the focus at the end of the frame because no text input box was run using this
            // id. So we run a dummy one here.
            g->imgui.TextInput({.draw = [](IMGUI_DRAW_TEXT_INPUT_ARGS) {}},
                               *BoxRect(builder, container),
                               container.imgui_id,
                               display_string);
        }
    }

    if (!__builtin_isnan(container.knob_percent)) {
        val = container.knob_percent;
        new_val = MapFrom01(val, param.info.linear_range.min, param.info.linear_range.max);
    }

    if (auto const r = BoxRect(builder, container)) {
        BeginParameterGUI(g, param, *r, container.imgui_id);

        EndParameterGUI(g, container.imgui_id, param, *r, new_val, ParamDisplayFlagsNoTooltip);
    }

    auto const control = DoBox(builder,
                               {
                                   .parent = container,
                                   .layout {
                                       .size = {width, height},
                                       .margins = margins,
                                   },
                               });

    if (auto const r = BoxRect(builder, control)) {
        DrawKnob(builder.imgui,
                 container.imgui_id,
                 *r,
                 val,
                 {
                     .highlight_col = style::Col(options.knob_highlight_col),
                     .line_col = style::Col(options.knob_line_col),
                     .overload_position = param.info.display_format == ParamDisplayFormat::VolumeAmp
                                              ? param.info.LineariseValue(1, true)
                                              : k_nullopt,
                     .greyed_out = options.greyed_out,
                     .is_fake = options.is_fake,
                 });
    }

    if (builder.imgui.TextInputHasFocus(container.imgui_id)) {
        if (auto const rel_r = BoxRect(builder, container)) {
            auto const r = builder.imgui.WindowRectToScreenRect(*rel_r);
            auto const rounding = LiveSize(builder.imgui, UiSizeId::CornerRounding);

            builder.imgui.graphics->AddRectFilled(r,
                                                  LiveCol(builder.imgui, UiColMap::KnobTextInputBack),
                                                  rounding);
            builder.imgui.graphics->AddRect(r,
                                            LiveCol(builder.imgui, UiColMap::KnobTextInputBorder),
                                            rounding);

            DrawTextInput(builder,
                          container,
                          {
                              .text_col = style::Colour::DarkModeText,
                              .cursor_col = style::Colour::DarkModeText,
                              .selection_col = style::Colour::Highlight,
                          });
        }
    }

    DoBox(builder,
          {
              .parent = container,
              .text = param.info.gui_label,
              .text_colours = {options.greyed_out ? style::Colour::DarkModeSubtext0
                                                  : style::Colour::DarkModeText},
              .text_align_x = TextAlignX::Centre,
              .text_align_y = TextAlignY::Centre,
              .layout {
                  .size = {width, style::k_font_body_size},
              },
          });

    return control;
}
