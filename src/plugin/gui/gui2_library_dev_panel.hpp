// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "foundation/foundation.hpp"

struct Engine;
struct GuiBoxSystem;

struct LibraryDevPanelContext {
    Engine& engine;
};

struct LibraryDevPanelState {
    enum class Tab : u32 {
        TagBuilder,
        Count,
    };
    bool open = false;
    Tab tab = Tab::TagBuilder;
};

void DoLibraryDevPanel(GuiBoxSystem& box_system,
                       LibraryDevPanelContext& context,
                       LibraryDevPanelState& state);
