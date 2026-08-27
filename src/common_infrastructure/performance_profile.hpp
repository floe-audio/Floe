// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/preferences.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

namespace perf_profile {

constexpr usize k_max_name_size = 100;
constexpr String k_profile_section_prefix = "performance-profile"_s;
constexpr String k_defaults_section = "performance-controls-defaults"_s;

struct Profile {
    bool operator==(Profile const&) const = default;

    DynamicArrayBounded<char, k_max_name_size> name {};
    PerformanceControls controls {};
};

enum class NameError : u8 {
    Empty,
    TooLong,
    InvalidCharacters,
    AlreadyExists,
};

// All profile names, sorted. The returned strings are allocated in 'arena'.
DynamicArray<String> ListProfileNames(prefs::PreferencesTable const& prefs, ArenaAllocator& arena);

// Returns the profile with this name, or nullopt if there's no such profile. Invalid values are ignored or
// clamped.
Optional<Profile> LookupProfile(prefs::PreferencesTable const& prefs, String name);

// The saved new-instance settings, or nullopt if none have been saved. The result has no name.
Optional<Profile> LookupDefault(prefs::PreferencesTable const& prefs);

// The settings used to seed new instances: the saved snapshot if one exists, otherwise the fallback (factory
// CC mappings and default PerformanceControls::Settings).
Profile DefaultOrFallback(prefs::PreferencesTable const& prefs);

// The settings used when no new-instance snapshot has been saved.
Profile FallbackProfile();

// Saves 'profile' as the settings that new instances start with. The profile's name is ignored.
void SaveDefault(prefs::Preferences& prefs, Profile const& profile);

// Creates a new profile, or overwrites the existing one with the same name.
void SaveProfile(prefs::Preferences& prefs, Profile const& profile);

void DeleteProfile(prefs::Preferences& prefs, String name);

void RenameProfile(prefs::Preferences& prefs, String old_name, String new_name);

// Trims and validates a candidate profile name. 'ignore' allows renaming a profile to its own name.
Optional<NameError>
ValidateProfileName(prefs::PreferencesTable const& prefs, String name, Optional<String> ignore = {});

// One-time migration from the legacy CC default system (pinned CCs in the preferences file + the
// default-mappings toggle) into the new-instance-defaults section. The legacy preferences keys are left in
// place so downgrading to an older Floe still works. Runs only if no profiles and no new-instance settings
// exist yet.
void MigrateIfNeeded(prefs::Preferences& prefs);

} // namespace perf_profile
