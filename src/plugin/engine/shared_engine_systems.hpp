// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/thread_extra/thread_pool.hpp"

#include "common_infrastructure/paths.hpp"
#include "common_infrastructure/persistent_store.hpp"
#include "common_infrastructure/preferences.hpp"
#include "common_infrastructure/sentry/sentry.hpp"

#include "check_for_update.hpp"
#include "clap/plugin.h"
#include "preset_server/preset_server.hpp"
#include "sample_lib_server/sample_library_server.hpp"

// Shared across plugin instances of the engine. This usually happens when the plugin is loaded multiple times
// in the host. Sometimes though, the host will load plugin instances in separate processes for
// crash-protection.

struct SharedEngineSystems {
    SharedEngineSystems(Span<sentry::Tag const> tags);
    ~SharedEngineSystems();

    void StartPollingThreadIfNeeded();

    void RegisterFloeInstance(FloeInstanceIndex index);
    void UnregisterFloeInstance(FloeInstanceIndex index);

    void AddMirageFoldersIfNeeded();

    RecursiveMutex registered_floe_instances_mutex {};
    DynamicArrayBounded<FloeInstanceIndex, k_max_num_floe_instances> registered_floe_instances {};

    ArenaAllocator arena;
    ThreadsafeErrorNotifications error_notifications {};
    FloePaths paths;
    prefs::Preferences prefs;
    persistent_store::Store persistent_store;
    ThreadPool thread_pool;
    sample_lib_server::Server sample_library_server;
    Optional<LockableSharedMemory> shared_attributions_store {};
    PresetServer preset_server;
    check_for_update::State check_for_update_state;

    Thread polling_thread {};
    Mutex polling_mutex {};
    Atomic<u32> polling_running = 0;
};
