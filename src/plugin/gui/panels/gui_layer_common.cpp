// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_layer_common.hpp"

#include <IconsFontAwesome6.h>

#include "engine/loop_modes.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

void DoLoopModeSelector(GuiState& g, Box parent, LayerProcessor& layer) {
    auto& params = g.engine.processor.main_params;
    auto const param = params.DescribedValue(layer.index, LayerParamIndex::LoopMode);
    auto const desired_loop_mode = param.IntValue<param_values::LoopMode>();
    auto const vol_env_on = layer.VolumeEnvelopeIsOn(params);
    auto const actual_loop_behaviour = ActualLoopBehaviour(layer.instrument, desired_loop_mode, vol_env_on);
    auto const default_loop_behaviour =
        ActualLoopBehaviour(layer.instrument, param_values::LoopMode::InstrumentDefault, vol_env_on);

    auto const default_mode_str =
        (String)fmt::Format(g.scratch_arena, "Default: {}", default_loop_behaviour.value.name);

    auto const row = DoMidPanelPrevNextRow(g.builder, parent, layout::k_fill_parent);

    Optional<f32> new_val {};

    // Text button showing current mode
    auto const menu_btn = DoBox(g.builder,
                                {
                                    .parent = row,
                                    .text = actual_loop_behaviour.value.short_name,
                                    .text_colours =
                                        ColSet {
                                            .base = LiveColStruct(UiColMap::MidText),
                                            .hot = LiveColStruct(UiColMap::MidTextHot),
                                            .active = LiveColStruct(UiColMap::MidTextHot),
                                        },
                                    .text_justification = TextJustification::CentredLeft,
                                    .text_overflow = TextOverflowType::ShowDotsOnRight,
                                    .layout {
                                        .size = {layout::k_fill_parent, TextButtonHeight()},
                                    },
                                    .tooltip = FunctionRef<String()> {[&]() -> String {
                                        return fmt::Format(g.scratch_arena,
                                                           "{}: {}\n\n{} {}",
                                                           param.info.name,
                                                           actual_loop_behaviour.value.name,
                                                           actual_loop_behaviour.value.description,
                                                           actual_loop_behaviour.reason);
                                    }},
                                    .button_behaviour = imgui::ButtonConfig {},
                                });

    // Popup menu
    auto const popup_id = (imgui::Id)(SourceLocationHash() ^ param.info.id);
    if (menu_btn.button_fired) g.imgui.OpenPopupMenu(popup_id, menu_btn.imgui_id);

    // NOTE: bounds is a Box so the run lambda is deferred - capture by value, not by reference.
    if (g.imgui.IsPopupMenuOpen(popup_id))
        DoBoxViewport(
            g.builder,
            {
                // This is a bit weird: it seems using a [=] capture-by-value here for the lambda results in
                // the lambda not being a trivially copyable type for some reason. To work around this, we add
                // this verbose lambda capture list.
                .run =
                    [desired_loop_mode = desired_loop_mode,
                     vol_env_on = vol_env_on,
                     default_mode_str = default_mode_str,
                     &g,
                     &layer](GuiBuilder&) {
                        auto const popup_root = DoBox(g.builder,
                                                      {
                                                          .layout {
                                                              .size = layout::k_hug_contents,
                                                              .contents_direction = layout::Direction::Column,
                                                              .contents_align = layout::Alignment::Start,
                                                          },
                                                      });

                        auto const default_loop_behaviour =
                            ActualLoopBehaviour(layer.instrument,
                                                param_values::LoopMode::InstrumentDefault,
                                                vol_env_on);

                        auto const param_index =
                            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopMode);

                        for (auto const i : Range(ToInt(param_values::LoopMode::Count))) {
                            auto const mode = (param_values::LoopMode)i;
                            bool const is_selected = mode == desired_loop_mode;
                            auto const behaviour = ActualLoopBehaviour(layer.instrument, mode, vol_env_on);
                            auto const valid = behaviour.is_desired;

                            auto const item_text = (mode == param_values::LoopMode::InstrumentDefault)
                                                       ? default_mode_str
                                                       : param_values::k_loop_mode_strings[i];

                            DynamicArray<char> tooltip_text {g.scratch_arena};
                            if (!valid)
                                fmt::Append(tooltip_text,
                                            ICON_FA_REPEAT " Not available: {}\n\n",
                                            behaviour.reason);
                            dyn::AppendSpan(tooltip_text, LoopModeDescription(mode));
                            if (mode == param_values::LoopMode::InstrumentDefault) {
                                fmt::Append(tooltip_text, "\n\n{}'s default behaviour: \n", layer.InstName());
                                dyn::AppendSpan(tooltip_text, default_loop_behaviour.value.description);
                                if (auto const reason = default_loop_behaviour.reason; reason.size) {
                                    dyn::Append(tooltip_text, ' ');
                                    dyn::AppendSpan(tooltip_text, reason);
                                }
                            }

                            auto const item = MenuItem(g.builder,
                                                       popup_root,
                                                       {
                                                           .text = item_text,
                                                           .tooltip = String(tooltip_text),
                                                           .is_selected = is_selected,
                                                           .mode = valid ? MenuItemOptions::Mode::Active
                                                                         : MenuItemOptions::Mode::Dimmed,
                                                       },
                                                       (u64)i);
                            if (item.button_fired && mode != desired_loop_mode)
                                SetParameterValue(g.engine.processor, param_index, (f32)i, {});
                        }
                    },
                .bounds = menu_btn,
                .imgui_id = popup_id,
                .viewport_config = k_default_popup_menu_viewport,
            });

    // Prev/next arrows (skip invalid modes)
    auto const arrows = DoMidPanelPrevNextButtons(g.builder,
                                                  row,
                                                  {
                                                      .prev_tooltip = "Previous loop mode"_s,
                                                      .next_tooltip = "Next loop mode"_s,
                                                  });

    if (arrows.prev_fired || arrows.next_fired) {
        f32 const step = arrows.prev_fired ? -1.0f : 1.0f;
        auto check_val = (f32)param.IntValue<int>() + step;
        for (auto _ : Range(ToInt(param_values::LoopMode::Count))) {
            if (check_val < param.info.linear_range.min) check_val = param.info.linear_range.max;
            if (check_val > param.info.linear_range.max) check_val = param.info.linear_range.min;

            auto const mode = (param_values::LoopMode)(int)check_val;
            if (mode != param_values::LoopMode::InstrumentDefault) {
                if (auto const other = ActualLoopBehaviour(layer.instrument, mode, vol_env_on);
                    other.is_desired && other.value.id != actual_loop_behaviour.value.id) {
                    new_val = check_val;
                    break;
                }
            }
            check_val += step;
        }
    }

    // Slider behaviour and parameter lifecycle
    if (auto const viewport_r = BoxRect(g.builder, menu_btn)) {
        auto const window_r = g.imgui.RegisterAndConvertRect(*viewport_r);

        auto current = param.LinearValue();
        if (g.imgui.SliderBehaviourRange({
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
}
