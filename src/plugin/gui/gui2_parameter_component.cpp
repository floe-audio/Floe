// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_parameter_component.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/gui_drawing_helpers.hpp"
#include "gui2_common_modal_panel.hpp"
#include "gui2_macros.hpp"
#include "gui_state.hpp"
#include "old/gui_widget_helpers.hpp"
#include "processor/param.hpp"
#include "processor/processor.hpp"

static void DoMidiLearnMenu(GuiState& g, ParamIndex param_index) {
    auto const root = DoBox(g.builder,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });
    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Set to Default Value",
                     .tooltip = "Set the parameter to its default value"_s,
                 })
            .button_fired) {
        SetParameterValue(g.engine.processor,
                          param_index,
                          k_param_descriptors[ToInt(param_index)].default_linear_value,
                          {});
    }

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Enter Value",
                     .tooltip = "Open a text input to enter a value for the parameter"_s,
                 })
            .button_fired) {
        g.param_text_editor_to_open = param_index;
    }

    if (IsMidiCCLearnActive(g.engine.processor)) {
        if (MenuItem(g.builder,
                     root,
                     {
                         .text = "Cancel MIDI CC Learn",
                         .tooltip = "Cancel waiting for CC to learn"_s,
                     })
                .button_fired) {
            CancelMidiCCLearn(g.engine.processor);
        }
    } else if (MenuItem(g.builder,
                        root,
                        {
                            .text = "MIDI CC Learn",
                            .tooltip = "Assign the next MIDI CC message received to this parameter"_s,
                        })
                   .button_fired) {
        LearnMidiCC(g.engine.processor, param_index);
    }

    auto const persistent_ccs = PersistentCcsForParam(g.prefs, ParamIndexToId(param_index));
    auto const ccs_bitset = GetLearnedCCsBitsetForParam(g.engine.processor, param_index);
    bool const closes_popups = ccs_bitset.AnyValuesSet();
    for (auto const cc_num : Range(128uz)) {
        if (!ccs_bitset.Get(cc_num)) continue;

        if (MenuItem(g.builder,
                     root,
                     {
                         .text = fmt::Format(g.scratch_arena, "Remove MIDI CC {}", cc_num),
                         .tooltip = "Remove the MIDI CC assignment for this parameter"_s,
                         .close_on_click = closes_popups,
                     })
                .button_fired) {
            UnlearnMidiCC(g.engine.processor, param_index, (u7)cc_num);
        }

        {
            bool state = persistent_ccs.Get(cc_num);
            if (MenuItem(g.builder,
                         root,
                         {
                             .text = fmt::Format(g.scratch_arena,
                                                 "Always set MIDI CC {} to this when Floe opens",
                                                 cc_num),
                             .tooltip = "Set this MIDI CC to this parameter value when Floe starts"_s,
                             .is_selected = state,
                             .close_on_click = closes_popups,
                         })
                    .button_fired) {
                state = !state;
                if (state)
                    AddPersistentCcToParamMapping(g.prefs, (u8)cc_num, ParamIndexToId(param_index));
                else
                    RemovePersistentCcToParamMapping(g.prefs, (u8)cc_num, ParamIndexToId(param_index));
            }
        }
    }
}

static void AddMidiLearnRightClickBehaviour(GuiState& g, Box const& box, DescribedParamValue const& param) {
    if (param.info.flags.not_automatable) return;

    if (auto const viewport_r = BoxRect(g.builder, box)) {
        auto const window_r = g.builder.imgui.RegisterAndConvertRect(*viewport_r);
        auto const popup_id = (imgui::Id)(SourceLocationHash() ^ param.info.id);

        if (g.builder.imgui.ButtonBehaviour(window_r,
                                            box.imgui_id,
                                            {
                                                .mouse_button = MouseButton::Right,
                                                .event = MouseButtonEvent::Up,
                                            })) {
            g.builder.imgui.OpenPopupMenu(popup_id, box.imgui_id);
        }

        if (g.builder.imgui.IsPopupMenuOpen(popup_id))
            DoBoxViewport(
                g.builder,
                {
                    .run = [&g, index = param.info.index](GuiBuilder&) { DoMidiLearnMenu(g, index); },
                    .bounds = box,
                    .imgui_id = popup_id,
                    .viewport_config = k_default_popup_menu_viewport,
                });
    }
}

static String ParamTooltipText(DescribedParamValue const& param, ArenaAllocator& arena) {
    auto const str = param.info.LinearValueToString(param.LinearValue());
    ASSERT(str);

    DynamicArray<char> buf {arena};
    fmt::Append(buf, "{}: {}\n{}", param.info.name, str.Value(), param.info.tooltip);
    if (param.info.value_type == ParamValueType::Int)
        fmt::Append(buf, ". Drag to edit or double-click to type a value");

    return buf.ToOwnedSpan();
}

static void DoMenuParameterMenu(GuiState& g, ParamIndex param_index) {
    auto const menu_root = DoBox(g.builder,
                                 {
                                     .layout {
                                         .size = layout::k_hug_contents,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_align = layout::Alignment::Start,
                                     },
                                 });
    auto const current = g.engine.processor.main_params.IntValue<int>(param_index);

    for (auto const [index, item] : Enumerate(ParameterMenuItems(param_index))) {
        g.builder.imgui.PushId(index);
        DEFER { g.builder.imgui.PopId(); };
        if (MenuItem(g.builder,
                     menu_root,
                     {
                         .text = item,
                         .is_selected = (int)index == current,
                     })
                .button_fired) {
            SetParameterValue(g.engine.processor, param_index, (f32)index, {});
        }
    }
}

// TODO: this needs a lot of work still
Box DoMenuParameter(GuiState& g,
                    Box parent,
                    DescribedParamValue const& param,
                    MenuParameterComponentOptions const& options) {
    ASSERT(param.info.value_type == ParamValueType::Menu);

    auto const btn_w = LiveWw(UiSizeId::NextPrevButtonSize);
    auto const margin_r = LiveWw(UiSizeId::ParamIntButtonMarginR);
    auto const menu_text_margin = LiveWw(UiSizeId::MenuButtonTextMarginL);
    auto const strings_width =
        g.imgui.draw_list->fonts.Current()->LargestStringWidth(0, ParameterMenuItems(param.info.index)) +
        (menu_text_margin * 2);
    auto const width = strings_width + (btn_w * 2) + margin_r;
    auto const height = LiveWw(UiSizeId::ParamPopupButtonHeight);

    auto const container = DoBox(g.builder,
                                 {
                                     .parent = parent,
                                     .id_extra = (u64)param.info.id,
                                     .layout {
                                         .size = layout::k_hug_contents,
                                         .contents_gap = LiveWw(UiSizeId::ParamComponentLabelGapY),
                                         .contents_direction = layout::Direction::Column,
                                         .contents_align = layout::Alignment::Start,
                                     },
                                 });

    // Row containing left button, menu text, right button.
    auto const row = DoBox(g.builder,
                           {
                               .parent = container,
                               .background_fill_colours = Col {.c = Col::Background0},
                               .layout {
                                   .size = {width, height},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Middle,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                           });

    Optional<f32> new_val {};

    // Menu text button that opens a popup.
    auto const popup_id = (imgui::Id)(SourceLocationHash() ^ param.info.id);
    auto const menu_btn =
        DoBox(g.builder,
              {
                  .parent = row,
                  .text = ParamMenuText(param.info.index, param.LinearValue()),
                  .text_colours = options.greyed_out ? Col {.c = Col::Overlay0, .dark_mode = true}
                                                     : Col {.c = Col::Text, .dark_mode = true},
                  .background_fill_auto_hot_active_overlay = true,
                  .layout {
                      .size = {layout::k_fill_parent, height},
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String {
                      if (options.override_tooltip.size) return options.override_tooltip;
                      return ParamTooltipText(param, g.builder.arena);
                  }},
                  .button_behaviour = imgui::ButtonConfig {},
              });
    if (menu_btn.button_fired) g.builder.imgui.OpenPopupMenu(popup_id, menu_btn.imgui_id);

    // Popup menu with items.
    if (g.builder.imgui.IsPopupMenuOpen(popup_id))
        DoBoxViewport(g.builder,
                      {
                          .run = [param_index = param.info.index,
                                  &g](GuiBuilder&) { DoMenuParameterMenu(g, param_index); },
                          .bounds = row,
                          .imgui_id = popup_id,
                          .viewport_config = k_default_popup_menu_viewport,
                      });

    // Left arrow button.
    {
        auto const left_btn = DoBox(g.builder,
                                    {
                                        .parent = row,
                                        .text = ICON_FA_CARET_LEFT,
                                        .font = FontType::Icons,
                                        .text_colours = Col {.c = Col::Subtext0},
                                        .background_fill_auto_hot_active_overlay = true,
                                        .layout {
                                            .size = {btn_w, height},
                                            .contents_align = layout::Alignment::Middle,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                        },
                                        .tooltip = "Previous"_s,
                                        .button_behaviour = imgui::ButtonConfig {},
                                    });
        if (left_btn.button_fired) {
            auto val = (f32)param.IntValue<int>() - 1;
            if (val < param.info.linear_range.min) val = param.info.linear_range.max;
            new_val = val;
        }
    }

    // Right arrow button.
    {
        auto const right_btn = DoBox(g.builder,
                                     {
                                         .parent = row,
                                         .text = ICON_FA_CARET_RIGHT,
                                         .font = FontType::Icons,
                                         .text_colours = Col {.c = Col::Subtext0},
                                         .background_fill_auto_hot_active_overlay = true,
                                         .layout {
                                             .size = {btn_w, height},
                                             .contents_align = layout::Alignment::Middle,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                         },
                                         .tooltip = "Next"_s,
                                         .button_behaviour = imgui::ButtonConfig {},
                                     });
        if (right_btn.button_fired) {
            auto val = (f32)param.IntValue<int>() + 1;
            if (val > param.info.linear_range.max) val = param.info.linear_range.min;
            new_val = val;
        }
    }

    // Slider behaviour
    if (auto const viewport_r = BoxRect(g.builder, menu_btn)) {
        auto const window_r = g.builder.imgui.RegisterAndConvertRect(*viewport_r);

        auto current = param.LinearValue();
        if (g.builder.imgui.SliderBehaviourRange({
                .rect_in_window_coords = window_r,
                .id = menu_btn.imgui_id,
                .min = param.info.linear_range.min,
                .max = param.info.linear_range.max,
                .value = current,
                .default_value = param.info.default_linear_value,
                .cfg = {.sensitivity = 20},
            })) {
            new_val = current;
        }

        if (g.imgui.WasJustActivated(menu_btn.imgui_id))
            ParameterJustStartedMoving(g.engine.processor, param.info.index);

        if (new_val) SetParameterValue(g.engine.processor, param.info.index, *new_val, {});

        if (g.imgui.WasJustDeactivated(menu_btn.imgui_id))
            ParameterJustStoppedMoving(g.engine.processor, param.info.index);

        ParameterValuePopup(g, param, menu_btn.imgui_id, window_r);

        MacroAddDestinationRegion(g, window_r, param.info.index);
    }

    // Right-click menu.
    AddMidiLearnRightClickBehaviour(g, menu_btn, param);

    // Label.
    if (options.label)
        DoBox(g.builder,
              {
                  .parent = container,
                  .text = options.override_label.size ? options.override_label : param.info.gui_label,
                  .text_colours = options.greyed_out ? Col {.c = Col::Overlay0, .dark_mode = true}
                                                     : Col {.c = Col::Text, .dark_mode = true},
                  .text_justification = TextJustification::Centred,
                  .layout {
                      .size = {width, k_font_body_size},
                  },
              });

    return container;
}

Box DoKnobParameter(GuiState& g,
                    Box parent,
                    DescribedParamValue const& param,
                    ParameterComponentOptions const& options) {
    ASSERT(param.info.value_type == ParamValueType::Float);

    auto const container = DoBox(g.builder,
                                 {
                                     .parent = parent,
                                     .id_extra = (u64)param.info.id,
                                     .layout {
                                         .size = layout::k_hug_contents,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_align = layout::Alignment::Start,
                                     },
                                     .tooltip = FunctionRef<String()> {[&]() -> String {
                                         if (options.override_tooltip.size) return options.override_tooltip;
                                         return ParamTooltipText(param, g.builder.arena);
                                     }},
                                 });

    auto val = param.NormalisedLinearValue();
    auto const display_string = param.info.LinearValueToString(val).ReleaseValueOr({});
    Optional<f32> new_val {};
    Optional<imgui::TextInputResult> param_text_input_result {};

    // Dragger behaviour.
    if (auto const viewport_r = BoxRect(g.builder, container)) {
        auto const window_r = g.builder.imgui.RegisterAndConvertRect(*viewport_r);

        auto const dragger_result = g.builder.imgui.DraggerBehaviour({
            .rect_in_window_coords = window_r,
            .id = container.imgui_id,
            .text = options.is_fake ? ""_s : (String)display_string,
            .min = 0,
            .max = 1,
            .value = val,
            .default_value = param.NormalisedDefaultLinearValue(),
            .text_input_button_cfg {
                .mouse_button = MouseButton::Left,
                .event = MouseButtonEvent::DoubleClick,
            },
            .text_input_cfg {
                .x_padding = GuiIo().WwToPixels(4.0f),
                .centre_align = true,
                .escape_unfocuses = true,
                .select_all_when_opening = true,
            },
            .slider_cfg {
                .sensitivity = 256,
                .slower_with_shift = true,
                .default_on_modifer = true,
            },
        });

        if (dragger_result.new_string_value) {
            if (auto v = param.info.StringToLinearValue(*dragger_result.new_string_value)) {
                new_val = v;
                GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
            }
        }
        if (dragger_result.value_changed) new_val = val;
        param_text_input_result = dragger_result.text_input_result;

        if (g.imgui.WasJustActivated(container.imgui_id))
            ParameterJustStartedMoving(g.engine.processor, param.info.index);

        if (new_val) SetParameterValue(g.engine.processor, param.info.index, *new_val, {});

        if (g.imgui.WasJustDeactivated(container.imgui_id))
            ParameterJustStoppedMoving(g.engine.processor, param.info.index);

        ParameterValuePopup(g, param, container.imgui_id, window_r);

        MacroAddDestinationRegion(g, window_r, param.info.index);
    }

    // Right-click menu.
    AddMidiLearnRightClickBehaviour(g, container, param);

    // Focus the text input if requested.
    if (g.builder.IsInputAndRenderPass()) {
        if (g.param_text_editor_to_open && *g.param_text_editor_to_open == param.info.index) {
            g.param_text_editor_to_open.Clear();
            g.imgui.SetTextInputFocus(container.imgui_id, display_string, false);
        }
    }

    auto const knob_width = ({
        f32 w;
        switch (options.size) {
            case ParameterComponentOptions::Size::Small:
                w = LiveWw(UiSizeId::ParamComponentExtraSmallWidth);
                break;
            case ParameterComponentOptions::Size::Medium:
                w = LiveWw(UiSizeId::ParamComponentSmallWidth);
                break;
            case ParameterComponentOptions::Size::Large:
                w = LiveWw(UiSizeId::ParamComponentLargeWidth);
                break;
        }
        w;
    });

    if (auto const r = BoxRect(
            g.builder,
            DoBox(g.builder,
                  {
                      .parent = container,
                      .layout {
                          .size = {knob_width, knob_width - LiveWw(UiSizeId::ParamComponentHeightOffset)},
                          .margins = {.b = LiveWw(UiSizeId::ParamComponentLabelGapY)},
                      },
                  }))) {
        DrawKnob(
            g.builder.imgui,
            container.imgui_id,
            g.builder.imgui.ViewportRectToWindowRect(*r),
            val,
            {
                .highlight_col = ToU32(options.knob_highlight_col),
                .line_col = ToU32(options.knob_line_col),
                .overload_position = param.info.display_format == ParamDisplayFormat::VolumeAmp
                                         ? param.info.LineariseValue(1, true)
                                         : k_nullopt,
                .outer_arc_percent = MapTo01(AdjustedLinearValue(g.engine.processor.main_params,
                                                                 g.engine.processor.main_macro_destinations,
                                                                 val,
                                                                 param.info.index),
                                             param.info.linear_range.min,
                                             param.info.linear_range.max),
                .greyed_out = options.greyed_out,
                .is_fake = options.is_fake,
            });
    }

    // Draw text input after the knob so its on top.
    if (param_text_input_result) {
        if (auto const rel_r = BoxRect(g.builder, container)) {
            auto const r = g.builder.imgui.ViewportRectToWindowRect(*rel_r);

            DrawParameterTextInput(g.builder.imgui, r, *param_text_input_result);
        }
    }

    if (options.label)
        DoBox(g.builder,
              {
                  .parent = container,
                  .text = options.override_label.size ? options.override_label : param.info.gui_label,
                  .text_colours = options.greyed_out ? Col {.c = Col::Overlay0, .dark_mode = true}
                                                     : Col {.c = Col::Text, .dark_mode = true},
                  .text_justification = TextJustification::Centred,
                  .layout {
                      .size = {knob_width, k_font_body_size},
                  },
              });

    return container;
}

#if 0

Box DoIntParameter(GuiState& g, Box parent, DescribedParamValue const& param) {
    auto& builder = g.builder;

    ASSERT(param.info.value_type == ParamValueType::Float);

    auto const type = param.info.IsLayerParam()
                          ? LayoutType::Layer
                          : (param.info.IsEffectParam() ? LayoutType::Effect : LayoutType::Generic);

    Margins margins {.b = LiveWw(UiSizeId::ParamComponentLabelGapY)};

    auto width = LiveWw(UiSizeId::FXDraggerWidth);
    auto height = LiveWw(UiSizeId::FXDraggerHeight);
    margins.t += LiveWw(UiSizeId::FXDraggerMarginT);
    margins.b += LiveWw(UiSizeId::FXDraggerMarginB);

    // TODO: modern GuiBuilder version of:
    // bool Dragger(GuiState& g, DescribedParamValue const& param, Rect r, Style const& style)
}

#endif
