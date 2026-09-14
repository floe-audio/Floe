// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/elements/gui_common_elements.hpp"

#include <IconsFontAwesome6.h>

#include "engine/engine_prefs.hpp"
#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui_framework/gui_live_edit.hpp"

static ColSet MidIconButtonColours(bool greyed_out, bool is_on = false) {
    if (greyed_out) {
        auto const dimmed = LiveColStruct(UiColMap::MidIconDimmed);
        return {.base = dimmed, .hot = dimmed, .active = dimmed};
    }
    return {
        .base = is_on ? LiveColStruct(UiColMap::MidTextOn) : LiveColStruct(UiColMap::MidIcon),
        .hot = LiveColStruct(UiColMap::MidTextHot),
        .active = LiveColStruct(UiColMap::MidTextOn),
    };
}

Box DoMidPanelPrevNextRow(GuiBuilder& builder, Box parent, f32 width) {
    return DoBox(builder,
                 {
                     .parent = parent,
                     .background_fill_colours = LiveColStruct(UiColMap::MidDarkSurface),
                     .round_background_corners = 0b1111,
                     .corner_rounding = k_corner_rounding,
                     .layout {
                         .size = {width, layout::k_hug_contents},
                         .contents_padding = {.l = 7.5f, .r = 2.9f},
                         .contents_direction = layout::Direction::Row,
                         .contents_align = layout::Alignment::Middle,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                     },
                 });
}

static Box DoMidIconButton(GuiBuilder& builder,
                           Box parent,
                           String icon,
                           String tooltip,
                           bool greyed_out,
                           f32 font_size = 0,
                           bool is_on = false) {
    auto const btn = DoBox(builder,
                           {
                               .parent = parent,
                               .id_extra = Hash(icon),
                               .layout {
                                   .size = layout::k_hug_contents,
                               },
                               .tooltip = tooltip,
                               .button_behaviour = imgui::ButtonConfig {},
                           });
    DoBox(builder,
          {
              .parent = btn,
              .text = icon,
              .size_from_text = true,
              .font = FontType::Icons,
              .font_size = font_size,
              .text_colours = MidIconButtonColours(greyed_out, is_on),
              .text_justification = TextJustification::Centred,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .margins = {.lrtb = 2.1f},
              },
          });
    return btn;
}

MidPanelPrevNextButtonsResult
DoMidPanelPrevNextButtons(GuiBuilder& builder, Box row, MidPanelPrevNextButtonsOptions const& options) {
    MidPanelPrevNextButtonsResult result {};

    result.prev_fired =
        DoMidIconButton(builder, row, ICON_FA_CARET_LEFT, options.prev_tooltip, options.greyed_out)
            .button_fired;
    result.next_fired =
        DoMidIconButton(builder, row, ICON_FA_CARET_RIGHT, options.next_tooltip, options.greyed_out)
            .button_fired;

    return result;
}

Box DoMidPanelIconButton(GuiBuilder& builder, Box row, MidPanelIconButtonOptions const& options) {
    auto const [icon, font_size] = ({
        struct {
            String icon;
            f32 font_size;
        } v;
        switch (options.icon) {
            case MidPanelIcon::Shuffle: v = {ICON_FA_SHUFFLE, k_font_icons_size * 0.82f}; break;
            case MidPanelIcon::Unload: v = {ICON_FA_XMARK, k_font_icons_size * 0.9f}; break;
            case MidPanelIcon::Power: v = {ICON_FA_POWER_OFF, k_font_icons_size * 0.85f}; break;
        }
        v;
    });
    return DoMidIconButton(builder, row, icon, options.tooltip, options.greyed_out, font_size, options.is_on);
}

static String FormatDbOrNegInf(ArenaAllocator& arena, f32 amp, f32 db) {
    if (amp <= 0.0f) return "-∞"_s;
    return fmt::Format(arena, "{.1}", db);
}

MeterTooltipText PeakMeterTooltipText(ArenaAllocator& arena,
                                      StereoPeakMeter const& level,
                                      DrawPeakMeterOptions const& options) {
    auto const raw = level.GetSnapshot().levels;
    auto const db = 20 * Log10(Max(raw, f32x2 {0.0000000001f}));

    DynamicArray<char> buf {arena};
    fmt::Append(
        buf,
        "This is a stereo peak meter, showing the loudest moment of the signal.\n\nDisplay range: {.0} to {.0} dB",
        options.min_db,
        options.max_db);
    if (options.marker_db && options.marker_description.size)
        fmt::Append(buf, "\n{}: {.1} dB", options.marker_description, *options.marker_db);
    if (options.show_db_markers) fmt::Append(buf, "\nLines every: {.0} dB", options.marker_interval_db);
    if (options.show_warning_zones)
        fmt::Append(buf, "\nYellow region: {.0} to {.0} dB", options.yellow_zone_min_db, 0.0f);

    auto const both_silent = raw[0] <= 0.0f && raw[1] <= 0.0f;

    return {
        .value_popup = both_silent ? String {}
                                   : String {fmt::Format(arena,
                                                         "{} | {} dB",
                                                         FormatDbOrNegInf(arena, raw[0], db[0]),
                                                         FormatDbOrNegInf(arena, raw[1], db[1]))},
        .tooltip = buf.ToOwnedSpan(),
    };
}

MeterTooltipText GainReductionMeterTooltipText(ArenaAllocator& arena,
                                               DrawGainReductionMeterOptions const& options) {
    return {
        .value_popup = fmt::Format(arena, "{.1} dB reduction", options.gain_reduction_db),
        .tooltip = fmt::Format(
            arena,
            "This is a gain reduction meter, showing how much the limiter is turning the signal down.\n\nRange: {.0} to {.0} dB",
            0.0f,
            options.max_reduction_db),
    };
}

MeterTooltipText LoudnessMeterTooltipText(ArenaAllocator& arena, DrawLoudnessMeterOptions const& options) {
    return {
        .value_popup = fmt::Format(arena,
                                   "{.1} | {.1} LUFS\nMomentary | Short-term loudness",
                                   options.momentary_lufs,
                                   options.short_term_lufs),
        .tooltip = fmt::Format(arena,
                               "Range: {.0} to {.0} LUFS\nGreen region: {.0} to {.0} LUFS",
                               options.min_lufs,
                               options.max_lufs,
                               options.target_min_lufs,
                               options.target_max_lufs),
    };
}

bool Tooltip(GuiState& g, imgui::Id id, Rect window_r, TooltipArgs const& args) {
    return Tooltip(g.builder, id, window_r, args);
}

void DoExperimentalModeIndicatorIfNeeded(GuiBuilder& builder,
                                         Box parent,
                                         prefs::Preferences const& preferences) {
    if constexpr (k_num_experimental_parameters == 0) return;
    if (!prefs::GetBool(preferences, ExperimentalParamsPreferenceDescriptor())) return;

    auto const flask = DoBox(
        builder,
        {
            .parent = parent,
            .layout {
                .size = layout::k_hug_contents,
                .contents_padding = {.lr = 5, .tb = 3},
            },
            .tooltip =
                "Experimental mode is enabled. Features may change or be removed. Presets and DAW projects that use experimental features may not be compatible with future versions of Floe. Disable in preferences."_s,
        });
    DoBox(builder,
          {
              .parent = flask,
              .text = ICON_FA_FLASK,
              .size_from_text = true,
              .font = FontType::Icons,
              .font_size = k_font_icons_size * 0.9f,
              .text_colours = Col {.c = Col::SkyBlue},
          });
}

Box DoTabButton(GuiBuilder& builder, Box parent, String text, TabButtonOptions const& options, u64 id_extra) {
    auto const btn =
        DoBox(builder,
              {
                  .parent = parent,
                  .id_extra = id_extra,
                  .background_fill_colours =
                      options.is_selected ? Colours {LiveColStruct(UiColMap::MidTabBackgroundActive)}
                                          : Colours {ColSet {
                                                .base = Col {.c = Col::None},
                                                .hot = LiveColStruct(UiColMap::MidTabBackgroundHot),
                                                .active = LiveColStruct(UiColMap::MidTabBackgroundActive),
                                            }},
                  .round_background_corners = 0b1111,
                  .corner_rounding = 4.0f,
                  .layout {
                      .size = {options.width, layout::k_hug_contents},
                      .contents_padding = options.width == layout::k_hug_contents ? Margins {.lr = 8, .tb = 4}
                                                                                  : Margins {.tb = 4},
                      .contents_gap = 4,
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Middle,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                  },
                  .tooltip = options.tooltip,
                  .button_behaviour = imgui::ButtonConfig {},
              });

    DoBox(builder,
          {
              .parent = btn,
              .text = text,
              .size_from_text = true,
              .font = FontType::Heading3,
              .text_colours = options.is_selected ? Colours {LiveColStruct(UiColMap::MidTabTextActive)}
                                                  : Colours {ColSet {
                                                        .base = LiveColStruct(UiColMap::MidTabText),
                                                        .hot = LiveColStruct(UiColMap::MidTabTextHot),
                                                        .active = LiveColStruct(UiColMap::MidTabTextActive),
                                                    }},
              .text_justification = TextJustification::Centred,
              .parent_dictates_hot_and_active = true,
          });

    if (options.show_dot_indicator) {
        auto const dot_box = DoBox(builder,
                                   {
                                       .parent = btn,
                                       .parent_dictates_hot_and_active = true,
                                       .layout {
                                           .size = {4, 4},
                                       },
                                   });
        if (auto const r = BoxRect(builder, dot_box)) {
            auto const window_r = builder.imgui.ViewportRectToWindowRect(*r);
            auto const col =
                options.is_selected ? LiveCol(UiColMap::MidTabTextActive) : LiveCol(UiColMap::MidTabText);
            builder.imgui.draw_list->AddCircleFilled(window_r.Centre(), WwToPixels(2.0f), col);
        }
    }

    return btn;
}

Box DoToggleIcon(GuiBuilder& builder, Box parent, ToggleIconOptions const& options) {
    auto on_colour = options.on_colour ? *options.on_colour : LiveColStruct(UiColMap::MidTextOn);
    if (options.greyed_out) on_colour.alpha = 150;

    return DoBox(
        builder,
        {
            .parent = parent,
            .text = options.state ? ICON_FA_TOGGLE_ON : ICON_FA_TOGGLE_OFF,
            .font = FontType::Icons,
            .font_size = k_font_icons_size * 0.75f,
            .text_colours = options.state ? Colours {on_colour} : MidIconButtonColours(options.greyed_out),
            .text_justification = options.justify,
            .parent_dictates_hot_and_active = options.parent_dictates_hot_and_active,
            .layout {
                .size = {options.width == layout::k_hug_contents ? 20 : options.width, k_mid_button_height},
            },
        });
}
