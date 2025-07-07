// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui_framework/gui_imgui.hpp"
#include "processor/param.hpp"

struct Gui;

void StartFloeMenu(Gui* g);
void EndFloeMenu(Gui* g);

f32 MaxStringLength(Gui* g, void* items, int num, String (*GetStr)(void* items, int index));
f32 MaxStringLength(Gui* g, Span<String const> strs);

// Adds an extra width to account for icons, etc.
f32 MenuItemWidth(Gui* g, void* items, int num, String (*GetStr)(void* items, int index));
f32 MenuItemWidth(Gui* g, Span<String const> strs);

//
//
//

void DoTooltipText(Gui* g, String str, Rect r, bool rect_is_window_pos = false);
bool Tooltip(Gui* g, imgui::Id id, Rect r, String str, bool rect_is_window_pos = false);

void DoParameterTooltipIfNeeded(Gui* g, Parameter const& param, imgui::Id imgui_id, Rect param_rect);
void DoParameterTooltipIfNeeded(Gui* g, Span<Parameter const*> param, imgui::Id imgui_id, Rect param_rect);
void ParameterValuePopup(Gui* g, Parameter const& param, imgui::Id id, Rect r);
void ParameterValuePopup(Gui* g, Span<Parameter const*> params, imgui::Id id, Rect r);

void MidiLearnMenu(Gui* g, Span<ParamIndex> params, Rect r);
void MidiLearnMenu(Gui* g, ParamIndex param, Rect r);

imgui::TextInputSettings GetParameterTextInputSettings();
void HandleShowingTextEditorForParams(Gui* g, Rect r, Span<ParamIndex const> params);

bool DoMultipleMenuItems(Gui* g,
                         void* items,
                         int num_items,
                         int& current,
                         String (*GetStr)(void* items, int index));
bool DoMultipleMenuItems(Gui* g, Span<String const> items, int& current);

imgui::Id BeginParameterGUI(Gui* g, Parameter const& param, Rect r, Optional<imgui::Id> id = {});
enum ParamDisplayFlags {
    ParamDisplayFlagsDefault = 0,
    ParamDisplayFlagsNoTooltip = 1,
    ParamDisplayFlagsNoValuePopup = 2,
};
void EndParameterGUI(Gui* g,
                     imgui::Id id,
                     Parameter const& param,
                     Rect r,
                     Optional<f32> new_val,
                     ParamDisplayFlags flags = ParamDisplayFlagsDefault);

//
// Misc
//
bool DoOverlayClickableBackground(Gui* g);
