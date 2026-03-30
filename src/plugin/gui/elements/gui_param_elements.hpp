// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/core/gui_fwd.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui_common_elements.hpp"
#include "gui_framework/gui_builder.hpp"

struct AudioProcessor;
struct StereoPeakMeter;

struct ParameterComponentOptions {
    f32 width; // In window-width units.
    f32 knob_height_fraction = 0.96f; // Height of the knob aspect = width * knob_height_fraction.
    Col knob_highlight_col = {Col::Highlight};
    Col knob_line_col = {Col::Background0};
    GuiStyleSystem style_system {};
    bool greyed_out = false;
    bool bidirectional = false;
    bool is_fake = false;
    bool label = true;
    String override_tooltip {};
    String override_label {};
    StereoPeakMeter const* peak_meter = nullptr; // If set, draws a peak meter inside the knob.
};

Box DoKnobParameter(GuiState& g,
                    Box parent,
                    DescribedParamValue const& param,
                    ParameterComponentOptions const& options = {});

struct MenuParameterComponentOptions {
    f32 width = layout::k_hug_contents;
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
    f32 width = layout::k_hug_contents;
    f32 height = k_mid_button_height;
    Margins margins {};
    bool greyed_out = false;
    Optional<Col> on_colour {}; // Custom colour for the toggle icon "on" state.
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

struct MuteSoloButtonsOptions {
    bool vertical = false; // If true, buttons stack vertically (M on top, S on bottom).
};

void DoMuteSoloButtons(GuiState& g,
                       Box parent,
                       DescribedParamValue const& mute_param,
                       DescribedParamValue const& solo_param,
                       MuteSoloButtonsOptions const& options = {});

struct VerticalSliderParameterOptions {
    f32 width;
    f32 height;
    Col highlight_col = {Col::Highlight};
    Col line_col = {Col::Background0};
    GuiStyleSystem style_system {};
    bool greyed_out = false;
    bool is_fake = false;
    String override_tooltip {};
};

Box DoVerticalSliderParameter(GuiState& g,
                              Box parent,
                              DescribedParamValue const& param,
                              VerticalSliderParameterOptions const& options);

String ParamTooltipText(DescribedParamValue const& param, ArenaAllocator& arena, bool greyed_out = false);

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
