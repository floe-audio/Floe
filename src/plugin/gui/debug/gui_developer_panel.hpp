// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

#include "gui/core/gui_fwd.hpp"
#include "gui_framework/gui_imgui.hpp"

struct DeveloperPanel {
    imgui::Context& imgui;
    Engine& engine;
    f32 y_pos = 0;
};

void DoDeveloperPanel(DeveloperPanel& g);

bool DoBasicTextButton(imgui::Context& imgui,
                       imgui::ButtonConfig flags,
                       Rect viewport_r,
                       imgui::Id id,
                       String str);

void DoBasicWhiteText(imgui::Context& imgui, Rect viewport_r, String str);
