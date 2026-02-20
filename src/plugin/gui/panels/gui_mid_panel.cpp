// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui2_constants.hpp"
#include "gui/elements/gui_drawing_helpers.hpp"
#include "gui/panels/gui2_inst_browser.hpp"
#include "gui/panels/gui2_ir_browser.hpp"
#include "gui/panels/gui_effects.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/image.hpp"

static void DoBlurredBackground(GuiState& g,
                                Rect r,
                                Rect clipped_to,
                                imgui::Viewport* viewport,
                                sample_lib::LibraryIdRef library_id,
                                f32x2 mid_panel_size,
                                f32 opacity) {
    auto const panel_rounding = LiveSize(UiSizeId::BlurredPanelRounding);

    if (!prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::HighContrastGui))) {
        auto imgs = GetLibraryImages(g.library_images,
                                     g.imgui,
                                     library_id,
                                     g.shared_engine_systems.sample_library_server,
                                     LibraryImagesTypes::Backgrounds);

        if (imgs.blurred_background) {
            if (auto tex = GuiIo().in.renderer->GetTextureFromImage(imgs.blurred_background)) {
                auto const whole_uv = GetMaxUVToMaintainAspectRatio(*imgs.blurred_background, mid_panel_size);
                auto const left_margin = r.x - viewport->parent_viewport->bounds.x;
                auto const top_margin = r.y - viewport->parent_viewport->bounds.y;

                f32x2 min_uv = {whole_uv.x * (left_margin / mid_panel_size.x),
                                whole_uv.y * (top_margin / mid_panel_size.y)};
                f32x2 max_uv = {whole_uv.x * (r.w + left_margin) / mid_panel_size.x,
                                whole_uv.y * (r.h + top_margin) / mid_panel_size.y};

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

static void DoOverlayGradient(GuiState& g, Rect r) {
    auto& imgui = g.imgui;
    auto const panel_rounding = LiveSize(UiSizeId::BlurredPanelRounding);

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

// Reduces chance of floating point errors.
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

static void DrawLayersContainerBackground(GuiState& g,
                                          imgui::Context const& imgui,
                                          f32x2 mid_panel_size,
                                          f32 mid_panel_title_height) {
    auto const panel_rounding = LiveSize(UiSizeId::BlurredPanelRounding);
    auto const viewport = imgui.curr_viewport;
    auto const& r = viewport->bounds;

    auto const layer_width_without_pad = RoundUpToNearestMultiple(r.w, k_num_layers) / k_num_layers;

    auto const overall_lib = LibraryForOverallBackground(g.engine);
    if (overall_lib)
        DoBlurredBackground(g,
                            r,
                            r,
                            viewport,
                            *overall_lib,
                            mid_panel_size,
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
            DoBlurredBackground(g, r, layer_r, viewport, *lib_id, mid_panel_size, layer_opacity);
        }
    }

    if (!prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::HighContrastGui))) DoOverlayGradient(g, r);

    imgui.draw_list->AddRect(r, LiveCol(UiColMap::MidViewportSurfaceBorder), panel_rounding);

    imgui.draw_list->AddLine({r.x, r.y + mid_panel_title_height},
                             {r.Right(), r.y + mid_panel_title_height},
                             LiveCol(UiColMap::MidViewportDivider));
    for (u32 i = 1; i < k_num_layers; ++i) {
        auto const x_pos = r.x + ((f32)i * layer_width_without_pad) - 1;
        imgui.draw_list->AddLine({x_pos, r.y + mid_panel_title_height},
                                 {x_pos, r.Bottom()},
                                 LiveCol(UiColMap::MidViewportDivider));
    }
}

static void DrawEffectsContainerBackground(GuiState& g,
                                           imgui::Context const& imgui,
                                           f32x2 mid_panel_size,
                                           f32 mid_panel_title_height) {
    auto const panel_rounding = LiveSize(UiSizeId::BlurredPanelRounding);
    auto const& r = imgui.curr_viewport->bounds;

    auto const overall_lib = LibraryForOverallBackground(g.engine);
    if (overall_lib)
        DoBlurredBackground(g,
                            r,
                            r,
                            imgui.curr_viewport,
                            *overall_lib,
                            mid_panel_size,
                            Clamp01(LiveRaw(UiSizeId::BackgroundBlurringOpacity1) / 100.0f));

    DoOverlayGradient(g, r);

    imgui.draw_list->AddRect(r, LiveCol(UiColMap::MidViewportSurfaceBorder), panel_rounding);

    imgui.draw_list->AddLine({r.x, r.y + mid_panel_title_height},
                             {r.Right(), r.y + mid_panel_title_height},
                             LiveCol(UiColMap::MidViewportDivider));
}

static Box DoTitleShuffleButton(GuiBuilder& builder, Box parent, f32 width, f32 margin_r, String tooltip) {
    return DoBox(builder,
                 {
                     .parent = parent,
                     .text = ICON_FA_SHUFFLE,
                     .font = FontType::Icons,
                     .font_size = k_font_icons_size * 0.82f,
                     .text_colours =
                         ColSet {
                             .base = LiveColStruct(UiColMap::MidIcon),
                             .hot = LiveColStruct(UiColMap::MidTextHot),
                             .active = LiveColStruct(UiColMap::MidTextOn),
                         },
                     .text_justification = TextJustification::Centred,
                     .layout {
                         .size = {width, layout::k_fill_parent},
                         .margins = {.r = margin_r},
                     },
                     .tooltip = tooltip,
                     .button_behaviour = imgui::ButtonConfig {},
                 });
}

static void DoLayersContainer(GuiBuilder& builder,
                              GuiState& g,
                              GuiFrameContext const& frame_context,
                              f32 mid_panel_title_height) {
    auto const ww = [](f32 v) { return GuiIo().PixelsToWw(v); };
    auto const title_height_ww = ww(mid_panel_title_height);
    auto const title_margin_ww = ww(LiveSize(UiSizeId::MidPanelTitleMarginLeft));
    auto const rand_btn_width_ww = ww(LiveSize(UiSizeId::ResourceSelectorRandomButtonW));
    auto& engine = g.engine;
    auto& lay = g.layout;

    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = GuiIo().PixelsToWw(builder.imgui.CurrentVpSize()),
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Title row
    auto const title_row = DoBox(builder,
                                 {
                                     .parent = root,
                                     .layout {
                                         .size = {layout::k_fill_parent, title_height_ww},
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
              .layout {
                  .margins = {.l = title_margin_ww},
              },
          });

    auto const rand_btn = DoTitleShuffleButton(builder,
                                               title_row,
                                               rand_btn_width_ww,
                                               title_margin_ww,
                                               "Load random instruments for all 3 layers"_s);

    if (rand_btn.button_fired) {
        for (auto& layer : engine.processor.layer_processors) {
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

    Box layer_boxes[k_num_layers];
    for (auto const i : Range(k_num_layers)) {
        layer_boxes[i] = DoBox(builder,
                               {
                                   .parent = layers_row,
                                   .id_extra = (u64)i,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_fill_parent},
                                   },
                               });
    }

    for (auto const i : Range(k_num_layers)) {
        if (auto const r = BoxRect(builder, layer_boxes[i])) {
            layer_gui::LayerLayoutTempIDs ids {};
            layer_gui::Layout(g, &engine.Layer(i), ids, &g.layer_gui[i], r->w, r->h);
            layout::RunContext(lay);
            layer_gui::Draw(g, frame_context, *r, &engine.Layer(i), ids, &g.layer_gui[i]);
            layout::ResetContext(lay);
        }
    }
}

static void DoEffectsContainer(GuiBuilder& builder,
                               GuiState& g,
                               GuiFrameContext const& frame_context,
                               f32 mid_panel_title_height) {
    auto const ww = [](f32 v) { return GuiIo().PixelsToWw(v); };
    auto const title_height_ww = ww(mid_panel_title_height);
    auto const title_margin_ww = ww(LiveSize(UiSizeId::MidPanelTitleMarginLeft));
    auto const rand_btn_width_ww = ww(LiveSize(UiSizeId::ResourceSelectorRandomButtonW));
    auto& engine = g.engine;

    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = GuiIo().PixelsToWw(builder.imgui.CurrentVpSize()),
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Title row
    auto const title_row = DoBox(builder,
                                 {
                                     .parent = root,
                                     .layout {
                                         .size = {layout::k_fill_parent, title_height_ww},
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
              .layout {
                  .margins = {.l = title_margin_ww},
              },
          });

    auto const rand_btn = DoTitleShuffleButton(builder,
                                               title_row,
                                               rand_btn_width_ww,
                                               title_margin_ww,
                                               "Randomise all of the effects"_s);

    if (rand_btn.button_fired) {
        RandomiseAllEffectParameterValues(engine.processor);
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
    auto& imgui = g.imgui;
    auto& builder = g.builder;

    imgui.BeginViewport(
        {
            .draw_background = [&](imgui::Context const& imgui) { DrawMidPanelOverallBackground(g, imgui); },
            .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
        },
        bounds,
        "MidPanel");
    DEFER { imgui.EndViewport(); };

    auto const layer_width = RoundUpToNearestMultiple(LiveSize(UiSizeId::LayerWidth), k_num_layers);
    auto const total_layer_width = layer_width * k_num_layers;
    auto const mid_panel_title_height = LiveSize(UiSizeId::MidPanelTitleHeight);
    auto const mid_panel_size = imgui.CurrentVpSize();

    DoBoxViewport(
        builder,
        {
            .run =
                [&](GuiBuilder& builder) {
                    DoLayersContainer(builder, g, frame_context, mid_panel_title_height);
                },
            .bounds = Rect {.xywh {0, 0, total_layer_width, imgui.CurrentVpHeight()}},
            .imgui_id = imgui.MakeId("layers-container"),
            .viewport_config {
                .draw_background =
                    [&](imgui::Context const& imgui) {
                        DrawLayersContainerBackground(g, imgui, mid_panel_size, mid_panel_title_height);
                    },
                .padding =
                    {
                        .l = LiveSize(UiSizeId::LayersBoxMarginL),
                        .r = LiveSize(UiSizeId::LayersBoxMarginR),
                        .t = LiveSize(UiSizeId::LayersBoxMarginT),
                        .b = LiveSize(UiSizeId::LayersBoxMarginB),
                    },
                .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
            },
            .debug_name = "layers-container",
        });

    DoBoxViewport(
        builder,
        {
            .run =
                [&](GuiBuilder& builder) {
                    DoEffectsContainer(builder, g, frame_context, mid_panel_title_height);
                },
            .bounds = Rect {.xywh {total_layer_width,
                                   0,
                                   imgui.CurrentVpWidth() - total_layer_width,
                                   imgui.CurrentVpHeight()}},
            .imgui_id = imgui.MakeId("effects-container"),
            .viewport_config {
                .draw_background =
                    [&](imgui::Context const& imgui) {
                        DrawEffectsContainerBackground(g, imgui, mid_panel_size, mid_panel_title_height);
                    },
                .padding =
                    {
                        .l = LiveSize(UiSizeId::FXListMarginL),
                        .r = LiveSize(UiSizeId::FXListMarginR),
                        .t = LiveSize(UiSizeId::FXListMarginT),
                        .b = LiveSize(UiSizeId::FXListMarginB),
                    },
                .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
            },
            .debug_name = "effects-container",
        });
}
