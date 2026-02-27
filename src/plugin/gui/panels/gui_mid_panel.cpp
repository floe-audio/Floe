// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_mid_panel.hpp"

#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/panels/gui_layer_maximised.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/image.hpp"

void DrawMidBlurredBackground(GuiState& g,
                              Rect r,
                              Rect clipped_to,
                              sample_lib::LibraryIdRef library_id,
                              f32 opacity) {
    auto const panel_rounding = WwToPixels(k_panel_rounding);

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
    auto const panel_rounding = WwToPixels(k_panel_rounding);

    auto const vtx_idx_0 = imgui.draw_list->vtx_buffer.size;
    auto const pos = r.Min() + f32x2 {1, 1};
    auto const size = f32x2 {r.w, r.h / 2} - f32x2 {2, 2};
    imgui.draw_list->AddRectFilled(pos, pos + size, 0xffffffff, panel_rounding);
    auto const vtx_idx_1 = imgui.draw_list->vtx_buffer.size;
    imgui.draw_list->AddRectFilled(pos, pos + size, 0xffffffff, panel_rounding);
    auto const vtx_idx_2 = imgui.draw_list->vtx_buffer.size;

    constexpr f32 k_overlay_gradient_colour = 0.0f;
    constexpr f32 k_overlay_gradient_opacity = 0.0f;
    auto const col_value = (u8)(Clamp01(k_overlay_gradient_colour / 100.0f) * 255);
    auto const col = ToU32({
        .a = (u8)(Clamp01(k_overlay_gradient_opacity / 100.0f) * 255),
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

void DrawMidBlurredPanelSurface(GuiState& g, Rect window_r, Optional<sample_lib::LibraryIdRef> lib_id) {
    auto const panel_rounding = WwToPixels(k_panel_rounding);

    if (lib_id)
        DrawMidBlurredBackground(g,
                                 window_r,
                                 window_r,
                                 *lib_id,
                                 Clamp01(k_background_blurring_opacity / 100.0f));
    else
        g.imgui.draw_list->AddRectFilled(window_r, LiveCol(UiColMap::MidViewportSurface), panel_rounding);

    if (!prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::HighContrastGui)))
        DoMidOverlayGradient(g.imgui, window_r);

    g.imgui.draw_list->AddRect(window_r, LiveCol(UiColMap::MidViewportSurfaceBorder), panel_rounding);
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

static Optional<sample_lib::LibraryIdRef> LibIdForCurrentTab(GuiState& g) {
    switch (g.mid_panel_state.tab) {
        case MidPanelTab::All: return LibraryForOverallBackground(g.engine);
        case MidPanelTab::Layer1: return g.engine.Layer(0).LibId();
        case MidPanelTab::Layer2: return g.engine.Layer(1).LibId();
        case MidPanelTab::Layer3: return g.engine.Layer(2).LibId();
        case MidPanelTab::Effects:
        case MidPanelTab::Count: return {};
    }
}

static void DrawMidPanelBackground(GuiState& g, imgui::Context const& imgui) {
    auto const r = imgui.curr_viewport->unpadded_bounds;
    auto const lib_id = LibIdForCurrentTab(g);

    if (lib_id)
        DrawMidPanelBackgroundImage(g, *lib_id);
    else
        imgui.draw_list->AddRectFilled(r, LiveCol(UiColMap::MidViewportBackground));
}

[[maybe_unused]] static void DoMidPanelTabBar(GuiBuilder& builder, GuiState& g, Box parent) {
    auto const tab_bar = DoBox(builder,
                               {
                                   .parent = parent,
                                   .layout {
                                       .size = {layout::k_hug_contents, 28.0f},
                                       .margins = {.t = 5},
                                       .contents_padding = {.lr = 3, .tb = 3},
                                       .contents_gap = 2,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_align = layout::Alignment::Middle,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

    if (auto const r = BoxRect(builder, tab_bar))
        DrawMidBlurredPanelSurface(g, builder.imgui.ViewportRectToWindowRect(*r), LibIdForCurrentTab(g));

    Optional<MidPanelTab> new_tab {};

    auto const divider = [&](u64 id_extra) {
        DoBox(builder,
              {
                  .parent = tab_bar,
                  .id_extra = id_extra,
                  .background_fill_colours = LiveColStruct(UiColMap::MidViewportDivider),
                  .layout {
                      .size = {1, layout::k_fill_parent},
                      .margins = {.lr = 3},
                  },
              });
    };

    for (auto const i : Range(ToInt(MidPanelTab::Count))) {
        auto const tab = (MidPanelTab)i;
        bool const is_layer_tab = tab >= MidPanelTab::Layer1 && tab <= MidPanelTab::Layer3;

        if (tab == MidPanelTab::Layer1 || tab == MidPanelTab::Effects) divider((u64)tab);

        bool const is_selected = tab == g.mid_panel_state.tab;

        auto const btn =
            DoBox(builder,
                  {
                      .parent = tab_bar,
                      .id_extra = (u64)tab,
                      .background_fill_colours =
                          is_selected ? Colours {LiveColStruct(UiColMap::MidTabBackgroundActive)}
                                      : Colours {ColSet {
                                            .base = Col {.c = Col::None},
                                            .hot = LiveColStruct(UiColMap::MidTabBackgroundHot),
                                            .active = LiveColStruct(UiColMap::MidTabBackgroundActive),
                                        }},
                      .round_background_corners = 0b1111,
                      .corner_rounding = 4.0f,
                      .layout {
                          .size = {layout::k_hug_contents, layout::k_fill_parent},
                          .contents_padding = {.lr = 8},
                          .contents_direction = layout::Direction::Row,
                          .contents_align = layout::Alignment::Middle,
                          .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                      },
                      .button_behaviour = imgui::ButtonConfig {},
                  });

        if (is_layer_tab) {
            auto const layer_index = (u32)(ToInt(tab) - ToInt(MidPanelTab::Layer1));
            auto& layer_obj = g.engine.Layer(layer_index);
            if (layer_obj.instrument_id.tag == InstrumentType::Sampler) {
                auto sample_inst_id = layer_obj.instrument_id.Get<sample_lib::InstrumentId>();
                auto imgs = GetLibraryImages(g.library_images,
                                             g.imgui,
                                             sample_inst_id.library,
                                             g.shared_engine_systems.sample_library_server,
                                             LibraryImagesTypes::Icon);
                if (imgs.icon) {
                    if (auto icon_tex = GuiIo().in.renderer->GetTextureFromImage(*imgs.icon)) {
                        auto const icon_box = DoBox(builder,
                                                    {
                                                        .parent = btn,
                                                        .parent_dictates_hot_and_active = true,
                                                        .layout {
                                                            .size = {12, 12},
                                                            .margins = {.r = 2},
                                                        },
                                                    });
                        if (auto const r = BoxRect(builder, icon_box)) {
                            g.imgui.draw_list->AddImageRect(*icon_tex,
                                                            builder.imgui.ViewportRectToWindowRect(*r));
                        }
                    }
                }
            }
        }

        DoBox(builder,
              {
                  .parent = btn,
                  .text = MidPanelTabLabel(tab),
                  .size_from_text = true,
                  .font = FontType::Heading3,
                  .text_colours = is_selected ? Colours {LiveColStruct(UiColMap::MidTabTextActive)}
                                              : Colours {ColSet {
                                                    .base = LiveColStruct(UiColMap::MidTabText),
                                                    .hot = LiveColStruct(UiColMap::MidTabTextHot),
                                                    .active = LiveColStruct(UiColMap::MidTabTextActive),
                                                }},
                  .text_justification = TextJustification::Centred,
                  .parent_dictates_hot_and_active = true,
              });

        if (is_layer_tab) {
            auto const layer_index = (u32)(ToInt(tab) - ToInt(MidPanelTab::Layer1));
            auto const meter_box = DoBox(builder,
                                         {
                                             .parent = btn,
                                             .layout {
                                                 .size = {4, 12},
                                                 .margins = {.l = 3},
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
}

void MidPanel(GuiState& g, Rect bounds, GuiFrameContext const& frame_context) {
    DoBoxViewport(
        g.builder,
        {
            .run =
                [&](GuiBuilder& builder) {
                    auto const root = DoBox(builder,
                                            {
                                                .layout {
                                                    .size = layout::k_fill_parent,
                                                    .contents_direction = layout::Direction::Column,
                                                },
                                            });

                    auto const current_tab = g.mid_panel_state.tab;

                    if constexpr (EXPERIMENTAL_MID_PANEL_TABS) DoMidPanelTabBar(builder, g, root);

                    auto const content = DoBox(builder,
                                               {
                                                   .parent = root,
                                                   .layout {
                                                       .size = layout::k_fill_parent,
                                                   },
                                               });

                    switch (current_tab) {
                        case MidPanelTab::All:
                            MidPanelCombinedContent(builder, g, frame_context, content);
                            break;
                        case MidPanelTab::Layer1:
                            MidPanelSingleLayerContent(builder, g, frame_context, 0, content);
                            break;
                        case MidPanelTab::Layer2:
                            MidPanelSingleLayerContent(builder, g, frame_context, 1, content);
                            break;
                        case MidPanelTab::Layer3:
                            MidPanelSingleLayerContent(builder, g, frame_context, 2, content);
                            break;
                        case MidPanelTab::Effects:
                        case MidPanelTab::Count: break;
                    }
                },
            .bounds = bounds,
            .imgui_id = SourceLocationHash(),
            .viewport_config {
                .draw_background = [&](imgui::Context const& imgui) { DrawMidPanelBackground(g, imgui); },
                .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
            },
            .debug_name = "MidPanel",
        });
}
