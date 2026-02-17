// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "gui2_inst_browser.hpp"
#include "gui2_ir_browser.hpp"
#include "gui_effects.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/image.hpp"
#include "gui_library_images.hpp"
#include "gui_prefs.hpp"
#include "gui_state.hpp"
#include "gui_viewport_utils.hpp"
#include "old/gui_button_widgets.hpp"
#include "old/gui_widget_helpers.hpp"

// TODO: this needs code entirely adapting to use GuiBuilder

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

// reduces chance of floating point errors
static f32 RoundUpToNearestMultiple(f32 value, f32 multiple) { return multiple * Ceil(value / multiple); }

void MidPanel(GuiState& g, GuiFrameContext const& frame_context) {
    auto& imgui = g.imgui;
    auto& lay = g.layout;
    auto& engine = g.engine;

    auto const layer_width = RoundUpToNearestMultiple(LiveSize(UiSizeId::LayerWidth), k_num_layers);
    auto const total_layer_width = layer_width * k_num_layers;
    auto const mid_panel_title_height = LiveSize(UiSizeId::MidPanelTitleHeight);
    auto const mid_panel_size = imgui.CurrentVpSize();

    auto const panel_rounding = LiveSize(UiSizeId::BlurredPanelRounding);

    auto do_randomise_button = [&](String tooltip) {
        auto const margin = LiveSize(UiSizeId::MidPanelTitleMarginLeft);
        auto const size = LiveSize(UiSizeId::ResourceSelectorRandomButtonW);
        Rect const btn_r = {
            .xywh {imgui.CurrentVpWidth() - (size + margin), 0, size, mid_panel_title_height}};
        auto const id = imgui.MakeId("rand");
        if (buttons::Button(g,
                            id,
                            btn_r,
                            ICON_FA_SHUFFLE,
                            buttons::IconButton(imgui).WithRandomiseIconScaling()))
            return true;
        Tooltip(g, id, imgui.ViewportRectToWindowRect(btn_r), tooltip, {});
        return false;
    };

    {
        imgui.BeginViewport(
            ({
                auto conf = FloeStandardConfig(imgui, [&](imgui::Context const& imgui) {
                    auto const viewport = imgui.curr_viewport;

                    auto const& r = viewport->bounds;

                    auto const layer_width_without_pad =
                        RoundUpToNearestMultiple(r.w, k_num_layers) / k_num_layers;

                    auto const overall_lib = LibraryForOverallBackground(engine);
                    if (overall_lib)
                        DoBlurredBackground(g,
                                            r,
                                            r,
                                            viewport,
                                            *overall_lib,
                                            mid_panel_size,
                                            Clamp01(LiveRaw(UiSizeId::BackgroundBlurringOpacity1) / 100.0f));

                    auto const layer_opacity =
                        Clamp01(LiveRaw(UiSizeId::BackgroundBlurringOpacitySingleLayer1) / 100.0f);
                    for (auto const layer_index : Range(k_num_layers)) {
                        if (auto const lib_id = g.engine.Layer(layer_index).LibId(); lib_id) {
                            if (*lib_id == overall_lib) continue;
                            auto const layer_r =
                                Rect {.x = r.x + ((f32)layer_index * layer_width_without_pad),
                                      .y = r.y,
                                      .w = layer_width_without_pad,
                                      .h = r.h}
                                    .CutTop(mid_panel_title_height);
                            DoBlurredBackground(g,
                                                r,
                                                layer_r,
                                                viewport,
                                                *lib_id,
                                                mid_panel_size,
                                                layer_opacity);
                        }
                    }

                    if (!prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::HighContrastGui)))
                        DoOverlayGradient(g, r);

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
                });

                conf.padding = {
                    .l = LiveSize(UiSizeId::LayersBoxMarginL),
                    .r = LiveSize(UiSizeId::LayersBoxMarginR),
                    .t = LiveSize(UiSizeId::LayersBoxMarginT),
                    .b = LiveSize(UiSizeId::LayersBoxMarginB),
                };

                conf.scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never;

                conf;
            }),
            {.xywh {0, 0, total_layer_width, imgui.CurrentVpHeight()}},
            "layers-container");
        DEFER { imgui.EndViewport(); };

        // do the title
        {
            Rect title_r {.xywh {LiveSize(UiSizeId::MidPanelTitleMarginLeft),
                                 0,
                                 imgui.CurrentVpWidth(),
                                 mid_panel_title_height}};
            title_r = imgui.RegisterAndConvertRect(title_r);
            imgui.draw_list->AddTextInRect(title_r,
                                           LiveCol(UiColMap::MidText),
                                           "Layers",
                                           {.justification = TextJustification::CentredLeft});
        }

        // randomise button
        if (do_randomise_button("Load random instruments for all 3 layers")) {
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

        // do the 3 panels
        auto const layer_width_minus_pad = imgui.CurrentVpWidth() / 3;
        auto const layer_height = imgui.CurrentVpHeight() - mid_panel_title_height;
        for (auto const i : Range(k_num_layers)) {
            layer_gui::LayerLayoutTempIDs ids {};
            layer_gui::Layout(g, &engine.Layer(i), ids, &g.layer_gui[i], layer_width_minus_pad, layer_height);
            layout::RunContext(lay);

            layer_gui::Draw(g,
                            frame_context,
                            {.xywh {(f32)i * layer_width_minus_pad,
                                    mid_panel_title_height,
                                    layer_width_minus_pad,
                                    layer_height}},
                            &engine.Layer(i),
                            ids,
                            &g.layer_gui[i]);
            layout::ResetContext(lay);
        }
    }

    {

        imgui.BeginViewport(
            ({
                auto conf = FloeStandardConfig(imgui, [&](imgui::Context const& imgui) {
                    auto const& r = imgui.curr_viewport->bounds;

                    auto const overall_lib = LibraryForOverallBackground(engine);
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
                });
                conf.padding = {
                    .l = LiveSize(UiSizeId::FXListMarginL),
                    .r = LiveSize(UiSizeId::FXListMarginR),
                    .t = LiveSize(UiSizeId::FXListMarginT),
                    .b = LiveSize(UiSizeId::FXListMarginB),
                };
                conf.scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never;
                conf;
            }),
            {.xywh {total_layer_width,
                    0,
                    imgui.CurrentVpWidth() - total_layer_width,
                    imgui.CurrentVpHeight()}},
            "effects-container");
        DEFER { imgui.EndViewport(); };

        // do the title
        {
            Rect title_r {.xywh {LiveSize(UiSizeId::MidPanelTitleMarginLeft),
                                 0,
                                 imgui.CurrentVpWidth(),
                                 mid_panel_title_height}};
            title_r = imgui.RegisterAndConvertRect(title_r);
            imgui.draw_list->AddTextInRect(title_r,
                                           LiveCol(UiColMap::MidText),
                                           "Effects",
                                           {.justification = TextJustification::CentredLeft});
        }

        // randomise button
        if (do_randomise_button("Randomise all of the effects")) {
            RandomiseAllEffectParameterValues(engine.processor);
            {
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
        }

        DoEffectsViewport(g,
                          frame_context,
                          {.xywh {0,
                                  mid_panel_title_height,
                                  imgui.CurrentVpWidth(),
                                  imgui.CurrentVpHeight() - mid_panel_title_height}});
    }
}
