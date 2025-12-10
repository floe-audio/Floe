// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/autosave.hpp"
#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/sample_library/attribution_requirements.hpp"
#include "common_infrastructure/state/instrument.hpp"
#include "common_infrastructure/state/macros.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "engine/package_installation.hpp"
#include "processor/processor.hpp"
#include "sample_lib_server/sample_library_server.hpp"
#include "shared_engine_systems.hpp"

struct Engine : ProcessorListener {
    struct PendingStateChange {
        ~PendingStateChange() {
            for (auto& r : retained_results)
                r.Release();
        }

        ArenaAllocator arena {PageAllocator::Instance()};
        DynamicArrayBounded<sample_lib_server::RequestId, k_num_layers + 1> requests;
        DynamicArrayBounded<sample_lib_server::LoadResult, k_num_layers + 1> retained_results;
        StateSnapshotWithName snapshot;
        StateSource source;
    };

    struct LastSnapshot {
        LastSnapshot() { name_or_path.name_or_path = "Default"; }

        void Set(StateSnapshotWithName const& snapshot) {
            state = snapshot.state;
            SetName(snapshot.name);
        }

        void SetName(StateSnapshotName const& m) {
            name_arena.ResetCursorAndConsolidateRegions();
            name_or_path = m.Clone(name_arena);
        }

        ArenaAllocatorWithInlineStorage<1000> name_arena {Malloc::Instance()};
        StateSnapshot state {};
        StateSnapshotName name_or_path {};
    };

    Engine(clap_host const& host,
           SharedEngineSystems& shared_engine_systems,
           PluginInstanceMessages& plugin_instance_messages);
    ~Engine();

    auto& Layer(u32 index) { return processor.layer_processors[index]; }

    void OnProcessorChange(ChangeFlags) override;

    clap_host const& host;
    SharedEngineSystems& shared_engine_systems;
    ArenaAllocator error_arena {PageAllocator::Instance()};
    ThreadsafeErrorNotifications error_notifications {};
    AudioProcessor processor {host, *this, shared_engine_systems.prefs};
    PluginInstanceMessages& plugin_instance_messages;

    u64 random_seed = RandomSeed();

    Atomic<bool> update_gui = false;
    AutosaveState autosave_state {};

    package::InstallJobs package_install_jobs {};

    AttributionRequirementsState attribution_requirements {
        .shared_attributions_store = shared_engine_systems.shared_attributions_store,
    };
    Optional<clap_id> timer_id {};
    TimePoint last_poll_thread_time {};

    DynamicArrayBounded<char, PRODUCTION_BUILD ? 0 : 200> state_change_description {};

    ThreadsafeFunctionQueue main_thread_callbacks {.arena = {PageAllocator::Instance()}};

    Optional<PendingStateChange> pending_state_change {};
    LastSnapshot last_snapshot {};

    StateMetadata state_metadata {};

    MacroNames macro_names = DefaultMacroNames();

    // GUI can set this to get notified when the state changes.
    TrivialFixedSizeFunction<8, void()> stated_changed_callback {};

    sample_lib_server::AsyncCommsChannel& sample_lib_server_async_channel;
};

extern PluginCallbacks<Engine> const g_engine_callbacks;

void RunFunctionOnMainThread(Engine& engine, ThreadsafeFunctionQueue::Function function);

constexpr sample_lib::LibraryIdRef k_default_background_lib_id = FLOE_VENDOR ".default-bg";

Optional<sample_lib::LibraryIdRef> LibraryForOverallBackground(Engine const& engine);

// one-off loading of a ir or instrument
void LoadConvolutionIr(Engine& engine, Optional<sample_lib::IrId> ir);
void LoadInstrument(Engine& engine, u32 layer_index, InstrumentId instrument_id);
String IrName(Engine const& engine);

usize MegabytesUsedBySamples(Engine const& engine);

bool StateChangedSinceLastSnapshot(Engine& engine);

void LoadPresetFromFile(Engine& engine, String path);

void SaveCurrentStateToFile(Engine& engine, String path);

void SetToDefaultState(Engine& engine);
