// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/autosave.hpp"
#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/preset_description.hpp"
#include "common_infrastructure/sample_library/attribution_requirements.hpp"
#include "common_infrastructure/state/instrument.hpp"
#include "common_infrastructure/state/macros.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "engine/package_installation.hpp"
#include "engine/undo.hpp"
#include "processor/processor.hpp"
#include "sample_lib_server/sample_library_server.hpp"
#include "shared_engine_systems.hpp"

struct EngineListener {
    virtual void OnEngineChange() = 0; // Called from the main thread.
    virtual ~EngineListener() = default;
};

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
           FloeInstanceIndex instance_index);
    ~Engine();

    auto& Layer(u32 index) { return processor.layer_processors[index]; }

    void OnProcessorChange(ChangeFlags) override;
    void OnParamChange(ProcessorListener::ParamChange, ParamIndex) override;

    clap_host const& host;
    SharedEngineSystems& shared_engine_systems;
    FloeInstanceIndex const instance_index;
    ArenaAllocator error_arena {PageAllocator::Instance()};
    ThreadsafeErrorNotifications error_notifications {};
    AudioProcessor processor {host, *this, shared_engine_systems.prefs};

    u64 random_seed = RandomSeed();

    AutosaveState autosave_state {};

    package::InstallJobs package_install_jobs {};

    AttributionRequirementsState attribution_requirements {
        .shared_attributions_store = shared_engine_systems.shared_attributions_store,
    };
    Optional<clap_id> timer_id {};
    TimePoint last_poll_thread_time {};

    Atomic<TimePoint> request_main_thread_callback_at {};

    DynamicArrayBounded<char, PRODUCTION_BUILD ? 0 : 200> state_change_description {};

    ThreadsafeFunctionQueue main_thread_callbacks {.arena = {PageAllocator::Instance()}};

    Optional<PendingStateChange> pending_state_change {};
    LastSnapshot last_snapshot {};

    struct PresetDescriptionCache {
        AutoDescription auto_desc {};
        // Resolved short/long views. Both point either into auto_desc or into
        // last_snapshot.state.metadata.description and remain valid until the next refresh.
        String short_text {};
        String long_text {};
        // True when long_text is the user-authored description rather than auto-generated.
        bool long_is_user_desc = false;
        // True when short_text is the user-authored description rather than auto-generated.
        bool short_is_user_desc = false;
    };
    PresetDescriptionCache preset_description_cache {};

    StateMetadata state_metadata {};

    Bitset<k_num_effect_types> fx_visible {};
    MacroNames macro_names = DefaultMacroNames();

    // Optional listener for engine changes. The GUI sets this when it's active.
    EngineListener* listener {};

    sample_lib_server::AsyncCommsChannel& sample_lib_server_async_channel;

    UndoHistory undo_history {PageAllocator::Instance()};
    u32 undoable_step_depth {};
    DynamicArrayBounded<char, k_undoable_step_name_max_size> pending_undoable_step_name {};
};

extern PluginCallbacks<Engine> const g_engine_callbacks;

void RunFunctionOnMainThread(Engine& engine, ThreadsafeFunctionQueue::Function function);

constexpr sample_lib::LibraryId k_default_background_lib_id =
    sample_lib::HashLibraryIdStringWithoutRegistration(FLOE_VENDOR ".default-bg");

Optional<sample_lib::LibraryId> LibraryForOverallBackground(Engine const& engine);

// One-off loading of an IR or instrument.
void LoadConvolutionIr(Engine& engine, Optional<sample_lib::IrId> ir);
void LoadInstrument(Engine& engine, u32 layer_index, InstrumentId instrument_id);

String IrName(Engine const& engine);

usize MegabytesUsedBySamples(Engine const& engine);

StateSnapshot CurrentStateSnapshot(Engine& engine);

void ApplySection(Engine& engine,
                  StateSnapshot const& source,
                  StateSnapshotSelector const& source_selector,
                  StateSnapshotSelector const& target_selector);

bool StateChangedSinceLastSnapshot(Engine& engine);

void RevertToLastSnapshot(Engine& engine);

void LoadPresetFromFile(Engine& engine, String path);

void SaveCurrentStateToFile(Engine& engine, String path);

void SetToDefaultState(Engine& engine);

// ================================================================================================
// Undo system.
//
// AudioProcessor functions ParameterJustStartedMoving/Finished and SetParameterValue will, by default, issue
// appropriate undoable steps to the engine. However, for non-parameters, or for when we want to coalesce
// multiple parameter changes into one step, the GUI needs to use these functions.

// Call once you've made a modification.
void RecordUndoableStep(Engine& engine, String name);

// Same as RecordUndoableStep except it should be used when multiple SetParameterValue or
// ParameterJustStartedMoving/Finished should be coalesced into a single step.
void BeginUndoableStep(Engine& engine, String name);
void EndUndoableStep(Engine& engine);

void Undo(Engine& engine);
void Redo(Engine& engine);
