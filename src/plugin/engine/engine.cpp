// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.hpp"

#include <clap/ext/params.h>

#include "foundation/foundation.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/preferences.hpp"
#include "common_infrastructure/sample_library/attribution_requirements.hpp"

#include "clap/ext/timer-support.h"
#include "plugin/plugin.hpp"
#include "processor/layer_processor.hpp"
#include "sample_lib_server/sample_library_server.hpp"
#include "shared_engine_systems.hpp"
#include "state/instrument.hpp"
#include "state/state_coding.hpp"
#include "state/state_snapshot.hpp"

Optional<sample_lib::LibraryIdRef> LibraryForOverallBackground(Engine const& engine) {
    ASSERT(g_is_logical_main_thread);

    Array<Optional<sample_lib::LibraryIdRef>, k_num_layers> lib_ids {};
    for (auto [layer_index, l] : Enumerate<u32>(engine.processor.layer_processors))
        lib_ids[layer_index] = engine.processor.layer_processors[layer_index].LibId();

    Optional<sample_lib::LibraryIdRef> first_lib_id {};
    for (auto const& lib_id : lib_ids) {
        if (!lib_id) continue;
        if (!first_lib_id) {
            first_lib_id = *lib_id;
            break;
        }
    }

    if (!first_lib_id) return k_default_background_lib_id;

    for (auto const& lib_id : lib_ids) {
        if (!lib_id) continue;
        if (*lib_id != *first_lib_id) return k_default_background_lib_id;
    }

    return *first_lib_id;
}

static void UpdateAttributionText(Engine& engine, ArenaAllocator& scratch_arena) {
    ASSERT(g_is_logical_main_thread);

    DynamicArrayBounded<sample_lib::Instrument const*, k_num_layers> insts {};
    for (auto& l : engine.processor.layer_processors)
        if (auto opt_i = l.instrument.TryGet<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>())
            dyn::Append(insts, &(*opt_i)->instrument);

    sample_lib::ImpulseResponse const* ir = nullptr;
    sample_lib_server::RefCounted<sample_lib::Library> ir_lib {};
    DEFER { ir_lib.Release(); }; // IMPORTANT: release before we return
    if (engine.processor.params[(usize)ParamIndex::ConvolutionReverbOn].ValueAsBool()) {
        if (auto const ir_id = engine.processor.convo.ir_id) {
            ir_lib =
                sample_lib_server::FindLibraryRetained(engine.shared_engine_systems.sample_library_server,
                                                       ir_id->library);
            if (ir_lib) {
                if (auto const found_ir = ir_lib->irs_by_name.Find(ir_id->ir_name)) ir = *found_ir;
            }
        }
    }

    UpdateAttributionText(engine.attribution_requirements, scratch_arena, insts, ir);
}

static void SetLastSnapshot(Engine& engine, StateSnapshotWithName const& state) {
    engine.last_snapshot.Set(state);
    engine.update_gui.Store(true, StoreMemoryOrder::Relaxed);
    engine.host.request_callback(&engine.host);
    // do this at the end because the pending state could be the arg of this function
    engine.pending_state_change.Clear();
}

static void LoadNewState(Engine& engine, StateSnapshotWithName const& state, StateSource source) {
    ZoneScoped;
    ASSERT(g_is_logical_main_thread);

    if (source == StateSource::Daw) SetInstanceId(engine.autosave_state, state.state.instance_id);

    auto const async = ({
        bool a = false;
        for (auto const& i : state.state.inst_ids) {
            if (i.tag == InstrumentType::Sampler) {
                a = true;
                break;
            }
        }
        if (state.state.ir_id) a = true;
        a;
    });

    if (!async) {
        for (auto [layer_index, i] : Enumerate<u32>(state.state.inst_ids)) {
            engine.processor.layer_processors[layer_index].instrument_id = i;
            switch (i.tag) {
                case InstrumentType::None:
                    SetInstrument(engine.processor, layer_index, InstrumentType::None);
                    break;
                case InstrumentType::WaveformSynth:
                    SetInstrument(engine.processor,
                                  layer_index,
                                  i.GetFromTag<InstrumentType::WaveformSynth>());
                    break;
                case InstrumentType::Sampler: PanicIfReached(); break;
            }
        }

        ASSERT(!state.state.ir_id.HasValue());
        engine.processor.convo.ir_id = k_nullopt;
        SetConvolutionIrAudioData(engine.processor, nullptr);

        engine.state_metadata = state.state.metadata;
        ApplyNewState(engine.processor, state.state, source);
        SetLastSnapshot(engine, state);
        if (engine.stated_changed_callback) engine.stated_changed_callback();

        MarkNeedsAttributionTextUpdate(engine.attribution_requirements);
        engine.host.request_callback(&engine.host);
    } else {
        engine.pending_state_change.Emplace();
        auto& pending = *engine.pending_state_change;
        pending.snapshot.state = state.state;
        pending.snapshot.name = state.name.Clone(pending.arena);
        pending.source = source;

        for (auto [layer_index, i] : Enumerate<u32>(state.state.inst_ids)) {
            engine.processor.layer_processors[layer_index].instrument_id = i;

            if (i.tag != InstrumentType::Sampler) continue;

            auto const async_id =
                sample_lib_server::SendAsyncLoadRequest(engine.shared_engine_systems.sample_library_server,
                                                        engine.sample_lib_server_async_channel,
                                                        sample_lib_server::LoadRequestInstrumentIdWithLayer {
                                                            .id = i.Get<sample_lib::InstrumentId>(),
                                                            .layer_index = layer_index,
                                                        });
            dyn::Append(pending.requests, async_id);
        }

        engine.processor.convo.ir_id = state.state.ir_id;
        if (state.state.ir_id) {
            auto const async_id =
                sample_lib_server::SendAsyncLoadRequest(engine.shared_engine_systems.sample_library_server,
                                                        engine.sample_lib_server_async_channel,
                                                        *state.state.ir_id);
            dyn::Append(pending.requests, async_id);
        }
    }
}

static Instrument InstrumentFromPendingState(Engine::PendingStateChange const& pending_state_change,
                                             u32 layer_index) {
    auto const inst_id = pending_state_change.snapshot.state.inst_ids[layer_index];

    Instrument instrument = InstrumentType::None;
    switch (inst_id.tag) {
        case InstrumentType::None: break;
        case InstrumentType::WaveformSynth: {
            instrument = inst_id.GetFromTag<InstrumentType::WaveformSynth>();
            break;
        }
        case InstrumentType::Sampler: {
            for (auto const& r : pending_state_change.retained_results) {
                auto const loaded_inst =
                    r.TryExtract<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>();

                if (loaded_inst && inst_id.GetFromTag<InstrumentType::Sampler>() == **loaded_inst)
                    instrument = *loaded_inst;
            }
            break;
        }
    }
    return instrument;
}

static AudioData const* IrAudioDataFromPendingState(Engine::PendingStateChange const& pending_state_change) {
    auto const ir_id = pending_state_change.snapshot.state.ir_id;
    if (!ir_id) return nullptr;
    for (auto const& r : pending_state_change.retained_results) {
        auto const loaded_ir = r.TryExtract<sample_lib_server::RefCounted<sample_lib::LoadedIr>>();
        if (loaded_ir && *ir_id == **loaded_ir) return (*loaded_ir)->audio_data;
    }
    return nullptr;
}

static void ApplyNewStateFromPending(Engine& engine) {
    ZoneScoped;
    ASSERT(g_is_logical_main_thread);

    auto const& pending_state_change = *engine.pending_state_change;

    for (auto const layer_index : Range(k_num_layers))
        SetInstrument(engine.processor,
                      layer_index,
                      InstrumentFromPendingState(pending_state_change, layer_index));
    SetConvolutionIrAudioData(engine.processor, IrAudioDataFromPendingState(pending_state_change));
    engine.state_metadata = pending_state_change.snapshot.state.metadata;
    ApplyNewState(engine.processor, pending_state_change.snapshot.state, pending_state_change.source);

    // do it last because it clears pending_state_change
    SetLastSnapshot(engine, pending_state_change.snapshot);

    if (engine.stated_changed_callback) engine.stated_changed_callback();
}

static void SampleLibraryChanged(Engine& engine, sample_lib::LibraryIdRef library_id) {
    ZoneScoped;
    ASSERT(g_is_logical_main_thread);

    auto const current_ir_id = engine.processor.convo.ir_id;
    if (current_ir_id.HasValue()) {
        if (current_ir_id->library == library_id) LoadConvolutionIr(engine, *current_ir_id);
    }

    for (auto [layer_index, l] : Enumerate<u32>(engine.processor.layer_processors)) {
        if (auto const i = l.instrument_id.TryGet<sample_lib::InstrumentId>()) {
            if (i->library == library_id) LoadInstrument(engine, layer_index, *i);
        }
    }
}

static void SampleLibraryResourceLoaded(Engine& engine, sample_lib_server::LoadResult result) {
    ZoneScoped;
    ASSERT(g_is_logical_main_thread);

    enum class Source : u32 { OneOff, PartOfPendingStateChange, LastInPendingStateChange, Count };

    auto const source = ({
        Source s {Source::OneOff};
        if (engine.pending_state_change) {
            auto& requests = engine.pending_state_change->requests;
            if (auto const opt_index = FindIf(requests, [&](sample_lib_server::RequestId const& id) {
                    return id == result.id;
                })) {
                s = Source::PartOfPendingStateChange;
                dyn::Remove(requests, *opt_index);
                if (requests.size == 0) s = Source::LastInPendingStateChange;
            }
        }
        s;
    });

    switch (source) {
        case Source::OneOff: {
            if (result.result.tag != sample_lib_server::LoadResult::ResultType::Success) break;

            auto const resource = result.result.Get<sample_lib_server::Resource>();
            switch (resource.tag) {
                case sample_lib_server::LoadRequestType::Instrument: {
                    auto const loaded_inst =
                        resource.Get<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>();

                    for (auto [layer_index, l] : Enumerate<u32>(engine.processor.layer_processors)) {
                        if (auto const i = l.instrument_id.TryGet<sample_lib::InstrumentId>()) {
                            if (*i == *loaded_inst) SetInstrument(engine.processor, layer_index, loaded_inst);
                        }
                    }
                    break;
                }
                case sample_lib_server::LoadRequestType::Ir: {
                    auto const loaded_ir =
                        resource.Get<sample_lib_server::RefCounted<sample_lib::LoadedIr>>();

                    auto const current_ir_id = engine.processor.convo.ir_id;
                    if (current_ir_id.HasValue()) {
                        if (*current_ir_id == *loaded_ir)
                            SetConvolutionIrAudioData(engine.processor, loaded_ir->audio_data);
                    }
                    break;
                }
            }
            break;
        }
        case Source::PartOfPendingStateChange: {
            result.Retain();
            dyn::Append(engine.pending_state_change->retained_results, result);
            break;
        }
        case Source::LastInPendingStateChange: {
            result.Retain();
            dyn::Append(engine.pending_state_change->retained_results, result);
            ApplyNewStateFromPending(engine);
            break;
        }
        case Source::Count: PanicIfReached(); break;
    }

    engine.update_gui.Store(true, StoreMemoryOrder::Relaxed);
    engine.host.request_callback(&engine.host);
}

static StateSnapshot CurrentStateSnapshot(Engine& engine) {
    StateSnapshot snapshot = engine.pending_state_change ? engine.pending_state_change->snapshot.state
                                                         : MakeStateSnapshot(engine.processor);
    snapshot.metadata = engine.state_metadata;
    snapshot.instance_id = InstanceId(engine.autosave_state);
    return snapshot;
}

bool StateChangedSinceLastSnapshot(Engine& engine) {
    auto current = CurrentStateSnapshot(engine);

    auto const& last = engine.pending_state_change ? engine.pending_state_change->snapshot.state
                                                   : engine.last_snapshot.state;

    // we don't check the params ccs for changes
    current.param_learned_ccs = last.param_learned_ccs;
    // we don't check the instance id for changes
    current.instance_id = last.instance_id;

    bool const changed = last != current;

    if constexpr (!PRODUCTION_BUILD) {
        if (changed)
            AssignDiffDescription(engine.state_change_description, last, current);
        else
            dyn::Clear(engine.state_change_description);
    }

    return changed;
}

// one-off load
void LoadConvolutionIr(Engine& engine, Optional<sample_lib::IrId> ir_id) {
    ASSERT(g_is_logical_main_thread);
    engine.processor.convo.ir_id = ir_id;

    if (ir_id)
        SendAsyncLoadRequest(engine.shared_engine_systems.sample_library_server,
                             engine.sample_lib_server_async_channel,
                             *ir_id);
    else {
        MarkNeedsAttributionTextUpdate(engine.attribution_requirements);
        engine.host.request_callback(&engine.host);
        SetConvolutionIrAudioData(engine.processor, nullptr);
    }
}

// one-off load
void LoadInstrument(Engine& engine, u32 layer_index, InstrumentId inst_id) {
    ASSERT(g_is_logical_main_thread);
    engine.processor.layer_processors[layer_index].instrument_id = inst_id;

    switch (inst_id.tag) {
        case InstrumentType::Sampler:
            SendAsyncLoadRequest(engine.shared_engine_systems.sample_library_server,
                                 engine.sample_lib_server_async_channel,
                                 sample_lib_server::LoadRequestInstrumentIdWithLayer {
                                     .id = inst_id.GetFromTag<InstrumentType::Sampler>(),
                                     .layer_index = layer_index,
                                 });
            break;
        case InstrumentType::None: {
            MarkNeedsAttributionTextUpdate(engine.attribution_requirements);
            SetInstrument(engine.processor, layer_index, InstrumentType::None);
            break;
        }
        case InstrumentType::WaveformSynth:
            MarkNeedsAttributionTextUpdate(engine.attribution_requirements);
            SetInstrument(engine.processor, layer_index, inst_id.Get<WaveformType>());
            break;
    }
}

void LoadPresetFromFile(Engine& engine, String path) {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator, Kb(16)};
    auto state_outcome = LoadPresetFile(path, scratch_arena, false);

    if (state_outcome.HasValue()) {
        LoadNewState(engine,
                     {
                         .state = state_outcome.Value(),
                         .name = {.name_or_path = path},
                     },
                     StateSource::PresetFile);
    } else {
        auto item = engine.error_notifications.NewError();
        item->value = {
            .title = "Failed to load preset"_s,
            .message = path,
            .error_code = state_outcome.Error(),
            .id = U64FromChars("statload"),
        };
        engine.error_notifications.AddOrUpdateError(item);
    }
}

void SaveCurrentStateToFile(Engine& engine, String path) {
    auto const current_state = CurrentStateSnapshot(engine);
    if (auto outcome = SavePresetFile(path, current_state); outcome.Succeeded()) {
        SetLastSnapshot(engine, {.state = current_state, .name = {.name_or_path = path}});
    } else {
        auto item = engine.error_notifications.NewError();
        item->value = {
            .title = "Failed to save preset"_s,
            .message = path,
            .error_code = outcome.Error(),
            .id = U64FromChars("statsave"),
        };
        engine.error_notifications.AddOrUpdateError(item);
    }
}

void RunFunctionOnMainThread(Engine& engine, ThreadsafeFunctionQueue::Function function) {
    if (auto thread_check =
            (clap_host_thread_check const*)engine.host.get_extension(&engine.host, CLAP_EXT_THREAD_CHECK)) {
        if (thread_check->is_main_thread(&engine.host)) {
            function();
            return;
        }
    }
    engine.main_thread_callbacks.Push(function);
    engine.host.request_callback(&engine.host);
}

static void OnMainThread(Engine& engine) {
    ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};
    while (auto f = engine.main_thread_callbacks.TryPop(scratch_arena))
        (*f)();

    while (auto r = engine.sample_lib_server_async_channel.results.TryPop()) {
        SampleLibraryResourceLoaded(engine, *r);
        r->Release();
        MarkNeedsAttributionTextUpdate(engine.attribution_requirements);
    }
    if (AttributionTextNeedsUpdate(engine.attribution_requirements))
        UpdateAttributionText(engine, scratch_arena);

    if (engine.update_gui.Exchange(false, RmwMemoryOrder::Relaxed))
        engine.plugin_instance_messages.UpdateGui();

    if (AutosaveNeeded(engine.autosave_state, engine.shared_engine_systems.prefs))
        QueueAutosave(engine.autosave_state, CurrentStateSnapshot(engine));
}

void Engine::OnProcessorChange(ChangeFlags flags) {
    if (flags & ProcessorListener::IrChanged) MarkNeedsAttributionTextUpdate(attribution_requirements);
    update_gui.Store(true, StoreMemoryOrder::Relaxed);
    host.request_callback(&host);
}

Engine::Engine(clap_host const& host,
               SharedEngineSystems& shared_engine_systems,
               PluginInstanceMessages& plugin_instance_messages)
    : host(host)
    , shared_engine_systems(shared_engine_systems)
    , plugin_instance_messages(plugin_instance_messages)
    , sample_lib_server_async_channel {sample_lib_server::OpenAsyncCommsChannel(
          shared_engine_systems.sample_library_server,
          {
              .error_notifications = error_notifications,
              .result_added_callback = [&engine = *this]() { engine.host.request_callback(&engine.host); },
              .library_changed_callback =
                  [&engine = *this](sample_lib::LibraryIdRef lib_id_ref) {
                      sample_lib::LibraryId lib_id = lib_id_ref;
                      engine.main_thread_callbacks.Push(
                          [lib_id, &engine]() { SampleLibraryChanged(engine, lib_id); });
                  },
          })} {

    last_snapshot.state = CurrentStateSnapshot(*this);

    InitAutosaveState(autosave_state, shared_engine_systems.prefs, random_seed, last_snapshot.state);

    {
        if (auto const timer_support =
                (clap_host_timer_support const*)host.get_extension(&host, CLAP_EXT_TIMER_SUPPORT);
            timer_support && timer_support->register_timer) {
            clap_id id;
            if (timer_support->register_timer(&host, 1000, &id)) timer_id = id;
        }
    }
    shared_engine_systems.StartPollingThreadIfNeeded();
}

Engine::~Engine() {
    ArenaAllocatorWithInlineStorage<1000> scratch_arena {PageAllocator::Instance()};
    DeinitAttributionRequirements(attribution_requirements, scratch_arena);
    package::ShutdownJobs(package_install_jobs);

    sample_lib_server::CloseAsyncCommsChannel(shared_engine_systems.sample_library_server,
                                              sample_lib_server_async_channel);

    if (timer_id) {
        if (auto const timer_support =
                (clap_host_timer_support const*)host.get_extension(&host, CLAP_EXT_TIMER_SUPPORT);
            timer_support && timer_support->unregister_timer)
            timer_support->unregister_timer(&host, *timer_id);
    }
}

static void PluginOnTimer(Engine& engine, clap_id timer_id) {
    ASSERT(g_is_logical_main_thread);
    if (timer_id == *engine.timer_id) OnMainThread(engine);
}

static void PluginOnPollThread(Engine& engine) {
    // If we don't have a timer, we shall use this thread to trigger regular main thread calls.
    if (!engine.timer_id) {
        if (engine.last_poll_thread_time.SecondsFromNow() >= 0.5) {
            engine.last_poll_thread_time = TimePoint::Now();
            engine.host.request_callback(&engine.host);
        }
    }

    AutosaveToFileIfNeeded(engine.autosave_state, engine.shared_engine_systems.paths);
}

static void PluginOnPreferenceChanged(Engine& engine, prefs::Key key, prefs::Value const* value) {
    ASSERT(g_is_logical_main_thread);
    OnPreferenceChanged(engine.autosave_state, key, value);
}

usize MegabytesUsedBySamples(Engine const& engine) {
    usize result = 0;
    for (auto& l : engine.processor.layer_processors) {
        if (auto i = l.instrument.TryGet<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>())
            for (auto& d : (*i)->audio_datas)
                result += d->RamUsageBytes();
    }

    return (result) / (1024 * 1024);
}

static bool PluginSaveState(Engine& engine, clap_ostream const& stream) {
    auto state = CurrentStateSnapshot(engine);
    ASSERT(state.instance_id.size);
    auto outcome = CodeState(state,
                             CodeStateArguments {
                                 .mode = CodeStateArguments::Mode::Encode,
                                 .read_or_write_data = [&](void* data, usize bytes) -> ErrorCodeOr<void> {
                                     u64 bytes_written = 0;
                                     while (bytes_written != bytes) {
                                         ASSERT(bytes_written < bytes);
                                         auto const n = stream.write(&stream,
                                                                     (u8 const*)data + bytes_written,
                                                                     bytes - bytes_written);
                                         if (n < 0) return ErrorCode(CommonError::PluginHostError);
                                         bytes_written += (u64)n;
                                     }
                                     return k_success;
                                 },
                                 .source = StateSource::Daw,
                                 .abbreviated_read = false,
                             });

    if (outcome.HasError()) {
        auto item = engine.error_notifications.NewError();
        item->value = {
            .title = "Failed to save state for DAW"_s,
            .message = {},
            .error_code = outcome.Error(),
            .id = U64FromChars("daw save"),
        };
        engine.error_notifications.AddOrUpdateError(item);
        return false;
    }
    return true;
}

static bool PluginLoadState(Engine& engine, clap_istream const& stream) {
    StateSnapshot state {};
    auto const outcome =
        CodeState(state,
                  CodeStateArguments {
                      .mode = CodeStateArguments::Mode::Decode,
                      .read_or_write_data = [&](void* data, usize bytes) -> ErrorCodeOr<void> {
                          u64 bytes_read = 0;
                          while (bytes_read != bytes) {
                              ASSERT(bytes_read < bytes);
                              auto const n = stream.read(&stream, (u8*)data + bytes_read, bytes - bytes_read);
                              if (n == 0) return ErrorCode(CommonError::InvalidFileFormat); // unexpected EOF
                              if (n < 0) return ErrorCode(CommonError::PluginHostError);
                              bytes_read += (u64)n;
                          }
                          return k_success;
                      },
                      .source = StateSource::Daw,
                      .abbreviated_read = false,
                  });

    if (outcome.HasError()) {
        auto const item = engine.error_notifications.NewError();
        item->value = {
            .title = "Failed to load DAW state"_s,
            .message = {},
            .error_code = outcome.Error(),
            .id = U64FromChars("daw load"),
        };
        engine.error_notifications.AddOrUpdateError(item);
        return false;
    }

    LoadNewState(engine, {.state = state, .name = {.name_or_path = "DAW State"}}, StateSource::Daw);
    return true;
}

PluginCallbacks<Engine> const g_engine_callbacks {
    .on_main_thread = OnMainThread,
    .on_timer = PluginOnTimer,
    .on_poll_thread = PluginOnPollThread,
    .on_preference_changed = PluginOnPreferenceChanged,
    .save_state = PluginSaveState,
    .load_state = PluginLoadState,
};
