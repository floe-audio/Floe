// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <IconsFontAwesome6.h>

#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_live_edit.hpp"

constexpr imgui::ViewportConfig k_default_popup_menu_viewport {
    .mode = imgui::ViewportMode::PopupMenu,
    .positioning = imgui::ViewportPositioning::AutoPosition,
    .draw_background = DrawOverlayViewportBackground,
    .draw_scrollbars = DrawModalScrollbars,
    .padding = {.lr = 1, .tb = k_panel_rounding},
    .scrollbar_padding = k_scrollbar_rhs_space,
    .scrollbar_width = k_scrollbar_width,
    .scroll_button_size = k_scroll_button_size,
    .auto_size = true,
};

struct MenuOpenButtonOptions {
    String text;
    TooltipString tooltip = k_nullopt;
    f32 width = layout::k_hug_contents;
    GuiStyleSystem style_system = GuiStyleSystem::Overlay;
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

struct MenuSubmenuItemOptions {
    String text;
    bool is_selected; // Marks the category with a dot, e.g. when the current value lives inside this submenu.
    TrivialFunctionRef<void(Box submenu_root)> do_submenu_items;
};

// A menu item that opens a nested flyout menu. The item shows as hot while its submenu is open.
// do_submenu_items is cloned into the frame arena: the submenu viewport runs after the current viewport's
// run function completes, so it must not capture by reference anything local to that function.
Box MenuSubmenuItem(GuiBuilder& builder,
                    Box parent,
                    MenuSubmenuItemOptions const& options,
                    u64 id_extra = SourceLocationHash());

// Horizontal divider sized for popup menus (with a small gap above and below).
Box MenuDivider(GuiBuilder& builder, Box parent, u64 id_extra = SourceLocationHash());

struct GuiState;

struct RightClickMenuOptions {
    imgui::Id button_id;
    imgui::Id popup_id;
    Rect interaction_r;
    Optional<Rect> popup_anchor_r {};
    TrivialFunctionRef<void(Box root)> do_menu_items;
};
void DoRightClickMenu(GuiState& g, RightClickMenuOptions const& options);

void DoRightClickMenu(GuiState& g,
                      Box const& box,
                      imgui::Id popup_id,
                      TrivialFunctionRef<void(Box root)> do_menu_items,
                      Optional<Rect> popup_anchor_r = {});
