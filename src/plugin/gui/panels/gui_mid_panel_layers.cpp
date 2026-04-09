// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/panels/gui_inst_browser.hpp"
#include "gui/panels/gui_layer_subtabbed.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_mid_panel.hpp"

static void
DoLayersContainer(GuiBuilder& builder, GuiState& g, GuiFrameContext const& frame_context, Box parent) {
    auto const overall_lib = LibraryForOverallBackground(g.engine);

    auto const layers_row = DoBox(builder,
                                  {
                                      .parent = parent,
                                      .layout {
                                          .size = {layout::k_fill_parent, layout::k_fill_parent},
                                          .contents_gap = 8,
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

        if (auto const r = BoxRect(builder, layer_box)) {
            auto const window_r = builder.imgui.ViewportRectToWindowRect(*r);
            auto const layer_lib = g.engine.Layer(layer_index).LibId();
            DrawMidBlurredPanelSurface(g, window_r, layer_lib ? *layer_lib : overall_lib);
        }

        DoLayerPanel(g, frame_context, layer_index, layer_box);
    }
}

void MidPanelLayersContent(GuiBuilder& builder,
                           GuiState& g,
                           GuiFrameContext const& frame_context,
                           Box parent,
                           Box tab_extra_buttons_box) {
    constexpr f32 k_subpanel_gap_x = 8.08f;

    {
        auto const rand_btn =
            DoMidPanelShuffleButton(builder,
                                    tab_extra_buttons_box,
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
    }

    auto const root = DoBox(builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding = {.t = 6.08f},
                                    .contents_gap = k_subpanel_gap_x,
                                    .contents_direction = layout::Direction::Row,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    DoLayersContainer(builder, g, frame_context, root);
}
