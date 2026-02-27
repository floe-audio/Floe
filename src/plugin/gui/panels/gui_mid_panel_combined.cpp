// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/panels/gui_effects_strip.hpp"
#include "gui/panels/gui_inst_browser.hpp"
#include "gui/panels/gui_ir_browser.hpp"
#include "gui/panels/gui_layer_subtabbed.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_mid_panel.hpp"

static f32 RoundUpToNearestMultiple(f32 value, f32 multiple) { return multiple * Ceil(value / multiple); }

constexpr f32 k_mid_panel_title_height = 25.81f;
constexpr f32 k_mid_panel_title_margin_left = 10.32f;

static void DrawLayerBackground(GuiState& g, Rect r, u8 layer_index) {
    auto const overall_lib = LibraryForOverallBackground(g.engine);

    if (auto const lib_id = g.engine.Layer(layer_index).LibId(); lib_id) {
        if (*lib_id != overall_lib)
            DrawMidBlurredBackground(g,
                                     r,
                                     r,
                                     *lib_id,
                                     {
                                         .opacity = 0.33f,
                                         .rounding_corners = ({
                                             u4 f = 0;
                                             if (layer_index == 0)
                                                 f = 0b0001;
                                             else if (layer_index == (k_num_layers - 1))
                                                 f = 0b0010;
                                             f;
                                         }),
                                     });
    }

    if (layer_index != (k_num_layers - 1))
        g.imgui.draw_list->AddBorderEdges(r, LiveCol(UiColMap::MidViewportDivider), 0b0010);
}

static void
DoLayersContainer(GuiBuilder& builder, GuiState& g, GuiFrameContext const& frame_context, Box parent) {
    auto const layer_width_ww = RoundUpToNearestMultiple(215.99f, k_num_layers);
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
        DrawMidBlurredPanelSurface(g,
                                   builder.imgui.ViewportRectToWindowRect(*r),
                                   LibraryForOverallBackground(g.engine));

    // Title row
    auto const title_row = DoBox(builder,
                                 {
                                     .parent = root,
                                     .border_colours = LiveColStruct(UiColMap::MidViewportDivider),
                                     .border_edges = 0b0001,
                                     .layout {
                                         .size = {layout::k_fill_parent, k_mid_panel_title_height},
                                         .contents_padding = {.lr = k_mid_panel_title_margin_left},
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
        if (auto const r = BoxRect(builder, layer_box))
            DrawLayerBackground(g, g.imgui.ViewportRectToWindowRect(*r), layer_index);

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
        DrawMidBlurredPanelSurface(g,
                                   builder.imgui.ViewportRectToWindowRect(*r),
                                   LibraryForOverallBackground(g.engine));

    // Title row
    auto const title_row = DoBox(builder,
                                 {
                                     .parent = root,
                                     .border_colours = LiveColStruct(UiColMap::MidViewportDivider),
                                     .border_edges = 0b0001,
                                     .layout {
                                         .size = {layout::k_fill_parent, k_mid_panel_title_height},
                                         .contents_padding = {.lr = k_mid_panel_title_margin_left},
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
                                           .size = layout::k_fill_parent,
                                       },
                                   });

    DoEffectsStripPanel(g, frame_context, effects_box);
}

void MidPanelCombinedContent(GuiBuilder& builder,
                             GuiState& g,
                             GuiFrameContext const& frame_context,
                             Box parent) {
    constexpr f32 k_subpanel_gap_x = 8.08f;

    auto const root = DoBox(builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding =
                                        {
                                            .lr = k_subpanel_gap_x,
                                            .tb = EXPERIMENTAL_MID_PANEL_TABS ? 6.08f : 15,
                                        },
                                    .contents_gap = k_subpanel_gap_x,
                                    .contents_direction = layout::Direction::Row,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    DoLayersContainer(builder, g, frame_context, root);
    DoEffectsContainer(builder, g, frame_context, root);
}
