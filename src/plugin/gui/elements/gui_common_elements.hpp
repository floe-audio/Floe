// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/core/gui_fwd.hpp"
#include "gui_framework/gui_builder.hpp"

struct TooltipOptions {
    Optional<Rect> avoid_r {}; // If nullopt, uses the window_r.
    bool ignore_show_tooltips_preference = false;
    TooltipJustification justification = TooltipJustification::AboveOrBelow;
};
bool Tooltip(GuiState& g, imgui::Id id, Rect window_r, String str, TooltipOptions const& options);
