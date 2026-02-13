// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

#include "gui_fwd.hpp"

struct DeveloperPanel {
    imgui::Context& imgui;
    Engine& engine;
    f32 y_pos = 0;
};

void DoDeveloperPanel(DeveloperPanel& g);
