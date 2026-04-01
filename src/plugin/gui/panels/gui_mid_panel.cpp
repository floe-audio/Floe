// Copyright 2024-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_mid_panel.hpp"

#include "engine/engine_prefs.hpp"
#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/panels/gui_effects.hpp"
#include "gui/panels/gui_perform.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/image.hpp"

void DrawMidBlurredBackground(GuiState& g,
                              Rect r,
                              Rect clipped_to,
                              sample_lib::LibraryId library_id,
                              MidBlurredBackgroundOptions const& options) {
    auto const panel_rounding = WwToPixels(k_panel_rounding);

    if (!prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::HighContrastGui))) {
        auto imgs = GetLibraryImages(g.library_images,
                                     g.imgui,
                                     library_id,
                                     g.shared_engine_systems.sample_library_server,
                                     g.engine.instance_index,
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
                    .a = (u8)(options.opacity * 255),
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
                                                   panel_rounding,
                                                   options.rounding_corners);
                return;
            }
        }
    }

    g.imgui.draw_list->AddRectFilled(r,
                                     LiveCol(UiColMap::MidViewportSurface),
                                     panel_rounding,
                                     options.rounding_corners);
}

constexpr u32 k_vignette_colour = Rgba(0, 0, 0, 0.4f);
constexpr f32 k_vignette_inner_radius = 0.20f;
constexpr u32 k_vignette_num_bands = 16;
constexpr f32 k_vignette_panel_opacity = 0.05f;

void DrawMidBlurredPanelSurface(GuiState& g, Rect window_r, Optional<sample_lib::LibraryId> lib_id) {
    auto const panel_rounding = WwToPixels(k_panel_rounding);

    if (lib_id)
        DrawMidBlurredBackground(g, window_r, window_r, *lib_id, {});
    else
        g.imgui.draw_list->AddRectFilled(window_r, LiveCol(UiColMap::MidViewportSurface), panel_rounding);

    // Darken blurred panels on the perform page so they blend with the vignetted background
    if (g.mid_panel_state.tab == MidPanelTab::Perform) {
        g.imgui.draw_list->AddRectFilled(window_r,
                                         ChangeAlpha(k_vignette_colour, k_vignette_panel_opacity),
                                         panel_rounding);
    }

    g.imgui.draw_list->AddRect(window_r, LiveCol(UiColMap::MidViewportSurfaceBorder), panel_rounding);
}

void DrawMidPanelBackgroundImage(GuiState& g, sample_lib::LibraryId library_id) {
    auto const r = g.imgui.curr_viewport->unpadded_bounds;

    g.imgui.draw_list->AddRectFilled(r, LiveCol(UiColMap::MidViewportBackground));

    if (!prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::HighContrastGui))) {
        auto imgs = GetLibraryImages(g.library_images,
                                     g.imgui,
                                     library_id,
                                     g.shared_engine_systems.sample_library_server,
                                     g.engine.instance_index,
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
        case MidPanelTab::Perform: return "PERFORM"_s;
        case MidPanelTab::Layers: return "LAYERS"_s;
        case MidPanelTab::Effects: return "EFFECTS"_s;
        case MidPanelTab::Count: PanicIfReached();
    }
}

static Optional<sample_lib::LibraryId> LibIdForCurrentTab(GuiState& g) {
    switch (g.mid_panel_state.tab) {
        case MidPanelTab::Perform: return LibraryForOverallBackground(g.engine);
        case MidPanelTab::Layers: return LibraryForOverallBackground(g.engine);
        case MidPanelTab::Effects: return LibraryForOverallBackground(g.engine);
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

    if (g.mid_panel_state.tab == MidPanelTab::Perform) {
        imgui.draw_list->AddRectFilled(r, Rgba(0, 0, 0, 0.0f));
        imgui.draw_list->AddVignetteRect(r, k_vignette_colour, k_vignette_inner_radius, k_vignette_num_bands);
    }
}

struct MidPanelTabBarResult {
    Box tab_extra_buttons_box;
};

static MidPanelTabBarResult DoMidPanelTabBar(GuiBuilder& builder, GuiState& g, Box parent) {
    // Full-width row: [left spacer] [centred tabs] [right extras area]
    auto const tab_row = DoBox(builder,
                               {
                                   .parent = parent,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .margins = {.t = 5},
                                       .contents_direction = layout::Direction::Row,
                                       .contents_align = layout::Alignment::Middle,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

    // Left spacer to balance the right extras area, keeping tabs centred
    DoBox(builder,
          {
              .parent = tab_row,
              .layout {
                  .size = {layout::k_fill_parent, 1},
              },
          });

    // Centred tab buttons container
    auto const tab_bar = DoBox(builder,
                               {
                                   .parent = tab_row,
                                   .layout {
                                       .size = layout::k_hug_contents,
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

    for (auto const tab : EnumIterator<MidPanelTab>()) {
        auto const btn = DoTabButton(builder,
                                     tab_bar,
                                     MidPanelTabLabel(tab),
                                     {.is_selected = tab == g.mid_panel_state.tab},
                                     (u64)tab);

        if (btn.button_fired) new_tab = tab;
    }

    if (new_tab) g.mid_panel_state.tab = *new_tab;

    // Right extras area for page-specific buttons (e.g. randomise)
    auto const extras = DoBox(builder,
                              {
                                  .parent = tab_row,
                                  .layout {
                                      .size = {layout::k_fill_parent, layout::k_fill_parent},
                                      .contents_padding = {.r = 8},
                                      .contents_direction = layout::Direction::Row,
                                      .contents_align = layout::Alignment::End,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                  },
                              });

    return {extras};
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

                    // Auto-switch mid-panel tab when a macro destination knob is being
                    // interacted with
                    if (g.macros_gui_state.active_destination_knob) {
                        auto const param_index = g.macros_gui_state.active_destination_knob->dest.param_index;
                        if (param_index) {
                            auto const& k_desc = ParamDescriptorAt(*param_index);
                            Optional<MidPanelTab> new_tab {};
                            if (k_desc.IsEffectParam())
                                new_tab = MidPanelTab::Effects;
                            else if (k_desc.IsLayerParam())
                                new_tab = MidPanelTab::Layers;

                            if (new_tab && *new_tab != g.mid_panel_state.tab) {
                                g.mid_panel_state.tab = *new_tab;
                                GuiIo().out.IncreaseUpdateInterval(
                                    GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
                            }
                        }
                    }

                    auto const current_tab = g.mid_panel_state.tab;

                    auto const tab_extra_buttons_box =
                        DoMidPanelTabBar(builder, g, root).tab_extra_buttons_box;

                    auto const content = DoBox(builder,
                                               {
                                                   .parent = root,
                                                   .layout {
                                                       .size = layout::k_fill_parent,
                                                   },
                                               });

                    switch (current_tab) {
                        case MidPanelTab::Perform:
                            MidPanelPerformContent(builder, g, frame_context, content, tab_extra_buttons_box);
                            break;
                        case MidPanelTab::Layers:
                            MidPanelLayersContent(builder, g, frame_context, content, tab_extra_buttons_box);
                            break;
                        case MidPanelTab::Effects:
                            MidPanelEffectsContent(builder, g, frame_context, content, tab_extra_buttons_box);
                            break;
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
