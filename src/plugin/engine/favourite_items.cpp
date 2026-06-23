// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "favourite_items.hpp"

#include "tests/framework.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"
#include "common_infrastructure/sample_library/server/sample_library_server.hpp"

#include "plugin/preset_server/preset_server.hpp"

constexpr auto k_favourite_inst_key_legacy = "favourite-instrument"_s;
constexpr auto k_favourite_ir_key_legacy = "favourite-ir"_s;
constexpr auto k_favourite_preset_key_legacy = "favourite-preset"_s;

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

    // Iterate through all loaded libraries and migrate any favourites found
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

bool HasLegacyPresetFavourites(prefs::PreferencesTable const& prefs) {
    return prefs::LookupValues(prefs, k_favourite_preset_key_legacy) != nullptr;
}

// Pre-UUID, preset favourites were keyed by file_hash = xxh3(file_bytes) + fnv1a(subpath). We
// rebuild each preset's legacy file_hash from its scanned bytes and translate matched favourites to
// the preset's new preset_uuid. This runs once libraries/folders have finished scanning and then
// retires the legacy key for good - mirroring the instrument/IR migration above. As with that, any
// favourite whose folder isn't scanned at migration time (unmounted drive, disabled, scan errored)
// is dropped; retrying indefinitely would never terminate for permanently-missing presets.
void MigrateLegacyPresetFavourites(prefs::Preferences& prefs, PresetServer& server) {
    auto const old_values = prefs::LookupValues(prefs, k_favourite_preset_key_legacy);
    if (!old_values) return;

    ArenaAllocatorWithInlineStorage<2048> scratch_arena {PageAllocator::Instance()};
    auto const [snapshot, handle] = BeginReadFolders(server, scratch_arena);
    DEFER { EndReadFolders(server, handle); };

    for (auto const folder_listing : snapshot.folders) {
        auto const folder = folder_listing->folder;
        if (!folder) continue;
        for (auto const& preset : folder->presets)
            if (IsFavourite(prefs, k_favourite_preset_key_legacy, preset.file_hash))
                prefs::AddValue(prefs,
                                k_favourite_preset_key,
                                (s64)preset.preset_uuid,
                                {.dont_send_on_change_event = true});
    }

    prefs::Remove(prefs, k_favourite_preset_key_legacy, {.dont_send_on_change_event = true});

    prefs.write_to_file_needed = true;
}

TEST_CASE(TestMigrateLegacyPresetFavourites) {
    auto const folder =
        (String)path::Join(tester.scratch_arena,
                           Array {tests::TestFilesFolder(tester), tests::k_preset_test_files_subdir});

    ThreadsafeErrorNotifications errors;
    PresetServer server {.error_notifications = errors};
    InitPresetServer(server, folder);
    DEFER { ShutdownPresetServer(server); };

    StartScanningIfNeeded(server);
    REQUIRE(WaitIfFoldersAreScanning(server, k_nullopt));

    // Discover a real preset's hashes from the scan so we don't have to replicate the legacy
    // file_hash computation here.
    u64 known_file_hash = 0;
    u64 known_uuid = 0;
    {
        auto const [snapshot, handle] = BeginReadFolders(server, tester.scratch_arena);
        DEFER { EndReadFolders(server, handle); };
        for (auto const folder_listing : snapshot.folders) {
            if (!folder_listing->folder) continue;
            if (folder_listing->folder->presets.size) {
                known_file_hash = folder_listing->folder->presets[0].file_hash;
                known_uuid = folder_listing->folder->presets[0].preset_uuid;
                break;
            }
        }
    }
    REQUIRE(known_file_hash != 0);

    SUBCASE("matched favourite is translated and legacy key retired") {
        prefs::Preferences prefs;
        prefs::AddValue(prefs, k_favourite_preset_key_legacy, (s64)known_file_hash);

        MigrateLegacyPresetFavourites(prefs, server);

        CHECK(IsFavourite(prefs, k_favourite_preset_key, known_uuid));
        CHECK(!prefs::LookupValues(prefs, k_favourite_preset_key_legacy));
        CHECK(prefs.write_to_file_needed);
    }

    SUBCASE("unmatched favourite is dropped and the legacy key is retired") {
        prefs::Preferences prefs;
        constexpr u64 k_unscanned_hash = 0xDEADBEEF;
        prefs::AddValue(prefs, k_favourite_preset_key_legacy, (s64)k_unscanned_hash);

        MigrateLegacyPresetFavourites(prefs, server);

        CHECK(!prefs::LookupValues(prefs, k_favourite_preset_key_legacy));
        CHECK(!prefs::LookupValues(prefs, k_favourite_preset_key));
    }

    return k_success;
}

TEST_REGISTRATION(RegisterFavouriteItemsTests) { REGISTER_TEST(TestMigrateLegacyPresetFavourites); }
