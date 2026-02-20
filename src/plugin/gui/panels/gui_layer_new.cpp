// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_layer_new.hpp"

#include <IconsFontAwesome6.h>

#include "engine/engine.hpp"
#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_inst_browser.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

namespace layer_gui_new {

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
    using enum UiSizeId;

    auto layer_obj = &g.engine.Layer(layer_index);
    auto const inst_name = layer_obj->InstName();

    // Selector row container
    auto const selector_box = DoBox(g.builder,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = {layout::k_fill_parent, LiveWw(LayerSelectorBoxHeight)},
                                            .contents_padding {.r = LiveWw(LayerSelectorBoxButtonsMarginR)},
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
                .size = layout::k_fill_parent,
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
        auto const icon_box = DoBox(g.builder,
                                    {
                                        .parent = inst_button,
                                        .parent_dictates_hot_and_active = true,
                                        .layout {
                                            .size = {LiveWw(LayerSelectorBoxHeight), layout::k_fill_parent},
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
                  .size = layout::k_fill_parent,
                  .margins {.l = icon_tex ? 0.0f : LiveWw(MenuButtonTextMarginL)},
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
    using enum UiSizeId;

    auto& params = g.engine.processor.main_params;

    auto const container = DoBox(g.builder,
                                 {
                                     .parent = root,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = LiveWw(LayerTopSectionSpaceBetweenVolAndMs),
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Middle,
                                     },
                                 });

    // Volume knob
    auto const volume_knob =
        DoKnobParameter(g,
                        container,
                        params.DescribedValue(layer_index, LayerParamIndex::Volume),
                        {
                            .width = LiveWw(UiSizeId::LayerVolumeKnobSize),
                            .knob_height_fraction = LiveRaw(UiSizeId::LayerVolumeKnobHeightPercent) / 100.0f,
                            .style_system = GuiStyleSystem::MidPanel,
                        });

    // Peak meter drawn on top of the volume knob
    if (auto const r = BoxRect(g.builder, volume_knob)) {
        auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
        auto const peak_meter_width = LivePx(LayerPeakMeterWidth);
        auto const peak_meter_height = LivePx(LayerPeakMeterHeight2);
        auto const peak_meter_y_offs = LivePx(LayerPeakMeterYOffs);

        Rect const peak_meter_r {
            .x = window_r.Centre().x - (peak_meter_width / 2),
            .y = window_r.y + peak_meter_y_offs,
            .w = peak_meter_width,
            .h = peak_meter_height,
        };
        auto const& processor = g.engine.processor.layer_processors[layer_index];
        DrawPeakMeter(g.imgui, peak_meter_r, processor.peak_meter, false);
    }

    // Mute/Solo buttons
    {
        auto const mute_solo_container =
            DoBox(g.builder,
                  {
                      .parent = container,
                      .layout {
                          .size = {LiveWw(LayerMuteSoloWidth), LiveWw(LayerMuteSoloHeight)},
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

static void DoMixerContainer2(GuiState& g, u8 layer_index, Box root) {
    using enum UiSizeId;

    auto& params = g.engine.processor.main_params;

    auto const container = DoBox(g.builder,
                                 {
                                     .parent = root,
                                     .layout {
                                         .size = layout::k_hug_contents,
                                         .contents_gap = LiveWw(LayerMixerKnobGapX),
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Middle,
                                     },
                                 });

    // Tune semitone (int dragger)
    DoIntParameter(g,
                   container,
                   params.DescribedValue(layer_index, LayerParamIndex::TuneSemitone),
                   {
                       .width = LiveWw(LayerPitchWidth),
                       .always_show_plus = true,
                   });

    // Tune cents (bidirectional knob)
    DoKnobParameter(g,
                    container,
                    params.DescribedValue(layer_index, LayerParamIndex::TuneCents),
                    {
                        .width = LiveWw(ParamComponentLargeWidth),
                        .knob_height_fraction = 0.96f,
                        .style_system = GuiStyleSystem::MidPanel,
                        .bidirectional = true,
                    });

    // Pan (bidirectional knob)
    DoKnobParameter(g,
                    container,
                    params.DescribedValue(layer_index, LayerParamIndex::Pan),
                    {
                        .width = LiveWw(ParamComponentLargeWidth),
                        .knob_height_fraction = 0.96f,
                        .style_system = GuiStyleSystem::MidPanel,
                        .bidirectional = true,
                    });
}

void DoLayerPanel(GuiState& g, GuiFrameContext const& frame_context, u8 layer_index, Box parent) {
    using enum UiSizeId;

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
                                                .l = LiveWw(LayerSelectorBoxMarginL),
                                                .r = LiveWw(LayerSelectorBoxMarginR),
                                                .t = LiveWw(LayerSelectorBoxMarginT),
                                                .b = LiveWw(LayerSelectorBoxMarginB),
                                            },
                                            .contents_gap = LiveWw(UiSizeId::LayerTopSectionGapAfterSelector),
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Start,
                                        },
                                    });

    DoInstSelector(g, frame_context, layer_index, top_controls);

    if (g.engine.Layer(layer_index).instrument.tag == InstrumentType::None) return;

    DoMixerContainer1(g, layer_index, top_controls);
    DoMixerContainer2(g, layer_index, top_controls);
}

} // namespace layer_gui_new
