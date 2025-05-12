// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "common_infrastructure/paths.hpp"
#include "common_infrastructure/preferences.hpp"

#include "state/state_snapshot.hpp"

struct AutosaveState {
    enum class State { Idle, SaveRequested, Saved };
    Atomic<u16> max_autosaves_per_instance {};
    Atomic<u16> autosave_delete_after_days {};
    Mutex instance_id_mutex {};
    DynamicArrayBounded<char, k_max_instance_id_size> instance_id;
    TimePoint last_save_time {};
    Mutex mutex {};
    StateSnapshot snapshot {};
    State state {State::Idle};
};

// Run from main thread
// Check if an autosave is needed, and if so, create a snapshot and queue it.
void InitAutosaveState(AutosaveState& state,
                       prefs::PreferencesTable const& prefs,
                       u64& random_seed,
                       StateSnapshot const& initial_state);
bool AutosaveNeeded(AutosaveState const& state, prefs::Preferences const& preferences);
void QueueAutosave(AutosaveState& state, StateSnapshot const& snapshot);
void SetInstanceId(AutosaveState& state, String instance_id);

// threadsafe
DynamicArrayBounded<char, k_max_instance_id_size> InstanceId(AutosaveState& state);

enum class AutosaveSetting : u8 {
    AutosaveIntervalSeconds,
    MaxAutosavesPerInstance,
    AutosaveDeleteAfterDays,
    Count,
};

// Use with prefs::SetValue, prefs::GetValue
prefs::Descriptor SettingDescriptor(AutosaveSetting setting);

void OnPreferenceChanged(AutosaveState& state, prefs::Key const& key, prefs::Value const* value);

// Run from background thread
void AutosaveToFileIfNeeded(AutosaveState& state, FloePaths const& paths);
