// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "foundation/foundation.hpp"

struct Engine;
struct GuiBoxSystem;
struct Notifications;

struct LibraryDevPanelContext {
    Engine& engine;
    Notifications& notifications;
};

struct LibraryDevPanelState {
    enum class Tab : u32 {
        TagBuilder,
        Utilities,
        Count,
    };
    bool open = false;
    Tab tab = Tab::TagBuilder;
    bool modeless = true;
};

void DoLibraryDevPanel(GuiBoxSystem& box_system,
                       LibraryDevPanelContext& context,
                       LibraryDevPanelState& state);
