// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/elements/gui_common_elements.hpp"

#include <IconsFontAwesome6.h>

#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui_framework/gui_live_edit.hpp"

static ColSet MidIconButtonColours() {
    return {
        .base = LiveColStruct(UiColMap::MidIcon),
        .hot = LiveColStruct(UiColMap::MidTextHot),
        .active = LiveColStruct(UiColMap::MidTextOn),
    };
}

static Margins ParamControlPadding() {
    return {
        .l = LiveWw(UiSizeId::ParamControlPadL),
        .r = LiveWw(UiSizeId::ParamControlPadR),
    };
}

f32 TextButtonHeight() { return k_font_body_size * LiveRaw(UiSizeId::TextBtnHPct) / 100.0f; }

Box DoMidPanelPrevNextRow(GuiBuilder& builder, Box parent, f32 width) {
    return DoBox(builder,
                 {
                     .parent = parent,
                     .background_fill_colours = LiveColStruct(UiColMap::MidDarkSurface),
                     .round_background_corners = 0b1111,
                     .corner_rounding = LiveWw(UiSizeId::CornerRounding),
                     .layout {
                         .size = {width, layout::k_hug_contents},
                         .contents_padding = ParamControlPadding(),
                         .contents_direction = layout::Direction::Row,
                         .contents_align = layout::Alignment::Middle,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                     },
                 });
}

static Box DoMidIconButton(GuiBuilder& builder, Box parent, String icon, String tooltip, f32 font_size = 0) {
    auto const margin = LiveWw(UiSizeId::IconButtonMargin);
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
              .text_colours = MidIconButtonColours(),
              .text_justification = TextJustification::Centred,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .margins = {.lrtb = margin},
              },
          });
    return btn;
}

MidPanelPrevNextButtonsResult
DoMidPanelPrevNextButtons(GuiBuilder& builder, Box row, MidPanelPrevNextButtonsOptions const& options) {
    MidPanelPrevNextButtonsResult result {};

    result.prev_fired = DoMidIconButton(builder, row, ICON_FA_CARET_LEFT, options.prev_tooltip).button_fired;
    result.next_fired = DoMidIconButton(builder, row, ICON_FA_CARET_RIGHT, options.next_tooltip).button_fired;

    return result;
}

Box DoMidPanelShuffleButton(GuiBuilder& builder, Box row, MidPanelShuffleButtonOptions const& options) {
    return DoMidIconButton(builder, row, ICON_FA_SHUFFLE, options.tooltip, k_font_icons_size * 0.82f);
}

bool Tooltip(GuiState& g, imgui::Id id, Rect window_r, String str, TooltipOptions const& options) {
    if (!options.ignore_show_tooltips_preference &&
        !prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::ShowTooltips)))
        return false;

    if (g.imgui.TooltipBehaviour(window_r, id)) {
        DrawOverlayTooltipForRect(g.imgui,
                                  g.fonts,
                                  str,
                                  {
                                      .r = window_r,
                                      .avoid_r = options.avoid_r.ValueOr(window_r),
                                      .justification = options.justification,
                                  });
        return true;
    }

    return false;
}

Box DoToggleIcon(GuiBuilder& builder, Box parent, ToggleIconOptions const& options) {
    auto const height = LiveWw(UiSizeId::PageHeadingHeight);
    auto const width = options.width != 0 ? options.width : LiveWw(UiSizeId::PageHeadingTextOffset);

    return DoBox(
        builder,
        {
            .parent = parent,
            .text = options.state ? ICON_FA_TOGGLE_ON : ICON_FA_TOGGLE_OFF,
            .font = FontType::Icons,
            .font_size = k_font_icons_size * 0.75f,
            .text_colours = options.state
                                ? Colours {options.on_colour ? *options.on_colour
                                                             : LiveColStruct(UiColMap::MidTextOn)}
                                : (options.greyed_out ? Colours {ColSet {
                                                            .base = LiveColStruct(UiColMap::MidTextDimmed),
                                                            .hot = LiveColStruct(UiColMap::MidIcon),
                                                            .active = LiveColStruct(UiColMap::MidTextOn),
                                                        }}
                                                      : Colours {ColSet {
                                                            .base = LiveColStruct(UiColMap::MidIcon),
                                                            .hot = LiveColStruct(UiColMap::MidIcon),
                                                            .active = LiveColStruct(UiColMap::MidTextOn),
                                                        }}),
            .text_justification = options.justify,
            .parent_dictates_hot_and_active = options.parent_dictates_hot_and_active,
            .layout {
                .size = {width, height},
            },
        });
}
