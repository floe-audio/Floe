// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_mid_panel.hpp"

#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/panels/gui_mid_panel_layer.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/image.hpp"

void DrawMidBlurredBackground(GuiState& g,
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

void DoMidOverlayGradient(imgui::Context const& imgui, Rect r) {
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

void DrawMidPanelBackgroundImage(GuiState& g, sample_lib::LibraryIdRef library_id) {
    auto const r = g.imgui.curr_viewport->unpadded_bounds;

    g.imgui.draw_list->AddRectFilled(r, LiveCol(UiColMap::MidViewportBackground));

    if (!prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::HighContrastGui))) {
        auto imgs = GetLibraryImages(g.library_images,
                                     g.imgui,
                                     library_id,
                                     g.shared_engine_systems.sample_library_server,
                                     LibraryImagesTypes::Backgrounds);
        if (imgs.background) {
            auto tex = GuiIo().in.renderer->GetTextureFromImage(*imgs.background);
            if (tex) {
                g.imgui.draw_list->AddImageRect(*tex,
                                                r,
                                                {0, 0},
                                                GetMaxUVToMaintainAspectRatio(*imgs.background, r.size));
            }
        }
    }
}

static String MidPanelTabLabel(MidPanelTab tab) {
    switch (tab) {
        case MidPanelTab::All: return "All"_s;
        case MidPanelTab::Layer1: return "L1"_s;
        case MidPanelTab::Layer2: return "L2"_s;
        case MidPanelTab::Layer3: return "L3"_s;
        case MidPanelTab::Effects: return "FX"_s;
        case MidPanelTab::Count: PanicIfReached();
    }
}

void MidPanelTabs(GuiState& g, Rect bounds) {
    DoBoxViewport(
        g.builder,
        {
            .run =
                [&](GuiBuilder& builder) {
                    auto const root =
                        DoBox(builder,
                              {
                                  .background_fill_colours = Col {.c = Col::Background1, .dark_mode = true},
                                  .layout {
                                      .size = layout::k_fill_parent,
                                      .contents_padding = {.lr = 3, .tb = 6},
                                      .contents_gap = 2,
                                      .contents_direction = layout::Direction::Column,
                                      .contents_align = layout::Alignment::Start,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                  },
                              });

                    Optional<MidPanelTab> new_tab {};

                    auto const divider = [&](u64 id_extra) {
                        DoBox(builder,
                              {
                                  .parent = root,
                                  .id_extra = id_extra,
                                  .background_fill_colours = LiveColStruct(UiColMap::MidViewportDivider),
                                  .layout {
                                      .size = {layout::k_fill_parent, 1},
                                      .margins = {.tb = 3},
                                  },
                              });
                    };

                    for (auto const i : Range(ToInt(MidPanelTab::Count))) {
                        auto const tab = (MidPanelTab)i;
                        bool const is_layer_tab =
                            tab >= MidPanelTab::Layer1 && tab <= MidPanelTab::Layer3;

                        if (tab == MidPanelTab::Layer1 || tab == MidPanelTab::Effects) divider((u64)tab);

                        bool const is_selected = tab == g.mid_panel_state.tab;

                        auto const btn =
                            DoBox(builder,
                                  {
                                      .parent = root,
                                      .id_extra = (u64)tab,
                                      .background_fill_colours = Col {.c = Col::None},
                                      .border_colours =
                                          is_selected ? Colours {LiveColStruct(UiColMap::MidTabBorderActive)}
                                                      : Colours {Col {.c = Col::None}},
                                      .round_background_corners = 0b1111,
                                      .corner_rounding = 4.0f,
                                      .layout {
                                          .size = {layout::k_fill_parent, 24},
                                          .contents_direction = layout::Direction::Row,
                                          .contents_align = layout::Alignment::Middle,
                                          .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                      },
                                      .button_behaviour = imgui::ButtonConfig {},
                                  });

                        if (auto const btn_r = BoxRect(builder, btn)) {
                            auto const window_r = g.imgui.ViewportRectToWindowRect(*btn_r);
                            auto const rounding = 4.0f;

                            Optional<sample_lib::LibraryIdRef> lib_id;
                            if (is_layer_tab) {
                                auto const layer_index =
                                    (u32)(ToInt(tab) - ToInt(MidPanelTab::Layer1));
                                lib_id = g.engine.Layer(layer_index).LibId();
                            } else {
                                lib_id = LibraryForOverallBackground(g.engine);
                            }

                            g.imgui.draw_list->PushClipRect(window_r.Min(), window_r.Max());

                            if (lib_id)
                                DrawMidBlurredBackground(g, window_r, window_r, *lib_id, 0.6f);
                            else
                                g.imgui.draw_list->AddRectFilled(
                                    window_r,
                                    LiveCol(UiColMap::MidViewportSurface),
                                    rounding);

                            if (btn.is_hot || btn.is_active) {
                                auto col = btn.is_active
                                               ? LiveCol(UiColMap::MidTabBackgroundActive)
                                               : LiveCol(UiColMap::MidTabBackgroundHot);
                                g.imgui.draw_list->AddRectFilled(window_r, col, rounding);
                            }

                            g.imgui.draw_list->PopClipRect();
                        }

                        DoBox(builder,
                              {
                                  .parent = btn,
                                  .text = MidPanelTabLabel(tab),
                                  .size_from_text = true,
                                  .font = FontType::Heading3,
                                  .text_colours =
                                      is_selected ? Colours {LiveColStruct(UiColMap::MidTabTextActive)}
                                                  : Colours {ColSet {
                                                        .base = LiveColStruct(UiColMap::MidTabText),
                                                        .hot = LiveColStruct(UiColMap::MidTabTextHot),
                                                        .active = LiveColStruct(UiColMap::MidTabTextActive),
                                                    }},
                                  .text_justification = TextJustification::Centred,
                                  .parent_dictates_hot_and_active = true,
                              });

                        if (is_layer_tab) {
                            auto const layer_index =
                                (u32)(ToInt(tab) - ToInt(MidPanelTab::Layer1));
                            auto const meter_box = DoBox(builder,
                                                         {
                                                             .parent = btn,
                                                             .layout {
                                                                 .size = {6, 12},
                                                                 .margins = {.l = 2},
                                                             },
                                                         });
                            if (auto const meter_r = BoxRect(builder, meter_box))
                                DrawPeakMeter(g.imgui,
                                              builder.imgui.RegisterAndConvertRect(*meter_r),
                                              g.engine.processor.layer_processors[layer_index].peak_meter,
                                              {
                                                  .flash_when_clipping = false,
                                                  .show_db_markers = false,
                                                  .gap = 1,
                                              });
                        }

                        if (btn.button_fired) new_tab = tab;
                    }

                    if (new_tab) g.mid_panel_state.tab = *new_tab;
                },
            .bounds = bounds,
            .imgui_id = SourceLocationHash(),
            .viewport_config {
                .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
            },
            .debug_name = "MidPanelTabs",
        });
}

void MidPanel(GuiState& g, Rect bounds, GuiFrameContext const& frame_context) {
    switch (g.mid_panel_state.tab) {
        case MidPanelTab::All: MidPanelCombined(g, bounds, frame_context); break;
        case MidPanelTab::Layer1: MidPanelSingleLayer(g, bounds, frame_context, 0); break;
        case MidPanelTab::Layer2: MidPanelSingleLayer(g, bounds, frame_context, 1); break;
        case MidPanelTab::Layer3: MidPanelSingleLayer(g, bounds, frame_context, 2); break;
        case MidPanelTab::Effects:
        case MidPanelTab::Count: break;
    }
}
