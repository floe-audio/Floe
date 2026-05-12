// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

#include "gui/core/gui_subsystem.hpp"

struct GuiState;

enum class BottomPanelType : u8 {
    Play,
    EditMacros,
    Count,
};

struct BottomPanelState {
    BottomPanelType type = BottomPanelType::Play;
};

void BotPanel(GuiState& g, Rect r);

extern GuiSubsystem<BottomPanelState> const g_bot_panel_subsystem;
