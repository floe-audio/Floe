// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <IconsFontAwesome6.h>

#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui_framework/gui_builder.hpp"

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
    bool horizontal = false;
    bool vertical = false;
    bool subtle = false;
    bool dark_mode = false;
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
                    Optional<String> text,
                    bool state,
                    TooltipString tooltip = k_nullopt,
                    GuiStyleSystem style = GuiStyleSystem::Overlay,
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
    GuiStyleSystem style = GuiStyleSystem::Overlay;
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
    GuiStyleSystem style = GuiStyleSystem::Overlay;
    bool midi_note_names = false;
    bool greyed_out = false;
};

Optional<s64> IntField(GuiBuilder& builder,
                       Box parent,
                       IntFieldOptions const& options,
                       u64 id_extra = SourceLocationHash());
