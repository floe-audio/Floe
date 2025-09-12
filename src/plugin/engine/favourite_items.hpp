// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/preferences.hpp"

PUBLIC bool IsFavourite(prefs::PreferencesTable const& prefs, prefs::Key const& key, s64 item_hash) {
    auto value_list = prefs::LookupValues(prefs, key);
    for (auto value = value_list; value; value = value->next)
        if (auto const v = value->TryGet<s64>())
            if (*v == item_hash) return true;
    return false;
}

PUBLIC void AddFavourite(prefs::Preferences& prefs, prefs::Key const& key, s64 item_hash) {
    prefs::AddValue(prefs, key, item_hash);
}

PUBLIC void RemoveFavourite(prefs::Preferences& prefs, prefs::Key const& key, s64 item_hash) {
    prefs::RemoveValue(prefs, key, item_hash);
}

PUBLIC void
ToggleFavourite(prefs::Preferences& prefs, prefs::Key const& key, s64 item_hash, bool is_favourite) {
    if (is_favourite)
        RemoveFavourite(prefs, key, item_hash);
    else
        AddFavourite(prefs, key, item_hash);
}
