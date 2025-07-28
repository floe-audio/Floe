// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

struct Gui;

enum class BottomPanelType : u8 {
    Play,
    EditMacros,
    Count,
};

struct BottomPanelState {
    BottomPanelType type = BottomPanelType::Play;
};

void BotPanel(Gui* g, Rect r);
