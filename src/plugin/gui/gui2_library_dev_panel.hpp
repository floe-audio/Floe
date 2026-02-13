// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "foundation/foundation.hpp"

struct Engine;
struct GuiBuilder;
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
    static constexpr u64 k_panel_id = SourceLocationHash();
    Tab tab = Tab::TagBuilder;
    bool modeless = true;
};

void DoLibraryDevPanel(GuiBuilder& builder, LibraryDevPanelContext& context, LibraryDevPanelState& state);
