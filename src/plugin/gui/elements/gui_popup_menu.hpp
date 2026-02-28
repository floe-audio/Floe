// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <IconsFontAwesome6.h>

#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui_framework/gui_builder.hpp"

constexpr imgui::ViewportConfig k_default_popup_menu_viewport {
    .mode = imgui::ViewportMode::PopupMenu,
    .positioning = imgui::ViewportPositioning::AutoPosition,
    .draw_background = DrawOverlayViewportBackground,
    .draw_scrollbars = DrawModalScrollbars,
    .padding = {.lr = 1, .tb = k_panel_rounding},
    .scrollbar_padding = k_scrollbar_rhs_space,
    .scrollbar_width = k_scrollbar_width,
    .auto_size = true,
};

struct MenuOpenButtonOptions {
    String text;
    TooltipString tooltip = k_nullopt;
    f32 width = layout::k_hug_contents;
};

Box MenuOpenButton(GuiBuilder& builder,
                   Box parent,
                   MenuOpenButtonOptions const& options,
                   u64 id_extra = SourceLocationHash());

struct MenuItemOptions {
    enum class Mode : u8 { Active, Dimmed, Disabled };
    String text;
    TooltipString tooltip = k_nullopt;
    Optional<String> subtext;
    bool is_selected;
    bool close_on_click = true;
    Mode mode {Mode::Active};
    bool no_icon_gap = false;
};

Box MenuItem(GuiBuilder& builder,
             Box parent,
             MenuItemOptions const& options,
             u64 id_extra = SourceLocationHash());
