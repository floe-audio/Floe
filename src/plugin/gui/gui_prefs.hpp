// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/preferences.hpp"

enum class GuiSetting : u8 {
    WindowWidth,
    ShowTooltips,
    ShowKeyboard,
    HighContrastGui,
    ShowInstanceName,
    Count,
};

prefs::Descriptor SettingDescriptor(GuiSetting);

UiSize DesiredAspectRatio(prefs::Preferences const&);
Optional<UiSize> DesiredWindowSize(prefs::Preferences const&);
