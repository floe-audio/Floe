// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/preferences.hpp"

enum class GuiPreference : u8 {
    WindowWidth,
    ShowTooltips,
    HighContrastGui,
    ShowInstanceName,
    ExperimentalFeatures,
    Count,
};

prefs::Descriptor SettingDescriptor(GuiPreference);

Optional<UiSize> DesiredWindowSize(prefs::Preferences const&);
