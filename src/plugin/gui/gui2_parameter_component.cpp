// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_parameter_component.hpp"

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/gui_drawing_helpers.hpp"
#include "gui/gui_utils.hpp"
#include "gui2_common_modal_panel.hpp"
#include "gui2_macros.hpp"
#include "gui_state.hpp"
#include "processor/param.hpp"
#include "processor/processor.hpp"

static void DoMidiLearnMenu(GuiState& g, Span<ParamIndex const> param_indices) {
    auto const root = DoBox(g.builder,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    for (auto const param_index : param_indices) {
        g.imgui.PushId(ToInt(param_index));
        DEFER { g.imgui.PopId(); };

        if (param_indices.size != 1) {
            MenuItem(g.builder,
                     root,
                     {
                         .text = fmt::Format(g.scratch_arena,
                                             "{}: ",
                                             k_param_descriptors[ToInt(param_index)].gui_label),
                         .mode = MenuItemOptions::Mode::Disabled,
                     });
        }

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

        if (k_param_descriptors[ToInt(param_index)].value_type == ParamValueType::Float) {
            if (MenuItem(g.builder,
                         root,
                         {
                             .text = "Enter Value",
                             .tooltip = "Open a text input to enter a value for the parameter"_s,
                         })
                    .button_fired) {
                g.param_text_editor_to_open = param_index;
            }
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

        if (param_indices.size != 1 && param_index != Last(param_indices))
            DoModalDivider(g.builder, root, {.horizontal = true});
    }
}

void AddMidiLearnRightClickBehaviour(GuiState& g,
                                     Rect window_r,
                                     imgui::Id id,
                                     Span<DescribedParamValue const> params) {
    if (AllOf(params, [](DescribedParamValue const& p) { return p.info.flags.not_automatable; })) return;

    auto const popup_id = (imgui::Id)(SourceLocationHash() ^ ({
                                          auto hash = HashInit();
                                          for (auto const& p : params)
                                              HashUpdate(hash, p.info.id);
                                          hash;
                                      }));

    if (g.builder.imgui.ButtonBehaviour(window_r,
                                        id,
                                        {
                                            .mouse_button = MouseButton::Right,
                                            .event = MouseButtonEvent::Up,
                                        })) {
        g.builder.imgui.OpenPopupMenu(popup_id, id);
    }

    if (g.builder.imgui.IsPopupMenuOpen(popup_id))
        DoBoxViewport(g.builder,
                      {
                          .run = [&g, indices = ({
                                          auto const indices =
                                              g.scratch_arena.AllocateExactSizeUninitialised<ParamIndex>(
                                                  params.size);
                                          for (auto const i : Range(params.size))
                                              indices[i] = params[i].info.index;
                                          indices;
                                      })](GuiBuilder&) { DoMidiLearnMenu(g, indices); },
                          .bounds = window_r,
                          .imgui_id = popup_id,
                          .viewport_config = k_default_popup_menu_viewport,
                      });
}

void AddMidiLearnRightClickBehaviour(GuiState& g,
                                     Rect window_r,
                                     imgui::Id id,
                                     DescribedParamValue const& param) {
    AddMidiLearnRightClickBehaviour(g, window_r, id, Array {param});
}

void AddMidiLearnRightClickBehaviour(GuiState& g, Box const& box, DescribedParamValue const& param) {
    if (param.info.flags.not_automatable) return;

    if (auto const viewport_r = BoxRect(g.builder, box)) {
        AddMidiLearnRightClickBehaviour(g,
                                        g.imgui.ViewportRectToWindowRect(*viewport_r),
                                        box.imgui_id,
                                        param);
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

static void DoParamMenuItems(GuiState& g, ParamIndex param_index) {
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

static Margins ParamControlPadding() {
    return {
        .l = LiveWw(UiSizeId::ParamControlPadL),
        .r = LiveWw(UiSizeId::ParamControlPadR),
        .t = LiveWw(UiSizeId::ParamControlPadT),
        .b = LiveWw(UiSizeId::ParamControlPadB),
    };
}

Box DoPrevNextRow(GuiBuilder& builder, Box parent, f32 width) {
    return DoBox(builder,
                 {
                     .parent = parent,
                     .background_fill_colours = LiveColStruct(UiColMap::MidDarkSurface),
                     .round_background_corners = 0b1111,
                     .corner_rounding = LiveWw(UiSizeId::CornerRounding),
                     .layout {
                         .size = {width, layout::k_hug_contents},
                         .contents_padding = ParamControlPadding(),
                         .contents_direction = layout::Direction::Row,
                         .contents_align = layout::Alignment::Middle,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                     },
                 });
}

PrevNextButtonsResult DoPrevNextButtons(GuiBuilder& builder, Box row, PrevNextButtonsOptions const& options) {
    PrevNextButtonsResult result {};

    auto const do_button = [&](String icon, String tooltip) {
        auto const btn = DoBox(builder,
                               {
                                   .parent = row,
                                   .id_extra = Hash(icon),
                                   .text = icon,
                                   .font = FontType::Icons,
                                   .text_colours =
                                       ColSet {
                                           .base = LiveColStruct(UiColMap::MidIcon),
                                           .hot = LiveColStruct(UiColMap::MidTextHot),
                                           .active = LiveColStruct(UiColMap::MidTextOn),
                                       },
                                   .text_justification = TextJustification::Centred,
                                   .layout {
                                       .size = {LiveWw(UiSizeId::NextPrevButtonSize), k_font_body_size},
                                   },
                                   .tooltip = tooltip,
                                   .button_behaviour = imgui::ButtonConfig {},
                               });
        return btn.button_fired;
    };

    result.prev_fired = do_button(ICON_FA_CARET_LEFT, options.prev_tooltip);
    result.next_fired = do_button(ICON_FA_CARET_RIGHT, options.next_tooltip);

    return result;
}

Box DoMenuParameter(GuiState& g,
                    Box parent,
                    DescribedParamValue const& param,
                    MenuParameterComponentOptions const& options) {
    ASSERT(param.info.value_type == ParamValueType::Menu);

    bool const auto_width = options.width == 0;

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

    auto const row = DoPrevNextRow(g.builder, container, auto_width ? layout::k_hug_contents : options.width);

    Optional<f32> new_val {};

    // Menu text button that opens a popup.
    auto const menu_btn =
        DoBox(g.builder,
              {
                  .parent = row,
                  .text = ParamMenuText(param.info.index, param.LinearValue()),
                  .text_colours = options.greyed_out ? Colours {LiveColStruct(UiColMap::MidTextDimmed)}
                                                     : Colours {ColSet {
                                                           .base = LiveColStruct(UiColMap::MidText),
                                                           .hot = LiveColStruct(UiColMap::MidTextHot),
                                                           .active = LiveColStruct(UiColMap::MidTextHot),
                                                       }},
                  .text_justification = TextJustification::CentredLeft,
                  .text_overflow = TextOverflowType::ShowDotsOnRight,
                  .layout {
                      .size = {auto_width ? g.imgui.draw_list->fonts.Current()
                                                ->LargestStringWidth(0, ParameterMenuItems(param.info.index))
                                          : layout::k_fill_parent,
                               k_font_body_size},
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String {
                      if (options.override_tooltip.size) return options.override_tooltip;
                      return ParamTooltipText(param, g.builder.arena);
                  }},
                  .button_behaviour = imgui::ButtonConfig {},
              });

    auto const popup_id = (imgui::Id)(SourceLocationHash() ^ param.info.id);
    if (menu_btn.button_fired) g.builder.imgui.OpenPopupMenu(popup_id, menu_btn.imgui_id);

    // Popup menu with items.
    if (g.builder.imgui.IsPopupMenuOpen(popup_id))
        DoBoxViewport(g.builder,
                      {
                          .run = [param_index = param.info.index,
                                  &g](GuiBuilder&) { DoParamMenuItems(g, param_index); },
                          .bounds = menu_btn,
                          .imgui_id = popup_id,
                          .viewport_config = k_default_popup_menu_viewport,
                      });

    auto const arrows = DoPrevNextButtons(g.builder, row);
    if (arrows.prev_fired || arrows.next_fired) {
        auto val = (f32)(param.IntValue<int>() + (arrows.prev_fired ? -1 : 1));
        if (val < param.info.linear_range.min) val = param.info.linear_range.max;
        if (val > param.info.linear_range.max) val = param.info.linear_range.min;
        new_val = val;
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

        if (g.imgui.WasJustActivated(menu_btn.imgui_id, MouseButton::Left))
            ParameterJustStartedMoving(g.engine.processor, param.info.index);

        if (new_val) SetParameterValue(g.engine.processor, param.info.index, *new_val, {});

        if (g.imgui.WasJustDeactivated(menu_btn.imgui_id, MouseButton::Left))
            ParameterJustStoppedMoving(g.engine.processor, param.info.index);

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
                  .text_colours = options.greyed_out ? Colours {LiveColStruct(UiColMap::MidTextDimmed)}
                                                     : Colours {LiveColStruct(UiColMap::MidText)},
                  .text_justification = TextJustification::Centred,
                  .text_overflow = TextOverflowType::ShowDotsOnRight,
                  .layout {
                      .size = {layout::k_fill_parent, k_font_body_size},
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
                                     .id_extra = param.info.id,
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

        if (g.imgui.WasJustActivated(container.imgui_id, MouseButton::Left))
            ParameterJustStartedMoving(g.engine.processor, param.info.index);

        if (new_val) SetParameterValue(g.engine.processor, param.info.index, *new_val, {});

        if (g.imgui.WasJustDeactivated(container.imgui_id, MouseButton::Left))
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

Box DoButtonParameter(GuiState& g,
                      Box parent,
                      DescribedParamValue const& param,
                      ButtonParameterComponentOptions const& options) {
    auto const height = LiveWw(UiSizeId::PageHeadingHeight);
    auto const icon_area_width = LiveWw(UiSizeId::PageHeadingTextOffset);
    bool const state = param.BoolValue();

    auto const label_text = options.override_label.size ? options.override_label : param.info.gui_label;
    bool const auto_width = options.width == 0;

    auto const container =
        DoBox(g.builder,
              {
                  .parent = parent,
                  .id_extra = (u64)param.info.id,
                  .layout {
                      .size = {auto_width ? layout::k_hug_contents : options.width, height},
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Start,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String {
                      if (options.override_tooltip.size) return options.override_tooltip;
                      return ParamTooltipText(param, g.builder.arena);
                  }},
                  .button_behaviour = imgui::ButtonConfig {},
              });

    // Toggle icon.
    DoBox(g.builder,
          {
              .parent = container,
              .text = state ? ICON_FA_TOGGLE_ON : ICON_FA_TOGGLE_OFF,
              .font = FontType::Icons,
              .font_size = k_font_icons_size * 0.75f,
              .text_colours = state ? Colours {LiveColStruct(UiColMap::MidTextOn)}
                                    : Colours {ColSet {
                                          .base = LiveColStruct(UiColMap::MidIcon),
                                          .hot = LiveColStruct(UiColMap::MidIcon),
                                          .active = LiveColStruct(UiColMap::MidTextOn),
                                      }},
              .text_justification = TextJustification::CentredLeft,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = {icon_area_width, height},
              },
          });

    // Text label.
    DoBox(g.builder,
          {
              .parent = container,
              .text = label_text,
              .text_colours = options.greyed_out ? Colours {ColSet {
                                                       .base = LiveColStruct(UiColMap::MidTextDimmed),
                                                       .hot = LiveColStruct(UiColMap::MidTextHot),
                                                       .active = LiveColStruct(UiColMap::MidTextHot),
                                                   }}
                                                 : Colours {ColSet {
                                                       .base = LiveColStruct(UiColMap::MidText),
                                                       .hot = LiveColStruct(UiColMap::MidTextHot),
                                                       .active = LiveColStruct(UiColMap::MidTextHot),
                                                   }},
              .text_justification = TextJustification::CentredLeft,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = {auto_width ? g.imgui.draw_list->fonts.CalcTextSize(label_text, {}).x
                                      : layout::k_fill_parent,
                           height},
              },
          });

    // Toggle behaviour.
    if (container.button_fired)
        SetParameterValue(g.engine.processor, param.info.index, state ? 0.0f : 1.0f, {});

    // Right-click menu.
    AddMidiLearnRightClickBehaviour(g, container, param);

    return container;
}

Box DoIntParameter(GuiState& g,
                   Box parent,
                   DescribedParamValue const& param,
                   IntParameterComponentOptions const& options) {
    ASSERT(param.info.value_type == ParamValueType::Int);

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

    auto const row = DoPrevNextRow(g.builder, container, options.width);

    auto const format_value = [&]() -> String {
        if (options.midi_note_names)
            return g.scratch_arena.Clone(NoteName(CheckedCast<u7>(param.IntValue<int>())));
        auto const format_str = options.always_show_plus ? "{+}"_s : "{}"_s;
        return fmt::Format(g.scratch_arena, format_str, param.IntValue<int>());
    };

    auto const display_string = format_value();
    Optional<f32> new_val {};
    Optional<imgui::TextInputResult> param_text_input_result {};

    // Dragger text area.
    auto const dragger_box =
        DoBox(g.builder,
              {
                  .parent = row,
                  .text = display_string,
                  .text_colours = Colours {options.greyed_out ? LiveColStruct(UiColMap::MidTextDimmed)
                                                              : LiveColStruct(UiColMap::MidText)},
                  .text_justification = TextJustification::Centred,
                  .text_overflow = TextOverflowType::ShowDotsOnRight,
                  .layout {
                      .size = {layout::k_fill_parent, k_font_body_size},
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String {
                      if (options.override_tooltip.size) return options.override_tooltip;
                      return ParamTooltipText(param, g.builder.arena);
                  }},
              });

    // Dragger behaviour.
    if (auto const viewport_r = BoxRect(g.builder, dragger_box)) {
        auto const window_r = g.builder.imgui.RegisterAndConvertRect(*viewport_r);

        auto val = (f32)param.IntValue<int>();

        imgui::TextInputConfig const text_input_cfg = {
            .chars_decimal = !options.midi_note_names,
            .chars_note_names = options.midi_note_names,
            .tab_focuses_next_input = true,
            .centre_align = true,
            .escape_unfocuses = true,
            .select_all_when_opening = true,
        };

        auto const dragger_result = g.builder.imgui.DraggerBehaviour({
            .rect_in_window_coords = window_r,
            .id = dragger_box.imgui_id,
            .text = display_string,
            .min = param.info.linear_range.min,
            .max = param.info.linear_range.max,
            .value = val,
            .default_value = param.info.default_linear_value,
            .text_input_button_cfg {
                .mouse_button = MouseButton::Left,
                .event = MouseButtonEvent::DoubleClick,
            },
            .text_input_cfg = text_input_cfg,
            .slider_cfg {
                .sensitivity = 15,
                .slower_with_shift = true,
                .default_on_modifer = true,
            },
        });

        if (dragger_result.new_string_value) {
            if (options.midi_note_names) {
                if (auto const midi_note = MidiNoteFromName(*dragger_result.new_string_value))
                    new_val = (f32)midi_note.Value();
            } else if (auto const o = ParseInt(*dragger_result.new_string_value, ParseIntBase::Decimal)) {
                new_val = (f32)Clamp((int)o.Value(),
                                     (int)param.info.linear_range.min,
                                     (int)param.info.linear_range.max);
            }
        }
        if (dragger_result.value_changed) new_val = (f32)(int)val;
        param_text_input_result = dragger_result.text_input_result;

        if (g.imgui.WasJustActivated(dragger_box.imgui_id, MouseButton::Left))
            ParameterJustStartedMoving(g.engine.processor, param.info.index);

        if (new_val) SetParameterValue(g.engine.processor, param.info.index, *new_val, {});

        if (g.imgui.WasJustDeactivated(dragger_box.imgui_id, MouseButton::Left))
            ParameterJustStoppedMoving(g.engine.processor, param.info.index);

        MacroAddDestinationRegion(g, window_r, param.info.index);
    }

    auto const arrows = DoPrevNextButtons(g.builder, row);
    if (arrows.prev_fired || arrows.next_fired) {
        auto val = (f32)(param.IntValue<int>() + (arrows.prev_fired ? -1 : 1));
        val = Clamp(val, param.info.linear_range.min, param.info.linear_range.max);
        SetParameterValue(g.engine.processor, param.info.index, val, {});
    }

    // Draw text input overlay after the row so it's on top.
    if (param_text_input_result) {
        if (auto const rel_r = BoxRect(g.builder, dragger_box)) {
            auto const r = g.builder.imgui.ViewportRectToWindowRect(*rel_r);
            DrawParameterTextInput(g.builder.imgui, r, *param_text_input_result);
        }
    }

    // Right-click menu.
    AddMidiLearnRightClickBehaviour(g, dragger_box, param);

    // Focus the text input if requested.
    if (g.builder.IsInputAndRenderPass()) {
        if (g.param_text_editor_to_open && *g.param_text_editor_to_open == param.info.index) {
            g.param_text_editor_to_open.Clear();
            g.imgui.SetTextInputFocus(dragger_box.imgui_id, display_string, false);
        }
    }

    // Label.
    if (options.label)
        DoBox(g.builder,
              {
                  .parent = container,
                  .text = options.override_label.size ? options.override_label : param.info.gui_label,
                  .text_colours = options.greyed_out ? Colours {LiveColStruct(UiColMap::MidTextDimmed)}
                                                     : Colours {LiveColStruct(UiColMap::MidText)},
                  .text_justification = TextJustification::Centred,
                  .text_overflow = TextOverflowType::ShowDotsOnRight,
                  .layout {
                      .size = {layout::k_fill_parent, k_font_body_size},
                  },
              });

    return container;
}
