// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/preferences.hpp"

enum class GuiPreference : u8 {
    WindowWidth,
    ShowTooltips,
    InstantValueReadouts,
    HighContrastGui,
    ShowInstanceName,
    ShowLufsMeter,
    ShowCutoffInSemitones,
    Count,
};

prefs::Descriptor SettingDescriptor(GuiPreference);

// Resolves the ShowCutoffInSemitones preference. Only affects params with flags.cutoff_frequency set.
bool ShowCutoffInSemitones(prefs::Preferences const&);

Optional<UiSize> DesiredWindowSize(prefs::Preferences const&);
