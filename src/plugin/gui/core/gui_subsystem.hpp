// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/persistent_store.hpp"

#include "gui_framework/gui_imgui.hpp"

namespace prefs {
struct Preferences;
}
struct Engine;

struct ScreenshotPrepContext {
    imgui::Context& imgui;
    prefs::Preferences& prefs;
    Engine& engine;
};

template <typename State>
struct GuiSubsystem {
    void (*encode)(State const&, imgui::Context&, persistent_store::StoreTable&, ArenaAllocator&) = nullptr;
    void (*decode)(State&, imgui::Context&, persistent_store::StoreTable const&) = nullptr;
};
