// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/panels/gui_effects.hpp"
#include "gui/panels/gui_inst_browser.hpp"
#include "gui/panels/gui_ir_browser.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/image.hpp"

static void DrawBlurredBackground(GuiState& g,
                                  Rect r,
                                  Rect clipped_to,
                                  sample_lib::LibraryIdRef library_id,
                                  f32 opacity) {
    auto const panel_rounding = LivePx(UiSizeId::BlurredPanelRounding);

    if (!prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::HighContrastGui))) {
        auto imgs = GetLibraryImages(g.library_images,
                                     g.imgui,
                                     library_id,
                                     g.shared_engine_systems.sample_library_server,
                                     LibraryImagesTypes::Backgrounds);

        if (imgs.blurred_background) {
            if (auto tex = GuiIo().in.renderer->GetTextureFromImage(imgs.blurred_background)) {
                // In order to align this blurred image with the overall blurred image, we consider the bounds
                // of the current viewport (which we assume to contain the overall image).
                auto const whole_panel_bounds = g.imgui.curr_viewport->bounds;
                auto const whole_uv =
                    GetMaxUVToMaintainAspectRatio(*imgs.blurred_background, whole_panel_bounds.size);

                auto const left_margin = r.x - whole_panel_bounds.x;
                auto const top_margin = r.y - whole_panel_bounds.y;

                f32x2 min_uv = {whole_uv.x * (left_margin / whole_panel_bounds.w),
                                whole_uv.y * (top_margin / whole_panel_bounds.h)};
                f32x2 max_uv = {whole_uv.x * (r.w + left_margin) / whole_panel_bounds.w,
                                whole_uv.y * (r.h + top_margin) / whole_panel_bounds.h};

                g.imgui.draw_list->PushClipRect(clipped_to.Min(), clipped_to.Max());
                DEFER { g.imgui.draw_list->PopClipRect(); };

                auto const image_draw_colour = ToU32({
                    .a = (u8)(opacity * 255),
                    .b = 255,
                    .g = 255,
                    .r = 255,
                });

                g.imgui.draw_list->AddImageRounded(*tex,
                                                   r.Min(),
                                                   r.Max(),
                                                   min_uv,
                                                   max_uv,
                                                   image_draw_colour,
                                                   panel_rounding);
                return;
            }
        }
    }

    g.imgui.draw_list->AddRectFilled(r, LiveCol(UiColMap::MidViewportSurface), panel_rounding);
}

static void DoOverlayGradient(imgui::Context const& imgui, Rect r) {
    auto const panel_rounding = LivePx(UiSizeId::BlurredPanelRounding);

    auto const vtx_idx_0 = imgui.draw_list->vtx_buffer.size;
    auto const pos = r.Min() + f32x2 {1, 1};
    auto const size = f32x2 {r.w, r.h / 2} - f32x2 {2, 2};
    imgui.draw_list->AddRectFilled(pos, pos + size, 0xffffffff, panel_rounding);
    auto const vtx_idx_1 = imgui.draw_list->vtx_buffer.size;
    imgui.draw_list->AddRectFilled(pos, pos + size, 0xffffffff, panel_rounding);
    auto const vtx_idx_2 = imgui.draw_list->vtx_buffer.size;

    auto const col_value =
        (u8)(Clamp01(LiveRaw(UiSizeId::BackgroundBlurringOverlayGradientColour1) / 100.0f) * 255);
    auto const col = ToU32({
        .a = (u8)(Clamp01(LiveRaw(UiSizeId::BackgroundBlurringOverlayGradientOpacity1) / 100.0f) * 255),
        .b = col_value,
        .g = col_value,
        .r = col_value,
    });

    DrawList::ShadeVertsLinearColorGradientSetAlpha(imgui.draw_list,
                                                    vtx_idx_0,
                                                    vtx_idx_1,
                                                    pos,
                                                    pos + f32x2 {0, size.y},
                                                    col,
                                                    0);
    DrawList::ShadeVertsLinearColorGradientSetAlpha(imgui.draw_list,
                                                    vtx_idx_1,
                                                    vtx_idx_2,
                                                    pos + f32x2 {size.x, 0},
                                                    pos + f32x2 {size.x, size.y},
                                                    col,
                                                    0);
}

static f32 RoundUpToNearestMultiple(f32 value, f32 multiple) { return multiple * Ceil(value / multiple); }

static void DrawMidPanelOverallBackground(GuiState& g, imgui::Context const& imgui) {
    auto const r = imgui.curr_viewport->unpadded_bounds;

    imgui.draw_list->AddRectFilled(r, LiveCol(UiColMap::MidViewportBackground));

    if (!prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::HighContrastGui))) {
        auto overall_library = LibraryForOverallBackground(g.engine);
        if (overall_library) {
            auto imgs = GetLibraryImages(g.library_images,
                                         g.imgui,
                                         *overall_library,
                                         g.shared_engine_systems.sample_library_server,
                                         LibraryImagesTypes::Backgrounds);
            if (imgs.background) {
                auto tex = GuiIo().in.renderer->GetTextureFromImage(*imgs.background);
                if (tex) {
                    imgui.draw_list->AddImageRect(*tex,
                                                  r,
                                                  {0, 0},
                                                  GetMaxUVToMaintainAspectRatio(*imgs.background, r.size));
                }
            }
        }
    }
}

static void DrawLayersContainerBackground(GuiState& g, Rect r) {
    auto const mid_panel_title_height = LivePx(UiSizeId::MidPanelTitleHeight);
    auto const panel_rounding = LivePx(UiSizeId::BlurredPanelRounding);

    auto const layer_width_without_pad = RoundUpToNearestMultiple(r.w, k_num_layers) / k_num_layers;

    auto const overall_lib = LibraryForOverallBackground(g.engine);
    if (overall_lib)
        DrawBlurredBackground(g,
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
            DrawBlurredBackground(g, r, layer_r, *lib_id, layer_opacity);
        }
    }

    if (!prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::HighContrastGui)))
        DoOverlayGradient(g.imgui, r);

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
        DrawBlurredBackground(g,
                              r,
                              r,
                              *overall_lib,
                              Clamp01(LiveRaw(UiSizeId::BackgroundBlurringOpacity1) / 100.0f));

    DoOverlayGradient(g.imgui, r);

    g.imgui.draw_list->AddRect(r, LiveCol(UiColMap::MidViewportSurfaceBorder), panel_rounding);

    g.imgui.draw_list->AddLine({r.x, r.y + mid_panel_title_height},
                               {r.Right(), r.y + mid_panel_title_height},
                               LiveCol(UiColMap::MidViewportDivider));
}

static Box DoTitleShuffleButton(GuiBuilder& builder, Box parent, String tooltip) {
    return DoBox(builder,
                 {
                     .parent = parent,
                     .text = ICON_FA_SHUFFLE,
                     .size_from_text = true,
                     .font = FontType::Icons,
                     .font_size = k_font_icons_size * 0.82f,
                     .text_colours =
                         ColSet {
                             .base = LiveColStruct(UiColMap::MidIcon),
                             .hot = LiveColStruct(UiColMap::MidTextHot),
                             .active = LiveColStruct(UiColMap::MidTextOn),
                         },
                     .text_justification = TextJustification::Centred,
                     .tooltip = tooltip,
                     .button_behaviour = imgui::ButtonConfig {},
                 });
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

    auto const rand_btn =
        DoTitleShuffleButton(builder, title_row, "Load random instruments for all 3 layers"_s);

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

    for (auto const i : Range(k_num_layers)) {
        if (auto const r = BoxRect(builder,
                                   DoBox(builder,
                                         {
                                             .parent = layers_row,
                                             .id_extra = (u64)i,
                                             .layout {
                                                 .size = {layout::k_fill_parent, layout::k_fill_parent},
                                             },
                                         }))) {
            // TODO: this will be replaced by a new layer_gui that use the GuiBuilder rather than us having to
            // do layout stuff here.
            layer_gui::LayerLayoutTempIDs ids {};
            layer_gui::Layout(g, &g.engine.Layer(i), ids, &g.layer_gui[i], r->w, r->h);
            layout::RunContext(g.layout);
            layer_gui::Draw(g, frame_context, *r, &g.engine.Layer(i), ids, &g.layer_gui[i]);
            layout::ResetContext(g.layout);
        }
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

    auto const rand_btn = DoTitleShuffleButton(builder, title_row, "Randomise all of the effects"_s);

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

    if (auto const r = BoxRect(builder, effects_box)) DoEffectsViewport(g, frame_context, *r);
}

void MidPanel(GuiState& g, Rect bounds, GuiFrameContext const& frame_context) {
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
