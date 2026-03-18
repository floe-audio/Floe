// Copyright 2024-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_layer_subtabbed.hpp"

#include <IconsFontAwesome6.h>

#include "engine/engine.hpp"
#include "engine/engine_prefs.hpp"
#include "engine/loop_modes.hpp"
#include "gui/controls/gui_curve_map.hpp"
#include "gui/controls/gui_envelope.hpp"
#include "gui/controls/gui_waveform.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_macros.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/granular.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

constexpr f32 k_page_row_gap_y = 7;
constexpr f32 k_page_row_gap_x = 7;
constexpr f32 k_knob_width = 36;

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

static void DoInstSelectorRightClickMenu(GuiState& g, Box selector_button, u8 layer_index) {
    auto const& layer_obj = g.engine.Layer(layer_index);
    auto const right_click_id = SourceLocationHash() + layer_index;

    if (auto const r = BoxRect(g.builder, selector_button)) {
        auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
        if (g.imgui.ButtonBehaviour(window_r,
                                    selector_button.imgui_id,
                                    {
                                        .mouse_button = MouseButton::Right,
                                        .event = MouseButtonEvent::Up,
                                    })) {
            g.imgui.OpenPopupMenu(right_click_id, selector_button.imgui_id);
        }

        if (g.imgui.IsPopupMenuOpen(right_click_id))
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
                            if (MenuItem(g.builder,
                                         root,
                                         {
                                             .text = "Unload instrument"_s,
                                             .mode = layer_obj.instrument_id.tag == InstrumentType::None
                                                         ? MenuItemOptions::Mode::Disabled
                                                         : MenuItemOptions::Mode::Active,
                                             .no_icon_gap = true,
                                         })
                                    .button_fired) {
                                LoadInstrument(g.engine, layer_index, InstrumentType::None);
                            }
                        },
                    .bounds = window_r,
                    .imgui_id = right_click_id,
                    .viewport_config = k_default_popup_menu_viewport,
                });
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
            if (num_regions == 0) {
                dyn::Append(segments, "Empty instrument"_s);
            } else if (num_regions == 1) {
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
                                             .size = {20, k_vol_slider_height},
                                         },
                                     });
        if (auto const r = BoxRect(g.builder, meter_box))
            DrawPeakMeter(g.imgui,
                          g.imgui.ViewportRectToWindowRect(*r),
                          layer_processor.peak_meter,
                          {
                              .flash_when_clipping = false,
                              .show_db_markers = true,
                              .gap = 1,
                          });

        // Volume slider
        DoKnobParameter(g,
                        vol_col,
                        params.DescribedValue(layer_index, LayerParamIndex::Volume),
                        {
                            .width = k_vol_slider_width,
                            .knob_height_fraction = k_vol_slider_height / k_vol_slider_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .label = false,
                            .vertical_slider = true,
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

    for (auto const i : Range(ToInt(LayerPageType::Count))) {
        auto const page_type = (LayerPageType)i;
        bool const is_selected = page_type == layer_state.selected_page;
        bool const tab_has_active_content = ({
            bool result = false;
            switch (page_type) {
                case LayerPageType::Filter:
                    result = params.BoolValue(layer_index, LayerParamIndex::FilterOn);
                    break;
                case LayerPageType::Lfo:
                    result = params.BoolValue(layer_index, LayerParamIndex::LfoOn);
                    break;
                case LayerPageType::Eq: result = params.BoolValue(layer_index, LayerParamIndex::EqOn); break;
                case LayerPageType::Envelope:
                case LayerPageType::Engine:
                case LayerPageType::Play:
                case LayerPageType::Count: break;
            }
            result;
        });

        auto const name = [&]() -> String {
            switch (page_type) {
                case LayerPageType::Envelope: return "ENVELOPE"_s;
                case LayerPageType::Engine: return "ENGINE"_s;
                case LayerPageType::Eq: return "EQ"_s;
                case LayerPageType::Play: return "PLAY"_s;
                case LayerPageType::Lfo: return "LFO"_s;
                case LayerPageType::Filter: return "FILTER"_s;
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
                    case ParameterModule::Loop:
                    case ParameterModule::VolEnv: new_page = LayerPageType::Envelope; break;
                    case ParameterModule::Lfo: new_page = LayerPageType::Lfo; break;
                    case ParameterModule::Filter: new_page = LayerPageType::Filter; break;
                    case ParameterModule::Playback: new_page = LayerPageType::Play; break;
                    case ParameterModule::Eq: new_page = LayerPageType::Eq; break;
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

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Heading row: FilterOn toggle + FilterType menu
    {
        auto const heading_row = DoBox(g.builder,
                                       {
                                           .parent = page,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .margins = {.b = 15},
                                               .contents_gap = 12,
                                               .contents_direction = layout::Direction::Row,
                                           },
                                       });

        DoButtonParameter(g,
                          heading_row,
                          params.DescribedValue(layer_index, LayerParamIndex::FilterOn),
                          {.width = layout::k_hug_contents});

        DoMenuParameter(g,
                        heading_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterType),
                        {.width = layout::k_fill_parent, .greyed_out = greyed_out, .label = false});
    }

    // Knobs row: Cutoff, Resonance, EnvAmount
    {
        auto const knobs_row = DoBox(g.builder,
                                     {
                                         .parent = page,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .margins = {.b = 20},
                                             .contents_gap = 22.6f,
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Middle,
                                         },
                                     });

        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterCutoff),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterResonance),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterEnvAmount),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                            .bidirectional = true,
                        });
    }

    // Filter envelope
    {
        auto const envelope_box = DoBox(g.builder,
                                        {
                                            .parent = page,
                                            .layout {
                                                .size = {layout::k_fill_parent, 75},
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

    // Container for all EQ band rows with consistent gap
    auto const bands_container = DoBox(g.builder,
                                       {
                                           .parent = page,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_gap = 26,
                                               .contents_direction = layout::Direction::Column,
                                               .contents_align = layout::Alignment::Start,
                                           },
                                       });

    // EQ band helper - adds menu row and knobs row to bands_container
    auto const do_eq_band = [&](LayerParamIndex type_param,
                                LayerParamIndex freq_param,
                                LayerParamIndex reso_param,
                                LayerParamIndex gain_param,
                                u8 band_number) {
        auto const band = DoBox(g.builder,
                                {
                                    .parent = bands_container,
                                    .id_extra = band_number,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_gap = 10,
                                        .contents_direction = layout::Direction::Column,

                                    },
                                });

        // Type menu
        auto const menu_row = DoBox(g.builder,
                                    {
                                        .parent = band,
                                        .layout {
                                            .size = layout::k_hug_contents,
                                            .contents_gap = 6,
                                            .contents_direction = layout::Direction::Row,
                                            .contents_align = layout::Alignment::Middle,
                                        },
                                    });

        DoBox(g.builder,
              {
                  .parent = menu_row,
                  .text = fmt::Format(g.scratch_arena, "Band {}", band_number),
                  .text_colours = LiveColStruct(greyed_out ? UiColMap::MidTextDimmed : UiColMap::MidText),
                  .text_justification = TextJustification::CentredRight,
                  .layout {
                      .size = {50, k_font_body_size},
                  },
              });
        DoMenuParameter(g,
                        menu_row,
                        params.DescribedValue(layer_index, type_param),
                        {
                            .width = 130,
                            .greyed_out = greyed_out,
                            .label = false,
                        });

        // Knobs row
        auto const knobs_row = DoBox(g.builder,
                                     {
                                         .parent = band,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = 24,
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Middle,
                                         },
                                     });

        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, freq_param),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, reso_param),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, gain_param),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                            .bidirectional = true,
                        });
    };

    // Band 1
    do_eq_band(LayerParamIndex::EqType1,
               LayerParamIndex::EqFreq1,
               LayerParamIndex::EqResonance1,
               LayerParamIndex::EqGain1,
               1);

    // Band 2
    do_eq_band(LayerParamIndex::EqType2,
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

    DoWhitespace(g.builder, page, 2);

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

static void DoPlayPage(GuiState& g, u8 layer_index, Box parent) {
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

static void DoEnginePage(GuiState& g, u8 layer_index, Box parent) {
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

    DoWhitespace(g.builder, page, 2);

    if (IsGranular(play_mode)) {
        auto const sections_row = DoBox(g.builder,
                                        {
                                            .parent = page,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_gap = 16,
                                                .contents_direction = layout::Direction::Row,
                                                .contents_align = layout::Alignment::Start,
                                                .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                            },
                                        });

        auto const do_section = [&](u64 loc_hash = SourceLocationHash()) {
            return DoBox(g.builder,
                         {
                             .parent = sections_row,
                             .id_extra = loc_hash,
                             .layout {
                                 .size = layout::k_hug_contents,
                                 .contents_gap = 8,
                                 .contents_direction = layout::Direction::Column,
                                 .contents_align = layout::Alignment::Start,
                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                             },
                         });
        };

        auto const do_heading = [&g](Box parent, String text) {
            DoBox(g.builder,
                  {
                      .parent = parent,
                      .text = text,
                      .size_from_text = true,
                      .font = FontType::Heading3,
                      .text_colours = LiveColStruct(UiColMap::MidTextDimmed),
                      .text_justification = TextJustification::CentredLeft,
                  });
        };

        auto const do_knob_container = [&g](Box parent, f32 width, u64 loc_hash = SourceLocationHash()) {
            return DoBox(g.builder,
                         {
                             .parent = parent,
                             .id_extra = loc_hash,
                             .layout {
                                 .size = {width, layout::k_hug_contents},
                                 .contents_padding = {.l = 4},
                                 .contents_gap = {24, 8},
                                 .contents_direction = layout::Direction::Row,
                                 .contents_multiline = true,
                                 .contents_align = layout::Alignment::Start,
                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                             },
                         });
        };

        auto const do_knob = [&](Box parent, LayerParamIndex param) {
            DoKnobParameter(g,
                            parent,
                            params.DescribedValue(layer_index, param),
                            {
                                .width = 28,
                                .style_system = GuiStyleSystem::MidPanel,
                            });
        };

        {
            auto const section = do_section();
            do_heading(section, "PLAYHEAD"_s);
            auto const knob_box = do_knob_container(section, 38);
            do_knob(knob_box,
                    (play_mode == param_values::PlayMode::GranularPlayback)
                        ? LayerParamIndex::GranularSpeed
                        : LayerParamIndex::GranularPosition);
            do_knob(knob_box, LayerParamIndex::GranularSpread);
        }

        {
            auto const section = do_section();
            do_heading(section, "GRAINS"_s);
            auto const knob_box = do_knob_container(section, 110);
            do_knob(knob_box, LayerParamIndex::GranularDensity);
            do_knob(knob_box, LayerParamIndex::GranularLength);
            do_knob(knob_box, LayerParamIndex::GranularSmoothing);
        }

        {
            auto const section = do_section();
            do_heading(section, "RANDOM"_s);
            auto const knob_box = do_knob_container(section, 106);
            do_knob(knob_box, LayerParamIndex::GranularRandomPan);
            do_knob(knob_box, LayerParamIndex::GranularRandomDetune);
        }
    }
}

static void DoEnvelopePage(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;

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

    // Volume envelope on button
    DoButtonParameter(g,
                      page,
                      params.DescribedValue(layer_index, LayerParamIndex::VolEnvOn),
                      {
                          .width = layout::k_fill_parent,
                      });

    // Envelope display
    {
        auto const envelope_box = DoBox(g.builder,
                                        {
                                            .parent = page,
                                            .layout {
                                                .size = {layout::k_fill_parent, 100},
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
        case LayerPageType::Envelope: DoEnvelopePage(g, layer_index, page_container); break;
        case LayerPageType::Filter: DoFilterPage(g, layer_index, page_container); break;
        case LayerPageType::Engine: DoEnginePage(g, layer_index, page_container); break;
        case LayerPageType::Lfo: DoLfoPage(g, layer_index, page_container); break;
        case LayerPageType::Eq: DoEqPage(g, layer_index, page_container); break;
        case LayerPageType::Play: DoPlayPage(g, layer_index, page_container); break;
        case LayerPageType::Count: PanicIfReached();
    }
}
