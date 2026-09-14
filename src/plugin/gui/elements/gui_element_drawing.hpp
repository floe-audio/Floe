// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui/elements/gui_constants.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processing_utils/peak_meter.hpp"

// Drawing functions always need window coordinates, not viewport coordinates.

void DrawDropShadow(imgui::Context const& imgui, Rect r, Optional<f32> rounding = {}, f32 opacity = 1);

struct VoiceMarkerLineOptions {
    f32 opacity = 1;
    u32 col = LiveCol(UiColMap::WaveformLoopVoiceMarkers);
};
void DrawVoiceMarkerLine(imgui::Context const& imgui,
                         f32x2 pos,
                         f32 height,
                         f32 left_min,
                         Optional<Line> upper_line,
                         VoiceMarkerLineOptions const& options);

struct DrawParameterTextInputOptions {
    bool dark_mode = true;
    bool draw_border = true;
};

void DrawParameterTextInput(imgui::Context const& imgui,
                            Rect r,
                            imgui::TextInputResult const& result,
                            DrawParameterTextInputOptions const& options = {});

struct DrawTextInputConfig {
    Col text_col = {.c = Col::Text};
    Col cursor_col = {.c = Col::Text};
    Col selection_col = {.c = Col::Highlight, .alpha = 128};
};

void DrawTextInput(imgui::Context& imgui,
                   imgui::TextInputResult const& result,
                   DrawTextInputConfig const& config = {});

struct DrawKnobOptions {
    u32 highlight_col;
    u32 line_col;
    Optional<f32> overload_position;
    Optional<f32> outer_arc_percent;
    Span<f32 const> voice_blips_01 {}; // Per-voice markers drawn on the outer arc.
    GuiStyleSystem style_system;
    bool greyed_out;
    bool is_fake;
    bool bidirectional;
};

void DrawKnob(imgui::Context& imgui, imgui::Id id, Rect r, f32 percent, DrawKnobOptions const& style);

struct DrawPeakMeterOptions {
    bool flash_when_clipping;
    bool show_db_markers = true;
    bool show_min_max_markers = false; // Also draws markers at min_db and max_db.
    f32 min_db = -60.0f;
    f32 max_db = 10.0f;
    f32 marker_interval_db = 12.0f;
    int gap_px = 2;

    // If false, the meter is drawn in a single colour throughout, with no yellow/red warning zones or
    // overload background tint. Use this where a hot reading doesn't itself indicate a problem, e.g. a
    // per-layer meter measured before layers are summed and before any master effect runs.
    bool show_warning_zones = true;

    // The warning-coloured region runs from this level up to 0dB. Ignored if show_warning_zones is false.
    f32 yellow_zone_min_db = -12.0f;

    // Draws a horizontal line across the channels at this level, e.g. a limiter threshold or ceiling.
    Optional<f32> marker_db {};
    u32 marker_col = 0; // 0 uses the default peak colour.
    String marker_description {}; // What the line means, for the tooltip. E.g. "Ceiling".

    // If the true level is above this but would otherwise be below min_db (and so not drawn at all),
    // draw a 1px sliver at the bottom of the meter to indicate there's still some signal present.
    Optional<f32> low_signal_threshold_db {};
};

struct DrawGainReductionMeterOptions {
    f32 gain_reduction_db;
    u32 col;
    f32 max_reduction_db = 12.0f; // The reduction level at which the bar is full.
};

// Single downward bar from 0dB showing how much a limiter is currently attenuating.
void DrawGainReductionMeter(imgui::Context& imgui, Rect r, DrawGainReductionMeterOptions const& options);

struct DrawLoudnessMeterOptions {
    f32 short_term_lufs; // bar fill
    f32 momentary_lufs; // marker line
    f32 target_min_lufs; // extent of the good-colour region of the fill
    f32 target_max_lufs;
    f32 fade_lu = 6.0f; // distance beyond the target band over which the good colour fades to quiet/hot
    f32 min_lufs = -44.0f;
    f32 max_lufs = -8.0f;
};

// Single bar of short-term loudness with a momentary marker. The fill is a gradient: good within the target
// band, fading to quiet below it and hot above it.
void DrawLoudnessMeter(imgui::Context& imgui, Rect r, DrawLoudnessMeterOptions const& options);

struct DrawVerticalSliderOptions {
    u32 highlight_col;
    u32 line_col;
    Optional<f32> modulation_percent; // If set, draws a line at this position showing the modulated value.
    Span<f32 const> voice_blips_01 {}; // Per-voice markers drawn on the channel.
    GuiStyleSystem style_system;
    bool greyed_out;
    bool is_fake;
};

void DrawVerticalSlider(imgui::Context& imgui,
                        imgui::Id id,
                        Rect r,
                        f32 percent,
                        DrawVerticalSliderOptions const& options);

void DrawPeakMeter(imgui::Context& imgui,
                   Rect r,
                   StereoPeakMeter const* level,
                   DrawPeakMeterOptions const& options);

void DrawOverlayTooltipForRect(imgui::Context const& imgui, Fonts& fonts, DrawTooltipArgs const& args);

void DrawMidPanelScrollbars(imgui::Context const& imgui, imgui::ViewportScrollbars const& bars);

void DrawModalScrollbars(imgui::Context const& imgui, imgui::ViewportScrollbars const& bars);
void DrawModalScrollbarsDarkMode(imgui::Context const& imgui, imgui::ViewportScrollbars const& bars);
void DrawModalViewportBackgroundWithFullscreenDim(imgui::Context const& imgui);
void DrawOverlayViewportBackground(imgui::Context const& imgui);
