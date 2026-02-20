// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/core/gui_fwd.hpp"
#include "gui_framework/gui_builder.hpp"

// Builder version of gui_widget_compounds.hpp

struct AudioProcessor;

struct ParameterComponentOptions {
    enum class Size { Small, Medium, Large };
    Size size {Size::Medium};
    Col knob_highlight_col = {Col::Highlight};
    Col knob_line_col = {Col::Background0};
    bool greyed_out = false;
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

// Reusable row with prev/next arrow buttons. Add your content to the row, then call DoPrevNextButtons.
Box DoPrevNextRow(GuiBuilder& builder, Box parent, f32 width);

struct PrevNextButtonsResult {
    bool prev_fired;
    bool next_fired;
};
struct PrevNextButtonsOptions {
    String prev_tooltip {"Previous"};
    String next_tooltip {"Next"};
};
PrevNextButtonsResult
DoPrevNextButtons(GuiBuilder& builder, Box row, PrevNextButtonsOptions const& options = {});

struct ShuffleButtonOptions {
    String tooltip {"Shuffle"};
};
Box DoShuffleButton(GuiBuilder& builder, Box row, ShuffleButtonOptions const& options = {});

void AddParamContextMenuBehaviour(GuiState& g, Rect window_r, imgui::Id id, DescribedParamValue const& param);
void AddParamContextMenuBehaviour(GuiState& g,
                                  Rect window_r,
                                  imgui::Id id,
                                  Span<DescribedParamValue const> params);

void AddParamContextMenuBehaviour(GuiState& g, Box const& box, DescribedParamValue const& param);
