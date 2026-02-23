// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/panels/gui_effects.hpp"
#include "gui/panels/gui_inst_browser.hpp"
#include "gui/panels/gui_ir_browser.hpp"
#include "gui/panels/gui_layer.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/image.hpp"
#include "gui_mid_panel.hpp"


static f32 RoundUpToNearestMultiple(f32 value, f32 multiple) { return multiple * Ceil(value / multiple); }

static void DrawMidPanelOverallBackground(GuiState& g, imgui::Context const& imgui) {
    auto overall_library = LibraryForOverallBackground(g.engine);
    if (overall_library)
        DrawMidPanelBackgroundImage(g, *overall_library);
    else
        imgui.draw_list->AddRectFilled(imgui.curr_viewport->unpadded_bounds,
                                       LiveCol(UiColMap::MidViewportBackground));
}

static void DrawLayersContainerBackground(GuiState& g, Rect r) {
    auto const mid_panel_title_height = LivePx(UiSizeId::MidPanelTitleHeight);
    auto const panel_rounding = LivePx(UiSizeId::BlurredPanelRounding);

    auto const layer_width_without_pad = RoundUpToNearestMultiple(r.w, k_num_layers) / k_num_layers;

    auto const overall_lib = LibraryForOverallBackground(g.engine);
    if (overall_lib)
        DrawMidBlurredBackground(g,
                              r,
                              r,
                              *overall_lib,
                              Clamp01(LiveRaw(UiSizeId::BackgroundBlurringOpacity1) / 100.0f));

    auto const layer_opacity = Clamp01(LiveRaw(UiSizeId::BackgroundBlurringOpacitySingleLayer1) / 100.0f);
    for (auto const layer_index : Range(k_num_layers)) {
        if (auto const lib_id = g.engine.Layer(layer_index).LibId(); lib_id) {
            if (*lib_id == overall_lib) continue;
            auto const layer_r = Rect {.x = r.x + ((f32)layer_index * layer_width_without_pad),
                                       .y = r.y,
                                       .w = layer_width_without_pad,
                                       .h = r.h}
                                     .CutTop(mid_panel_title_height);
            DrawMidBlurredBackground(g, r, layer_r, *lib_id, layer_opacity);
        }
    }

    if (!prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::HighContrastGui)))
        DoMidOverlayGradient(g.imgui, r);

    g.imgui.draw_list->AddRect(r, LiveCol(UiColMap::MidViewportSurfaceBorder), panel_rounding);

    g.imgui.draw_list->AddLine({r.x, r.y + mid_panel_title_height},
                               {r.Right(), r.y + mid_panel_title_height},
                               LiveCol(UiColMap::MidViewportDivider));
    for (u32 i = 1; i < k_num_layers; ++i) {
        auto const x_pos = r.x + ((f32)i * layer_width_without_pad) - 1;
        g.imgui.draw_list->AddLine({x_pos, r.y + mid_panel_title_height},
                                   {x_pos, r.Bottom()},
                                   LiveCol(UiColMap::MidViewportDivider));
    }
}

static void DrawEffectsContainerBackground(GuiState& g, Rect r) {
    auto const mid_panel_title_height = LivePx(UiSizeId::MidPanelTitleHeight);
    auto const panel_rounding = LivePx(UiSizeId::BlurredPanelRounding);

    auto const overall_lib = LibraryForOverallBackground(g.engine);
    if (overall_lib)
        DrawMidBlurredBackground(g,
                              r,
                              r,
                              *overall_lib,
                              Clamp01(LiveRaw(UiSizeId::BackgroundBlurringOpacity1) / 100.0f));

    DoMidOverlayGradient(g.imgui, r);

    g.imgui.draw_list->AddRect(r, LiveCol(UiColMap::MidViewportSurfaceBorder), panel_rounding);

    g.imgui.draw_list->AddLine({r.x, r.y + mid_panel_title_height},
                               {r.Right(), r.y + mid_panel_title_height},
                               LiveCol(UiColMap::MidViewportDivider));
}

static void
DoLayersContainer(GuiBuilder& builder, GuiState& g, GuiFrameContext const& frame_context, Box parent) {
    auto const layer_width_ww = RoundUpToNearestMultiple(LiveWw(UiSizeId::LayerWidth), k_num_layers);
    auto const total_layer_width_ww = layer_width_ww * k_num_layers;

    auto const root = DoBox(builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = {total_layer_width_ww, layout::k_fill_parent},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    if (auto const r = BoxRect(builder, root))
        DrawLayersContainerBackground(g, builder.imgui.ViewportRectToWindowRect(*r));

    // Title row
    auto const title_row =
        DoBox(builder,
              {
                  .parent = root,
                  .layout {
                      .size = {layout::k_fill_parent, LiveWw(UiSizeId::MidPanelTitleHeight)},
                      .contents_padding = {.lr = LiveWw(UiSizeId::MidPanelTitleMarginLeft)},
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Justify,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                  },
              });

    DoBox(builder,
          {
              .parent = title_row,
              .text = "Layers",
              .size_from_text = true,
              .text_colours = LiveColStruct(UiColMap::MidText),
              .text_justification = TextJustification::CentredLeft,
          });

    auto const rand_btn = DoMidPanelShuffleButton(builder,
                                                  title_row,
                                                  {.tooltip = "Load random instruments for all 3 layers"_s});

    if (rand_btn.button_fired) {
        for (auto& layer : g.engine.processor.layer_processors) {
            InstBrowserContext context {
                .layer = layer,
                .sample_library_server = g.shared_engine_systems.sample_library_server,
                .library_images = g.library_images,
                .engine = g.engine,
                .prefs = g.prefs,
                .notifications = g.notifications,
                .persistent_store = g.shared_engine_systems.persistent_store,
                .confirmation_dialog_state = g.confirmation_dialog_state,
                .frame_context = frame_context,
            };
            LoadRandomInstrument(context, g.inst_browser_state[layer.index]);
        }
    }

    // Layer panels
    auto const layers_row = DoBox(builder,
                                  {
                                      .parent = root,
                                      .layout {
                                          .size = {layout::k_fill_parent, layout::k_fill_parent},
                                          .contents_direction = layout::Direction::Row,
                                      },
                                  });

    for (auto const layer_index : Range<u8>(k_num_layers)) {
        auto const layer_box = DoBox(builder,
                                     {
                                         .parent = layers_row,
                                         .id_extra = layer_index,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_fill_parent},
                                         },
                                     });

        DoLayerPanel(g, frame_context, layer_index, layer_box);
    }
}

static void
DoEffectsContainer(GuiBuilder& builder, GuiState& g, GuiFrameContext const& frame_context, Box parent) {
    auto const root = DoBox(builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    if (auto const r = BoxRect(builder, root))
        DrawEffectsContainerBackground(g, builder.imgui.ViewportRectToWindowRect(*r));

    // Title row
    auto const title_row =
        DoBox(builder,
              {
                  .parent = root,
                  .layout {
                      .size = {layout::k_fill_parent, LiveWw(UiSizeId::MidPanelTitleHeight)},
                      .contents_padding = {.lr = LiveWw(UiSizeId::MidPanelTitleMarginLeft)},
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Justify,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                  },
              });

    DoBox(builder,
          {
              .parent = title_row,
              .text = "Effects",
              .size_from_text = true,
              .text_colours = LiveColStruct(UiColMap::MidText),
              .text_justification = TextJustification::CentredLeft,
          });

    auto const rand_btn =
        DoMidPanelShuffleButton(builder, title_row, {.tooltip = "Randomise all of the effects"_s});

    if (rand_btn.button_fired) {
        RandomiseAllEffectParameterValues(g.engine.processor);
        IrBrowserContext ir_context {
            .sample_library_server = g.shared_engine_systems.sample_library_server,
            .library_images = g.library_images,
            .engine = g.engine,
            .prefs = g.prefs,
            .notifications = g.notifications,
            .persistent_store = g.shared_engine_systems.persistent_store,
            .confirmation_dialog_state = g.confirmation_dialog_state,
            .frame_context = frame_context,
        };
        LoadRandomIr(ir_context, g.ir_browser_state);
    }

    // Effects content
    auto const effects_box = DoBox(builder,
                                   {
                                       .parent = root,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_fill_parent},
                                       },
                                   });

    DoEffectsPanel(g, frame_context, effects_box);
}

void MidPanelCombined(GuiState& g, Rect bounds, GuiFrameContext const& frame_context) {
    DoBoxViewport(g.builder,
                  {
                      .run =
                          [&](GuiBuilder& builder) {
                              auto const subpanel_gap_ww = LiveWw(UiSizeId::MidPanelSubpanelGapX);

                              auto const root =
                                  DoBox(builder,
                                        {
                                            .layout {
                                                .size = layout::k_fill_parent,
                                                .contents_padding =
                                                    {
                                                        .lr = subpanel_gap_ww,
                                                        .tb = LiveWw(UiSizeId::MidPanelSubpanelGapY),
                                                    },
                                                .contents_gap = subpanel_gap_ww,
                                                .contents_direction = layout::Direction::Row,
                                                .contents_align = layout::Alignment::Start,
                                            },
                                        });

                              DoLayersContainer(builder, g, frame_context, root);
                              DoEffectsContainer(builder, g, frame_context, root);
                          },
                      .bounds = bounds,
                      .imgui_id = SourceLocationHash(),
                      .viewport_config {
                          .draw_background =
                              [&](imgui::Context const& imgui) { DrawMidPanelOverallBackground(g, imgui); },
                          .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
                      },
                      .debug_name = "MidPanel",
                  });
}
