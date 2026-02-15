// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <IconsFontAwesome6.h>

#include "gui_framework/gui_builder.hpp"

void DrawModalScrollbars(imgui::Context const& imgui, imgui::ViewportScrollbars const& bars);
void DrawModalViewportBackgroundWithFullscreenDim(imgui::Context const& imgui);
void DrawOverlayViewportBackground(imgui::Context const& imgui);

constexpr imgui::ViewportConfig k_default_modal_viewport {
    .mode = imgui::ViewportMode::Modal,
    .positioning = imgui::ViewportPositioning::WindowAbsolute,
    .draw_background = DrawModalViewportBackgroundWithFullscreenDim,
    .draw_scrollbars = DrawModalScrollbars,
    .scrollbar_padding = k_scrollbar_rhs_space,
    .scrollbar_width = k_scrollbar_width,
    .scrollbar_visibility = {imgui::ViewportScrollbarVisibility::Never,
                             imgui::ViewportScrollbarVisibility::Auto},
    .exclusive_focus = true,
    .close_on_click_outside = true,
    .close_on_escape = true,
};

constexpr imgui::ViewportConfig k_default_modal_subviewport {
    .draw_scrollbars = DrawModalScrollbars,
    .scrollbar_padding = k_scrollbar_rhs_space,
    .scrollbar_width = k_scrollbar_width,
    .scrollbar_visibility = {imgui::ViewportScrollbarVisibility::Never,
                             imgui::ViewportScrollbarVisibility::Auto},
};

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

// Creates the root container for a panel
Box DoModalRootBox(GuiBuilder& builder);

// Configuration structs for panel components
struct ModalHeaderConfig {
    Box parent;
    String title;
    bool* modeless {};
};

// Creates a standard panel header with title and close button
Box DoModalHeader(GuiBuilder& builder, ModalHeaderConfig const& config);

struct DividerOptions {
    f32 margin = 0;
    bool horizontal = 0;
    bool vertical = 0;
    bool subtle = 0;
};
Box DoModalDivider(GuiBuilder& builder,
                   Box parent,
                   DividerOptions options,
                   u64 id_extra = SourceLocationHash());

struct ModalTabConfig {
    Optional<String> icon;
    String text;
    u32 index;
};

struct ModalTabBarConfig {
    Box parent;
    Span<ModalTabConfig const> tabs;
    u32& current_tab_index;
};

// Creates a tab bar with configurable tabs
Box DoModalTabBar(GuiBuilder& builder, ModalTabBarConfig const& config);

struct ModalConfig {
    String title;
    bool* modeless {};
    Span<ModalTabConfig const> tabs;
    u32& current_tab_index;
};

// High-level function that creates a complete modal layout within an already open modal window.
Box DoModal(GuiBuilder& builder, ModalConfig const& config);

bool CheckboxButton(GuiBuilder& builder,
                    Box parent,
                    String text,
                    bool state,
                    TooltipString tooltip = k_nullopt,
                    u64 id_extra = SourceLocationHash());

struct TextButtonOptions {
    String text;
    TooltipString tooltip = k_nullopt;
    bool fill_x = false;
    bool disabled = false;
};

bool TextButton(GuiBuilder& builder,
                Box parent,
                TextButtonOptions const& options,
                u64 id_extra = SourceLocationHash());

Box IconButton(GuiBuilder& builder,
               Box parent,
               String icon,
               String tooltip,
               f32 font_size,
               f32x2 size,
               u64 id_extra = SourceLocationHash());

struct TextInputOptions {
    String text;
    TooltipString tooltip = k_nullopt;
    f32x2 size;
    bool border = true;
    bool background = true;
    bool multiline = false;
};

struct TextInputResult {
    Box box;
    Optional<imgui::TextInputResult> result;
};

TextInputResult TextInput(GuiBuilder& builder,
                          Box parent,
                          TextInputOptions const& options,
                          u64 id_extra = SourceLocationHash());

struct IntFieldOptions {
    String label;
    TooltipString tooltip = k_nullopt;
    f32 width;
    s64 value;
    FunctionRef<s64(s64 value)> constrainer;
};

Optional<s64> IntField(GuiBuilder& builder,
                       Box parent,
                       IntFieldOptions const& options,
                       u64 id_extra = SourceLocationHash());

struct MenuButtonOptions {
    String text;
    TooltipString tooltip = k_nullopt;
    f32 width = layout::k_hug_contents;
};

Box MenuButton(GuiBuilder& builder,
               Box parent,
               MenuButtonOptions const& options,
               u64 id_extra = SourceLocationHash());

struct MenuItemOptions {
    String text;
    TooltipString tooltip = k_nullopt;
    Optional<String> subtext;
    bool is_selected;
    bool close_on_click = true;
};

Box MenuItem(GuiBuilder& builder,
             Box parent,
             MenuItemOptions const& options,
             u64 id_extra = SourceLocationHash());
