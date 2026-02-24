// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/elements/gui_param_elements.hpp"

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_macros.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/param.hpp"
#include "processor/processor.hpp"

static void DoParamContextMenu(GuiState& g, Span<ParamIndex const> param_indices) {
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

void AddParamContextMenuBehaviour(GuiState& g,
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
                                      })](GuiBuilder&) { DoParamContextMenu(g, indices); },
                          .bounds = window_r,
                          .imgui_id = popup_id,
                          .viewport_config = k_default_popup_menu_viewport,
                      });
}

void AddParamContextMenuBehaviour(GuiState& g,
                                  Rect window_r,
                                  imgui::Id id,
                                  DescribedParamValue const& param) {
    AddParamContextMenuBehaviour(g, window_r, id, Array {param});
}

void AddParamContextMenuBehaviour(GuiState& g, Box const& box, DescribedParamValue const& param) {
    if (param.info.flags.not_automatable) return;

    if (auto const viewport_r = BoxRect(g.builder, box))
        AddParamContextMenuBehaviour(g, g.imgui.ViewportRectToWindowRect(*viewport_r), box.imgui_id, param);
}

String ParamTooltipText(DescribedParamValue const& param, ArenaAllocator& arena) {
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

Box DoMenuParameter(GuiState& g,
                    Box parent,
                    DescribedParamValue const& param,
                    MenuParameterComponentOptions const& options) {
    ASSERT(param.info.value_type == ParamValueType::Menu);

    bool const auto_width = options.width == 0;
    bool const fill_parent = options.width == layout::k_fill_parent;

    auto const container =
        DoBox(g.builder,
              {
                  .parent = parent,
                  .id_extra = (u64)param.info.id,
                  .layout {
                      .size = {fill_parent ? layout::k_fill_parent : layout::k_hug_contents,
                               layout::k_hug_contents},
                      .contents_gap = LiveWw(UiSizeId::ParamElementMenuLabelGap),
                      .contents_direction = layout::Direction::Column,
                      .contents_align = layout::Alignment::Start,
                  },
              });

    auto const row_width =
        auto_width ? layout::k_hug_contents : (fill_parent ? layout::k_fill_parent : options.width);
    auto const row = DoMidPanelPrevNextRow(g.builder, container, row_width);

    Optional<f32> new_val {};

    // Menu text button that opens a popup.
    auto const menu_btn_width =
        auto_width
            ? g.imgui.draw_list->fonts.Current()->LargestStringWidth(0, ParameterMenuItems(param.info.index))
            : layout::k_fill_parent;
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
                      .size = {menu_btn_width, TextButtonHeight()},
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

    auto const arrows = DoMidPanelPrevNextButtons(g.builder, row, {.greyed_out = options.greyed_out});
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

        AddParamContextMenuBehaviour(g, window_r, menu_btn.imgui_id, param);
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

Box DoKnobParameter(GuiState& g,
                    Box parent,
                    DescribedParamValue const& param,
                    ParameterComponentOptions const& options) {
    ASSERT(param.info.value_type == ParamValueType::Float);

    auto container = DoBox(g.builder,
                           {
                               .parent = parent,
                               .id_extra = param.info.id,
                               .layout {
                                   .size = layout::k_hug_contents,
                                   .contents_gap = LiveWw(UiSizeId::ParamElementKnobLabelGap),
                                   .contents_direction = layout::Direction::Column,
                                   .contents_align = layout::Alignment::Start,
                               },
                               .tooltip = FunctionRef<String()> {[&]() -> String {
                                   if (options.override_tooltip.size) return options.override_tooltip;
                                   return ParamTooltipText(param, g.builder.arena);
                               }},
                           });

    auto val = param.LinearValue();
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
            .min = param.info.linear_range.min,
            .max = param.info.linear_range.max,
            .value = val,
            .default_value = param.info.default_linear_value,
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
                .sensitivity = 256 / param.info.linear_range.Delta(),
                .slower_with_shift = true,
                .default_on_modifer = true,
            },
        });

        container.is_active = g.imgui.IsActive(container.imgui_id, MouseButton::Left);
        container.is_hot = g.imgui.IsHot(container.imgui_id);

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

        AddParamContextMenuBehaviour(g, window_r, container.imgui_id, param);
        OverlayMacroDestinationRegion(g, window_r, param.info.index);
    }

    // Focus the text input if requested.
    if (g.builder.IsInputAndRenderPass()) {
        if (g.param_text_editor_to_open && *g.param_text_editor_to_open == param.info.index) {
            g.param_text_editor_to_open.Clear();
            g.imgui.SetTextInputFocus(container.imgui_id, display_string, false);
            g.imgui.TextInputSelectAll();
        }
    }

    auto const knob_width = options.width;
    auto const knob_height = knob_width * options.knob_height_fraction;

    if (auto const r = BoxRect(g.builder,
                               DoBox(g.builder,
                                     {
                                         .parent = container,
                                         .layout {
                                             .size = {knob_width, knob_height},
                                         },
                                     }))) {
        if (options.peak_meter) {
            auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
            auto const knob_width_px = window_r.w;
            auto const peak_meter_width_px = LivePx(UiSizeId::ParamElementKnobPeakMeterW);
            auto const peak_meter_height_px =
                knob_width_px * LiveRaw(UiSizeId::ParamElementKnobPeakMeterHeightPercent) / 100.0f;
            auto const peak_meter_y_offs =
                knob_width_px * LiveRaw(UiSizeId::ParamElementKnobPeakMeterYOffsetPercent) / 100.0f;

            Rect const peak_meter_r {
                .x = window_r.Centre().x - (peak_meter_width_px / 2),
                .y = window_r.y + peak_meter_y_offs,
                .w = peak_meter_width_px,
                .h = peak_meter_height_px,
            };
            DrawPeakMeter(g.imgui, peak_meter_r, *options.peak_meter, {.flash_when_clipping = false});
        }

        DrawKnob(
            g.builder.imgui,
            container.imgui_id,
            g.builder.imgui.ViewportRectToWindowRect(*r),
            MapTo01(new_val ? *new_val : val, param.info.linear_range.min, param.info.linear_range.max),
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
                .style_system = options.style_system,
                .greyed_out = options.greyed_out,
                .is_fake = options.is_fake,
                .bidirectional = options.bidirectional,
            });
    }

    // Draw text input after the knob so its on top.
    if (param_text_input_result) {
        if (auto const rel_r = BoxRect(g.builder, container)) {
            auto const r = g.builder.imgui.ViewportRectToWindowRect(*rel_r);

            DrawParameterTextInput(g.builder.imgui, r, *param_text_input_result);
        }
    }

    if (options.label) {
        auto const label_colours = [&]() -> Colours {
            switch (options.style_system) {
                case GuiStyleSystem::MidPanel: {
                    return options.greyed_out ? Colours {LiveColStruct(UiColMap::MidTextDimmed)}
                                              : Colours {LiveColStruct(UiColMap::MidText)};
                }
                case GuiStyleSystem::TopBottomPanels:
                case GuiStyleSystem::Overlay: {
                    return Col {.c = options.greyed_out ? Col::Overlay0 : Col::Text, .dark_mode = true};
                }
            }
            Panic("Invalid style system");
        }();

        DoBox(g.builder,
              {
                  .parent = container,
                  .text = options.override_label.size ? options.override_label : param.info.gui_label,
                  .text_colours = label_colours,
                  .text_justification = TextJustification::Centred,
                  .layout {
                      .size = {knob_width, k_font_body_size},
                  },
              });
    }

    return container;
}

Box DoButtonParameter(GuiState& g,
                      Box parent,
                      DescribedParamValue const& param,
                      ButtonParameterComponentOptions const& options) {
    auto const height = TextButtonHeight();
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
    DoToggleIcon(g.builder,
                 container,
                 {.state = state, .greyed_out = options.greyed_out, .on_colour = options.on_colour});

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
    AddParamContextMenuBehaviour(g, container, param);

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
                                         .contents_gap = LiveWw(UiSizeId::ParamElementMenuLabelGap),
                                         .contents_direction = layout::Direction::Column,
                                         .contents_align = layout::Alignment::Start,
                                     },
                                 });

    auto const row = DoMidPanelPrevNextRow(g.builder, container, options.width);

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
                  .text_justification = TextJustification::CentredLeft,
                  .text_overflow = TextOverflowType::AllowOverflow,
                  .layout {
                      .size = {layout::k_fill_parent, TextButtonHeight()},
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
            .centre_align = false,
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

        AddParamContextMenuBehaviour(g, window_r, dragger_box.imgui_id, param);
        OverlayMacroDestinationRegion(g, window_r, param.info.index);
    }

    auto const arrows = DoMidPanelPrevNextButtons(g.builder, row, {.greyed_out = options.greyed_out});
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

void ParameterValuePopup(GuiState& g, DescribedParamValue const& param, imgui::Id id, Rect window_r) {
    auto param_ptr = &param;
    ParameterValuePopup(g, {&param_ptr, 1}, id, window_r);
}

void ParameterValuePopup(GuiState& g, Span<DescribedParamValue const*> params, imgui::Id id, Rect window_r) {
    if (!g.imgui.IsActive(id, MouseButton::Left)) return;

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
                                  .justification = TooltipJustification::AboveOrBelow,
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
