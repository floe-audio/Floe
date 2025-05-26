// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/preferences.hpp"

namespace check_for_update {

constexpr Version k_no_version = Version {0u};

struct State {
    // Atomic doesn't like a 24-bit value so we pad it to 32-bits.
    struct PaddedVersion {
        Version version = k_no_version;
        u8 unused_padding {0};
    };
    static_assert(sizeof(PaddedVersion) == 4, "padding might be wrong");

    enum class StateEnum { Inactive, ShouldCheck, Checked };

    Atomic<StateEnum> state = StateEnum::Inactive;
    Atomic<PaddedVersion> latest_version {};
    Atomic<bool> checking_allowed {false};
};

struct NewVersion {
    Version version {};
    bool is_ignored {false}; // if the user has ignored this version
};
// Threadsafe (probably main thread)
Optional<NewVersion> NewerVersionAvailable(State& state, prefs::Preferences const& prefs);
inline bool ShowNewVersionIndicator(State& state, prefs::Preferences const& prefs) {
    auto const v = NewerVersionAvailable(state, prefs);
    return v && !v->is_ignored;
}

// Main thread
void Init(State& state, prefs::Preferences const& prefs);
void IgnoreUpdatesUntilAfter(prefs::Preferences& prefs, Version version);
void OnPreferenceChanged(State& state, prefs::Key const& key, prefs::Value const* value);

// Threadsafe (probably main thread). CheckForUpdateIfNeeded will not do the HTTP request until after this is
// called, allowing to defer the request until it's actually needed.
void FetchLatestIfNeeded(State& state);

// Main thread. Use with prefs::SetValue, prefs::GetValue.
prefs::Descriptor CheckAllowedPrefDescriptor();

// Run from background thread. Can be polled, it will only check once.
void CheckForUpdateIfNeeded(State& state);

} // namespace check_for_update
