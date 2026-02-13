// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui_framework/gui_imgui.hpp"
#include "processor/param.hpp"

// IMPORTANT: This file considered technical debt. It's due to be superseded by new code that uses better
// techniques for the same result by leaning into the GuiBuilder system amongst other things.
// The problems with this file:
// - Messy functions
// - Many function arguments rather an options structs with designated initialiser syntax
// - Overuse of function default arguments
// - Use of monolithic 'GuiState' struct rather than specific arguments

struct GuiState;

void StartFloeMenu(GuiState& g);
void EndFloeMenu(GuiState& g);

f32 MaxStringLength(GuiState& g, Span<String const> strs);

// Adds an extra width to account for icons, etc.
f32 MenuItemWidth(GuiState& g, Span<String const> strs);

//
//
//

struct TooltipOptions {
    bool ignore_show_tooltips_preference = false;
};
bool Tooltip(GuiState& g, imgui::Id id, Rect window_r, String str, TooltipOptions const& options);

void DoParameterTooltipIfNeeded(GuiState& g,
                                DescribedParamValue const& param,
                                imgui::Id imgui_id,
                                Rect window_r);
void DoParameterTooltipIfNeeded(GuiState& g,
                                Span<DescribedParamValue const*> param,
                                imgui::Id imgui_id,
                                Rect window_r);
void ParameterValuePopup(GuiState& g, DescribedParamValue const& param, imgui::Id id, Rect window_r);
void ParameterValuePopup(GuiState& g, Span<DescribedParamValue const*> params, imgui::Id id, Rect window_r);

void MidiLearnMenu(GuiState& g, Span<ParamIndex> params, Rect r);
void MidiLearnMenu(GuiState& g, ParamIndex param, Rect r);

constexpr imgui::ButtonConfig k_param_text_input_button_flags = {
    .mouse_button = MouseButton::Left,
    .event = MouseButtonEvent::DoubleClick,
};

constexpr imgui::TextInputConfig k_param_text_input_flags = {
    .centre_align = true,
    .escape_unfocuses = true,
    .select_all_when_opening = true,
};

void HandleShowingTextEditorForParams(GuiState& g, Rect r, Span<ParamIndex const> params);

bool DoMultipleMenuItems(GuiState& g,
                         void* items,
                         int num_items,
                         int& current,
                         String (*GetStr)(void* items, int index));
bool DoMultipleMenuItems(GuiState& g, Span<String const> items, int& current);

imgui::Id
BeginParameterGUI(GuiState& g, DescribedParamValue const& param, Rect r, Optional<imgui::Id> id = {});
enum ParamDisplayFlags {
    ParamDisplayFlagsDefault = 0,
    ParamDisplayFlagsNoTooltip = 1,
    ParamDisplayFlagsNoValuePopup = 2,
};
void EndParameterGUI(GuiState& g,
                     imgui::Id id,
                     DescribedParamValue const& param,
                     Rect r,
                     Optional<f32> new_val,
                     ParamDisplayFlags flags = ParamDisplayFlagsDefault);

bool DoBasicTextButton(imgui::Context& imgui,
                       imgui::ButtonConfig flags,
                       Rect viewport_r,
                       imgui::Id id,
                       String str);

void DoBasicWhiteText(imgui::Context& imgui, Rect viewport_r, String str);

//
// Misc
//
bool DoOverlayClickableBackground(GuiState& g);
