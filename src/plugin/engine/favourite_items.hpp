// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/preferences.hpp"

namespace sample_lib_server {
struct Server;
}

constexpr auto k_favourite_inst_key = "favourite-instrument-v2"_s;
constexpr auto k_favourite_ir_key = "favourite-ir-v2"_s;

bool IsFavourite(prefs::PreferencesTable const& prefs, prefs::Key const& key, u64 item_hash);
void AddFavourite(prefs::Preferences& prefs, prefs::Key const& key, u64 item_hash);
void RemoveFavourite(prefs::Preferences& prefs, prefs::Key const& key, u64 item_hash);
void ToggleFavourite(prefs::Preferences& prefs, prefs::Key const& key, u64 item_hash, bool is_favourite);

bool HasLegacyFavourites(prefs::PreferencesTable const& prefs);
void MigrateLegacyFavourites(prefs::Preferences& prefs, sample_lib_server::Server& server);
