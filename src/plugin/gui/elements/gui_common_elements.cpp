// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/elements/gui_common_elements.hpp"

#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_element_drawing.hpp"

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
