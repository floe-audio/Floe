// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

struct PreferencesPanelState {
    enum class Tab : u32 {
        General,
        Folders,
        Packages,
        Count,
    };
    static constexpr u64 k_panel_id = HashFnv1a("prefs-panel");
    Tab tab {Tab::General};
};
