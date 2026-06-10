// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/autosave.hpp"
#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/preset_description.hpp"
#include "common_infrastructure/sample_library/attribution_requirements.hpp"
#include "common_infrastructure/sample_library/server/sample_library_server.hpp"
#include "common_infrastructure/state/instrument.hpp"
#include "common_infrastructure/state/macros.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "engine/package_installation.hpp"
#include "engine/undo.hpp"
#include "processor/processor.hpp"
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

        DynamicArrayBounded<sample_lib_server::RequestId, k_num_layers + 1> requests;
        DynamicArrayBounded<sample_lib_server::LoadResult, k_num_layers + 1> retained_results;
        StateSnapshot snapshot;
        DynamicArray<char> preset_path {Malloc::Instance()}; // May be empty
        u64 known_preset_id {}; // Optional. 0 == unknown. Opaque id supplied by the loader (typically the
                                // preset browser) used to disambiguate presets sharing identical content.
        StateSource source;
        bool update_pinned_snapshot_on_complete = true;
    };

    struct PinnedSnapshot {
        struct DescriptionCache {
            AutoDescription auto_desc {};
            String short_text {};
            String long_text {};
            bool long_is_user_desc = false;
            bool short_is_user_desc = false;
        };

        StateSnapshot state {DefaultStateSnapshot()};
        DynamicArray<char> preset_path {Malloc::Instance()}; // May be empty
        u64 known_preset_id {}; // See PendingStateChange::known_preset_id.

        // We sometimes don't have the full path, but it's worth seeing if it's our known preset index. To
        // avoid wasteful repeated lookup in the preset index, we use this bool to only do it once.
        bool preset_path_needs_lookup {};

        DescriptionCache description_cache {};
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
    PinnedSnapshot pinned_snapshot {};

    // Holds the modified state set aside while auditioning the pinned snapshot.
    Optional<StateSnapshot> stashed_modifications {};

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

// Load new sampler instruments into multiple layers as a single, synchronised operation: all
// samples are streamed in together and swapped in atomically, and the change is recorded as one
// undo step. Layers with k_nullopt are left unchanged.
void LoadInstruments(Engine& engine,
                     Array<Optional<sample_lib::InstrumentId>, k_num_layers> const& new_ids,
                     String undo_name);

struct LoadStateOptions {
    StateSource source;
    String preset_path = ""_s;
    u64 known_preset_id = 0;
    bool update_pinned_snapshot = true;
};

// Load a state snapshot into the engine. May be synchronous or async (kicks off sample-library
// requests and completes in CompletePendingStateLoad) depending on whether the snapshot
// references samples or IRs that need to be loaded.
void LoadState(Engine& engine, StateSnapshot const& state, LoadStateOptions const& opts);

sample_lib::ImpulseResponse const* CurrentIr(Engine const& engine);
String IrName(Engine const& engine);

usize MegabytesUsedBySamples(Engine const& engine);

StateSnapshot CurrentStateSnapshot(Engine& engine);

String PinnedPresetFolderName(Engine const& engine);

void ApplySectionOfState(Engine& engine,
                         StateSnapshot const& source,
                         StateSnapshotSection const& source_section,
                         StateSnapshotSection const& target_section);

bool StateModifiedFromPinned(Engine& engine);

// Returns the pinned snapshot if it originated from a preset, or nullptr if the pinned snapshot is just the
// default initial state.
StateSnapshot const* PinnedPresetState(Engine const& engine);

bool ViewingPinnedSnapshot(Engine const& engine);

// Editing while auditioning the pinned snapshot discards the stash (auto-promote).
void TogglePinnedView(Engine& engine);

void LoadPresetFromFile(Engine& engine, String path, u64 known_preset_id = 0);

void SaveCurrentStateToFile(Engine& engine, String path);

void SetToDefaultState(Engine& engine);

// ================================================================================================
// Undo system.
//
// AudioProcessor functions ParameterJustStartedMoving/Finished and SetParameterValue will, by default, issue
// appropriate undoable steps to the engine. However, for non-parameters, or for when we want to coalesce
// multiple parameter changes into one step, the GUI needs to use these functions.

// Call once you've made a modification. Pass is_pinned_snapshot_anchor=true for baseline-establishing events
// (preset load, DAW state load, default reset, save) so undo/redo can correctly relocate the pinned
// snapshot when navigating across these boundaries.
void RecordUndoableStep(Engine& engine, String name, bool is_pinned_snapshot_anchor = false);

// Same as RecordUndoableStep except it should be used when multiple SetParameterValue or
// ParameterJustStartedMoving/Finished should be coalesced into a single step.
void BeginUndoableStep(Engine& engine, String name);
void EndUndoableStep(Engine& engine);

void Undo(Engine& engine);
void Redo(Engine& engine);
