// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui_framework/gui_builder.hpp"

static void LoadingOverlayPanel(GuiBuilder& builder) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_padding = {.lrtb = k_default_spacing},
                                },
                            });
    DoBox(builder,
          {
              .parent = root,
              .text = "Loading…",
              .size_from_text = true,
              .font = FontType::Heading1,
          });
}

PUBLIC void DoLoadingOverlay(GuiBuilder& builder, bool is_loading) {
    if (!is_loading) return;

    auto viewport_config = k_default_modal_viewport;
    viewport_config.draw_background = DrawOverlayViewportBackground;
    viewport_config.mode = imgui::ViewportMode::Floating;
    viewport_config.positioning = imgui::ViewportPositioning::WindowCentred;
    viewport_config.close_on_click_outside = false;
    viewport_config.close_on_escape = false;
    viewport_config.exclusive_focus = false;
    viewport_config.auto_size = true;
    viewport_config.scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never;

    constexpr imgui::Id k_loading_overlay_id = HashFnv1a("loading-overlay");

    DoBoxViewport(builder,
                  {
                      .run = [](GuiBuilder& b) { LoadingOverlayPanel(b); },
                      .bounds = Rect {},
                      .imgui_id = k_loading_overlay_id,
                      .viewport_config = viewport_config,
                      .debug_name = "loading-overlay",
                  });
}
