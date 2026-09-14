// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/preferences.hpp"

// The preset that a fresh instance of Floe opens with. Either chosen explicitly by the user, or suggested by
// the most recently installed preset bank. An explicit choice always wins.

prefs::Descriptor DefaultPresetDescriptor();
prefs::Descriptor SuggestedDefaultPresetDescriptor();

struct DefaultPreset {
    String path;
    bool is_user_set;
};

Optional<DefaultPreset> ResolveDefaultPreset(prefs::PreferencesTable const& prefs);

void SetDefaultPreset(prefs::Preferences& prefs, String path);
void SetSuggestedDefaultPreset(prefs::Preferences& prefs, String path);

// Removes both the explicit and the suggested default.
void ClearDefaultPreset(prefs::Preferences& prefs);

bool IsDefaultPreset(prefs::PreferencesTable const& prefs, String path);
