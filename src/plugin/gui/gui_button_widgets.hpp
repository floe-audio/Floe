// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <IconsFontAwesome6.h>

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui_framework/draw_list.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/layout.hpp"
#include "gui_fwd.hpp"

namespace buttons {

enum class LayoutAndSizeType {
    None,
    IconOrText,
    IconOrTextKeyboardIcon,
    IconAndText,
    IconAndTextMenuItem,
    IconAndTextSubMenuItem,
    IconAndTextMidiButton,
    IconAndTextLayerTab,
    IconAndTextInstSelector,
    VelocityButton
};

struct ColourSet {
    u32 reg {0};
    u32 on {0};
    u32 hot_on {0};
    u32 hot_off {0};
    u32 active_on {0};
    u32 active_off {0};
    u32 greyed_out {0};
    u32 greyed_out_on {0};
    bool grey_out_aware {false};
};

struct Style {
    static constexpr f32 k_regular_icon_scaling = 1.0f;
    static constexpr f32 k_large_icon_scaling = 1.0f;

    Style& ClosesPopups(bool state) {
        closes_popups = state;
        return *this;
    }

    Style& WithLargeIcon() {
        icon_scaling = k_regular_icon_scaling;
        return *this;
    }

    Style& WithIconScaling(f32 v) {
        icon_scaling = v;
        return *this;
    }

    Style& WithRandomiseIconScaling() {
        icon_scaling = 0.82f;
        return *this;
    }

    LayoutAndSizeType type {LayoutAndSizeType::IconOrText};
    f32 icon_scaling = 1.0f;
    f32 text_scaling = 1.0f;
    ColourSet main_cols {};
    ColourSet text_cols {}; // used if there is text as well as an icon
    ColourSet back_cols {};
    bool closes_popups {};
    bool greyed_out {};
    bool no_tooltips {};
    bool draw_with_overlay_graphics {};
    int corner_rounding_flags {~0};

    struct {
        bool add_margin_x {};
        TextOverflowType overflow_type {};
        TextJustification justification {};
        String default_icon {};
        bool capitalise {};
    } icon_or_text;

    struct {
        String on_icon {};
        String off_icon {};
        Optional<graphics::TextureHandle> icon_texture {};
        bool capitalise {};
    } icon_and_text;

    struct {
        param_values::VelocityMappingMode index {};
    } velocity_button;
};

PUBLIC Style IconButton(imgui::Context const& imgui) {
    Style s {};
    s.type = LayoutAndSizeType::IconOrText;
    s.main_cols.reg = LiveCol(imgui, UiColMap::IconButton1Regular);
    s.main_cols.on = LiveCol(imgui, UiColMap::IconButton1On);
    s.main_cols.hot_on = LiveCol(imgui, UiColMap::IconButton1Hover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = LiveCol(imgui, UiColMap::IconButton1Active);
    s.main_cols.active_off = s.main_cols.active_on;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.justification = TextJustification::Centred;
    s.icon_scaling = Style::k_regular_icon_scaling;
    return s;
}

PUBLIC Style SettingsWindowButton(imgui::Context const& imgui) {
    auto s = IconButton(imgui);
    s.type = LayoutAndSizeType::IconAndText;
    s.text_cols.reg = LiveCol(imgui, UiColMap::SettingsWindowMainText);
    s.text_cols.hot_on = LiveCol(imgui, UiColMap::SettingsWindowHoveredMainText);
    s.text_cols.hot_off = s.text_cols.hot_on;
    s.text_cols.active_on = s.text_cols.reg;
    s.text_cols.active_off = s.text_cols.active_on;
    s.text_cols.on = s.text_cols.reg;
    s.main_cols.reg = LiveCol(imgui, UiColMap::SettingsWindowIconButton);
    s.main_cols.hot_on = s.main_cols.reg;
    s.main_cols.hot_off = s.main_cols.reg;
    s.main_cols.active_on = s.main_cols.reg;
    s.main_cols.active_off = s.main_cols.active_on;
    s.main_cols.on = s.main_cols.reg;
    s.icon_and_text.on_icon = ICON_FA_SQUARE_CHECK;
    s.icon_and_text.off_icon = ICON_FA_SQUARE;
    s.icon_and_text.capitalise = false;
    return s;
}

PUBLIC Style TopPanelIconButton(imgui::Context const& imgui) {
    auto s = IconButton(imgui);
    s.main_cols.reg = LiveCol(imgui, UiColMap::TopPanelIconButtonRegular);
    s.main_cols.on = LiveCol(imgui, UiColMap::TopPanelIconButtonOn);
    s.main_cols.hot_on = LiveCol(imgui, UiColMap::TopPanelIconButtonHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = LiveCol(imgui, UiColMap::TopPanelIconButtonActive);
    s.main_cols.active_off = s.main_cols.active_on;
    return s;
}

PUBLIC Style TopPanelAttributionIconButton(imgui::Context const& imgui) {
    auto s = IconButton(imgui).WithLargeIcon();
    s.main_cols.reg = LiveCol(imgui, UiColMap::TopPanelAttributionIconButtonRegular);
    s.main_cols.on = LiveCol(imgui, UiColMap::TopPanelAttributionIconButtonOn);
    s.main_cols.hot_on = LiveCol(imgui, UiColMap::TopPanelAttributionIconButtonHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = LiveCol(imgui, UiColMap::TopPanelAttributionIconButtonActive);
    s.main_cols.active_off = s.main_cols.active_on;
    return s;
}

PUBLIC Style BrowserIconButton(imgui::Context const& imgui) {
    auto s = IconButton(imgui);
    s.main_cols.reg = LiveCol(imgui, UiColMap::BrowserIconButtonRegular);
    s.main_cols.on = LiveCol(imgui, UiColMap::BrowserIconButtonOn);
    s.main_cols.hot_on = LiveCol(imgui, UiColMap::BrowserIconButtonHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = LiveCol(imgui, UiColMap::BrowserIconButtonActive);
    s.main_cols.active_on = s.main_cols.active_off;
    return s;
}

PUBLIC Style LayerHeadingButton(imgui::Context const& imgui, u32 highlight_col = {}) {
    Style s {};
    if (!highlight_col) highlight_col = LiveCol(imgui, UiColMap::ToggleButtonIconOn);
    s.type = LayoutAndSizeType::IconAndText;
    s.main_cols.reg = LiveCol(imgui, UiColMap::ToggleButtonIconOff);
    s.main_cols.on = highlight_col;
    s.main_cols.hot_off = s.main_cols.reg;
    s.main_cols.hot_on = s.main_cols.on;
    s.main_cols.active_on = s.main_cols.hot_on;
    s.main_cols.active_off = s.main_cols.active_on;
    s.text_cols.reg = LiveCol(imgui, UiColMap::ToggleButtonTextOff);
    s.text_cols.on = LiveCol(imgui, UiColMap::ToggleButtonTextOn);
    s.text_cols.hot_on = LiveCol(imgui, UiColMap::ToggleButtonTextHover);
    s.text_cols.hot_off = s.text_cols.hot_on;
    s.text_cols.active_on = s.text_cols.hot_on;
    s.text_cols.active_off = s.text_cols.active_on;
    s.icon_and_text.on_icon = ICON_FA_SQUARE_CHECK;
    s.icon_and_text.off_icon = ICON_FA_SQUARE;
    s.icon_and_text.capitalise = false;
    s.icon_scaling = 0.75f;
    return s;
}

PUBLIC Style ParameterToggleButton(imgui::Context const& imgui,
                                   u32 highlight_col = {},
                                   bool _greyed_out = false) {
    auto s = LayerHeadingButton(imgui, highlight_col);
    s.icon_and_text.on_icon = ICON_FA_TOGGLE_ON;
    s.icon_and_text.off_icon = ICON_FA_TOGGLE_OFF;
    s.text_cols.grey_out_aware = true;
    s.greyed_out = _greyed_out;
    s.text_cols.greyed_out = LiveCol(imgui, UiColMap::MenuButtonTextInactive);
    s.text_cols.greyed_out_on = s.text_cols.greyed_out;
    return s;
}

PUBLIC Style LayerTabButton(imgui::Context const& imgui, bool has_dot) {
    Style s {};
    if (!has_dot)
        s.type = LayoutAndSizeType::IconOrText;
    else
        s.type = LayoutAndSizeType::IconAndTextLayerTab;
    s.main_cols.reg = LiveCol(imgui, UiColMap::LayerTabButtonText);
    s.main_cols.on = LiveCol(imgui, UiColMap::LayerTabButtonTextActive);
    s.main_cols.hot_on = LiveCol(imgui, UiColMap::LayerTabButtonTextHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = s.main_cols.hot_on;
    s.main_cols.active_off = s.main_cols.active_on;
    s.text_cols = s.main_cols;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.justification = TextJustification::Centred;
    s.icon_and_text.on_icon = ICON_FA_CIRCLE;
    s.icon_and_text.off_icon = s.icon_and_text.on_icon;
    s.icon_scaling = 0.30f;
    return s;
}

PUBLIC Style ParameterPopupButton(imgui::Context const& imgui, bool _greyed_out = false) {
    auto s = LayerHeadingButton(imgui);
    s.type = LayoutAndSizeType::IconOrText;
    s.main_cols.reg = LiveCol(imgui, UiColMap::MenuButtonText);
    s.main_cols.greyed_out = LiveCol(imgui, UiColMap::MenuButtonTextInactive);
    s.main_cols.greyed_out_on = s.main_cols.greyed_out;
    s.main_cols.on = s.main_cols.reg;
    s.main_cols.hot_on = LiveCol(imgui, UiColMap::MenuButtonTextHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = s.main_cols.hot_on;
    s.main_cols.active_off = s.main_cols.active_on;
    s.main_cols.grey_out_aware = true;
    s.greyed_out = _greyed_out;

    s.icon_or_text.add_margin_x = true;
    s.icon_or_text.justification = TextJustification::CentredLeft;
    s.icon_or_text.overflow_type = TextOverflowType::ShowDotsOnRight;

    s.back_cols.reg = LiveCol(imgui, UiColMap::MenuButtonBack);
    s.back_cols.on = s.back_cols.reg;
    s.back_cols.hot_on = s.back_cols.reg;
    s.back_cols.hot_off = s.back_cols.reg;
    s.back_cols.active_on = s.back_cols.hot_on;
    s.back_cols.active_off = s.back_cols.active_on;
    return s;
}

PUBLIC Style InstSelectorPopupButton(imgui::Context const& imgui,
                                     Optional<graphics::TextureHandle> icon_texture) {
    auto s = ParameterPopupButton(imgui);
    s.main_cols.grey_out_aware = false;
    s.back_cols = {};
    s.icon_and_text.icon_texture = icon_texture;
    s.type = LayoutAndSizeType::IconAndTextInstSelector;
    return s;
}

PUBLIC Style PresetsPopupButton(imgui::Context const& imgui) {
    auto s = ParameterPopupButton(imgui);
    s.main_cols.grey_out_aware = false;
    s.back_cols = {};
    return s;
}

PUBLIC Style MidiButton(imgui::Context const& imgui) {
    auto s = ParameterToggleButton(imgui);
    s.type = LayoutAndSizeType::IconAndTextMidiButton;
    return s;
}

PUBLIC Style PresetsBrowserFolderButton(imgui::Context const& imgui) {
    Style s {};
    s.type = LayoutAndSizeType::IconOrText;
    s.back_cols.reg = LiveCol(imgui, UiColMap::PresetBrowserFolderButtonBackOff);
    s.back_cols.on = LiveCol(imgui, UiColMap::PresetBrowserFolderButtonBackOn);
    s.back_cols.hot_on = LiveCol(imgui, UiColMap::PresetBrowserFolderButtonBackHover);
    s.back_cols.hot_off = s.back_cols.hot_on;
    s.back_cols.active_on = LiveCol(imgui, UiColMap::PresetBrowserFolderButtonBackActive);
    s.back_cols.active_off = s.back_cols.active_on;
    s.main_cols.reg = LiveCol(imgui, UiColMap::PresetBrowserFolderButtonTextOff);
    s.main_cols.on = LiveCol(imgui, UiColMap::PresetBrowserFolderButtonTextOn);
    s.main_cols.hot_on = LiveCol(imgui, UiColMap::PresetBrowserFolderButtonTextHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = LiveCol(imgui, UiColMap::PresetBrowserFolderButtonTextActive);
    s.main_cols.active_off = s.main_cols.active_on;
    s.icon_or_text.add_margin_x = true;
    s.icon_or_text.justification = TextJustification::CentredLeft;
    s.icon_or_text.overflow_type = TextOverflowType::ShowDotsOnRight;
    return s;
}

PUBLIC Style PresetsBrowserFileButton(imgui::Context const& imgui) {
    Style s {};
    s.type = LayoutAndSizeType::IconOrText;
    s.back_cols.reg = LiveCol(imgui, UiColMap::PresetBrowserFileButtonBackOff);
    s.back_cols.on = LiveCol(imgui, UiColMap::PresetBrowserFileButtonBackOn);
    s.back_cols.hot_on = LiveCol(imgui, UiColMap::PresetBrowserFileButtonBackHover);
    s.back_cols.hot_off = s.back_cols.hot_on;
    s.back_cols.active_on = LiveCol(imgui, UiColMap::PresetBrowserFileButtonBackActive);
    s.back_cols.active_off = s.back_cols.active_on;
    s.main_cols.reg = LiveCol(imgui, UiColMap::PresetBrowserFileButtonTextOff);
    s.main_cols.on = LiveCol(imgui, UiColMap::PresetBrowserFileButtonTextOn);
    s.main_cols.hot_on = LiveCol(imgui, UiColMap::PresetBrowserFileButtonTextHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = LiveCol(imgui, UiColMap::PresetBrowserFileButtonTextActive);
    s.main_cols.active_off = s.main_cols.active_on;
    s.icon_or_text.add_margin_x = true;
    s.icon_or_text.justification = TextJustification::CentredLeft;
    s.icon_or_text.overflow_type = TextOverflowType::ShowDotsOnRight;
    return s;
}

PUBLIC Style PresetsBrowserPopupButton(imgui::Context const& imgui) {
    auto s = ParameterPopupButton(imgui);
    s.icon_or_text.overflow_type = TextOverflowType::ShowDotsOnLeft;
    s.main_cols.grey_out_aware = false;
    s.main_cols.reg = LiveCol(imgui, UiColMap::BrowserFolderPopupButtonText);
    s.main_cols.on = s.main_cols.reg;
    s.main_cols.hot_on = LiveCol(imgui, UiColMap::BrowserFolderPopupButtonTextHover);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = s.main_cols.hot_on;
    s.main_cols.active_off = s.main_cols.active_on;
    s.back_cols.reg = LiveCol(imgui, UiColMap::BrowserFolderPopupButtonBack);
    s.back_cols.on = s.back_cols.reg;
    s.back_cols.hot_on = s.back_cols.reg;
    s.back_cols.hot_off = s.back_cols.hot_on;
    s.back_cols.active_on = s.back_cols.reg;
    s.back_cols.active_off = s.back_cols.active_on;
    return s;
}

PUBLIC Style MenuItem(imgui::Context const& imgui, bool _closes_popups, bool greyed_out = false) {
    Style s {};
    s.type = LayoutAndSizeType::IconAndTextMenuItem;
    s.closes_popups = _closes_popups;
    s.back_cols.reg = 0;
    s.back_cols.hot_on =
        LiveCol(imgui, !greyed_out ? UiColMap::PopupItemBackHover : UiColMap::PopupItemBackHoverGreyedOut);
    s.back_cols.hot_off = s.back_cols.hot_on;
    s.back_cols.active_on = s.back_cols.hot_on;
    s.back_cols.active_off = s.back_cols.active_on;
    s.back_cols.on = greyed_out ? 0 : LiveCol(imgui, UiColMap::PopupItemBackHover);
    s.text_cols.reg = LiveCol(imgui, greyed_out ? UiColMap::PopupItemTextGreyedOut : UiColMap::PopupItemText);
    s.text_cols.hot_on = s.text_cols.reg;
    s.text_cols.hot_off = s.text_cols.reg;
    s.text_cols.active_on = s.text_cols.reg;
    s.text_cols.active_off = s.text_cols.active_on;
    s.text_cols.on = s.text_cols.reg;
    s.main_cols.reg = LiveCol(imgui, UiColMap::PopupItemIcon);
    s.main_cols.hot_on = s.main_cols.reg;
    s.main_cols.hot_off = s.main_cols.reg;
    s.main_cols.active_on = s.main_cols.reg;
    s.main_cols.active_off = s.main_cols.active_on;
    s.main_cols.on = s.main_cols.reg;
    s.icon_scaling = 0.8f;
    s.icon_and_text.on_icon = ICON_FA_CHECK;
    return s;
}

PUBLIC Style MenuToggleItem(imgui::Context const& imgui, bool _closes_popups) {
    auto s = MenuItem(imgui, _closes_popups);
    s.back_cols.on = 0;
    return s;
}

PUBLIC Style SubMenuItem(imgui::Context const& imgui) {
    auto s = MenuItem(imgui, false);
    s.type = LayoutAndSizeType::IconAndTextSubMenuItem;
    s.icon_and_text.on_icon = ICON_FA_CARET_RIGHT;
    s.icon_and_text.off_icon = s.icon_and_text.on_icon;
    return s;
}

PUBLIC Style EffectButtonGrabber(imgui::Context const& imgui) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::CentredRight;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.default_icon = ICON_FA_ARROWS_UP_DOWN;
    s.icon_scaling = 0.7f;
    s.main_cols = {};
    s.main_cols.hot_on = LiveCol(imgui, UiColMap::FXButtonGripIcon);
    s.main_cols.hot_off = s.main_cols.hot_on;
    return s;
}

PUBLIC Style EffectHeading(imgui::Context const& imgui, u32 back_col) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::Centred;
    s.main_cols.reg = LiveCol(imgui, UiColMap::FXHeading);
    s.main_cols.active_on = s.main_cols.reg;
    s.main_cols.active_off = s.main_cols.active_on;
    s.main_cols.hot_on = s.main_cols.reg;
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.text_scaling = 1.1f;
    s.icon_or_text.add_margin_x = false;
    s.back_cols.reg = back_col;
    s.back_cols.hot_on = back_col;
    s.back_cols.hot_off = back_col;
    s.back_cols.active_on = back_col;
    s.back_cols.active_off = s.back_cols.active_on;
    s.corner_rounding_flags = 4;
    return s;
}

PUBLIC Style LicencesFoldButton(imgui::Context const& imgui) { return MenuItem(imgui, false); }

PUBLIC Style VelocityButton(imgui::Context const&, param_values::VelocityMappingMode index) {
    Style s {};
    s.type = LayoutAndSizeType::VelocityButton;
    s.velocity_button.index = index;
    return s;
}

//
//
//

struct ButtonReturnObject {
    bool changed;
    imgui::Id id;
};

// Full
bool Button(Gui* g, imgui::Id id, Rect r, String str, Style const& style);
bool Toggle(Gui* g, imgui::Id id, Rect r, bool& state, String str, Style const& style);
bool Popup(Gui* g, imgui::Id button_id, imgui::Id popup_id, Rect r, String str, Style const& style);

// No imgui ID
bool Button(Gui* g, Rect r, String str, Style const& style);
bool Toggle(Gui* g, Rect r, bool& state, String str, Style const& style);
bool Popup(Gui* g, imgui::Id popup_id, Rect r, String str, Style const& style);

// Params
ButtonReturnObject Toggle(Gui* g, Parameter const& param, Rect r, String str, Style const& style);
ButtonReturnObject Toggle(Gui* g, Parameter const& param, Rect r, Style const& style);
ButtonReturnObject PopupWithItems(Gui* g, Parameter const& param, Rect r, Style const& style);

void FakeButton(Gui* g, Rect r, String str, Style const& style);

// LayID
bool Button(Gui* g, imgui::Id id, layout::Id lay_id, String str, Style const& style);
bool Toggle(Gui* g, imgui::Id id, layout::Id lay_id, bool& state, String str, Style const& style);
bool Popup(Gui* g,
           imgui::Id button_id,
           imgui::Id popup_id,
           layout::Id lay_id,
           String str,
           Style const& style);

bool Button(Gui* g, layout::Id lay_id, String str, Style const& style);
bool Toggle(Gui* g, layout::Id lay_id, bool& state, String str, Style const& style);
bool Popup(Gui* g, imgui::Id popup_id, layout::Id lay_id, String str, Style const& style);

ButtonReturnObject Toggle(Gui* g, Parameter const& param, layout::Id lay_id, String str, Style const& style);
ButtonReturnObject Toggle(Gui* g, Parameter const& param, layout::Id lay_id, Style const& style);
ButtonReturnObject PopupWithItems(Gui* g, Parameter const& param, layout::Id lay_id, Style const& style);

void FakeButton(Gui* g, layout::Id lay_id, String str, Style const& style);
void FakeButton(Gui* g, Rect r, String str, Style const& style);
void FakeButton(Gui* g, Rect r, String str, bool state, Style const& style);

} // namespace buttons
