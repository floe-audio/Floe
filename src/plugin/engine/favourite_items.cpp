// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "favourite_items.hpp"

#include "tests/framework.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

#include "plugin/sample_lib_server/sample_library_server.hpp"

constexpr auto k_favourite_inst_key_legacy = "favourite-instrument"_s;
constexpr auto k_favourite_ir_key_legacy = "favourite-ir"_s;

bool IsFavourite(prefs::PreferencesTable const& prefs, prefs::Key const& key, u64 item_hash) {
    auto value_list = prefs::LookupValues(prefs, key);
    for (auto value = value_list; value; value = value->next)
        if (auto const v = value->TryGet<s64>())
            if (*v == (s64)item_hash) return true;
    return false;
}

void AddFavourite(prefs::Preferences& prefs, prefs::Key const& key, u64 item_hash) {
    prefs::AddValue(prefs, key, (s64)item_hash);
}

void RemoveFavourite(prefs::Preferences& prefs, prefs::Key const& key, u64 item_hash) {
    prefs::RemoveValue(prefs, key, (s64)item_hash);
}

void ToggleFavourite(prefs::Preferences& prefs, prefs::Key const& key, u64 item_hash, bool is_favourite) {
    if (is_favourite)
        RemoveFavourite(prefs, key, item_hash);
    else
        AddFavourite(prefs, key, item_hash);
}

bool HasLegacyFavourites(prefs::PreferencesTable const& prefs) {
    return prefs::LookupValues(prefs, k_favourite_inst_key_legacy) ||
           prefs::LookupValues(prefs, k_favourite_ir_key_legacy);
}

// We used to use hashes for favourites that are no longer guaranteed to be stable across library updates. We
// migrate these to new stable hashes - this code will most likely run before any libraries are updated and so
// it should be seamless to the user. However, if the user upgrades libraries that have name changes before
// running this code, some favourites may be lost.
void MigrateLegacyFavourites(prefs::Preferences& prefs, sample_lib_server::Server& server) {
    // Check if old keys exist
    auto const old_inst_values = prefs::LookupValues(prefs, k_favourite_inst_key_legacy);
    auto const old_ir_values = prefs::LookupValues(prefs, k_favourite_ir_key_legacy);
    if (!old_inst_values && !old_ir_values) return; // Nothing to migrate

    // Iterate through all loaded libraries and migrate any favorites found
    for (auto& lib_node : sample_lib_server::LibrariesList(server)) {
        if (auto listed_lib = lib_node.TryScoped()) {
            if (!listed_lib->lib) continue;
            auto const& lib = *listed_lib->lib;

            // Migrate instruments
            if (old_inst_values) {
                for (auto const& [_, inst, _] : lib.insts_by_id) {
                    auto const legacy_hash = sample_lib::LegacyPersistentInstHash(*inst);
                    if (IsFavourite(prefs, k_favourite_inst_key_legacy, legacy_hash)) {
                        auto const new_hash = sample_lib::PersistentInstHash(*inst);
                        prefs::AddValue(prefs,
                                        k_favourite_inst_key,
                                        (s64)new_hash,
                                        {.dont_send_on_change_event = true});
                    }
                }
            }

            // Migrate IRs
            if (old_ir_values) {
                for (auto const [_, ir, _] : lib.irs_by_id) {
                    auto const legacy_hash = sample_lib::LegacyPersistentIrHash(*ir);
                    if (IsFavourite(prefs, k_favourite_ir_key_legacy, legacy_hash)) {
                        auto const new_hash = sample_lib::PersistentIrHash(*ir);
                        prefs::AddValue(prefs,
                                        k_favourite_ir_key,
                                        (s64)new_hash,
                                        {.dont_send_on_change_event = true});
                    }
                }
            }
        }
    }

    prefs::Remove(prefs, k_favourite_inst_key_legacy, {.dont_send_on_change_event = true});
    prefs::Remove(prefs, k_favourite_ir_key_legacy, {.dont_send_on_change_event = true});

    prefs.write_to_file_needed = true;
}
