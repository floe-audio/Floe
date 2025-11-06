// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "gui.hpp"
#include "gui/gui2_inst_browser.hpp"
#include "gui/gui2_ir_browser.hpp"
#include "gui/gui_button_widgets.hpp"
#include "gui_effects.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/image.hpp"
#include "gui_prefs.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"

static void DoBlurredBackground(Gui* g,
                                Rect r,
                                Rect clipped_to,
                                imgui::Window* window,
                                sample_lib::LibraryIdRef library_id,
                                f32x2 mid_panel_size,
                                f32 opacity) {
    if (prefs::GetBool(g->prefs, SettingDescriptor(GuiSetting::HighContrastGui))) return;
    auto& imgui = g->imgui;
    auto const panel_rounding = LiveSize(imgui, UiSizeId::BlurredPanelRounding);

    auto imgs = LibraryImagesFromLibraryId(g, library_id, LibraryImagesTypes::Backgrounds);

    if (imgs.blurred_background) {
        if (auto tex = g->frame_input.graphics_ctx->GetTextureFromImage(imgs.blurred_background)) {
            auto const whole_uv = GetMaxUVToMaintainAspectRatio(*imgs.background, mid_panel_size);
            auto const left_margin = r.x - window->parent_window->bounds.x;
            auto const top_margin = r.y - window->parent_window->bounds.y;

            f32x2 min_uv = {whole_uv.x * (left_margin / mid_panel_size.x),
                            whole_uv.y * (top_margin / mid_panel_size.y)};
            f32x2 max_uv = {whole_uv.x * (r.w + left_margin) / mid_panel_size.x,
                            whole_uv.y * (r.h + top_margin) / mid_panel_size.y};

            imgui.graphics->PushClipRect(clipped_to.Min(), clipped_to.Max());
            DEFER { imgui.graphics->PopClipRect(); };

            auto const image_draw_colour = colours::ToU32({
                .a = (u8)(opacity * 255),
                .b = 255,
                .g = 255,
                .r = 255,
            });

            imgui.graphics
                ->AddImageRounded(*tex, r.Min(), r.Max(), min_uv, max_uv, image_draw_colour, panel_rounding);
        } else {
            imgui.graphics->AddRectFilled(r.Min(),
                                          r.Max(),
                                          LiveCol(imgui, UiColMap::BlurredImageFallback),
                                          panel_rounding);
        }
    }
}

static void DoOverlayGradient(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto const panel_rounding = LiveSize(imgui, UiSizeId::BlurredPanelRounding);

    int const vtx_idx_0 = imgui.graphics->vtx_buffer.size;
    auto const pos = r.Min() + f32x2 {1, 1};
    auto const size = f32x2 {r.w, r.h / 2} - f32x2 {2, 2};
    imgui.graphics->AddRectFilled(pos, pos + size, 0xffffffff, panel_rounding);
    int const vtx_idx_1 = imgui.graphics->vtx_buffer.size;
    imgui.graphics->AddRectFilled(pos, pos + size, 0xffffffff, panel_rounding);
    int const vtx_idx_2 = imgui.graphics->vtx_buffer.size;

    auto const col_value =
        (u8)(Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringOverlayGradientColour) / 100.0f) * 255);
    auto const col = colours::ToU32({
        .a =
            (u8)(Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringOverlayGradientOpacity) / 100.0f) * 255),
        .b = col_value,
        .g = col_value,
        .r = col_value,
    });

    graphics::DrawList::ShadeVertsLinearColorGradientSetAlpha(imgui.graphics,
                                                              vtx_idx_0,
                                                              vtx_idx_1,
                                                              pos,
                                                              pos + f32x2 {0, size.y},
                                                              col,
                                                              0);
    graphics::DrawList::ShadeVertsLinearColorGradientSetAlpha(imgui.graphics,
                                                              vtx_idx_1,
                                                              vtx_idx_2,
                                                              pos + f32x2 {size.x, 0},
                                                              pos + f32x2 {size.x, size.y},
                                                              col,
                                                              0);
}

// reduces chance of floating point errors
static f32 RoundUpToNearestMultiple(f32 value, f32 multiple) { return multiple * Ceil(value / multiple); }

void MidPanel(Gui* g) {
    auto& imgui = g->imgui;
    auto& lay = g->layout;
    auto& engine = g->engine;

    auto const layer_width = RoundUpToNearestMultiple(LiveSize(imgui, UiSizeId::LayerWidth), k_num_layers);
    auto const total_layer_width = layer_width * k_num_layers;
    auto const mid_panel_title_height = LiveSize(imgui, UiSizeId::MidPanelTitleHeight);
    auto const mid_panel_size = imgui.Size();

    auto const panel_rounding = LiveSize(imgui, UiSizeId::BlurredPanelRounding);

    auto do_randomise_button = [&](String tooltip) {
        auto const margin = LiveSize(imgui, UiSizeId::MidPanelTitleMarginLeft);
        auto const size = LiveSize(imgui, UiSizeId::ResourceSelectorRandomButtonW);
        Rect const btn_r = {.xywh {imgui.Width() - (size + margin), 0, size, mid_panel_title_height}};
        auto const id = imgui.GetID("rand");
        if (buttons::Button(g,
                            id,
                            btn_r,
                            ICON_FA_SHUFFLE,
                            buttons::IconButton(imgui).WithRandomiseIconScaling()))
            return true;
        Tooltip(g, id, btn_r, tooltip);
        return false;
    };

    {
        auto settings = FloeWindowSettings(imgui, [&](IMGUI_DRAW_WINDOW_BG_ARGS) {
            auto const& r = window->bounds;

            auto const layer_width_without_pad = RoundUpToNearestMultiple(r.w, k_num_layers) / k_num_layers;

            if (!prefs::GetBool(g->prefs, SettingDescriptor(GuiSetting::HighContrastGui))) {
                auto const overall_lib = LibraryForOverallBackground(engine);
                if (overall_lib)
                    DoBlurredBackground(
                        g,
                        r,
                        r,
                        window,
                        *overall_lib,
                        mid_panel_size,
                        Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringOpacity) / 100.0f));

                auto const layer_opacity =
                    Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringOpacitySingleLayer) / 100.0f);
                for (auto layer_index : Range(k_num_layers)) {
                    if (auto const lib_id = g->engine.Layer(layer_index).LibId(); lib_id) {
                        if (*lib_id == overall_lib) continue;
                        auto const layer_r = Rect {.x = r.x + ((f32)layer_index * layer_width_without_pad),
                                                   .y = r.y,
                                                   .w = layer_width_without_pad,
                                                   .h = r.h}
                                                 .CutTop(mid_panel_title_height);
                        DoBlurredBackground(g, r, layer_r, window, *lib_id, mid_panel_size, layer_opacity);
                    }
                }

                DoOverlayGradient(g, r);
            }

            imgui.graphics->AddRect(r.Min(),
                                    r.Max(),
                                    LiveCol(imgui, UiColMap::BlurredImageBorder),
                                    panel_rounding);

            imgui.graphics->AddLine({r.x, r.y + mid_panel_title_height},
                                    {r.Right(), r.y + mid_panel_title_height},
                                    LiveCol(imgui, UiColMap::LayerDividerLine));
            for (u32 i = 1; i < k_num_layers; ++i) {
                auto const x_pos = r.x + ((f32)i * layer_width_without_pad) - 1;
                imgui.graphics->AddLine({x_pos, r.y + mid_panel_title_height},
                                        {x_pos, r.Bottom()},
                                        LiveCol(imgui, UiColMap::LayerDividerLine));
            }
        });

        settings.pad_top_left.x = LiveSize(imgui, UiSizeId::LayersBoxMarginL);
        settings.pad_top_left.y = LiveSize(imgui, UiSizeId::LayersBoxMarginT);
        settings.pad_bottom_right.x = LiveSize(imgui, UiSizeId::LayersBoxMarginR);
        settings.pad_bottom_right.y = LiveSize(imgui, UiSizeId::LayersBoxMarginB);
        imgui.BeginWindow(settings, {.xywh {0, 0, total_layer_width, imgui.Height()}}, "Layers");

        // do the title
        {
            Rect title_r {.xywh {LiveSize(imgui, UiSizeId::MidPanelTitleMarginLeft),
                                 0,
                                 imgui.Width(),
                                 mid_panel_title_height}};
            imgui.RegisterAndConvertRect(&title_r);
            imgui.graphics->AddTextJustified(title_r,
                                             "Layers",
                                             LiveCol(imgui, UiColMap::MidPanelTitleText),
                                             TextJustification::CentredLeft);
        }

        // randomise button
        if (do_randomise_button("Load random instruments for all 3 layers")) {
            for (auto& layer : engine.processor.layer_processors) {
                InstBrowserContext context {
                    .layer = layer,
                    .sample_library_server = g->shared_engine_systems.sample_library_server,
                    .library_images = g->library_images,
                    .engine = g->engine,
                    .prefs = g->prefs,
                    .unknown_library_icon = UnknownLibraryIcon(g),
                    .notifications = g->notifications,
                    .persistent_store = g->shared_engine_systems.persistent_store,
                    .confirmation_dialog_state = g->confirmation_dialog_state,
                };
                context.Init(g->scratch_arena);
                DEFER { context.Deinit(); };
                LoadRandomInstrument(context, g->inst_browser_state[layer.index]);
            }
        }

        // do the 3 panels
        auto const layer_width_minus_pad = imgui.Width() / 3;
        auto const layer_height = imgui.Height() - mid_panel_title_height;
        for (auto const i : Range(k_num_layers)) {
            layer_gui::LayerLayoutTempIDs ids {};
            layer_gui::Layout(g,
                              &engine.Layer(i),
                              ids,
                              &g->layer_gui[i],
                              layer_width_minus_pad,
                              layer_height);
            layout::RunContext(lay);

            layer_gui::Draw(g,
                            &engine,
                            {.xywh {(f32)i * layer_width_minus_pad,
                                    mid_panel_title_height,
                                    layer_width_minus_pad,
                                    layer_height}},
                            &engine.Layer(i),
                            ids,
                            &g->layer_gui[i]);
            layout::ResetContext(lay);
        }

        imgui.EndWindow();
    }

    {
        auto settings = FloeWindowSettings(imgui, [&](IMGUI_DRAW_WINDOW_BG_ARGS) {
            auto const& r = window->bounds;

            auto const overall_lib = LibraryForOverallBackground(engine);
            if (overall_lib)
                DoBlurredBackground(g,
                                    r,
                                    r,
                                    window,
                                    *overall_lib,
                                    mid_panel_size,
                                    Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringOpacity) / 100.0f));

            DoOverlayGradient(g, r);

            imgui.graphics->AddRect(r.Min(),
                                    r.Max(),
                                    LiveCol(imgui, UiColMap::BlurredImageBorder),
                                    panel_rounding);

            imgui.graphics->AddLine({r.x, r.y + mid_panel_title_height},
                                    {r.Right(), r.y + mid_panel_title_height},
                                    LiveCol(imgui, UiColMap::LayerDividerLine));
        });
        settings.pad_top_left.x = LiveSize(imgui, UiSizeId::FXListMarginL);
        settings.pad_top_left.y = LiveSize(imgui, UiSizeId::FXListMarginT);
        settings.pad_bottom_right.x = LiveSize(imgui, UiSizeId::FXListMarginR);
        settings.pad_bottom_right.y = LiveSize(imgui, UiSizeId::FXListMarginB);

        imgui.BeginWindow(settings,
                          {.xywh {total_layer_width, 0, imgui.Width() - total_layer_width, imgui.Height()}},
                          "EffectsContainer");

        // do the title
        {
            Rect title_r {.xywh {LiveSize(imgui, UiSizeId::MidPanelTitleMarginLeft),
                                 0,
                                 imgui.Width(),
                                 mid_panel_title_height}};
            imgui.RegisterAndConvertRect(&title_r);
            imgui.graphics->AddTextJustified(title_r,
                                             "Effects",
                                             LiveCol(imgui, UiColMap::MidPanelTitleText),
                                             TextJustification::CentredLeft);
        }

        // randomise button
        if (do_randomise_button("Randomise all of the effects")) {
            RandomiseAllEffectParameterValues(engine.processor);
            {
                IrBrowserContext ir_context {
                    .sample_library_server = g->shared_engine_systems.sample_library_server,
                    .library_images = g->library_images,
                    .engine = g->engine,
                    .prefs = g->prefs,
                    .unknown_library_icon = UnknownLibraryIcon(g),
                    .notifications = g->notifications,
                    .persistent_store = g->shared_engine_systems.persistent_store,
                    .confirmation_dialog_state = g->confirmation_dialog_state,
                };
                ir_context.Init(g->scratch_arena);
                DEFER { ir_context.Deinit(); };
                LoadRandomIr(ir_context, g->ir_browser_state);
            }
        }

        DoEffectsWindow(
            g,
            {.xywh {0, mid_panel_title_height, imgui.Width(), imgui.Height() - mid_panel_title_height}});

        imgui.EndWindow();
    }
}
