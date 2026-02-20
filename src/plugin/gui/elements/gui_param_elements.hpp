// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/core/gui_fwd.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui_framework/gui_builder.hpp"

struct AudioProcessor;

struct ParameterComponentOptions {
    f32 width; // In window-width units.
    f32 knob_height_fraction = 1; // Height = width * knob_height_fraction.
    Col knob_highlight_col = {Col::Highlight};
    Col knob_line_col = {Col::Background0};
    GuiStyleSystem style_system {};
    bool greyed_out = false;
    bool bidirectional = false;
    bool is_fake = false;
    bool label = true;
    String override_tooltip {};
    String override_label {};
};

Box DoKnobParameter(GuiState& g,
                    Box parent,
                    DescribedParamValue const& param,
                    ParameterComponentOptions const& options = {});

struct MenuParameterComponentOptions {
    f32 width = 0; // 0 means auto-size from the widest menu item string.
    bool greyed_out = false;
    bool label = true;
    String override_tooltip {};
    String override_label {};
};

Box DoMenuParameter(GuiState& g,
                    Box parent,
                    DescribedParamValue const& param,
                    MenuParameterComponentOptions const& options = {});

struct ButtonParameterComponentOptions {
    f32 width = 0; // 0 means auto-size from text + icon.
    bool greyed_out = false;
    String override_tooltip {};
    String override_label {};
};

Box DoButtonParameter(GuiState& g,
                      Box parent,
                      DescribedParamValue const& param,
                      ButtonParameterComponentOptions const& options = {});

struct IntParameterComponentOptions {
    f32 width; // Required, no auto-width.
    bool greyed_out = false;
    bool always_show_plus = false;
    bool midi_note_names = false;
    bool label = true;
    String override_tooltip {};
    String override_label {};
};

Box DoIntParameter(GuiState& g,
                   Box parent,
                   DescribedParamValue const& param,
                   IntParameterComponentOptions const& options);

String ParamTooltipText(DescribedParamValue const& param, ArenaAllocator& arena);

void AddParamContextMenuBehaviour(GuiState& g, Rect window_r, imgui::Id id, DescribedParamValue const& param);
void AddParamContextMenuBehaviour(GuiState& g,
                                  Rect window_r,
                                  imgui::Id id,
                                  Span<DescribedParamValue const> params);

void AddParamContextMenuBehaviour(GuiState& g, Box const& box, DescribedParamValue const& param);

void HandleShowingTextEditorForParams(GuiState& g, Rect r, Span<ParamIndex const> params);

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
