// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

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
    String inactive_reason {}; // If greyed_out, shown in the value popup rather than the tooltip.
    String override_tooltip {};
    String override_value_popup {};
    String override_label {};
    StereoPeakMeter const* peak_meter = nullptr; // If set, draws a peak meter inside the knob.
    Span<f32 const> voice_blips_01 {}; // Per-voice value markers drawn on the highlight arc.
};

Box DoKnobParameter(GuiState& g,
                    Box parent,
                    DescribedParamValue const& param,
                    ParameterComponentOptions options = {});

struct MenuParameterComponentOptions {
    f32 width = layout::k_hug_contents;
    bool greyed_out = false;
    bool label = true;
    bool allow_text_overflow = false;
    String override_tooltip {};
    String override_label {};
    String override_button_text {}; // If non-empty, shown on the button instead of the menu item text.
    Box const* tooltip_avoid_box = nullptr; // Defaults to this widget's own container. Set it to a box that
                                            // also encloses a caller-drawn label so the tooltip clears that
                                            // too.
};

Box DoMenuParameter(GuiState& g,
                    Box parent,
                    DescribedParamValue const& param,
                    MenuParameterComponentOptions options = {});

struct ButtonParameterComponentOptions {
    f32 width = layout::k_hug_contents;
    f32 height = k_mid_button_height;
    Margins margins {};
    bool greyed_out = false;
    Optional<Col> on_colour {}; // Custom colour for the toggle icon "on" state.
    Optional<bool> locked_state {}; // If set, shows this state instead of the param's and the button becomes
                                    // greyed out and non-interactive.
    String override_tooltip {};
    String override_label {};
};

Box DoButtonParameter(GuiState& g,
                      Box parent,
                      DescribedParamValue const& param,
                      ButtonParameterComponentOptions options = {});

struct IntParameterComponentOptions {
    f32 width; // Required, no auto-width.
    bool greyed_out = false;
    bool always_show_plus = false;
    bool midi_note_names = false;
    bool label = true;
    String override_tooltip {};
    String override_label {};
    Box const* tooltip_avoid_box = nullptr; // See MenuParameterComponentOptions::tooltip_avoid_box.
};

Box DoIntParameter(GuiState& g,
                   Box parent,
                   DescribedParamValue const& param,
                   IntParameterComponentOptions options);

struct PercentDraggerOptions {
    f32 width;
    bool greyed_out = false;
    bool label = true;
    String override_label {};
    Box const* tooltip_avoid_box = nullptr; // See MenuParameterComponentOptions::tooltip_avoid_box.
};

Box DoPercentDraggerParameter(GuiState& g,
                              Box parent,
                              DescribedParamValue const& param,
                              PercentDraggerOptions options);

struct MuteSoloButtonsOptions {
    bool vertical = false; // If true, buttons stack vertically (M on top, S on bottom).
    Optional<f32> button_extent {}; // Per-button height (horizontal) / width (vertical). Defaults to
                                    // k_mid_button_height.
    String name {};
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
    Span<f32 const> voice_blips_01 {}; // Per-voice value markers drawn on the highlight bar.
};

Box DoVerticalSliderParameter(GuiState& g,
                              Box parent,
                              DescribedParamValue const& param,
                              VerticalSliderParameterOptions options);

// Per-voice values mapped into dest_knob_param's 0-1 space, for drawing as blips on that parameter's
// control. Pass no layer_index to include voices from all layers. Requests animation updates while any
// blips are showing.
Span<f32 const> VoiceBlips01(GuiState& g,
                             Optional<u8> layer_index,
                             param_values::MpeDestination destination,
                             DescribedParamValue const& dest_knob_param);

// Adds the standard pair of "Reset {name} to Default" / "Reset {name} to "<preset>" state" menu items for a
// snapshot section. The pinned-preset item is only shown when a preset is loaded. Returns true on the frame
// either item fires, so the caller can run post-reset cleanup.
bool DoResetSectionMenuItems(GuiState& g,
                             Box menu_root,
                             StateSnapshotSection const& section,
                             String name,
                             bool no_icon_gap = true,
                             ApplySectionOptions default_reset_options = {});

// Help text: the parameter's description.
String ParamTooltipText(DescribedParamValue const& param, ArenaAllocator& arena, bool greyed_out = false);

// Interaction hints shown dimmed beneath a parameter's tooltip. These belong to the widget rather than the
// parameter, so the descriptor text shouldn't repeat them.
constexpr String k_dragger_tooltip_footer =
    "Shift-drag for fine control. " MODIFIER_KEY_NAME
    "-click to reset. Double-click to type. Right-click for more options."_s;
constexpr String k_right_click_tooltip_footer = "Right-click for more options."_s;
// For buttons, which only have a right-click menu when automatable.
constexpr String ParamClickableTooltipFooter(DescribedParamValue const& param) {
    return param.info.flags.not_automatable ? String {} : k_right_click_tooltip_footer;
}
// For menus, which additionally reset to their default value on modifier-click.
constexpr String ParamMenuTooltipFooter(DescribedParamValue const& param) {
    if (param.info.flags.not_automatable) return MODIFIER_KEY_NAME "-click to reset."_s;
    return MODIFIER_KEY_NAME "-click to reset. Right-click for more options."_s;
}

// Value readout: just the value for a single parameter, "Label: value" lines for several.
String ParamValuePopupText(GuiState const& g, Span<DescribedParamValue const*> params, ArenaAllocator& arena);
String ParamValuePopupText(GuiState const& g, DescribedParamValue const& param, ArenaAllocator& arena);

void AddParamContextMenuBehaviour(GuiState& g, Rect window_r, imgui::Id id, DescribedParamValue const& param);
void AddParamContextMenuBehaviour(GuiState& g,
                                  Rect window_r,
                                  imgui::Id id,
                                  Span<DescribedParamValue const> params);

void AddParamContextMenuBehaviour(GuiState& g, Box const& box, DescribedParamValue const& param);

void HandleShowingTextEditorForParams(GuiState& g, Rect r, Span<ParamIndex const> params);

// Id of the text input that HandleShowingTextEditorForParams opens. Elements that request one for
// themselves pass this as the request's widget_id.
imgui::Id ParamTextEditorOverlayId(imgui::Context& imgui);

// Whether this element should open a text input for the parameter, clearing the request if so.
bool ConsumeParamTextEditorRequest(GuiState& g, ParamIndex param, imgui::Id widget_id);

// Value popup and tooltip for custom IMGUI parameter controls (envelope grabbers, waveform handles, etc.).
// avoid_r: region the popups are placed outside of. If nullopt, uses window_r.
// tooltip_note: extra paragraph appended after the parameter descriptions, for things the widget knows
// about but the parameters don't.
// value_popup_fixed_width: see TooltipArgs::value_popup_fixed_width. Use when a param's displayed value
// can change width between frames (e.g. a note name), to stop the popup resizing/jumping.
void ParameterTooltip(GuiState& g,
                      DescribedParamValue const& param,
                      imgui::Id imgui_id,
                      Rect window_r,
                      Optional<Rect> avoid_r,
                      String tooltip_footer);
void ParameterTooltip(GuiState& g,
                      Span<DescribedParamValue const*> params,
                      imgui::Id imgui_id,
                      Rect window_r,
                      Optional<Rect> avoid_r,
                      String tooltip_footer,
                      String tooltip_note = {},
                      Optional<f32> value_popup_fixed_width = k_nullopt);
