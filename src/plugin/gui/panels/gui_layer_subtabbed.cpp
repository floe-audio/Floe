// Copyright 2024-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_layer_subtabbed.hpp"

#include <IconsFontAwesome6.h>

#include "engine/engine.hpp"
#include "engine/engine_prefs.hpp"
#include "engine/loop_modes.hpp"
#include "gui/controls/gui_arp_step_sequencer.hpp"
#include "gui/controls/gui_curve_map.hpp"
#include "gui/controls/gui_envelope.hpp"
#include "gui/controls/gui_eq.hpp"
#include "gui/controls/gui_filter_visualiser.hpp"
#include "gui/controls/gui_lfo_display.hpp"
#include "gui/controls/gui_waveform.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_layer_common.hpp"
#include "gui/panels/gui_macros.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processing_utils/arpeggiator.hpp"
#include "processor/granular.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

constexpr f32 k_page_row_gap_y = 7;
constexpr f32 k_page_row_gap_x = 7;
constexpr f32 k_knob_width = 36;

static void DoTabRightClickMenu(GuiState& g,
                                Box tab_button,
                                u8 layer_index,
                                ParameterModule tab_module,
                                String tab_name,
                                u64 id_extra) {
    auto const right_click_id = SourceLocationHash() + id_extra;

    auto const r = BoxRect(g.builder, tab_button);
    if (!r) return;

    auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
    if (g.imgui.ButtonBehaviour(window_r,
                                tab_button.imgui_id,
                                {
                                    .mouse_button = MouseButton::Right,
                                    .event = MouseButtonEvent::Up,
                                })) {
        g.imgui.OpenPopupMenu(right_click_id, tab_button.imgui_id);
    }

    if (!g.imgui.IsPopupMenuOpen(right_click_id)) return;

    DoBoxViewport(
        g.builder,
        {
            .run =
                [&](GuiBuilder&) {
                    auto const root = DoBox(g.builder,
                                            {
                                                .layout {
                                                    .size = layout::k_hug_contents,
                                                    .contents_direction = layout::Direction::Column,
                                                    .contents_align = layout::Alignment::Start,
                                                },
                                            });

                    ParamModules const target_modules {LayerModuleFromIndex(layer_index), tab_module};

                    if (MenuItem(g.builder,
                                 root,
                                 {
                                     .text = fmt::Format(g.scratch_arena, "Copy {}"_s, tab_name),
                                     .no_icon_gap = true,
                                 })
                            .button_fired) {
                        g.snapshot_clipboard = GuiState::CopiedSection {
                            .snapshot = CurrentStateSnapshot(g.engine),
                            .selector = ParamModules {LayerModuleFromIndex(layer_index), tab_module},
                        };
                    }

                    auto const can_paste =
                        g.snapshot_clipboard.HasValue() &&
                        g.snapshot_clipboard->selector.tag == SelectorKind::Modules &&
                        g.snapshot_clipboard->selector.Get<ParamModules>()[1] == tab_module;

                    if (MenuItem(g.builder,
                                 root,
                                 {
                                     .text = fmt::Format(g.scratch_arena, "Paste {}"_s, tab_name),
                                     .mode = can_paste ? MenuItemOptions::Mode::Active
                                                       : MenuItemOptions::Mode::Disabled,
                                     .no_icon_gap = true,
                                 })
                            .button_fired &&
                        can_paste) {
                        ApplySection(g.engine,
                                     g.snapshot_clipboard->snapshot,
                                     g.snapshot_clipboard->selector,
                                     StateSnapshotSelector {target_modules});
                    }

                    if (MenuItem(g.builder,
                                 root,
                                 {
                                     .text = fmt::Format(g.scratch_arena, "Reset {}"_s, tab_name),
                                     .no_icon_gap = true,
                                 })
                            .button_fired) {
                        StateSnapshotSelector const selector {target_modules};
                        ApplySection(g.engine, DefaultStateSnapshot(), selector, selector);
                    }
                },
            .bounds = window_r,
            .imgui_id = right_click_id,
            .viewport_config = k_default_popup_menu_viewport,
        });
}

static void DoLoopModeSelector(GuiState& g, Box parent, LayerProcessor& layer) {
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
                                    .text = actual_loop_behaviour.value.name,
                                    .text_colours =
                                        ColSet {
                                            .base = LiveColStruct(UiColMap::MidText),
                                            .hot = LiveColStruct(UiColMap::MidTextHot),
                                            .active = LiveColStruct(UiColMap::MidTextHot),
                                        },
                                    .text_justification = TextJustification::CentredLeft,
                                    .text_overflow = TextOverflowType::ShowDotsOnRight,
                                    .layout {
                                        .size = {layout::k_fill_parent, k_mid_button_height},
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

void DoInstSelector(GuiState& g, GuiFrameContext const& frame_context, u8 layer_index, Box root) {
    auto& layer_obj = g.engine.Layer(layer_index);
    auto const inst_name = layer_obj.InstName();

    // Selector row container
    auto const selector_box = DoBox(g.builder,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_padding {.r = 3.44f},
                                            .contents_direction = layout::Direction::Row,
                                            .contents_align = layout::Alignment::Start,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                        },
                                    });

    // Background drawing
    if (auto const r = BoxRect(g.builder, selector_box)) {
        auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
        auto const rounding = WwToPixels(k_corner_rounding);

        {
            auto const col = LiveCol(UiColMap::MidDarkSurface);
            g.imgui.draw_list->AddRectFilled(window_r, col, rounding);
        }

        // Timbre layer highlight
        if (layer_obj.UsesTimbreLayering() &&
            (g.timbre_slider_is_held ||
             CcControllerMovedParamRecently(g.engine.processor, ParamIndex::MasterTimbre))) {
            g.imgui.draw_list->AddRectFilled(window_r,
                                             LiveCol(UiColMap::InstSelectorMenuBackHighlight),
                                             rounding);
        }

        // Loading progress bar
        if (auto percent =
                g.engine.sample_lib_server_async_channel.instrument_loading_percents[(usize)layer_index].Load(
                    LoadMemoryOrder::Relaxed);
            percent != -1) {
            f32 const load_percent = (f32)percent / 100.0f;
            auto const min = window_r.Min();
            auto const max = f32x2 {window_r.x + Max(4.0f, window_r.w * load_percent), window_r.Bottom()};
            g.imgui.draw_list->AddRectFilled(min, max, LiveCol(UiColMap::InstSelectorMenuLoading), rounding);
            GuiIo().WakeupAtTimedInterval(g.redraw_counter, 0.1, SourceLocationHash());
        }
    }

    // Instrument name button (icon + text)
    Optional<TextureHandle> icon_tex {};
    if (layer_obj.instrument_id.tag == InstrumentType::Sampler) {
        auto const sample_inst_id = layer_obj.instrument_id.Get<sample_lib::InstrumentId>();
        auto const imgs = GetLibraryImages(g.library_images,
                                           g.imgui,
                                           sample_inst_id.library,
                                           g.shared_engine_systems.sample_library_server,
                                           g.engine.instance_index,
                                           LibraryImagesTypes::Icon);
        if (imgs.icon) icon_tex = GuiIo().in.renderer->GetTextureFromImage(*imgs.icon);
    }

    auto const inst_button = DoBox(
        g.builder,
        {
            .parent = selector_box,
            .parent_dictates_hot_and_active = true,
            .layout {
                .size = {layout::k_fill_parent, layout::k_hug_contents},
                .contents_direction = layout::Direction::Row,
                .contents_align = layout::Alignment::Start,
                .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
            },
            .tooltip = FunctionRef<String()> {[&]() -> String {
                switch (layer_obj.instrument.tag) {
                    case InstrumentType::None: return "Select the instrument for this layer"_s;
                    case InstrumentType::WaveformSynth:
                        return fmt::Format(
                            g.scratch_arena,
                            "Current instrument: {}\nChange or remove the instrument for this layer",
                            inst_name);
                    case InstrumentType::Sampler: {
                        auto const& sample = layer_obj.instrument.GetFromTag<InstrumentType::Sampler>();
                        return fmt::Format(
                            g.scratch_arena,
                            "Change or remove the instrument for this layer\n\nCurrent instrument: {} from {} by {}.{}{}",
                            inst_name,
                            sample->instrument.library.name,
                            sample->instrument.library.author,
                            sample->instrument.description ? "\n\n" : "",
                            sample->instrument.description ? sample->instrument.description : "");
                    }
                }
                return {};
            }},
            .button_behaviour = imgui::ButtonConfig {},
        });

    // Icon box
    if (icon_tex) {
        auto const icon_box = DoBox(g.builder,
                                    {
                                        .parent = inst_button,
                                        .parent_dictates_hot_and_active = true,
                                        .layout {
                                            .size = {22.54f, layout::k_fill_parent},
                                        },
                                    });
        if (auto const r = BoxRect(g.builder, icon_box)) {
            auto const icon_r = r->Reduced(r->h / 10);
            g.imgui.draw_list->AddImageRect(*icon_tex, g.imgui.ViewportRectToWindowRect(icon_r));
        }
    }

    // Text box
    DoBox(g.builder,
          {
              .parent = inst_button,
              .text = inst_name,
              .text_colours =
                  ColSet {
                      .base = LiveColStruct(UiColMap::MidText),
                      .hot = LiveColStruct(UiColMap::MidTextHot),
                      .active = LiveColStruct(UiColMap::MidTextOn),
                  },
              .text_justification = TextJustification::CentredLeft,
              .text_overflow = TextOverflowType::ShowDotsOnRight,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = {layout::k_fill_parent, k_mid_button_height},
                  .margins {.l = icon_tex ? 0.0f : 7.54f},
              },
          });

    if (inst_button.button_fired) {
        g.imgui.OpenModalViewport(g.inst_browser_state[layer_index].id);
        if (auto const r = BoxRect(g.builder, inst_button))
            g.inst_browser_state[layer_index].common_state.absolute_button_rect =
                g.imgui.ViewportRectToWindowRect(*r);
    }

    // Right-click menu
    DoInstSelectorRightClickMenu(g, inst_button, layer_index);

    // Prev/next buttons
    auto const prev_next = DoMidPanelPrevNextButtons(
        g.builder,
        selector_box,
        {
            .prev_tooltip =
                "Load the previous instrument\n\nThis is based on the currently selected filters."_s,
            .next_tooltip = "Load the next instrument\n\nThis is based on the currently selected filters."_s,
        });

    auto const make_browser_context = [&]() -> InstBrowserContext {
        return {
            .layer = layer_obj,
            .sample_library_server = g.shared_engine_systems.sample_library_server,
            .library_images = g.library_images,
            .engine = g.engine,
            .prefs = g.prefs,
            .notifications = g.notifications,
            .persistent_store = g.shared_engine_systems.persistent_store,
            .confirmation_dialog_state = g.confirmation_dialog_state,
            .frame_context = frame_context,
        };
    };

    if (prev_next.prev_fired) {
        auto context = make_browser_context();
        LoadAdjacentInstrument(context, g.inst_browser_state[layer_index], SearchDirection::Backward);
    }
    if (prev_next.next_fired) {
        auto context = make_browser_context();
        LoadAdjacentInstrument(context, g.inst_browser_state[layer_index], SearchDirection::Forward);
    }

    // Shuffle button
    auto const shuffle_btn = DoMidPanelShuffleButton(
        g.builder,
        selector_box,
        {.tooltip = "Load a random instrument.\n\nThis is based on the currently selected filters."_s});
    if (shuffle_btn.button_fired) {
        auto context = make_browser_context();
        LoadRandomInstrument(context, g.inst_browser_state[layer_index]);
    }
}

void DoInstrumentInfoStrip(GuiState& g, u8 layer_index, Box parent) {
    auto& layer_processor = g.engine.processor.layer_processors[layer_index];

    if (layer_processor.instrument.tag == InstrumentType::None) return;

    // Collect info segments to display, separated by dot dividers
    DynamicArrayBounded<String, 6> segments {};

    switch (layer_processor.instrument.tag) {
        case InstrumentType::WaveformSynth: {
            dyn::Append(segments, "Oscillator waveform"_s);
            break;
        }
        case InstrumentType::Sampler: {
            auto const& inst = layer_processor.instrument
                                   .Get<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>();
            auto const num_regions = inst->instrument.regions.size;
            if (inst->instrument.category == sample_lib::SamplerCategory::Empty) {
                dyn::Append(segments, "Empty instrument"_s);
            } else if (inst->instrument.category == sample_lib::SamplerCategory::Sliced) {
                dyn::Append(segments, "Sliced"_s);
                auto const& smpl = *inst->audio_datas[0];
                dyn::Append(
                    segments,
                    fmt::Format(g.scratch_arena, "{} slices", inst->instrument.regions[0].slices.size));
                if (smpl.channels == 1) dyn::Append(segments, "Mono"_s);
                dyn::Append(
                    segments,
                    fmt::Format(g.scratch_arena, "{.2} s", (f64)smpl.num_frames / (f64)smpl.sample_rate));
            } else if (inst->instrument.category == sample_lib::SamplerCategory::SingleSample) {
                dyn::Append(segments, "Single sample"_s);
                auto const& smpl = *inst->audio_datas[0];
                if (smpl.channels == 1) dyn::Append(segments, "Mono"_s);
                dyn::Append(
                    segments,
                    fmt::Format(g.scratch_arena, "{.2} s", (f64)smpl.num_frames / (f64)smpl.sample_rate));
                dyn::Append(segments,
                            fmt::Format(g.scratch_arena,
                                        "Root key {}",
                                        NoteName((u7)inst->instrument.regions[0].root_key)));
            } else {
                dyn::Append(segments, "Multisample instrument"_s);
                dyn::Append(segments, fmt::Format(g.scratch_arena, "{} samples", num_regions));
            }
            break;
        }
        case InstrumentType::None: break;
    }

    if (!segments.size) return;

    auto const strip = DoBox(g.builder,
                             {
                                 .parent = parent,
                                 .layout {
                                     .size = {layout::k_fill_parent, 20},
                                     .contents_gap = 6,
                                     .contents_direction = layout::Direction::Row,
                                     .contents_align = layout::Alignment::Start,
                                     .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                 },
                             });

    for (auto const i : Range(segments.size)) {
        if (i > 0) {
            auto const dot = DoBox(g.builder,
                                   {
                                       .parent = strip,
                                       .id_extra = i,
                                       .layout {
                                           .size = {3, 3},
                                       },
                                   });
            if (auto const r = BoxRect(g.builder, dot)) {
                auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
                g.imgui.draw_list->AddCircleFilled(window_r.Centre(), 1.5f, LiveCol(UiColMap::MidTextDimmed));
            }
        }

        DoBox(g.builder,
              {
                  .parent = strip,
                  .id_extra = i,
                  .text = segments[i],
                  .size_from_text = true,
                  .font = FontType::Heading3,
                  .text_colours = LiveColStruct(UiColMap::MidTextDimmed),
                  .text_justification = TextJustification::CentredLeft,
              });
    }
}

static void DoMixerRow(GuiState& g, u8 layer_index, Box root) {
    auto& params = g.engine.processor.main_params;

    constexpr f32 k_vol_slider_width = 16;
    constexpr f32 k_vol_slider_height = 75;

    auto const container = DoBox(g.builder,
                                 {
                                     .parent = root,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = 24,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Middle,
                                     },
                                 });

    // Volume: peak meter + vertical slider
    {
        auto const vol_col = DoBox(g.builder,
                                   {
                                       .parent = container,
                                       .layout {
                                           .size = layout::k_hug_contents,
                                           .contents_gap = 4,
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Start,
                                       },
                                   });

        // Peak meter
        auto const& layer_processor = g.engine.processor.layer_processors[layer_index];
        auto const meter_box = DoBox(g.builder,
                                     {
                                         .parent = vol_col,
                                         .layout {
                                             .size = {k_peak_meter_standard_width, k_vol_slider_height},
                                         },
                                     });
        if (auto const r = BoxRect(g.builder, meter_box))
            DrawPeakMeter(g.imgui,
                          g.imgui.ViewportRectToWindowRect(*r),
                          layer_processor.peak_meter,
                          {.flash_when_clipping = false});

        // Volume slider
        DoVerticalSliderParameter(g,
                                  vol_col,
                                  params.DescribedValue(layer_index, LayerParamIndex::Volume),
                                  {
                                      .width = k_vol_slider_width,
                                      .height = k_vol_slider_height,
                                      .style_system = GuiStyleSystem::MidPanel,
                                  });
    }

    // Middle column: Pan knob + Mute/Solo
    {
        auto const mid_col = DoBox(g.builder,
                                   {
                                       .parent = container,
                                       .layout {
                                           .size = layout::k_hug_contents,
                                           .contents_gap = 6,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Middle,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                   });

        DoKnobParameter(g,
                        mid_col,
                        params.DescribedValue(layer_index, LayerParamIndex::Pan),
                        {
                            .width = 30,
                            .style_system = GuiStyleSystem::MidPanel,
                            .bidirectional = true,
                        });

        // Mute/Solo buttons (horizontal)
        DoMuteSoloButtons(g,
                          mid_col,
                          params.DescribedValue(layer_index, LayerParamIndex::Mute),
                          params.DescribedValue(layer_index, LayerParamIndex::Solo));
    }

    // Right column: Pitch + Detune
    {
        auto const right_col = DoBox(g.builder,
                                     {
                                         .parent = container,
                                         .layout {
                                             .size = layout::k_hug_contents,
                                             .contents_gap = 4,
                                             .contents_direction = layout::Direction::Column,
                                             .contents_align = layout::Alignment::Middle,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                         },
                                     });

        DoKnobParameter(g,
                        right_col,
                        params.DescribedValue(layer_index, LayerParamIndex::TuneCents),
                        {
                            .width = 30,
                            .style_system = GuiStyleSystem::MidPanel,
                            .bidirectional = true,
                        });

        DoIntParameter(g,
                       right_col,
                       params.DescribedValue(layer_index, LayerParamIndex::TuneSemitone),
                       {
                           .width = 58,
                           .always_show_plus = true,
                       });
    }
}

static void DoWhitespace(GuiBuilder& builder, Box parent, f32 height, u64 loc_hash = SourceLocationHash()) {
    DoBox(builder,
          {
              .parent = parent,
              .id_extra = loc_hash,
              .layout {.size = {1, height}},
          });
}

static void DoPageTabs(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    auto& layer_state = g.layer_panel_states[layer_index];

    auto const tabs_row = DoBox(g.builder,
                                {
                                    .parent = parent,
                                    .background_fill_colours = Col {.c = Col::Black, .alpha = 20},
                                    .corner_rounding = k_panel_rounding,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_padding = {.lr = 3, .tb = 3},
                                        .contents_gap = 2,
                                        .contents_direction = layout::Direction::Row,
                                        .contents_align = layout::Alignment::Middle,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                    },
                                });

    auto const experimental_params = prefs::GetBool(g.prefs, ExperimentalParamsPreferenceDescriptor());

    for (auto const i : Range(ToInt(LayerPageType::Count))) {
        auto const page_type = (LayerPageType)i;
        if (page_type == LayerPageType::Arp && !experimental_params) continue;
        bool const is_selected = page_type == layer_state.selected_page;
        bool const tab_has_active_content = ({
            bool result = false;
            switch (page_type) {
                case LayerPageType::Lfo:
                    result = params.BoolValue(layer_index, LayerParamIndex::LfoOn);
                    break;
                case LayerPageType::Eq: result = params.BoolValue(layer_index, LayerParamIndex::EqOn); break;
                case LayerPageType::Arp:
                    result = g.engine.processor.layer_processors[layer_index].arp_state.on_for_gui.Load(
                        LoadMemoryOrder::Relaxed);
                    break;
                case LayerPageType::Main:
                case LayerPageType::Playback:
                case LayerPageType::Config:
                case LayerPageType::Count: break;
            }
            result;
        });

        auto const name = [&]() -> String {
            switch (page_type) {
                case LayerPageType::Main: return "MAIN"_s;
                case LayerPageType::Playback: return "PLAYBACK"_s;
                case LayerPageType::Eq: return "EQ"_s;
                case LayerPageType::Config: return "CONFIG"_s;
                case LayerPageType::Lfo: return "LFO"_s;
                case LayerPageType::Arp: return "ARP"_s;
                case LayerPageType::Count: PanicIfReached();
            }
            return {};
        }();

        auto const tab_btn = DoTabButton(g.builder,
                                         tabs_row,
                                         name,
                                         {
                                             .is_selected = is_selected,
                                             .show_dot_indicator = tab_has_active_content,
                                             .tooltip = FunctionRef<String()> {[&]() -> String {
                                                 return fmt::Format(g.scratch_arena, "Open {} tab", name);
                                             }},
                                         },
                                         (u64)i);

        if (tab_btn.button_fired) layer_state.selected_page = page_type;

        auto const tab_module = ({
            ParameterModule m {};
            switch (page_type) {
                case LayerPageType::Main: m = ParameterModule::Main; break;
                case LayerPageType::Playback: m = ParameterModule::Playback; break;
                case LayerPageType::Lfo: m = ParameterModule::Lfo; break;
                case LayerPageType::Eq: m = ParameterModule::Eq; break;
                case LayerPageType::Config: m = ParameterModule::Config; break;
                case LayerPageType::Arp: m = ParameterModule::Arp; break;
                case LayerPageType::Count: PanicIfReached();
            }
            m;
        });
        DoTabRightClickMenu(g, tab_btn, layer_index, tab_module, name, (u64)(i + (layer_index * 16)));
    }

    // Auto-switch tab when a macro destination knob is being interacted
    if (g.macros_gui_state.active_destination_knob) {
        auto const param =
            LayerParamIndexAndLayerFor(*g.macros_gui_state.active_destination_knob->dest.param_index);
        if (param && param->layer_num == layer_index) {
            auto const k_desc = ParamDescriptorAt(ParamIndexFromLayerParamIndex(layer_index, param->param));
            Optional<LayerPageType> new_page {};
            if (k_desc.module_parts.size >= 2) {
                switch (k_desc.module_parts[1]) {
                    case ParameterModule::Main: new_page = LayerPageType::Main; break;
                    case ParameterModule::Playback: new_page = LayerPageType::Playback; break;
                    case ParameterModule::Lfo: new_page = LayerPageType::Lfo; break;
                    case ParameterModule::Eq: new_page = LayerPageType::Eq; break;
                    case ParameterModule::Config: new_page = LayerPageType::Config; break;
                    case ParameterModule::Arp: new_page = LayerPageType::Arp; break;
                    default: break;
                }
            }
            if (new_page && *new_page != layer_state.selected_page) {
                layer_state.selected_page = *new_page;
                GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
            }
        }
    }
}

static void DoFilterPage(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;
    bool const filter_on = params.BoolValue(layer_index, LayerParamIndex::FilterOn);
    bool const greyed_out = !filter_on;

    constexpr f32 k_small_knob_w = 24.0f;

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .margins = {.t = 15},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    DoButtonParameter(g,
                      page,
                      params.DescribedValue(layer_index, LayerParamIndex::FilterOn),
                      {.width = layout::k_fill_parent});

    DoWhitespace(g.builder, page, 8);

    // Visual filter at top.
    {
        auto const vis_box = DoBox(g.builder,
                                   {
                                       .parent = page,
                                       .layout {
                                           .size = {layout::k_fill_parent, 80},
                                       },
                                   });
        if (auto const r = BoxRect(g.builder, vis_box)) DoFilterVisualizer(g, layer_index, *r, greyed_out);
    }

    DoWhitespace(g.builder, page, 12);

    // Envelope and controls side by side.
    auto const bottom_row = DoBox(g.builder,
                                  {
                                      .parent = page,
                                      .layout {
                                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                                          .contents_gap = 12,
                                          .contents_direction = layout::Direction::Row,
                                          .contents_align = layout::Alignment::Start,
                                      },
                                  });

    // Type menu + compact knobs.
    {
        auto const controls_col = DoBox(g.builder,
                                        {
                                            .parent = bottom_row,
                                            .layout {
                                                .size = {layout::k_hug_contents, layout::k_hug_contents},
                                                .contents_gap = 8,
                                                .contents_direction = layout::Direction::Column,
                                                .contents_align = layout::Alignment::Start,
                                                .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                            },
                                        });

        DoMenuParameter(g,
                        controls_col,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterType),
                        {
                            .width = 120,
                            .greyed_out = greyed_out,
                            .label = false,
                        });

        auto const knobs_row = DoBox(g.builder,
                                     {
                                         .parent = controls_col,
                                         .layout {
                                             .size = layout::k_hug_contents,
                                             .contents_gap = 12,
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Middle,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                         },
                                     });

        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterCutoff),
                        {
                            .width = k_small_knob_w,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterResonance),
                        {
                            .width = k_small_knob_w,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterEnvAmount),
                        {
                            .width = k_small_knob_w,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                            .bidirectional = true,
                        });
    }

    // Narrower envelope.
    {
        auto const envelope_box = DoBox(g.builder,
                                        {
                                            .parent = bottom_row,
                                            .layout {
                                                .size = {layout::k_fill_parent, 60},
                                            },
                                        });

        bool const env_greyed_out =
            greyed_out || (params.LinearValue(layer_index, LayerParamIndex::FilterEnvAmount) == 0);
        if (auto const r = BoxRect(g.builder, envelope_box))
            DoEnvelopeGui(g,
                          layer,
                          *r,
                          env_greyed_out,
                          {LayerParamIndex::FilterAttack,
                           LayerParamIndex::FilterDecay,
                           LayerParamIndex::FilterSustain,
                           LayerParamIndex::FilterRelease},
                          GuiEnvelopeType::Filter);
    }
}

static void DoEqPage(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    bool const greyed_out = !params.BoolValue(layer_index, LayerParamIndex::EqOn);

    constexpr f32 k_small_knob_w = 24.0f;
    constexpr f32 k_small_knob_gap = 12.0f;

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    // EqOn heading
    DoButtonParameter(g,
                      page,
                      params.DescribedValue(layer_index, LayerParamIndex::EqOn),
                      {
                          .width = layout::k_fill_parent,
                      });

    DoWhitespace(g.builder, page, 8);

    // Visual EQ at top.
    {
        auto const vis_box = DoBox(g.builder,
                                   {
                                       .parent = page,
                                       .layout {
                                           .size = {layout::k_fill_parent, 140},
                                       },
                                   });
        if (auto const r = BoxRect(g.builder, vis_box)) DoEqVisualizer(g, layer_index, *r, greyed_out);
    }

    DoWhitespace(g.builder, page, 22);

    // Compact band control rows at the bottom.
    auto const bands_container = DoBox(g.builder,
                                       {
                                           .parent = page,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_gap = 8,
                                               .contents_direction = layout::Direction::Column,
                                               .contents_align = layout::Alignment::Start,
                                           },
                                       });

    auto const do_eq_band_row = [&](LayerParamIndex type_param,
                                    LayerParamIndex freq_param,
                                    LayerParamIndex reso_param,
                                    LayerParamIndex gain_param,
                                    u8 band_number) {
        auto const row = DoBox(g.builder,
                               {
                                   .parent = bands_container,
                                   .id_extra = band_number,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = k_small_knob_gap,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_align = layout::Alignment::Middle,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        auto const label_and_menu = DoBox(g.builder,
                                          {
                                              .parent = row,
                                              .layout {
                                                  .size = layout::k_hug_contents,
                                                  .contents_gap = 5,
                                                  .contents_direction = layout::Direction::Row,
                                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                              },
                                          });

        DoBox(g.builder,
              {
                  .parent = label_and_menu,
                  .text = fmt::Format(g.scratch_arena, "{}", band_number),
                  .text_colours = LiveColStruct(greyed_out ? UiColMap::MidTextDimmed : UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {
                      .size = {8, k_font_body_size},
                  },
              });

        DoMenuParameter(g,
                        label_and_menu,
                        params.DescribedValue(layer_index, type_param),
                        {
                            .width = 110,
                            .greyed_out = greyed_out,
                            .label = false,
                        });

        DoKnobParameter(g,
                        row,
                        params.DescribedValue(layer_index, freq_param),
                        {
                            .width = k_small_knob_w,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        row,
                        params.DescribedValue(layer_index, reso_param),
                        {
                            .width = k_small_knob_w,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        row,
                        params.DescribedValue(layer_index, gain_param),
                        {
                            .width = k_small_knob_w,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                            .bidirectional = true,
                        });
    };

    do_eq_band_row(LayerParamIndex::EqType1,
                   LayerParamIndex::EqFreq1,
                   LayerParamIndex::EqResonance1,
                   LayerParamIndex::EqGain1,
                   1);

    do_eq_band_row(LayerParamIndex::EqType2,
                   LayerParamIndex::EqFreq2,
                   LayerParamIndex::EqResonance2,
                   LayerParamIndex::EqGain2,
                   2);
}

static void DoLfoPage(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    bool const greyed_out = !params.BoolValue(layer_index, LayerParamIndex::LfoOn);

    constexpr f32 k_menu_width = 135;
    constexpr f32 k_menu_label_width = 50;

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_gap = k_page_row_gap_y,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                },
                            });

    // LfoOn
    DoButtonParameter(g,
                      page,
                      params.DescribedValue(layer_index, LayerParamIndex::LfoOn),
                      {
                          .width = layout::k_fill_parent,
                      });

    DoWhitespace(g.builder, page, 6);

    // Visual display of the LFO shape scaled by amount.
    {
        auto const vis_box = DoBox(g.builder,
                                   {
                                       .parent = page,
                                       .layout {
                                           .size = {layout::k_fill_parent, 70},
                                       },
                                   });
        if (auto const r = BoxRect(g.builder, vis_box)) DoLfoDisplay(g, layer_index, *r, greyed_out);
    }

    DoWhitespace(g.builder, page, 10);

    // Menu + label rows
    auto const do_menu_label_row = [&](LayerParamIndex param_index, u64 loc_hash = SourceLocationHash()) {
        auto const param = params.DescribedValue(layer_index, param_index);

        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .id_extra = loc_hash,
                                   .layout {
                                       .size = layout::k_hug_contents,
                                       .contents_gap = k_page_row_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours = LiveColStruct(greyed_out ? UiColMap::MidTextDimmed : UiColMap::MidText),
                  .text_justification = TextJustification::CentredRight,
                  .layout {
                      .size = {k_menu_label_width, k_font_body_size},
                  },
              });

        DoMenuParameter(g,
                        row,
                        param,
                        {
                            .width = k_menu_width,
                            .greyed_out = greyed_out,
                            .label = false,
                        });
    };

    do_menu_label_row(LayerParamIndex::LfoDestination);
    do_menu_label_row(LayerParamIndex::LfoShape);
    do_menu_label_row(LayerParamIndex::LfoRestart);

    DoWhitespace(g.builder, page, 5);

    // Knobs row: Amount + Rate column
    {
        auto const knobs_row = DoBox(g.builder,
                                     {
                                         .parent = page,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = 20,
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Middle,
                                         },
                                     });

        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::LfoAmount),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                            .bidirectional = true,
                        });

        // Rate column
        auto const rate_col = DoBox(g.builder,
                                    {
                                        .parent = knobs_row,
                                        .layout {
                                            .size = layout::k_hug_contents,
                                            .contents_gap = 3,
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Middle,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                        },
                                    });

        if (params.BoolValue(layer_index, LayerParamIndex::LfoSyncSwitch)) {
            DoMenuParameter(g,
                            rate_col,
                            params.DescribedValue(layer_index, LayerParamIndex::LfoRateTempoSynced),
                            {.greyed_out = greyed_out});
        } else {
            DoKnobParameter(g,
                            rate_col,
                            params.DescribedValue(layer_index, LayerParamIndex::LfoRateHz),
                            {
                                .width = k_knob_width,
                                .style_system = GuiStyleSystem::MidPanel,
                                .greyed_out = greyed_out,
                            });
        }

        DoButtonParameter(g,
                          rate_col,
                          params.DescribedValue(layer_index, LayerParamIndex::LfoSyncSwitch),
                          {.width = layout::k_hug_contents, .greyed_out = greyed_out});
    }
}

static void DoConfigPage(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;

    constexpr auto k_play_label_width = 105;
    constexpr auto k_control_width = 76;
    constexpr auto k_narrow_control_width = 63;
    constexpr auto k_narrow_control_gap_x = 3;

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .margins = {.t = 15},
                                    .contents_gap = 6,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                },
                            });

    // Helper for int dragger + label rows (Transpose, PitchBend)
    auto const do_int_label_row = [&](LayerParamIndex param_index, u64 loc_hash = SourceLocationHash()) {
        auto const param = params.DescribedValue(layer_index, param_index);

        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .id_extra = loc_hash,
                                   .layout {
                                       .size = layout::k_hug_contents,
                                       .contents_gap = k_page_row_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredRight,
                  .layout {
                      .size = {k_play_label_width, k_font_body_size},
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String { return param.info.tooltip; }},
              });

        DoIntParameter(g,
                       row,
                       param,
                       {
                           .width = k_control_width,
                           .label = false,
                       });
    };

    do_int_label_row(LayerParamIndex::MidiTranspose);

    do_int_label_row(LayerParamIndex::PitchBendRange);

    // Keytrack
    {
        auto const param = params.DescribedValue(layer_index, LayerParamIndex::Keytrack);
        bool const state = param.BoolValue();

        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .id_extra = (u64)param.info.id,
                                   .layout {
                                       .size = layout::k_hug_contents,
                                       .contents_gap = k_page_row_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                                   .tooltip = FunctionRef<String()> {[&]() -> String {
                                       return ParamTooltipText(param, g.builder.arena);
                                   }},
                                   .button_behaviour = imgui::ButtonConfig {},
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours =
                      ColSet {
                          .base = LiveColStruct(UiColMap::MidText),
                          .hot = LiveColStruct(UiColMap::MidTextHot),
                          .active = LiveColStruct(UiColMap::MidTextHot),
                      },
                  .text_justification = TextJustification::CentredRight,
                  .parent_dictates_hot_and_active = true,
                  .layout {
                      .size = {k_play_label_width, k_font_body_size},
                  },
              });

        DoToggleIcon(g.builder,
                     row,
                     {
                         .state = state,
                         .width = k_control_width,
                         .justify = TextJustification::CentredLeft,
                     });

        if (row.button_fired)
            SetParameterValue(g.engine.processor, param.info.index, state ? 0.0f : 1.0f, {});

        AddParamContextMenuBehaviour(g, row, param);
    }

    // Monophonic mode
    {
        auto const param = params.DescribedValue(layer_index, LayerParamIndex::MonophonicMode);

        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = layout::k_hug_contents,
                                       .contents_gap = k_page_row_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredRight,
                  .layout {
                      .size = {k_play_label_width, k_font_body_size},
                  },
              });

        DoMenuParameter(g, row, param, {.width = k_control_width, .label = false});
    }

    // Key Range row
    {
        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = layout::k_hug_contents,
                                       .contents_gap = k_narrow_control_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = "Range"_s,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredRight,
                  .layout {
                      .size = {k_play_label_width, k_font_body_size},
                      .margins {.r = k_page_row_gap_x - k_narrow_control_gap_x},
                  },
              });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeLow),
                       {
                           .width = k_narrow_control_width,
                           .midi_note_names = true,
                           .label = false,
                       });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeHigh),
                       {
                           .width = k_narrow_control_width,
                           .midi_note_names = true,
                           .label = false,
                       });
    }

    // Key Fade row
    {
        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = layout::k_hug_contents,
                                       .contents_gap = k_narrow_control_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = "Key Fade"_s,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredRight,
                  .layout {
                      .size = {k_play_label_width, k_font_body_size},
                      .margins {.r = k_page_row_gap_x - k_narrow_control_gap_x},
                  },
              });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeLowFade),
                       {
                           .width = k_narrow_control_width,
                           .label = false,
                       });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeHighFade),
                       {
                           .width = k_narrow_control_width,
                           .label = false,
                       });
    }

    // Velocity curve
    {
        auto const col = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .margins = {.t = 4},
                                       .contents_gap = 2,
                                       .contents_direction = layout::Direction::Column,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });
        // Label
        DoBox(g.builder,
              {
                  .parent = col,
                  .text = "Velocity to volume curve"_s,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {
                      .size = {layout::k_fill_parent, k_font_body_size},
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String {
                      return "Curve that maps velocity to volume"_s;
                  }},
              });

        // Element
        auto const velo_box = DoBox(g.builder,
                                    {
                                        .parent = col,
                                        .layout {
                                            .size = {layout::k_fill_parent, 80},
                                        },
                                    });

        if (auto const r = BoxRect(g.builder, velo_box)) {
            auto const window_r = g.imgui.ViewportRectToWindowRect(*r);

            Optional<f32> velocity {};
            if (g.engine.processor.voice_pool.num_active_voices.Load(LoadMemoryOrder::Relaxed))
                velocity =
                    g.engine.processor.voice_pool.last_velocity[layer_index].Load(LoadMemoryOrder::Relaxed);

            DoCurveMap(
                g,
                layer.velocity_curve_map,
                window_r,
                velocity,
                "Configures how MIDI velocity maps to volume. X-axis: velocity, Y-axis: volume. Adjust the curve to customize this relationship.");
        }
    }
}

static void
HarmonySelectionMenu(GuiState& g, LayerProcessor& layer, Box parent, HarmonyIntervalsBitset intervals) {
    auto const label = HarmonyIntervalsLabel(intervals, g.scratch_arena);
    auto const intervals_btn =
        MenuOpenButton(g.builder,
                       parent,
                       {
                           .text = label,
                           .tooltip = "Select which harmony intervals grains can spawn at"_s,
                           .width = 70,
                           .style_system = GuiStyleSystem::MidPanel,
                       });

    auto const popup_id = (imgui::Id)(SourceLocationHash() ^ (u64)layer.index);
    if (intervals_btn.button_fired) g.imgui.OpenPopupMenu(popup_id, intervals_btn.imgui_id);

    if (g.imgui.IsPopupMenuOpen(popup_id) && g.builder.IsInputAndRenderPass()) {
        auto const popup_r = ({
            auto const popup_size = WwToPixels(f32x2 {460, 450});
            Rect r {.size = popup_size};
            if (auto const btn_r = BoxRect(g.builder, intervals_btn)) {
                auto const avoid_r = g.imgui.ViewportRectToWindowRect(*btn_r);
                r.pos = imgui::BestPopupPos(Rect {.pos = avoid_r.pos, .size = popup_size},
                                            avoid_r,
                                            GuiIo().in.window_size.ToFloat2(),
                                            imgui::PopupJustification::LeftOrRight);
            }
            r;
        });

        DoBoxViewport(
            g.builder,
            {
                .run =
                    [&g, &layer](GuiBuilder&) {
                        auto const popup_root = DoBox(g.builder,
                                                      {
                                                          .layout {
                                                              .size = layout::k_fill_parent,
                                                              .contents_direction = layout::Direction::Row,
                                                              .contents_align = layout::Alignment::Start,
                                                          },
                                                      });

                        // Left column: presets
                        {
                            auto const presets_col =
                                DoBox(g.builder,
                                      {
                                          .parent = popup_root,
                                          .layout {
                                              .size = {layout::k_hug_contents, layout::k_fill_parent},
                                              .contents_direction = layout::Direction::Column,
                                              .contents_align = layout::Alignment::Start,
                                          },
                                      });

                            DoBox(g.builder,
                                  {
                                      .parent = presets_col,
                                      .text = "PRESETS"_s,
                                      .font = FontType::Heading3,
                                      .text_colours = Col {.c = Col::Subtext0},
                                      .text_justification = TextJustification::CentredLeft,
                                      .layout {
                                          .size = {layout::k_fill_parent, 20},
                                          .margins = {.l = 8, .t = 2, .b = 2},
                                      },
                                  });

                            auto const current_intervals = layer.harmony_intervals.GetBlockwise();
                            for (auto const i : Range(k_harmony_presets.size)) {
                                auto const& preset = k_harmony_presets[i];
                                if (preset.divider_before)
                                    DoBox(g.builder,
                                          {
                                              .parent = presets_col,
                                              .id_extra = SourceLocationHash() + i,
                                              .background_fill_colours = Col {.c = Col::Surface2},
                                              .layout {
                                                  .size = {layout::k_fill_parent, 1},
                                                  .margins = {.tb = 2},
                                              },
                                          });
                                bool const is_selected = current_intervals == preset.intervals;
                                auto const item = MenuItem(g.builder,
                                                           presets_col,
                                                           {
                                                               .text = preset.name,
                                                               .tooltip = preset.tooltip,
                                                               .is_selected = is_selected,
                                                               .close_on_click = false,
                                                           },
                                                           SourceLocationHash() + i);
                                if (item.button_fired)
                                    layer.harmony_intervals.AssignBlockwise(preset.intervals);
                            }
                        }

                        // Divider
                        DoBox(g.builder,
                              {
                                  .parent = popup_root,
                                  .background_fill_colours = Col {.c = Col::Surface2},
                                  .layout {
                                      .size = {1, layout::k_fill_parent},
                                  },
                              });

                        // Right column: interval grid (manually laid out)
                        {
                            constexpr int k_grid_cols = 12;
                            constexpr int k_num_rows = 9; // 4 neg + root + 4 pos
                            constexpr f32 k_heading_h = 18;
                            constexpr f32 k_cell_gap = 1;
                            constexpr f32 k_pad = 4;

                            auto const grid_box = DoBox(g.builder,
                                                        {
                                                            .parent = popup_root,
                                                            .layout {
                                                                .size = layout::k_fill_parent,
                                                            },
                                                        });

                            if (auto const vp_r = BoxRect(g.builder, grid_box)) {
                                auto const r = g.imgui.ViewportRectToWindowRect(*vp_r);
                                auto const cell_w =
                                    (r.w - k_pad * 2 - k_cell_gap * (k_grid_cols - 1)) / (f32)k_grid_cols;
                                auto const cell_h =
                                    (r.h - k_pad * 2 - k_heading_h - k_cell_gap * (k_num_rows - 1)) /
                                    (f32)k_num_rows;
                                auto const grid_top = r.y + k_pad + k_heading_h;

                                // Heading
                                {
                                    Rect heading_r {
                                        .pos = {r.x + k_pad + 4, r.y + k_pad},
                                        .size = {r.w - (k_pad * 2), k_heading_h},
                                    };
                                    g.fonts.Push(g.fonts.atlas[ToInt(FontType::Heading3)]);
                                    g.imgui.draw_list->AddTextInRect(
                                        heading_r,
                                        ToU32(Col {.c = Col::Subtext0}),
                                        "INTERVALS",
                                        {.justification = TextJustification::CentredLeft});
                                    g.fonts.Pop();
                                }

                                g.imgui.PushId("interval_grid"_s);
                                DEFER { g.imgui.PopId(); };

                                auto const draw_cell = [&](int grid_row, int grid_col, int semitones) {
                                    auto const cx = r.x + k_pad + ((f32)grid_col * (cell_w + k_cell_gap));
                                    auto const cy = grid_top + ((f32)grid_row * (cell_h + k_cell_gap));
                                    Rect cell_r {.pos = {cx, cy}, .size = {cell_w, cell_h}};

                                    auto const bit = HarmonyIntervalBitIndex(semitones);
                                    bool const is_selected = layer.harmony_intervals.Get(bit);
                                    auto const id = g.imgui.MakeId((uintptr)bit);

                                    if (g.imgui.ButtonBehaviour(cell_r, id, imgui::ButtonConfig {}))
                                        layer.harmony_intervals.Flip(bit);

                                    bool const hot_or_active = g.imgui.IsHotOrActive(id);
                                    auto const rounding = WwToPixels(2.0f);

                                    u32 bg_col;
                                    if (is_selected && hot_or_active)
                                        bg_col = ToU32(Col {.c = Col::Surface0});
                                    else if (is_selected)
                                        bg_col = ToU32(Col {.c = Col::Surface1});
                                    else if (hot_or_active)
                                        bg_col = ToU32(Col {.c = Col::Surface2});
                                    else
                                        bg_col = 0;

                                    if (bg_col) g.imgui.draw_list->AddRectFilled(cell_r, bg_col, rounding);

                                    auto const text_col = ToU32(Col {.c = Col::Text});
                                    auto const label = fmt::Format(g.scratch_arena, "{}", semitones);
                                    g.fonts.Push(g.fonts.atlas[ToInt(FontType::Heading3)]);
                                    g.imgui.draw_list->AddTextInRect(
                                        cell_r,
                                        text_col,
                                        label,
                                        {.justification = TextJustification::Centred});
                                    g.fonts.Pop();

                                    auto const tooltip_name = HarmonyIntervalName(semitones, g.scratch_arena);
                                    Tooltip(g, id, cell_r, tooltip_name, {});
                                };

                                // Positive rows: +48 to +1 (top-left = 48, descending)
                                for (int row_idx = 0; row_idx < 4; ++row_idx) {
                                    for (int col = 0; col < k_grid_cols; ++col) {
                                        int semitones = ((4 - row_idx) * k_grid_cols) - col;
                                        draw_cell(row_idx, col, semitones);
                                    }
                                }

                                // Root row (row index 4)
                                {
                                    auto const root_y = grid_top + (4.0f * (cell_h + k_cell_gap));
                                    Rect root_r {
                                        .pos = {r.x + k_pad, root_y},
                                        .size = {r.w - (k_pad * 2), cell_h},
                                    };
                                    g.fonts.Push(g.fonts.atlas[ToInt(FontType::Heading3)]);
                                    g.imgui.draw_list->AddTextInRect(
                                        root_r,
                                        ToU32(Col {.c = Col::Subtext0}),
                                        "0",
                                        {.justification = TextJustification::Centred});
                                    g.fonts.Pop();
                                }

                                // Negative rows: -1 to -48 (below root, closest first)
                                for (int row_idx = 0; row_idx < 4; ++row_idx) {
                                    int grid_row = 5 + row_idx;
                                    for (int col = 0; col < k_grid_cols; ++col) {
                                        int semitones = -((row_idx * k_grid_cols) + col + 1);
                                        draw_cell(grid_row, col, semitones);
                                    }
                                }
                            }
                        }
                    },
                .bounds = popup_r,
                .imgui_id = popup_id,
                .viewport_config = ({
                    auto cfg = k_default_popup_menu_viewport;
                    cfg.positioning = imgui::ViewportPositioning::WindowAbsolute;
                    cfg.auto_size = false;
                    cfg.scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never;
                    cfg;
                }),
            });
    }
}

static void DoPlaybackPage(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_gap = k_page_row_gap_y,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    auto const experimental_params = prefs::GetBool(g.prefs, ExperimentalParamsPreferenceDescriptor());

    // Engine type menu
    if (experimental_params) {
        auto const param = params.DescribedValue(layer_index, LayerParamIndex::PlayMode);

        DoMenuParameter(g, page, param, {.width = layout::k_fill_parent, .label = false});
    }

    auto const play_mode =
        experimental_params ? params.IntValue<param_values::PlayMode>(layer_index, LayerParamIndex::PlayMode)
                            : param_values::PlayMode::Standard;

    // Waveform display + info strip
    {
        auto const waveform_group = DoBox(g.builder,
                                          {
                                              .parent = page,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                  .contents_direction = layout::Direction::Column,
                                              },
                                          });

        if (auto const r = BoxRect(g.builder,
                                   DoBox(g.builder,
                                         {
                                             .parent = waveform_group,
                                             .layout {
                                                 .size = {layout::k_fill_parent, 78},
                                             },
                                         })))
            DoWaveformElement(g, layer, *r, {.play_mode = play_mode});

        DoInstrumentInfoStrip(g, layer_index, waveform_group);
    }

    // Reverse toggle
    {
        auto const param = params.DescribedValue(layer_index, LayerParamIndex::Reverse);
        bool const is_waveform_synth = layer.instrument_id.tag == InstrumentType::WaveformSynth;

        DoButtonParameter(g,
                          page,
                          param,
                          {
                              .width = layout::k_fill_parent,
                              .greyed_out = is_waveform_synth,
                          });
    }

    if (play_mode != param_values::PlayMode::GranularFixed) DoLoopModeSelector(g, page, layer);

    if (IsGranular(play_mode)) {
        auto const granular_container = DoBox(g.builder,
                                              {
                                                  .parent = page,
                                                  .layout {
                                                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                      .margins = {.t = 2},
                                                      .contents_gap = 12,
                                                      .contents_direction = layout::Direction::Column,
                                                  },
                                              });

        auto const do_heading = [&g](Box parent, String text, u64 loc_hash = SourceLocationHash()) {
            DoBox(g.builder,
                  {
                      .parent = parent,
                      .id_extra = loc_hash,
                      .text = text,
                      .size_from_text = true,
                      .font = FontType::Heading3,
                      .text_colours = LiveColStruct(UiColMap::MidTextDimmed),
                      .text_justification = TextJustification::CentredLeft,
                  });
        };

        auto const do_knob = [&](Box parent, LayerParamIndex param) {
            DoKnobParameter(g,
                            parent,
                            params.DescribedValue(layer_index, param),
                            {
                                .width = 28,
                                .style_system = GuiStyleSystem::MidPanel,
                                .bidirectional = param == LayerParamIndex::GranularSpeed,
                            });
        };

        auto const do_row = [&g](Box parent, u64 loc_hash = SourceLocationHash()) {
            return DoBox(g.builder,
                         {
                             .parent = parent,
                             .id_extra = loc_hash,
                             .layout {
                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                 .contents_gap = 42,
                                 .contents_direction = layout::Direction::Row,
                                 .contents_align = layout::Alignment::Start,
                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                             },
                         });
        };

        auto const do_section = [&g](Box parent, u64 loc_hash = SourceLocationHash()) {
            return DoBox(g.builder,
                         {
                             .parent = parent,
                             .id_extra = loc_hash,
                             .layout {
                                 .size = layout::k_hug_contents,
                                 .contents_gap = 4,
                                 .contents_direction = layout::Direction::Column,
                                 .contents_align = layout::Alignment::Start,
                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                             },
                         });
        };

        auto const do_knob_container = [&g](Box parent, u64 loc_hash = SourceLocationHash()) {
            return DoBox(g.builder,
                         {
                             .parent = parent,
                             .id_extra = loc_hash,
                             .layout {
                                 .size = layout::k_hug_contents,
                                 .margins = {.l = 2},
                                 .contents_gap = 30,
                                 .contents_direction = layout::Direction::Row,
                                 .contents_align = layout::Alignment::Start,
                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                             },
                         });
        };

        // Row 1: PLAYHEAD + GRAINS
        {
            auto const row = do_row(granular_container);

            {
                auto const section = do_section(row);
                do_heading(section, "PLAYHEAD"_s);
                auto const knob_box = do_knob_container(section);
                do_knob(knob_box,
                        (play_mode == param_values::PlayMode::GranularPlayback)
                            ? LayerParamIndex::GranularSpeed
                            : LayerParamIndex::GranularPosition);
                do_knob(knob_box, LayerParamIndex::GranularSpread);
            }

            {
                auto const section = do_section(row);
                do_heading(section, "GRAINS"_s);
                auto const knob_box = do_knob_container(section);
                do_knob(knob_box, LayerParamIndex::GranularDensity);
                do_knob(knob_box, LayerParamIndex::GranularLength);
                do_knob(knob_box, LayerParamIndex::GranularSmoothing);
            }
        }

        // Row 2: RANDOM
        {
            auto const row = do_row(granular_container);

            {
                auto const section = do_section(row);

                do_heading(section, "RANDOM"_s);

                // Inner box to allow the harmony menu to sit closer to the knob box.
                auto const row_inner = DoBox(g.builder,
                                             {
                                                 .parent = section,
                                                 .layout {
                                                     .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                     .contents_gap = 20, // Gap before harmony menu.
                                                     .contents_direction = layout::Direction::Row,
                                                 },
                                             });

                auto const knob_box = do_knob_container(row_inner);
                do_knob(knob_box, LayerParamIndex::GranularRandomPan);
                do_knob(knob_box, LayerParamIndex::GranularRandomDetune);
                do_knob(knob_box, LayerParamIndex::GranularRandomDirection);

                // Harmony: knob + intervals button
                {
                    auto const intervals = layer.harmony_intervals.GetBlockwise();
                    auto non_unison = intervals;
                    non_unison.Clear(k_harmony_interval_centre_bit);
                    bool const has_intervals = non_unison.AnyValuesSet();

                    DoKnobParameter(g,
                                    knob_box,
                                    params.DescribedValue(layer_index, LayerParamIndex::GranularHarmony),
                                    {
                                        .width = 28,
                                        .style_system = GuiStyleSystem::MidPanel,
                                        .greyed_out = !has_intervals,
                                    });

                    HarmonySelectionMenu(g, layer, row_inner, intervals);
                }
            }
        }
    }
}

static void DoEnvelopeSection(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;

    auto const section = DoBox(g.builder,
                               {
                                   .parent = parent,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                   },
                               });

    DoButtonParameter(g,
                      section,
                      params.DescribedValue(layer_index, LayerParamIndex::VolEnvOn),
                      {
                          .width = layout::k_fill_parent,
                      });

    {
        auto const envelope_box = DoBox(g.builder,
                                        {
                                            .parent = section,
                                            .layout {
                                                .size = {layout::k_fill_parent, 80},
                                                .margins {
                                                    .tb = 10,
                                                },
                                            },
                                        });

        bool const env_on = params.BoolValue(layer_index, LayerParamIndex::VolEnvOn) ||
                            layer.instrument.tag == InstrumentType::WaveformSynth;
        if (auto const r = BoxRect(g.builder, envelope_box))
            DoEnvelopeGui(g,
                          layer,
                          *r,
                          !env_on,
                          {LayerParamIndex::VolumeAttack,
                           LayerParamIndex::VolumeDecay,
                           LayerParamIndex::VolumeSustain,
                           LayerParamIndex::VolumeRelease},
                          GuiEnvelopeType::Volume);
    }
}

static void DoMainPage(GuiState& g, u8 layer_index, Box parent) {
    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    DoEnvelopeSection(g, layer_index, page);
    DoFilterPage(g, layer_index, page);
}

static void DoArpPage(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    auto& layer_proc = g.engine.processor.layer_processors[layer_index];
    auto& arp_state = layer_proc.arp_state;

    auto const slices = ({
        Span<sample_lib::Region::Slice const> s {};
        if (auto sm = layer_proc.instrument.TryGetFromTag<InstrumentType::Sampler>()) {
            if (*sm) {
                if ((*sm)->instrument.category == sample_lib::SamplerCategory::Sliced)
                    s = (*sm)->instrument.regions[0].slices;
            }
        }
        s;
    });
    auto const snapshot = CreateArpGuiSnapshot(params, layer_index, arp_state, slices);
    auto const& edit = snapshot.edit;
    bool const is_fixed = snapshot.type == param_values::ArpMode::Fixed;
    bool const is_sliced = snapshot.activation == ArpGuiSnapshot::Activation::ForcedBySlicing;

    // Humanise/Rate/TriggerMode stay editable when arp is effectively on (including slice mode).
    bool const secondary_greyed = !snapshot.on;

    // Whether a control that's non-editable should be shown anyway. In slice mode we hide irrelevant
    // controls; in user-off mode we show everything greyed.
    auto const show_if_non_editable = [&](bool editable) { return editable || !is_sliced; };

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_gap = k_page_row_gap_y,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                },
                            });

    // Heading row: ArpMode menu (Off / Played / Fixed). Hidden for sliced instruments.
    if (snapshot.activation == ArpGuiSnapshot::Activation::UserDefined) {
        DoMenuParameter(g,
                        page,
                        params.DescribedValue(layer_index, LayerParamIndex::ArpMode),
                        {.width = layout::k_fill_parent, .label = false});
    }

    // Step sequencer
    {
        auto const seq_box = DoBox(g.builder,
                                   {
                                       .parent = page,
                                       .layout {.size = {layout::k_fill_parent, 120}},
                                   });

        if (auto const r = BoxRect(g.builder, seq_box)) {
            auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
            auto const playing = arp_state.current_step_for_gui.Load(LoadMemoryOrder::Relaxed);
            DoArpStepSequencer(g,
                               arp_state,
                               window_r,
                               snapshot,
                               playing,
                               g.layer_panel_states[layer_index].arp_step_sequencer_show_all);
        }
    }

    if (!is_fixed) arp_state.recording.Store(false, StoreMemoryOrder::Relaxed);

    // Controls below the step sequencer, laid out in granular-tab style:
    // sections with headings, controls in horizontal rows with labels below.

    constexpr f32 k_control_width = 68;

    auto const controls = DoBox(g.builder,
                                {
                                    .parent = page,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_gap = 12,
                                        .contents_direction = layout::Direction::Column,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                    },
                                });

    auto const do_heading = [&](Box parent, String text, u64 loc_hash = SourceLocationHash()) {
        DoBox(g.builder,
              {
                  .parent = parent,
                  .id_extra = loc_hash,
                  .text = text,
                  .size_from_text = true,
                  .font = FontType::Heading3,
                  .text_colours = LiveColStruct(UiColMap::MidTextDimmed),
                  .text_justification = TextJustification::CentredLeft,
              });
    };

    auto const do_section = [&](Box parent, u64 loc_hash = SourceLocationHash()) {
        return DoBox(g.builder,
                     {
                         .parent = parent,
                         .id_extra = loc_hash,
                         .layout {
                             .size = layout::k_hug_contents,
                             .contents_gap = 4,
                             .contents_direction = layout::Direction::Column,
                             .contents_align = layout::Alignment::Start,
                             .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                         },
                     });
    };

    auto const do_control_row = [&](Box parent, u64 loc_hash = SourceLocationHash()) {
        return DoBox(g.builder,
                     {
                         .parent = parent,
                         .id_extra = loc_hash,
                         .layout {
                             .size = layout::k_hug_contents,
                             .margins = {.l = 2},
                             .contents_gap = 6,
                             .contents_direction = layout::Direction::Row,
                             .contents_align = layout::Alignment::Start,
                             .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                         },
                     });
    };

    auto const do_label = [&](Box parent, String text, bool grey, u64 loc_hash = SourceLocationHash()) {
        DoBox(g.builder,
              {
                  .parent = parent,
                  .id_extra = loc_hash,
                  .text = text,
                  .text_colours = LiveColStruct(grey ? UiColMap::MidTextDimmed : UiColMap::MidText),
                  .text_justification = TextJustification::Centred,
                  .layout {.size = {k_control_width, k_font_body_size}},
              });
    };

    auto const do_cell = [&](Box parent, u64 loc_hash = SourceLocationHash()) {
        return DoBox(g.builder,
                     {
                         .parent = parent,
                         .id_extra = loc_hash,
                         .layout {
                             .size = layout::k_hug_contents,
                             .contents_gap = 2,
                             .contents_direction = layout::Direction::Column,
                             .contents_align = layout::Alignment::Middle,
                             .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                         },
                     });
    };

    auto const do_toggle_cell =
        [&](Box control_row, LayerParamIndex param_index, bool grey, u64 loc_hash = SourceLocationHash()) {
            auto const cell = do_cell(control_row, loc_hash);
            auto const param = params.DescribedValue(layer_index, param_index);
            bool const state = param.BoolValue();

            auto const btn = DoBox(g.builder,
                                   {
                                       .parent = cell,
                                       .layout {
                                           .size = {k_control_width, k_mid_button_height},
                                           .contents_align = layout::Alignment::Middle,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                       .tooltip = FunctionRef<String()> {[&]() -> String {
                                           return ParamTooltipText(param, g.builder.arena);
                                       }},
                                       .button_behaviour = imgui::ButtonConfig {},
                                   });

            DoToggleIcon(g.builder, btn, {.state = state, .greyed_out = grey});

            if (btn.button_fired)
                SetParameterValue(g.engine.processor, param.info.index, state ? 0.0f : 1.0f, {});

            AddParamContextMenuBehaviour(g, btn, param);
            do_label(cell, param.info.gui_label, grey, loc_hash + 1);
        };

    auto const do_menu_cell = [&](Box control_row,
                                  LayerParamIndex param_index,
                                  bool grey,
                                  bool allow_text_overflow = false,
                                  u64 loc_hash = SourceLocationHash()) {
        auto const cell = do_cell(control_row, loc_hash);
        auto const param = params.DescribedValue(layer_index, param_index);
        DoMenuParameter(g,
                        cell,
                        param,
                        {
                            .width = k_control_width,
                            .greyed_out = grey,
                            .label = false,
                            .allow_text_overflow = allow_text_overflow,
                        });
        do_label(cell, param.info.gui_label, grey, loc_hash);
    };

    auto const do_int_dragger_cell = [&](Box control_row,
                                         String label,
                                         int current,
                                         int min_val,
                                         int max_val,
                                         String display_string,
                                         bool grey,
                                         u64 loc_hash = SourceLocationHash()) -> Optional<int> {
        auto const cell = do_cell(control_row, loc_hash);

        auto const prev_next_row = DoMidPanelPrevNextRow(g.builder, cell, k_control_width);

        auto const dragger_box =
            DoBox(g.builder,
                  {
                      .parent = prev_next_row,
                      .text = display_string,
                      .text_colours = LiveColStruct(grey ? UiColMap::MidTextDimmed : UiColMap::MidText),
                      .text_justification = TextJustification::CentredLeft,
                      .text_overflow = TextOverflowType::AllowOverflow,
                      .layout {.size = {layout::k_fill_parent, k_mid_button_height}},
                  });

        Optional<int> new_val {};
        Optional<imgui::TextInputResult> text_input_result {};

        if (auto const viewport_r = BoxRect(g.builder, dragger_box)) {
            auto const window_r = g.builder.imgui.RegisterAndConvertRect(*viewport_r);
            auto val = (f32)current;
            auto const dragger_result = g.builder.imgui.DraggerBehaviour({
                .rect_in_window_coords = window_r,
                .id = dragger_box.imgui_id,
                .text = display_string,
                .min = (f32)min_val,
                .max = (f32)max_val,
                .value = val,
                .default_value = (f32)min_val,
                .text_input_button_cfg {
                    .mouse_button = MouseButton::Left,
                    .event = MouseButtonEvent::DoubleClick,
                },
                .text_input_cfg {
                    .chars_decimal = true,
                    .escape_unfocuses = true,
                    .select_all_when_opening = true,
                },
                .slider_cfg {
                    .sensitivity = 15,
                    .slower_with_shift = true,
                    .default_on_modifer = true,
                },
            });
            if (dragger_result.new_string_value) {
                if (auto const o = ParseInt(*dragger_result.new_string_value, ParseIntBase::Decimal))
                    new_val = Clamp((int)o.Value(), min_val, max_val);
            }
            if (dragger_result.value_changed) new_val = (int)val;
            text_input_result = dragger_result.text_input_result;
        }

        auto const arrows = DoMidPanelPrevNextButtons(g.builder, prev_next_row, {.greyed_out = grey});
        if (arrows.prev_fired || arrows.next_fired) {
            auto val = current + (arrows.prev_fired ? -1 : 1);
            new_val = Clamp(val, min_val, max_val);
        }

        if (text_input_result) {
            if (auto const rel_r = BoxRect(g.builder, dragger_box)) {
                auto const r = g.builder.imgui.ViewportRectToWindowRect(*rel_r);
                DrawParameterTextInput(g.builder.imgui, r, *text_input_result);
            }
        }

        do_label(cell, label, grey, loc_hash + 1);
        return new_val;
    };

    bool const auto_rate_on =
        edit.auto_rate_visible && params.BoolValue(layer_index, LayerParamIndex::ArpAutoRate);

    // SEQUENCE section
    {
        auto const section = do_section(controls);
        do_heading(section, "SEQUENCE"_s);
        auto const row = do_control_row(section);

        if (edit.auto_rate_visible) do_toggle_cell(row, LayerParamIndex::ArpAutoRate, secondary_greyed);

        do_menu_cell(row, LayerParamIndex::ArpRate, secondary_greyed || auto_rate_on, true);

        if (!is_sliced && show_if_non_editable(edit.length)) {
            auto const cell = do_cell(row);
            DoIntParameter(g,
                           cell,
                           params.DescribedValue(layer_index, LayerParamIndex::ArpLength),
                           {
                               .width = k_control_width,
                               .greyed_out = !edit.length,
                               .label = false,
                           });
            do_label(cell, "Length"_s, !edit.length);
        }

        if (!is_fixed && show_if_non_editable(edit.note_order))
            do_menu_cell(row, LayerParamIndex::ArpNoteOrder, !edit.note_order);

        {
            auto const cell = do_cell(row);
            DoPercentDraggerParameter(g,
                                      cell,
                                      params.DescribedValue(layer_index, LayerParamIndex::ArpHumanise),
                                      {
                                          .width = k_control_width,
                                          .greyed_out = secondary_greyed,
                                          .label = false,
                                      });
            do_label(cell, "Humanise"_s, secondary_greyed);
        }
    }

    // CONFIG section
    {
        auto const section = do_section(controls);
        do_heading(section, "CONFIG"_s);
        auto const row = do_control_row(section);

        do_menu_cell(row, LayerParamIndex::ArpTriggerMode, secondary_greyed);
        do_menu_cell(row, LayerParamIndex::ArpOctavePolyrate, secondary_greyed);
        do_toggle_cell(row, LayerParamIndex::ArpOneShot, secondary_greyed);

        if (is_fixed) {
            auto const is_recording = !secondary_greyed && arp_state.recording.Load(LoadMemoryOrder::Relaxed);

            auto const cell = do_cell(row);
            auto const rec_btn = DoBox(g.builder,
                                       {
                                           .parent = cell,
                                           .layout {
                                               .size = {k_control_width, k_mid_button_height},
                                               .contents_align = layout::Alignment::Middle,
                                               .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                           },
                                           .tooltip = "Record a fixed note sequence by playing keys"_s,
                                           .button_behaviour = imgui::ButtonConfig {},
                                       });

            if (auto const r = BoxRect(g.builder, rec_btn)) {
                auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
                auto const col = is_recording ? ToU32(Col {.c = Col::Coral, .alpha = 255})
                                              : ToU32(Col {.c = Col::White, .alpha = 60});
                g.imgui.draw_list->AddCircleFilled(window_r.Centre(), WwToPixels(5.5f), col);
            }

            do_label(cell, is_recording ? "Recording..."_s : "Record"_s, secondary_greyed && !is_recording);

            if (!secondary_greyed && rec_btn.button_fired) {
                if (is_recording) {
                    arp_state.recording.Store(false, StoreMemoryOrder::Relaxed);
                    arp_state.current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
                } else {
                    arp_state.current_step_for_gui.Store(0, StoreMemoryOrder::Relaxed);
                    arp_state.recording.Store(true, StoreMemoryOrder::Relaxed);
                }
            }
        }
    }

    // SLICING section (slice mode only)
    if (is_sliced) {
        auto const section = do_section(controls);
        do_heading(section, "SLICING"_s);
        auto const row = do_control_row(section);

        u32 num_slices = 0;
        if (auto s = layer_proc.instrument.TryGetFromTag<InstrumentType::Sampler>()) {
            if (*s) {
                auto const& regions = (*s)->instrument.regions;
                if (regions.size == 1) num_slices = (u32)regions[0].slices.size;
            }
        }
        if (num_slices > 1) {
            auto const offset_current = (int)arp_state.slice_start_offset.Load(LoadMemoryOrder::Relaxed);
            auto const offset_new = do_int_dragger_cell(row,
                                                        "Offset"_s,
                                                        offset_current,
                                                        0,
                                                        (int)Min(num_slices - 1, (u32)255),
                                                        fmt::Format(g.scratch_arena, "{}", offset_current),
                                                        secondary_greyed);
            if (offset_new && !secondary_greyed)
                arp_state.slice_start_offset.Store((u8)*offset_new, StoreMemoryOrder::Relaxed);

            auto const length_current = (int)arp_state.slice_loop_length.Load(LoadMemoryOrder::Relaxed);
            auto const length_new = do_int_dragger_cell(
                row,
                "Length"_s,
                length_current,
                0,
                (int)Min(num_slices, (u32)255),
                length_current ? (String)fmt::Format(g.scratch_arena, "{}", length_current) : "All"_s,
                secondary_greyed);
            if (length_new && !secondary_greyed)
                arp_state.slice_loop_length.Store((u8)*length_new, StoreMemoryOrder::Relaxed);
        }
    }
}

void DoLayerPanel(GuiState& g, GuiFrameContext const& frame_context, u8 layer_index, Box parent) {
    auto const root = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Top controls: houses the instrument selector and will later contain volume, tune, pan, etc.
    auto const top_controls = DoBox(g.builder,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_padding {
                                                .lr = 6.3f,
                                                .t = 6.3f,
                                            },
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Start,
                                        },
                                    });

    DoInstSelector(g, frame_context, layer_index, top_controls);

    if (g.engine.Layer(layer_index).instrument.tag == InstrumentType::None) return;

    DoWhitespace(g.builder, top_controls, 12);

    DoMixerRow(g, layer_index, top_controls);

    DoWhitespace(g.builder, top_controls, 12);

    DoPageTabs(g, layer_index, root);

    auto const page_container = DoBox(g.builder,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = layout::k_fill_parent,
                                              .contents_padding = {.lr = 15, .tb = 12},
                                          },
                                      });

    // Page content
    switch (g.layer_panel_states[layer_index].selected_page) {
        case LayerPageType::Main: DoMainPage(g, layer_index, page_container); break;
        case LayerPageType::Playback: DoPlaybackPage(g, layer_index, page_container); break;
        case LayerPageType::Lfo: DoLfoPage(g, layer_index, page_container); break;
        case LayerPageType::Eq: DoEqPage(g, layer_index, page_container); break;
        case LayerPageType::Arp: DoArpPage(g, layer_index, page_container); break;
        case LayerPageType::Config: DoConfigPage(g, layer_index, page_container); break;
        case LayerPageType::Count: PanicIfReached();
    }
}
