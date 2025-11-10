// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_parameter_component.hpp"

#include "gui.hpp"
#include "gui/gui2_common_modal_panel.hpp"
#include "gui/gui_draw_knob.hpp"
#include "gui/gui_widget_helpers.hpp"
#include "processor/param.hpp"

static void DoMidiLearnMenu(Gui* g, ParamIndex param_index) {
    auto const root = DoBox(g->box_system,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });
    if (MenuItem(g->box_system,
                 root,
                 {
                     .text = "Set to Default Value",
                     .tooltip = "Set the parameter to its default value"_s,
                 })
            .button_fired) {
        SetParameterValue(g->engine.processor,
                          param_index,
                          k_param_descriptors[ToInt(param_index)].default_linear_value,
                          {});
    }

    if (MenuItem(g->box_system,
                 root,
                 {
                     .text = "Enter Value",
                     .tooltip = "Open a text input to enter a value for the parameter"_s,
                 })
            .button_fired) {
        g->param_text_editor_to_open = param_index;
    }

    if (IsMidiCCLearnActive(g->engine.processor)) {
        if (MenuItem(g->box_system,
                     root,
                     {
                         .text = "Cancel MIDI CC Learn",
                         .tooltip = "Cancel waiting for CC to learn"_s,
                     })
                .button_fired) {
            CancelMidiCCLearn(g->engine.processor);
        }
    } else if (MenuItem(g->box_system,
                        root,
                        {
                            .text = "MIDI CC Learn",
                            .tooltip = "Assign the next MIDI CC message received to this parameter"_s,
                        })
                   .button_fired) {
        LearnMidiCC(g->engine.processor, param_index);
    }

    auto const persistent_ccs = PersistentCcsForParam(g->prefs, ParamIndexToId(param_index));
    auto const ccs_bitset = GetLearnedCCsBitsetForParam(g->engine.processor, param_index);
    bool const closes_popups = ccs_bitset.AnyValuesSet();
    for (auto const cc_num : Range(128uz)) {
        if (!ccs_bitset.Get(cc_num)) continue;

        if (MenuItem(g->box_system,
                     root,
                     {
                         .text = fmt::Format(g->scratch_arena, "Remove MIDI CC {}", cc_num),
                         .tooltip = "Remove the MIDI CC assignment for this parameter"_s,
                         .close_on_click = closes_popups,
                     })
                .button_fired) {
            UnlearnMidiCC(g->engine.processor, param_index, (u7)cc_num);
        }

        {
            bool state = persistent_ccs.Get(cc_num);
            if (MenuItem(g->box_system,
                         root,
                         {
                             .text = fmt::Format(g->scratch_arena,
                                                 "Always set MIDI CC {} to this when Floe opens",
                                                 cc_num),
                             .tooltip = "Set this MIDI CC to this parameter value when Floe starts"_s,
                             .is_selected = state,
                             .close_on_click = closes_popups,
                         })
                    .button_fired) {
                state = !state;
                if (state)
                    AddPersistentCcToParamMapping(g->prefs, (u8)cc_num, ParamIndexToId(param_index));
                else
                    RemovePersistentCcToParamMapping(g->prefs, (u8)cc_num, ParamIndexToId(param_index));
            }
        }
    }
}

// IMPROVE: this is too complicated and messy. It's code pasted from the old GUI system. We will want to
// remove lots of the dependency on Gui*.

Box DoParameterComponent(Gui* g,
                         Box parent,
                         DescribedParamValue const& param,
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

    if (auto const index_for_menu_items = param.info.value_type == ParamValueType::Menu
                                              ? Optional<ParamIndex> {param.info.index}
                                              : k_nullopt) {
        auto const menu_items = ParameterMenuItems(*index_for_menu_items);
        auto strings_width =
            MaxStringLength(g, menu_items) + (live_size(UiSizeId::MenuButtonTextMarginL) * 2);
        auto const btn_w = live_size(UiSizeId::NextPrevButtonSize);
        auto const margin_r = live_size(UiSizeId::ParamIntButtonMarginR);
        auto const param_popup_button_height = live_size(UiSizeId::ParamPopupButtonHeight);
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
                  .text = options.is_fake ? ""_s : (String)display_string,
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
                  .behaviour = options.is_fake ? Behaviour::None : Behaviour::TextInput | Behaviour::Knob,
                  .activate_on_double_click = true,
                  .activation_click_event = ActivationClickEvent::Down,
                  .knob_percent = val,
                  .knob_default_percent = param.NormalisedDefaultLinearValue(),
              });

    // Check for a new value.
    Optional<f32> new_val {};
    {
        if (container.text_input_result &&
            (container.text_input_result->buffer_changed || container.text_input_result->enter_pressed)) {
            if (auto v = param.info.StringToLinearValue(container.text_input_result->text)) {
                new_val = v;
                g->imgui.frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
            }
        }

        if (!__builtin_isnan(container.knob_percent)) {
            val = container.knob_percent;
            new_val = MapFrom01(val, param.info.linear_range.min, param.info.linear_range.max);
        }
    }

    // MIDI learn menu.
    {
        auto const popup_id = (imgui::Id)(SourceLocationHash() + param.info.id);
        if (AdditionalClickBehaviour(builder,
                                     container,
                                     {
                                         .right_mouse = true,
                                         .triggers_on_mouse_up = true,
                                     })) {
            builder.imgui.OpenPopup(popup_id, container.imgui_id);
        }

        if (builder.imgui.IsPopupOpen(popup_id))
            AddPanel(builder,
                     Panel {
                         .run = [g, index = param.info.index](GuiBoxSystem&) { DoMidiLearnMenu(g, index); },
                         .data =
                             PopupPanel {
                                 .creator_layout_id = container.layout_id,
                                 .popup_imgui_id = popup_id,
                             },
                     });
    }

    // Focus the text input if requested.
    if (builder.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender) {
        if (g->param_text_editor_to_open && *g->param_text_editor_to_open == param.info.index) {
            g->param_text_editor_to_open.Clear();
            g->imgui.SetTextInputFocus(container.imgui_id, display_string, false);
        }
    }

    if (auto const r = BoxRect(builder, container)) {
        EndParameterGUI(g, container.imgui_id, param, *r, new_val, ParamDisplayFlagsNoTooltip);

        MacroAddDestinationRegion(g, *r, param.info.index);
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
        DrawKnob(
            builder.imgui,
            container.imgui_id,
            builder.imgui.WindowRectToScreenRect(*r),
            val,
            {
                .highlight_col = style::Col(options.knob_highlight_col),
                .line_col = style::Col(options.knob_line_col),
                .overload_position = param.info.display_format == ParamDisplayFormat::VolumeAmp
                                         ? param.info.LineariseValue(1, true)
                                         : k_nullopt,
                .outer_arc_percent = MapTo01(AdjustedLinearValue(g->engine.processor.main_params,
                                                                 g->engine.processor.main_macro_destinations,
                                                                 val,
                                                                 param.info.index),
                                             param.info.linear_range.min,
                                             param.info.linear_range.max),
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
                              .text_col = style::Colour::Text | style::Colour::DarkMode,
                              .cursor_col = style::Colour::Text | style::Colour::DarkMode,
                              .selection_col = style::Colour::Highlight | style::Colour::Alpha50,
                          });
        }
    }

    if (options.label)
        DoBox(builder,
              {
                  .parent = container,
                  .text = options.override_label.size ? options.override_label : param.info.gui_label,
                  .text_colours = {options.greyed_out ? style::Colour::Overlay0 | style::Colour::DarkMode
                                                      : style::Colour::Text | style::Colour::DarkMode},
                  .text_align_x = TextAlignX::Centre,
                  .text_align_y = TextAlignY::Centre,
                  .layout {
                      .size = {width, style::k_font_body_size},
                  },
              });

    return container;
}
