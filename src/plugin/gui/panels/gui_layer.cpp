// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_layer.hpp"

#include <IconsFontAwesome6.h>

#include "engine/engine.hpp"
#include "engine/loop_modes.hpp"
#include "gui/controls/gui_curve_map.hpp"
#include "gui/controls/gui_envelope.hpp"
#include "gui/controls/gui_waveform.hpp"
#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_inst_browser.hpp"
#include "gui/panels/gui_macros.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

static void DoInstSelectorRightClickMenu(GuiState& g, Box selector_button, u8 layer_index) {
    auto& imgui = g.imgui;
    auto layer_obj = &g.engine.Layer(layer_index);
    auto const right_click_id = imgui.MakeId("inst-selector-popup");

    if (auto const r = BoxRect(g.builder, selector_button)) {
        auto const window_r = imgui.ViewportRectToWindowRect(*r);
        if (imgui.ButtonBehaviour(window_r,
                                  selector_button.imgui_id,
                                  {
                                      .mouse_button = MouseButton::Right,
                                      .event = MouseButtonEvent::Up,
                                  })) {
            imgui.OpenPopupMenu(right_click_id, selector_button.imgui_id);
        }

        if (imgui.IsPopupMenuOpen(right_click_id))
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
                                             .mode = layer_obj->instrument_id.tag == InstrumentType::None
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

static void DoInstSelector(GuiState& g, GuiFrameContext const& frame_context, u8 layer_index, Box root) {
    auto layer_obj = &g.engine.Layer(layer_index);
    auto const inst_name = layer_obj->InstName();

    // Selector row container
    auto const selector_box =
        DoBox(g.builder,
              {
                  .parent = root,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_padding {.r = LiveWw(UiSizeId::LayerInstSelectorPadR)},
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Start,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                  },
              });

    // Custom background drawing
    if (auto const r = BoxRect(g.builder, selector_box)) {
        bool should_highlight = false;
        if (layer_obj->UsesTimbreLayering() &&
            (g.timbre_slider_is_held ||
             CcControllerMovedParamRecently(g.engine.processor, ParamIndex::MasterTimbre))) {
            should_highlight = true;
        }

        auto const col = should_highlight ? LiveCol(UiColMap::InstSelectorMenuBackHighlight)
                                          : LiveCol(UiColMap::MidDarkSurface);
        auto const rounding = LivePx(UiSizeId::CornerRounding);
        auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
        g.imgui.draw_list->AddRectFilled(window_r, col, rounding);

        // Loading progress bar
        if (auto percent =
                g.engine.sample_lib_server_async_channel.instrument_loading_percents[(usize)layer_index].Load(
                    LoadMemoryOrder::Relaxed);
            percent != -1) {
            f32 const load_percent = (f32)percent / 100.0f;
            auto const min = window_r.Min();
            auto const max = f32x2 {window_r.x + Max(4.0f, window_r.w * load_percent), window_r.Bottom()};
            g.imgui.draw_list->AddRectFilled(min, max, LiveCol(UiColMap::InstSelectorMenuLoading), rounding);
            GuiIo().WakeupAtTimedInterval(g.redraw_counter, 0.1);
        }
    }

    // Instrument name button (icon + text)
    Optional<TextureHandle> icon_tex {};
    if (layer_obj->instrument_id.tag == InstrumentType::Sampler) {
        auto sample_inst_id = layer_obj->instrument_id.Get<sample_lib::InstrumentId>();
        auto imgs = GetLibraryImages(g.library_images,
                                     g.imgui,
                                     sample_inst_id.library,
                                     g.shared_engine_systems.sample_library_server,
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
                switch (layer_obj->instrument.tag) {
                    case InstrumentType::None: return "Select the instrument for this layer"_s;
                    case InstrumentType::WaveformSynth:
                        return fmt::Format(
                            g.scratch_arena,
                            "Current instrument: {}\nChange or remove the instrument for this layer",
                            inst_name);
                    case InstrumentType::Sampler: {
                        auto const& sample = layer_obj->instrument.GetFromTag<InstrumentType::Sampler>();
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

    // Icon box: takes up layout space so text is pushed over
    if (icon_tex) {
        auto const icon_box =
            DoBox(g.builder,
                  {
                      .parent = inst_button,
                      .parent_dictates_hot_and_active = true,
                      .layout {
                          .size = {LiveWw(UiSizeId::LayerInstIconSize), layout::k_fill_parent},
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
                  .size = {layout::k_fill_parent,
                           k_font_body_size * LiveRaw(UiSizeId::TextBtnHPct) / 100.0f},
                  .margins {.l = icon_tex ? 0.0f : LiveWw(UiSizeId::MenuTextMarginL)},
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
            .layer = *layer_obj,
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

static void DoMuteSoloButton(GuiState& g, Box parent, DescribedParamValue const& param, bool is_solo) {
    auto const state = param.BoolValue();
    auto const on_back_col =
        is_solo ? LiveColStruct(UiColMap::SoloButtonBackOn) : LiveColStruct(UiColMap::MuteButtonBackOn);

    auto const btn = DoBox(
        g.builder,
        {
            .parent = parent,
            .id_extra = is_solo,
            .text = is_solo ? "S"_s : "M"_s,
            .text_colours = state ? Colours {ColSet {
                                        .base = LiveColStruct(UiColMap::MuteSoloButtonTextOn),
                                        .hot = LiveColStruct(UiColMap::MuteSoloButtonTextOnHot),
                                        .active = LiveColStruct(UiColMap::MuteSoloButtonTextOnHot),
                                    }}
                                  : Colours {ColSet {
                                        .base = LiveColStruct(UiColMap::MidText),
                                        .hot = LiveColStruct(UiColMap::MidTextHot),
                                        .active = LiveColStruct(UiColMap::MidTextHot),
                                    }},
            .text_justification = TextJustification::Centred,
            .background_fill_colours = state ? Colours {on_back_col} : Colours {Col {.c = Col::None}},
            .round_background_corners = is_solo ? (Corners)0b0110 : (Corners)0b1001,
            .corner_rounding = LiveWw(UiSizeId::CornerRounding),
            .layout {
                .size = layout::k_fill_parent,
            },
            .tooltip =
                FunctionRef<String()> {[&]() -> String { return ParamTooltipText(param, g.builder.arena); }},
            .button_behaviour = imgui::ButtonConfig {},
        });

    if (btn.button_fired) SetParameterValue(g.engine.processor, param.info.index, state ? 0.0f : 1.0f, {});

    AddParamContextMenuBehaviour(g, btn, param);
}

static void DoMixerContainer1(GuiState& g, u8 layer_index, Box root) {
    auto& params = g.engine.processor.main_params;

    auto const container =
        DoBox(g.builder,
              {
                  .parent = root,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_gap = LiveWw(UiSizeId::LayerMixerRowGap),
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Middle,
                  },
              });

    // Volume knob with peak meter
    auto const& layer_processor = g.engine.processor.layer_processors[layer_index];
    DoKnobParameter(g,
                    container,
                    params.DescribedValue(layer_index, LayerParamIndex::Volume),
                    {
                        .width = LiveWw(UiSizeId::LayerVolumeKnobW),
                        .knob_height_fraction = LiveRaw(UiSizeId::LayerVolumeKnobHPct) / 100.0f,
                        .style_system = GuiStyleSystem::MidPanel,
                        .peak_meter = &layer_processor.peak_meter,
                    });

    // Mute/Solo buttons
    {
        auto const mute_solo_container = DoBox(
            g.builder,
            {
                .parent = container,
                .layout {
                    .size = {LiveWw(UiSizeId::LayerMuteSoloW), LiveWw(UiSizeId::LayerMuteSoloH)},
                    .contents_direction = layout::Direction::Row,
                    .contents_align = layout::Alignment::Start,
                },
            });

        // Background and divider
        if (auto const r = BoxRect(g.builder, mute_solo_container)) {
            auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
            auto const rounding = LivePx(UiSizeId::CornerRounding);
            g.imgui.draw_list->AddRectFilled(window_r, LiveCol(UiColMap::MidDarkSurface), rounding);
            // Vertical divider in the middle
            g.imgui.draw_list->AddLine({window_r.Centre().x, window_r.y},
                                       {window_r.Centre().x, window_r.Bottom()},
                                       LiveCol(UiColMap::MuteSoloButtonDivider));
        }

        DoMuteSoloButton(g,
                         mute_solo_container,
                         params.DescribedValue(layer_index, LayerParamIndex::Mute),
                         false);
        DoMuteSoloButton(g,
                         mute_solo_container,
                         params.DescribedValue(layer_index, LayerParamIndex::Solo),
                         true);
    }
}

static void DoWhitespace(GuiBuilder& builder, Box parent, f32 height, u64 loc_hash = SourceLocationHash()) {
    DoBox(builder,
          {
              .parent = parent,
              .layout {.size = {1, height}},
          },
          loc_hash);
}

static void DoDivider(GuiState& g, Box parent, u64 loc_hash = SourceLocationHash()) {
    auto const divider = DoBox(g.builder,
                               {
                                   .parent = parent,
                                   .layout {
                                       .size = {layout::k_fill_parent, 1},
                                   },
                               },
                               loc_hash);
    if (auto const r = BoxRect(g.builder, divider)) {
        auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
        g.imgui.draw_list->AddLine({window_r.x, window_r.Bottom()},
                                   {window_r.Right(), window_r.Bottom()},
                                   LiveCol(UiColMap::MidViewportDivider));
    }
}

static void DoPageTabs(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    auto& layer_state = g.layer_panel_states[layer_index];

    auto const tabs_row = DoBox(g.builder,
                                {
                                    .parent = parent,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_direction = layout::Direction::Row,
                                        .contents_align = layout::Alignment::Middle,
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
                case LayerPageType::Main:
                case LayerPageType::Play:
                case LayerPageType::Count: break;
            }
            result;
        });

        auto const name = [&]() -> String {
            switch (page_type) {
                case LayerPageType::Main: return "Main"_s;
                case LayerPageType::Eq: return "EQ"_s;
                case LayerPageType::Play: return "Play"_s;
                case LayerPageType::Lfo: return "LFO"_s;
                case LayerPageType::Filter: return "Filter"_s;
                case LayerPageType::Count: PanicIfReached();
            }
            return {};
        }();

        auto const tab_btn = DoBox(g.builder,
                                   {
                                       .parent = tabs_row,
                                       .id_extra = (u64)i,
                                       .layout {
                                           .size = layout::k_hug_contents,
                                       },
                                       .tooltip = FunctionRef<String()> {[&]() -> String {
                                           return fmt::Format(g.scratch_arena, "Open {} tab", name);
                                       }},
                                       .button_behaviour = imgui::ButtonConfig {},
                                   });

        DoBox(g.builder,
              {
                  .parent = tab_btn,
                  .text = name,
                  .size_from_text = true,
                  .text_colours = is_selected ? Colours {ColSet {
                                                    .base = LiveColStruct(UiColMap::MidTextOn),
                                                    .hot = LiveColStruct(UiColMap::MidTextHot),
                                                    .active = LiveColStruct(UiColMap::MidTextHot),
                                                }}
                                              : Colours {ColSet {
                                                    .base = LiveColStruct(UiColMap::MidText),
                                                    .hot = LiveColStruct(UiColMap::MidTextHot),
                                                    .active = LiveColStruct(UiColMap::MidTextHot),
                                                }},
                  .text_justification = TextJustification::Centred,
                  .parent_dictates_hot_and_active = true,
                  .layout {
                      .margins {.lr = LiveWw(UiSizeId::LayerTabPadLR),
                                .tb = LiveWw(UiSizeId::LayerTabPadTB)},
                  },
              });

        if (tab_btn.button_fired) layer_state.selected_page = page_type;

        // Draw active-content dot indicator
        if (tab_has_active_content) {
            if (auto const r = BoxRect(g.builder, tab_btn)) {
                auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
                auto const dot_size = k_font_icons_size * 0.40f;
                auto const dot_centre = f32x2 {window_r.x + (LivePx(UiSizeId::LayerTabDotOffsetX) / 2),
                                               window_r.Centre().y};
                auto const col = is_selected ? LiveCol(UiColMap::MidTextOn) : LiveCol(UiColMap::MidText);
                g.imgui.draw_list->AddCircleFilled(dot_centre, dot_size / 2, col);
            }
        }
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
                    case ParameterModule::VolEnv: new_page = LayerPageType::Main; break;
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

static void DoMixerContainer2(GuiState& g, u8 layer_index, Box root) {
    auto& params = g.engine.processor.main_params;

    auto const container = DoBox(g.builder,
                                 {
                                     .parent = root,
                                     .layout {
                                         .size = layout::k_hug_contents,
                                         .contents_gap = LiveWw(UiSizeId::LayerTunePanGap),
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Middle,
                                     },
                                 });

    // Tune semitone (int dragger)
    DoIntParameter(g,
                   container,
                   params.DescribedValue(layer_index, LayerParamIndex::TuneSemitone),
                   {
                       .width = LiveWw(UiSizeId::LayerTuneSemitoneW),
                       .always_show_plus = true,
                   });

    // Tune cents (bidirectional knob)
    DoKnobParameter(g,
                    container,
                    params.DescribedValue(layer_index, LayerParamIndex::TuneCents),
                    {
                        .width = LiveWw(UiSizeId::KnobLargeW),
                        .knob_height_fraction = 0.96f,
                        .style_system = GuiStyleSystem::MidPanel,
                        .bidirectional = true,
                    });

    // Pan (bidirectional knob)
    DoKnobParameter(g,
                    container,
                    params.DescribedValue(layer_index, LayerParamIndex::Pan),
                    {
                        .width = LiveWw(UiSizeId::KnobLargeW),
                        .knob_height_fraction = 0.96f,
                        .style_system = GuiStyleSystem::MidPanel,
                        .bidirectional = true,
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
    auto const menu_btn =
        DoBox(g.builder,
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
                      .size = {layout::k_fill_parent,
                               k_font_body_size * LiveRaw(UiSizeId::TextBtnHPct) / 100.0f},
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
                // the lambda not being a trvially copyable type for some reason. To work around this, we add
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
                                               .contents_padding {
                                                   .t = LiveWw(UiSizeId::PageTogglePadT),
                                                   .b = LiveWw(UiSizeId::PageTogglePadB),
                                               },
                                               .contents_gap = LiveWw(UiSizeId::FilterHeadingGap),
                                               .contents_direction = layout::Direction::Row,
                                           },
                                       });

        DoButtonParameter(g,
                          heading_row,
                          params.DescribedValue(layer_index, LayerParamIndex::FilterOn),
                          {.width = LiveWw(UiSizeId::FilterOnBtnW)});

        DoMenuParameter(
            g,
            heading_row,
            params.DescribedValue(layer_index, LayerParamIndex::FilterType),
            {.width = LiveWw(UiSizeId::FilterTypeMenuW), .greyed_out = greyed_out, .label = false});
    }

    DoWhitespace(g.builder, page, LiveWw(UiSizeId::FilterKnobsGapAbove));

    // Knobs row: Cutoff, Resonance, EnvAmount
    {
        auto const knobs_row = DoBox(g.builder,
                                     {
                                         .parent = page,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_padding {.lr = LiveWw(UiSizeId::PageKnobRowPadLR)},
                                             .contents_gap = LiveWw(UiSizeId::PageKnobRowGap),
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Middle,
                                         },
                                     });

        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterCutoff),
                        {
                            .width = LiveWw(UiSizeId::KnobLargeW),
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterResonance),
                        {
                            .width = LiveWw(UiSizeId::KnobLargeW),
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterEnvAmount),
                        {
                            .width = LiveWw(UiSizeId::KnobLargeW),
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                            .bidirectional = true,
                        });
    }

    // Filter envelope
    {
        auto const envelope_box =
            DoBox(g.builder,
                  {
                      .parent = page,
                      .layout {
                          .size = {layout::k_fill_parent, LiveWw(UiSizeId::MainEnvelopeH)},
                          .margins {
                              .lr = LiveWw(UiSizeId::FilterEnvelopeMarginLR),
                              .tb = LiveWw(UiSizeId::FilterEnvelopeMarginTB),
                          },
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
                                },
                            });

    // EqOn heading
    {
        auto const heading_wrapper = DoBox(g.builder,
                                           {
                                               .parent = page,
                                               .layout {
                                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                   .contents_padding {
                                                       .t = LiveWw(UiSizeId::PageTogglePadT),
                                                       .b = LiveWw(UiSizeId::PageTogglePadB),
                                                   },
                                                   .contents_align = layout::Alignment::Middle,
                                               },
                                           });

        DoButtonParameter(g,
                          heading_wrapper,
                          params.DescribedValue(layer_index, LayerParamIndex::EqOn),
                          {
                              .width = LiveWw(UiSizeId::PageToggleBtnW),
                          });
    }

    // Container for all EQ band rows with consistent gap
    auto const bands_container = DoBox(g.builder,
                                       {
                                           .parent = page,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_gap = LiveWw(UiSizeId::EqBandGap),
                                               .contents_direction = layout::Direction::Column,
                                               .contents_align = layout::Alignment::Start,
                                           },
                                       });

    // EQ band helper - adds menu row and knobs row to bands_container
    auto const do_eq_band = [&](LayerParamIndex type_param,
                                LayerParamIndex freq_param,
                                LayerParamIndex reso_param,
                                LayerParamIndex gain_param,
                                u64 loc_hash = SourceLocationHash()) {
        // Type menu
        auto const menu_row = DoBox(g.builder,
                                    {
                                        .parent = bands_container,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_padding {.lr = LiveWw(UiSizeId::EqMenuPadLR)},
                                        },
                                    },
                                    loc_hash);
        DoMenuParameter(g,
                        menu_row,
                        params.DescribedValue(layer_index, type_param),
                        {
                            .width = layout::k_fill_parent,
                            .greyed_out = greyed_out,
                            .label = false,
                        });

        // Knobs row
        auto const knobs_row = DoBox(g.builder,
                                     {
                                         .parent = bands_container,
                                         .id_extra = loc_hash,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_padding {.lr = LiveWw(UiSizeId::PageKnobRowPadLR)},
                                             .contents_gap = LiveWw(UiSizeId::PageKnobRowGap),
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Middle,
                                         },
                                     });

        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, freq_param),
                        {
                            .width = LiveWw(UiSizeId::KnobLargeW),
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, reso_param),
                        {
                            .width = LiveWw(UiSizeId::KnobLargeW),
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, gain_param),
                        {
                            .width = LiveWw(UiSizeId::KnobLargeW),
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                            .bidirectional = true,
                        });
    };

    // Band 1
    do_eq_band(LayerParamIndex::EqType1,
               LayerParamIndex::EqFreq1,
               LayerParamIndex::EqResonance1,
               LayerParamIndex::EqGain1);

    // Band 2
    do_eq_band(LayerParamIndex::EqType2,
               LayerParamIndex::EqFreq2,
               LayerParamIndex::EqResonance2,
               LayerParamIndex::EqGain2);
}

static void DoLfoPage(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    bool const greyed_out = !params.BoolValue(layer_index, LayerParamIndex::LfoOn);

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // LfoOn heading
    {
        auto const heading_wrapper = DoBox(g.builder,
                                           {
                                               .parent = page,
                                               .layout {
                                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                   .contents_padding {
                                                       .t = LiveWw(UiSizeId::PageTogglePadT),
                                                       .b = LiveWw(UiSizeId::PageTogglePadB),
                                                   },
                                                   .contents_align = layout::Alignment::Middle,
                                               },
                                           });

        DoButtonParameter(g,
                          heading_wrapper,
                          params.DescribedValue(layer_index, LayerParamIndex::LfoOn),
                          {
                              .width = LiveWw(UiSizeId::PageToggleBtnW),
                          });
    }

    {
        auto const menu_row_container =
            DoBox(g.builder,
                  {
                      .parent = page,
                      .layout {
                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                          .contents_padding = {.l = LiveWw(UiSizeId::LfoMenusPadL)},
                          .contents_gap = LiveWw(UiSizeId::LfoMenusGapY),
                          .contents_direction = layout::Direction::Column,
                      },
                  });

        // Menu + label rows
        auto const do_menu_label_row = [&](LayerParamIndex param_index, u64 loc_hash = SourceLocationHash()) {
            auto const param = params.DescribedValue(layer_index, param_index);

            auto const row = DoBox(g.builder,
                                   {
                                       .parent = menu_row_container,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_gap = LiveWw(UiSizeId::LfoMenuLabelGap),
                                           .contents_direction = layout::Direction::Row,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                   },
                                   loc_hash);

            DoMenuParameter(g,
                            row,
                            param,
                            {
                                .width = LiveWw(UiSizeId::LfoMenuW),
                                .greyed_out = greyed_out,
                                .label = false,
                            });

            DoBox(g.builder,
                  {
                      .parent = row,
                      .text = param.info.gui_label,
                      .text_colours = LiveColStruct(UiColMap::MidText),
                      .text_justification = TextJustification::CentredLeft,
                      .layout {
                          .size = {layout::k_fill_parent, k_font_body_size},
                      },
                  },
                  loc_hash);
        };

        do_menu_label_row(LayerParamIndex::LfoDestination);
        do_menu_label_row(LayerParamIndex::LfoShape);
        do_menu_label_row(LayerParamIndex::LfoRestart);
    }

    DoWhitespace(g.builder, page, LiveWw(UiSizeId::LfoKnobsGapAbove));

    // Knobs row: Amount + Rate column
    {
        auto const knobs_row = DoBox(g.builder,
                                     {
                                         .parent = page,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = LiveWw(UiSizeId::LfoKnobsGap),
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Middle,
                                         },
                                     });

        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::LfoAmount),
                        {
                            .width = LiveWw(UiSizeId::KnobLargeW),
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
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Middle,
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
                                .width = LiveWw(UiSizeId::KnobLargeW),
                                .style_system = GuiStyleSystem::MidPanel,
                                .greyed_out = greyed_out,
                            });
        }

        DoWhitespace(g.builder, rate_col, LiveWw(UiSizeId::LfoSyncGapAbove));

        DoButtonParameter(g,
                          rate_col,
                          params.DescribedValue(layer_index, LayerParamIndex::LfoSyncSwitch),
                          {.width = LiveWw(UiSizeId::LfoSyncBtnW)});
    }
}

static void DoPlayPage(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;

    auto const row_height = LiveWw(UiSizeId::PlayRowH);
    auto const row_padding_lr = LiveWw(UiSizeId::PlayRowPadLR);
    auto const item_gap = LiveWw(UiSizeId::PlayItemGap);
    auto const control_label_gap = LiveWw(UiSizeId::PlayLabelGap);
    auto const control_width = LiveWw(UiSizeId::PlayControlW);

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_gap = LiveWw(UiSizeId::PlayRowGap),
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Helper for int dragger + label rows (Transpose, PitchBend)
    auto const do_int_label_row = [&](LayerParamIndex param_index, u64 loc_hash = SourceLocationHash()) {
        auto const param = params.DescribedValue(layer_index, param_index);

        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = {layout::k_fill_parent, row_height},
                                       .contents_padding {.lr = row_padding_lr},
                                       .contents_gap = control_label_gap,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               },
                               loc_hash);

        DoIntParameter(g,
                       row,
                       param,
                       {
                           .width = control_width,
                           .label = false,
                       });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String { return param.info.tooltip; }},
              },
              loc_hash);
    };

    // Transpose
    do_int_label_row(LayerParamIndex::MidiTranspose);

    // PitchBend
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
                                       .size = {layout::k_fill_parent, row_height},
                                       .contents_padding {.lr = row_padding_lr},
                                       .contents_gap = control_label_gap,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                                   .tooltip = FunctionRef<String()> {[&]() -> String {
                                       return ParamTooltipText(param, g.builder.arena);
                                   }},
                                   .button_behaviour = imgui::ButtonConfig {},
                               });

        DoToggleIcon(g.builder,
                     row,
                     {
                         .state = state,
                         .width = control_width,
                         .justify = TextJustification::CentredRight,
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
                  .text_justification = TextJustification::CentredLeft,
                  .parent_dictates_hot_and_active = true,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
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
                                       .size = {layout::k_fill_parent, row_height},
                                       .contents_padding {.lr = row_padding_lr},
                                       .contents_gap = control_label_gap,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoMenuParameter(g, row, param, {.width = control_width, .label = false});

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
              });
    }

    // Key Range row
    {
        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = {layout::k_fill_parent, row_height},
                                       .contents_padding {.lr = row_padding_lr},
                                       .contents_gap = item_gap,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = "Range"_s,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
              });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeLow),
                       {
                           .width = LiveWw(UiSizeId::PlayNarrowControlW),
                           .midi_note_names = true,
                           .label = false,
                       });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeHigh),
                       {
                           .width = LiveWw(UiSizeId::PlayNarrowControlW),
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
                                       .size = {layout::k_fill_parent, row_height},
                                       .contents_padding {.lr = row_padding_lr},
                                       .contents_gap = item_gap,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = "Key Fade"_s,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
              });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeLowFade),
                       {
                           .width = LiveWw(UiSizeId::PlayNarrowControlW),
                           .label = false,
                       });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeHighFade),
                       {
                           .width = LiveWw(UiSizeId::PlayNarrowControlW),
                           .label = false,
                       });
    }

    // Velocity label
    {
        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = {layout::k_fill_parent, row_height},
                                       .contents_padding {.lr = row_padding_lr},
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = "Velocity to volume curve"_s,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String {
                      return "Curve that maps velocity to volume"_s;
                  }},
              });
    }

    // Velocity graph
    {
        auto const velo_box =
            DoBox(g.builder,
                  {
                      .parent = page,
                      .layout {
                          .size = {layout::k_fill_parent, LiveWw(UiSizeId::PlayVeloGraphH)},
                          .margins {.lr = row_padding_lr},
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

static void DoMainPage(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Waveform display
    {
        auto const waveform_box =
            DoBox(g.builder,
                  {
                      .parent = page,
                      .layout {
                          .size = {layout::k_fill_parent, LiveWw(UiSizeId::MainWaveformH)},
                          .margins {
                              .lr = LiveWw(UiSizeId::MainWaveformMarginLR),
                              .tb = LiveWw(UiSizeId::MainWaveformMarginTB),
                          },
                      },
                  });
        if (auto const r = BoxRect(g.builder, waveform_box)) DoWaveformElement(g, layer, *r);
    }

    // Waveform label (instrument type name)
    DoBox(g.builder,
          {
              .parent = page,
              .text = layer.InstTypeName(),
              .text_colours = LiveColStruct(UiColMap::MidText),
              .text_justification = TextJustification::Centred,
              .layout {
                  .size = {layout::k_fill_parent, LiveWw(UiSizeId::MainWaveformLabelH)},
                  .margins {.lr = LiveWw(UiSizeId::MainWaveformMarginLR)},
              },
          });

    // Button row: Reverse toggle + Loop mode selector
    {
        auto const btn_row = DoBox(g.builder,
                                   {
                                       .parent = page,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_padding {
                                               .l = LiveWw(UiSizeId::MainRowPadL),
                                               .r = LiveWw(UiSizeId::MainRowPadR),
                                               .tb = LiveWw(UiSizeId::MainRowPadTB),
                                           },
                                           .contents_direction = layout::Direction::Row,
                                       },
                                   });

        bool const is_waveform_synth = layer.instrument_id.tag == InstrumentType::WaveformSynth;
        DoButtonParameter(g,
                          btn_row,
                          params.DescribedValue(layer_index, LayerParamIndex::Reverse),
                          {
                              .width = LiveWw(UiSizeId::MainReverseBtnW),
                              .greyed_out = is_waveform_synth,
                          });

        DoLoopModeSelector(g, btn_row, layer);
    }

    // Divider
    DoWhitespace(g.builder, page, LiveWw(UiSizeId::MainDividerGapAbove));
    DoDivider(g, page);
    DoWhitespace(g.builder, page, LiveWw(UiSizeId::MainDividerGapBelow));

    // Volume envelope heading
    {
        auto const heading_wrapper = DoBox(g.builder,
                                           {
                                               .parent = page,
                                               .layout {
                                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                   .contents_padding {
                                                       .l = LiveWw(UiSizeId::PageHeadingPadL),
                                                       .t = LiveWw(UiSizeId::PageHeadingPadT),
                                                   },
                                                   .contents_align = layout::Alignment::Start,
                                               },
                                           });

        DoButtonParameter(g,
                          heading_wrapper,
                          params.DescribedValue(layer_index, LayerParamIndex::VolEnvOn),
                          {});
    }

    // Envelope display
    {
        auto const envelope_box =
            DoBox(g.builder,
                  {
                      .parent = page,
                      .layout {
                          .size = {layout::k_fill_parent, LiveWw(UiSizeId::MainEnvelopeH)},
                          .margins {
                              .lr = LiveWw(UiSizeId::MainEnvelopeMarginLR),
                              .tb = LiveWw(UiSizeId::MainEnvelopeMarginTB),
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
                                                .l = LiveWw(UiSizeId::LayerTopPadL),
                                                .r = LiveWw(UiSizeId::LayerTopPadR),
                                                .t = LiveWw(UiSizeId::LayerTopPadT),
                                                .b = LiveWw(UiSizeId::LayerTopPadB),
                                            },
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Start,
                                        },
                                    });

    DoInstSelector(g, frame_context, layer_index, top_controls);

    if (g.engine.Layer(layer_index).instrument.tag == InstrumentType::None) return;

    DoWhitespace(g.builder, top_controls, LiveWw(UiSizeId::LayerGapAfterSelector));

    DoMixerContainer1(g, layer_index, top_controls);

    DoWhitespace(g.builder, top_controls, LiveWw(UiSizeId::LayerGapBetweenMixers));

    DoMixerContainer2(g, layer_index, top_controls);

    DoWhitespace(g.builder, root, LiveWw(UiSizeId::LayerDividerGapAbove));
    DoDivider(g, root);
    DoPageTabs(g, layer_index, root);
    DoDivider(g, root);
    DoWhitespace(g.builder, root, LiveWw(UiSizeId::LayerDividerGapBelow));

    // Page content
    switch (g.layer_panel_states[layer_index].selected_page) {
        case LayerPageType::Main: DoMainPage(g, layer_index, root); break;
        case LayerPageType::Filter: DoFilterPage(g, layer_index, root); break;
        case LayerPageType::Lfo: DoLfoPage(g, layer_index, root); break;
        case LayerPageType::Eq: DoEqPage(g, layer_index, root); break;
        case LayerPageType::Play: DoPlayPage(g, layer_index, root); break;
        case LayerPageType::Count: PanicIfReached();
    }
}
