// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui_framework/gui_imgui.hpp"

imgui::ViewportConfig FloeStandardConfig(imgui::Context const& imgui,
                                         imgui::DrawViewportBackgroundFunction draw_background);
