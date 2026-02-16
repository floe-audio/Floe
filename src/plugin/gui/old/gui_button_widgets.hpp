// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <IconsFontAwesome6.h>

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "../gui_fwd.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/layout.hpp"
#include "gui_framework/renderer.hpp"

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
    u4 corner_rounding_flags {0b1111};

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
        Optional<TextureHandle> icon_texture {};
        bool capitalise {};
    } icon_and_text;

    struct {
        param_values::VelocityMappingMode index {};
    } velocity_button;
};

PUBLIC Style IconButton(imgui::Context const&) {
    Style s {};
    s.type = LayoutAndSizeType::IconOrText;
    s.main_cols.reg = LiveCol(UiColMap::MidIcon);
    s.main_cols.on = LiveCol(UiColMap::MidTextOn);
    s.main_cols.hot_on = LiveCol(UiColMap::MidTextHot);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = LiveCol(UiColMap::MidTextOn);
    s.main_cols.active_off = s.main_cols.active_on;
    s.main_cols.greyed_out = LiveCol(UiColMap::MidTextDimmed);
    s.main_cols.greyed_out_on = LiveCol(UiColMap::MidTextDimmed);
    s.main_cols.grey_out_aware = true;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.justification = TextJustification::Centred;
    s.icon_scaling = Style::k_regular_icon_scaling;
    return s;
}

static Style MuteSoloButton(imgui::Context const&, bool is_solo) {
    Style s {};
    s.type = LayoutAndSizeType::IconOrText;
    s.main_cols.reg = LiveCol(UiColMap::MidText);
    s.main_cols.on = LiveCol(UiColMap::MuteSoloButtonTextOn);
    s.main_cols.hot_on = LiveCol(UiColMap::MuteSoloButtonTextOnHot);
    s.main_cols.hot_off = LiveCol(UiColMap::MidTextHot);
    s.main_cols.active_on = s.main_cols.hot_on;
    s.main_cols.active_off = s.main_cols.hot_off;
    s.back_cols.reg = {};
    s.back_cols.on = LiveCol(is_solo ? UiColMap::SoloButtonBackOn : UiColMap::MuteButtonBackOn);
    s.back_cols.hot_on = s.back_cols.on;
    s.back_cols.hot_off = s.back_cols.reg;
    s.back_cols.active_on = s.back_cols.on;
    s.back_cols.active_off = s.back_cols.reg;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.justification = TextJustification::Centred;
    s.icon_scaling = Style::k_regular_icon_scaling;
    s.corner_rounding_flags = is_solo ? 0b0110 : 0b1001;
    return s;
}

PUBLIC Style MuteButton(imgui::Context const& imgui) { return MuteSoloButton(imgui, false); }

PUBLIC Style SoloButton(imgui::Context const& imgui) { return MuteSoloButton(imgui, true); }

PUBLIC Style LayerHeadingButton(imgui::Context const&, u32 highlight_col = {}) {
    Style s {};
    if (!highlight_col) highlight_col = LiveCol(UiColMap::MidTextOn);
    s.type = LayoutAndSizeType::IconAndText;
    s.main_cols.reg = LiveCol(UiColMap::MidIcon);
    s.main_cols.on = highlight_col;
    s.main_cols.hot_off = s.main_cols.reg;
    s.main_cols.hot_on = s.main_cols.on;
    s.main_cols.active_on = s.main_cols.hot_on;
    s.main_cols.active_off = s.main_cols.active_on;
    s.text_cols.reg = LiveCol(UiColMap::MidText);
    s.text_cols.on = LiveCol(UiColMap::MidText);
    s.text_cols.hot_on = LiveCol(UiColMap::MidTextHot);
    s.text_cols.hot_off = s.text_cols.hot_on;
    s.text_cols.active_on = s.text_cols.hot_on;
    s.text_cols.active_off = s.text_cols.active_on;
    s.icon_and_text.on_icon = ICON_FA_TOGGLE_ON;
    s.icon_and_text.off_icon = ICON_FA_TOGGLE_OFF;
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
    s.text_cols.greyed_out = LiveCol(UiColMap::MidTextDimmed);
    s.text_cols.greyed_out_on = s.text_cols.greyed_out;
    return s;
}

PUBLIC Style LayerTabButton(imgui::Context const&, bool has_dot) {
    Style s {};
    if (!has_dot)
        s.type = LayoutAndSizeType::IconOrText;
    else
        s.type = LayoutAndSizeType::IconAndTextLayerTab;
    s.main_cols.reg = LiveCol(UiColMap::MidText);
    s.main_cols.on = LiveCol(UiColMap::MidTextOn);
    s.main_cols.hot_on = LiveCol(UiColMap::MidTextHot);
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
    s.main_cols.reg = LiveCol(UiColMap::MidText);
    s.main_cols.greyed_out = LiveCol(UiColMap::MidTextDimmed);
    s.main_cols.greyed_out_on = s.main_cols.greyed_out;
    s.main_cols.on = s.main_cols.reg;
    s.main_cols.hot_on = LiveCol(UiColMap::MidTextHot);
    s.main_cols.hot_off = s.main_cols.hot_on;
    s.main_cols.active_on = s.main_cols.hot_on;
    s.main_cols.active_off = s.main_cols.active_on;
    s.main_cols.grey_out_aware = true;
    s.greyed_out = _greyed_out;

    s.icon_or_text.add_margin_x = true;
    s.icon_or_text.justification = TextJustification::CentredLeft;
    s.icon_or_text.overflow_type = TextOverflowType::ShowDotsOnRight;

    s.back_cols.reg = LiveCol(UiColMap::MidDarkSurface);
    s.back_cols.on = s.back_cols.reg;
    s.back_cols.hot_on = s.back_cols.reg;
    s.back_cols.hot_off = s.back_cols.reg;
    s.back_cols.active_on = s.back_cols.hot_on;
    s.back_cols.active_off = s.back_cols.active_on;
    return s;
}

PUBLIC Style InstSelectorPopupButton(imgui::Context const& imgui, Optional<TextureHandle> icon_texture) {
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

PUBLIC Style MenuItem(imgui::Context const&, bool _closes_popups, bool greyed_out = false) {
    Style s {};
    s.type = LayoutAndSizeType::IconAndTextMenuItem;
    s.closes_popups = _closes_popups;
    s.back_cols.reg = 0;
    s.back_cols.hot_on = ToU32(Col {.c = Col::Highlight100});
    s.back_cols.hot_off = s.back_cols.hot_on;
    s.back_cols.active_on = s.back_cols.hot_on;
    s.back_cols.active_off = s.back_cols.active_on;
    s.back_cols.on = greyed_out ? 0 : ToU32(Col {.c = Col::Highlight50});
    s.text_cols.reg = ToU32(Col {.c = greyed_out ? Col::Subtext0 : Col::Text});
    s.text_cols.hot_on = s.text_cols.reg;
    s.text_cols.hot_off = s.text_cols.reg;
    s.text_cols.active_on = s.text_cols.reg;
    s.text_cols.active_off = s.text_cols.active_on;
    s.text_cols.on = s.text_cols.reg;
    s.main_cols.reg = ToU32(Col {.c = Col::Subtext1});
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

PUBLIC Style EffectButtonGrabber(imgui::Context const&) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::CentredRight;
    s.icon_or_text.add_margin_x = false;
    s.icon_or_text.default_icon = ICON_FA_ARROWS_UP_DOWN;
    s.icon_scaling = 0.7f;
    s.main_cols = {};
    s.main_cols.hot_on = LiveCol(UiColMap::FXButtonGripIcon);
    s.main_cols.hot_off = s.main_cols.hot_on;
    return s;
}

PUBLIC Style EffectHeading(imgui::Context const&, u32 back_col) {
    Style s {};
    s.type = buttons::LayoutAndSizeType::IconOrText;
    s.icon_or_text.justification = TextJustification::Centred;
    s.main_cols.reg = LiveCol(UiColMap::MidText);
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
    s.corner_rounding_flags = 0b0010;
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
bool Button(GuiState& g, imgui::Id id, Rect r, String str, Style const& style);
bool Toggle(GuiState& g, imgui::Id id, Rect r, bool& state, String str, Style const& style);
bool Popup(GuiState& g, imgui::Id button_id, imgui::Id popup_id, Rect r, String str, Style const& style);

// No imgui ID
bool Button(GuiState& g, Rect r, String str, Style const& style);
bool Toggle(GuiState& g, Rect r, bool& state, String str, Style const& style);
bool Popup(GuiState& g, imgui::Id popup_id, Rect r, String str, Style const& style);

// Params
ButtonReturnObject
Toggle(GuiState& g, DescribedParamValue const& param, Rect r, String str, Style const& style);
ButtonReturnObject Toggle(GuiState& g, DescribedParamValue const& param, Rect r, Style const& style);
ButtonReturnObject PopupWithItems(GuiState& g, DescribedParamValue const& param, Rect r, Style const& style);

void FakeButton(GuiState& g, Rect r, String str, Style const& style);

// LayID
bool Button(GuiState& g, imgui::Id id, layout::Id lay_id, String str, Style const& style);
bool Toggle(GuiState& g, imgui::Id id, layout::Id lay_id, bool& state, String str, Style const& style);
bool Popup(GuiState& g,
           imgui::Id button_id,
           imgui::Id popup_id,
           layout::Id lay_id,
           String str,
           Style const& style);

bool Button(GuiState& g, layout::Id lay_id, String str, Style const& style);
bool Toggle(GuiState& g, layout::Id lay_id, bool& state, String str, Style const& style);
bool Popup(GuiState& g, imgui::Id popup_id, layout::Id lay_id, String str, Style const& style);

ButtonReturnObject
Toggle(GuiState& g, DescribedParamValue const& param, layout::Id lay_id, String str, Style const& style);
ButtonReturnObject
Toggle(GuiState& g, DescribedParamValue const& param, layout::Id lay_id, Style const& style);
ButtonReturnObject
PopupWithItems(GuiState& g, DescribedParamValue const& param, layout::Id lay_id, Style const& style);

void FakeButton(GuiState& g, layout::Id lay_id, String str, Style const& style);
void FakeButton(GuiState& g, Rect r, String str, Style const& style);
void FakeButton(GuiState& g, Rect r, String str, bool state, Style const& style);

} // namespace buttons
