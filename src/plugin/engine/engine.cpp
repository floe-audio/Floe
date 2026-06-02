// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.hpp"

#include <clap/ext/params.h>

#include "foundation/foundation.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/preferences.hpp"
#include "common_infrastructure/sample_library/attribution_requirements.hpp"
#include "common_infrastructure/sample_library/server/sample_library_server.hpp"
#include "common_infrastructure/state/instrument.hpp"
#include "common_infrastructure/state/state_coding.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "clap/ext/timer-support.h"
#include "engine/engine_prefs.hpp"
#include "engine/favourite_items.hpp"
#include "engine/loop_modes.hpp"
#include "plugin/plugin.hpp"
#include "processing_utils/arpeggiator.hpp"
#include "processing_utils/eq_bands.hpp"
#include "processor/layer_processor.hpp"
#include "shared_engine_systems.hpp"

static void NotifyListener(Engine& engine) {
    if (engine.listener) engine.listener->OnEngineChange();
}

String PinnedPresetFolderName(Engine const& engine) {
    String folder_name {};
    if (auto const& preset_path = engine.pinned_snapshot.preset_path; preset_path.size) {
        if (auto const dir = path::Directory(preset_path)) folder_name = path::Filename(*dir);
    } else {
        if (auto const& cat = engine.pinned_snapshot.state.extras.display_category; cat.size)
            folder_name = cat;
    }
    return StripNumberedPrefix(folder_name);
}

static void RefreshPresetDescriptionCache(Engine& engine) {
    Array<AutoDescriptionLayerInfo, k_num_layers> layer_info {};
    for (auto const i : Range(k_num_layers)) {
        auto& layer = engine.processor.layer_processors[i];
        layer_info[i].inst_name = layer.InstName();
        auto const desired_loop_mode =
            engine.processor.main_params.IntValue<param_values::LoopMode>(layer.index,
                                                                          LayerParamIndex::LoopMode);
        layer_info[i].actual_loop_behaviour =
            ActualLoopBehaviour(layer.instrument,
                                desired_loop_mode,
                                layer.VolumeEnvelopeIsOn(engine.processor.main_params));
    }

    auto& cache = engine.pinned_snapshot.description_cache;

    cache.auto_desc = GenerateAutoDescription(engine.pinned_snapshot.state,
                                              layer_info,
                                              PinnedPresetFolderName(engine),
                                              Hash(engine.pinned_snapshot.state.extras.display_name));

    String const real_desc = engine.pinned_snapshot.state.metadata.description;
    auto const real_split = SplitPresetDescription(real_desc);

    if (real_desc.size) {
        cache.short_text = real_split.short_part.ValueOr(real_desc);
        cache.short_is_user_desc = true;
    } else {
        cache.short_text = cache.auto_desc.short_text;
        cache.short_is_user_desc = false;
    }

    if (real_desc.size && real_split.long_part) {
        cache.long_text = *real_split.long_part;
        cache.long_is_user_desc = true;
    } else {
        cache.long_text = cache.auto_desc.long_text;
        cache.long_is_user_desc = false;
    }
}

Optional<sample_lib::LibraryId> LibraryForOverallBackground(Engine const& engine) {
    ASSERT(g_is_logical_main_thread);

    Array<Optional<sample_lib::LibraryId>, k_num_layers> lib_ids {};
    for (auto [layer_index, l] : Enumerate<u32>(engine.processor.layer_processors))
        lib_ids[layer_index] = engine.processor.layer_processors[layer_index].LibId();

    Optional<sample_lib::LibraryId> first_lib_id {};
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
        if (auto opt_i =
                l.instrument.TryGet<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>())
            dyn::Append(insts, &(*opt_i)->instrument);

    sample_lib::ImpulseResponse const* ir = nullptr;
    sample_lib_server::ResourcePointer<sample_lib::Library> ir_lib {};
    DEFER { ir_lib.Release(); }; // IMPORTANT: release before we return
    if (engine.processor.main_params.BoolValue(ParamIndex::ConvolutionReverbOn)) {
        if (auto const ir_id = engine.processor.convo.ir_id) {
            ir_lib =
                sample_lib_server::FindLibraryRetained(engine.shared_engine_systems.sample_library_server,
                                                       ir_id->library);
            if (ir_lib) {
                if (auto const found_ir = ir_lib->irs_by_id.Find(ir_id->ir_id)) ir = *found_ir;
            }
        }
    }

    UpdateAttributionText(engine.attribution_requirements, scratch_arena, insts, ir);
}

static void FillStateExtrasFromPath(StateSnapshot& state, String preset_path) {
    dyn::AssignFitInCapacity(state.extras.display_name, path::FilenameWithoutExtension(preset_path));
    dyn::AssignFitInCapacity(state.extras.display_category,
                             path::Filename(path::Directory(preset_path).ValueOr({})));
}

static void
SetPinnedSnapshot(Engine& engine, StateSnapshot const& state, String preset_path, u64 known_preset_id) {
    if (preset_path.size) {
        ASSERT(IsValidUtf8(preset_path));
        ASSERT(path::IsAbsolute(preset_path));
    }
    engine.pinned_snapshot.state = state;
    dyn::Assign(engine.pinned_snapshot.preset_path, preset_path);
    engine.pinned_snapshot.known_preset_id = known_preset_id;
    engine.pinned_snapshot.preset_path_needs_lookup = preset_path.size == 0;
    RefreshPresetDescriptionCache(engine);
    ASSERT(engine.pinned_snapshot.state.extras.display_name.size);
}

static void AfterStateChanged(Engine& engine) {
    NotifyListener(engine);
    engine.host.request_callback(&engine.host);
    engine.pending_state_change.Clear();
}

void LoadState(Engine& engine, StateSnapshot const& state, LoadStateOptions const& opts) {
    auto const preset_path = opts.preset_path;
    auto const known_preset_id = opts.known_preset_id;
    auto const source = opts.source;
    auto const update_pinned_snapshot = opts.update_pinned_snapshot;
    ZoneScoped;
    ASSERT(g_is_logical_main_thread);

    if (source == StateSource::Daw) SetInstanceId(engine.autosave_state, state.extras.instance_id);

    auto const async = ({
        bool a = false;
        for (auto const& i : state.inst_ids) {
            if (i.tag == InstrumentType::Sampler) {
                a = true;
                break;
            }
        }
        if (state.ir_id) a = true;
        a;
    });

    if (!async) {
        for (auto [layer_index, i] : Enumerate<u32>(state.inst_ids)) {
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

        ASSERT(!state.ir_id.HasValue());
        engine.processor.convo.ir_id = k_nullopt;
        SetConvolutionIrAudioData(engine.processor, nullptr, {});

        engine.state_metadata = state.metadata;
        engine.macro_names = state.macro_names;
        engine.fx_visible = state.fx_visible;
        ApplyState(engine.processor, state, source);
        if (update_pinned_snapshot) SetPinnedSnapshot(engine, state, preset_path, known_preset_id);

        MarkNeedsAttributionTextUpdate(engine.attribution_requirements);
        AfterStateChanged(engine);
    } else {
        engine.pending_state_change.Emplace();
        auto& pending = *engine.pending_state_change;
        pending.snapshot = state;
        dyn::Assign(pending.preset_path, preset_path);
        pending.known_preset_id = known_preset_id;
        pending.source = source;
        pending.update_pinned_snapshot_on_complete = update_pinned_snapshot;

        for (auto [layer_index, i] : Enumerate<u32>(state.inst_ids)) {
            engine.processor.layer_processors[layer_index].instrument_id = i;

            if (i.tag != InstrumentType::Sampler) continue;

            auto const async_id =
                sample_lib_server::SendAsyncLoadRequest(engine.shared_engine_systems.sample_library_server,
                                                        engine.sample_lib_server_async_channel,
                                                        sample_lib_server::LoadRequestInstrumentIdWithLayer {
                                                            .id = i.Get<sample_lib::InstrumentId>(),
                                                            .layer_index = layer_index,
                                                        });
            auto const appended1 = dyn::Append(pending.requests, async_id);
            ASSERT(appended1);
        }

        engine.processor.convo.ir_id = state.ir_id;
        if (state.ir_id) {
            auto const async_id =
                sample_lib_server::SendAsyncLoadRequest(engine.shared_engine_systems.sample_library_server,
                                                        engine.sample_lib_server_async_channel,
                                                        *state.ir_id);
            auto const appended2 = dyn::Append(pending.requests, async_id);
            ASSERT(appended2);
        }
    }
}

static Instrument InstrumentFromPendingState(Engine::PendingStateChange const& pending_state_change,
                                             u32 layer_index) {
    auto const inst_id = pending_state_change.snapshot.inst_ids[layer_index];

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
                    r.TryExtract<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>();

                if (loaded_inst && inst_id.GetFromTag<InstrumentType::Sampler>() == **loaded_inst)
                    instrument = *loaded_inst;
            }
            break;
        }
    }
    return instrument;
}

static sample_lib_server::ResourcePointer<sample_lib::LoadedIr>
IrFromPendingState(Engine::PendingStateChange const& pending_state_change) {
    auto const ir_id = pending_state_change.snapshot.ir_id;
    if (!ir_id) return {};
    for (auto const& r : pending_state_change.retained_results) {
        auto const loaded_ir = r.TryExtract<sample_lib_server::ResourcePointer<sample_lib::LoadedIr>>();
        if (loaded_ir && *ir_id == **loaded_ir) return *loaded_ir;
    }
    return {};
}

static void CompletePendingStateLoad(Engine& engine) {
    ZoneScoped;
    ASSERT(g_is_logical_main_thread);

    auto const& pending_state_change = *engine.pending_state_change;

    for (auto const layer_index : Range(k_num_layers))
        SetInstrument(engine.processor,
                      layer_index,
                      InstrumentFromPendingState(pending_state_change, layer_index));
    {
        auto const ir = IrFromPendingState(pending_state_change);
        SetConvolutionIrAudioData(engine.processor,
                                  ir ? ir->audio_data : nullptr,
                                  ir ? ir->ir.audio_props : sample_lib::ImpulseResponse::AudioProperties {});
    }
    engine.state_metadata = pending_state_change.snapshot.metadata;
    engine.macro_names = pending_state_change.snapshot.macro_names;
    engine.fx_visible = pending_state_change.snapshot.fx_visible;

    // IMPORTANT: we clear the pending state before applying the new state because some hosts, such as Bitwig,
    // will call our param get_value during the call to rescan that we make, and we want our get_value to
    // correctly use the new values.
    ArenaAllocatorWithInlineStorage<1000> temp_arena {Malloc::Instance()};
    auto const snapshot = pending_state_change.snapshot;
    auto const preset_path = pending_state_change.preset_path.size
                                 ? String(temp_arena.Clone(pending_state_change.preset_path))
                                 : ""_s;
    auto const known_preset_id = pending_state_change.known_preset_id;
    auto const source = pending_state_change.source;
    auto const update_pinned_snapshot = pending_state_change.update_pinned_snapshot_on_complete;
    engine.pending_state_change.Clear();

    ApplyState(engine.processor, snapshot, source);
    if (update_pinned_snapshot) SetPinnedSnapshot(engine, snapshot, preset_path, known_preset_id);
    AfterStateChanged(engine);
}

static void SampleLibraryChanged(Engine& engine, sample_lib::LibraryId library_id) {
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

    enum class Source : u8 { OneOff, PartOfPendingStateChange, LastInPendingStateChange, Count };

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
                        resource.Get<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>();

                    for (auto [layer_index, l] : Enumerate<u32>(engine.processor.layer_processors)) {
                        if (auto const i = l.instrument_id.TryGet<sample_lib::InstrumentId>()) {
                            if (*i == *loaded_inst) SetInstrument(engine.processor, layer_index, loaded_inst);
                        }
                    }
                    break;
                }
                case sample_lib_server::LoadRequestType::Ir: {
                    auto const loaded_ir =
                        resource.Get<sample_lib_server::ResourcePointer<sample_lib::LoadedIr>>();

                    auto const current_ir_id = engine.processor.convo.ir_id;
                    if (current_ir_id.HasValue()) {
                        if (*current_ir_id == *loaded_ir)
                            SetConvolutionIrAudioData(engine.processor,
                                                      loaded_ir->audio_data,
                                                      loaded_ir->ir.audio_props);
                    }
                    break;
                }
            }
            break;
        }
        case Source::PartOfPendingStateChange: {
            result.Retain();
            auto const appended = dyn::Append(engine.pending_state_change->retained_results, result);
            ASSERT(appended);
            break;
        }
        case Source::LastInPendingStateChange: {
            result.Retain();
            auto const appended = dyn::Append(engine.pending_state_change->retained_results, result);
            ASSERT(appended);
            CompletePendingStateLoad(engine);
            break;
        }
        case Source::Count: PanicIfReached(); break;
    }

    NotifyListener(engine);
    engine.host.request_callback(&engine.host);
}

static StateSnapshot const& PinnedSnapshotForModificationCheck(Engine& engine) {
    auto const& result =
        engine.pending_state_change ? engine.pending_state_change->snapshot : engine.pinned_snapshot.state;
    ASSERT(result.extras.display_name.size);
    return result;
}

StateSnapshot CurrentStateSnapshot(Engine& engine) {
    StateSnapshot snapshot = CaptureStateSnapshot(engine.processor);

    // We always want to retain our learned CCs unless there's pending DAW state.
    auto const ccs = snapshot.extras.param_learned_ccs;
    DEFER {
        if (!(engine.pending_state_change && engine.pending_state_change->source == StateSource::Daw))
            snapshot.extras.param_learned_ccs = ccs;
    };

    if (engine.pending_state_change) {
        snapshot = engine.pending_state_change->snapshot;
    } else {
        snapshot.metadata = engine.state_metadata;
        snapshot.macro_names = engine.macro_names;
        snapshot.fx_visible = engine.fx_visible;
    }

    auto const& last = PinnedSnapshotForModificationCheck(engine);
    if (last.extras.modified_from_origin_preset) {
        snapshot.extras = last.extras;
    } else {
        snapshot.extras = last.extras; // Ignore extras before the != next line.
        snapshot.extras.modified_from_origin_preset = snapshot != last;
    }

    snapshot.extras.instance_id = InstanceId(engine.autosave_state);
    ASSERT(snapshot.extras.display_name.size);

    return snapshot;
}

static void CopyLayerArpState(Engine& engine, StateSnapshot const& source, u8 src_layer, u8 dst_layer) {
    StateSnapshot s {};
    s.arp_steps[dst_layer] = source.arp_steps[src_layer];
    s.slice_arp_configs[dst_layer] = source.slice_arp_configs[src_layer];
    ArpApplyState(engine.processor.layer_processors[dst_layer].arp_state, s, dst_layer);
}

static void CopyLayerVelocity(Engine& engine, StateSnapshot const& source, u8 src_layer, u8 dst_layer) {
    engine.processor.layer_processors[dst_layer].velocity_curve_map.SetNewPoints(
        source.velocity_curve_points[src_layer]);
}

static void CopyLayerHarmony(Engine& engine, StateSnapshot const& source, u8 src_layer, u8 dst_layer) {
    engine.processor.layer_processors[dst_layer].harmony_intervals.AssignBlockwise(
        source.harmony_intervals[src_layer]);
}

static void SendMacroDestination(AudioProcessor& processor, u8 macro_index, u8 dest_index) {
    auto const& dest = processor.main_macro_destinations[macro_index].items[dest_index];
    auto& event = processor.macro_dest_inbox[macro_index][dest_index];
    event.Produce(!dest.param_index
                      ? audio_thread_inbox::MacroDestinationUpdate::ProduceOptions {.clear = true}
                      : audio_thread_inbox::MacroDestinationUpdate::ProduceOptions {
                            .new_value = dest.value,
                            .new_param_index = dest.param_index,
                        });
}

struct EqBandResolved {
    ParamIndex type, freq, reso, gain;
};

static EqBandResolved ResolveEqBand(ParameterModule scope, u8 band) {
    ASSERT(band < k_num_eq_bands);
    if (auto const layer = LayerIndexFromModule(scope)) {
        constexpr LayerParamIndex k_lp[k_num_eq_bands][4] = {
            {LayerParamIndex::EqType1,
             LayerParamIndex::EqFreq1,
             LayerParamIndex::EqResonance1,
             LayerParamIndex::EqGain1},
            {LayerParamIndex::EqType2,
             LayerParamIndex::EqFreq2,
             LayerParamIndex::EqResonance2,
             LayerParamIndex::EqGain2},
            {LayerParamIndex::EqType3,
             LayerParamIndex::EqFreq3,
             LayerParamIndex::EqResonance3,
             LayerParamIndex::EqGain3},
        };
        return {
            ParamIndexFromLayerParamIndex(*layer, k_lp[band][0]),
            ParamIndexFromLayerParamIndex(*layer, k_lp[band][1]),
            ParamIndexFromLayerParamIndex(*layer, k_lp[band][2]),
            ParamIndexFromLayerParamIndex(*layer, k_lp[band][3]),
        };
    }
    ASSERT(scope == ParameterModule::Effect);
    constexpr ParamIndex k_pi[k_num_eq_bands][4] = {
        {ParamIndex::EqType1, ParamIndex::EqFreq1, ParamIndex::EqResonance1, ParamIndex::EqGain1},
        {ParamIndex::EqType2, ParamIndex::EqFreq2, ParamIndex::EqResonance2, ParamIndex::EqGain2},
        {ParamIndex::EqType3, ParamIndex::EqFreq3, ParamIndex::EqResonance3, ParamIndex::EqGain3},
    };
    return {k_pi[band][0], k_pi[band][1], k_pi[band][2], k_pi[band][3]};
}

void ApplySectionOfState(Engine& engine,
                         StateSnapshot const& source,
                         StateSnapshotSection const& source_section,
                         StateSnapshotSection const& target_section) {
    ASSERT(g_is_logical_main_thread);
    if (source_section.tag != target_section.tag) return;

    BeginUndoableStep(engine, "Apply section"_s);
    DEFER { EndUndoableStep(engine); };

    auto const set_param = [&](ParamIndex src, ParamIndex dst) {
        auto const& dst_range = k_param_descriptors[ToInt(dst)].linear_range;
        SetParameterValue(engine.processor,
                          dst,
                          Clamp(source.param_values[ToInt(src)], dst_range.min, dst_range.max),
                          {});
    };

    switch (source_section.tag) {
        case StateSnapshotSectionKind::Param: {
            set_param(source_section.Get<ParamSection>().param, target_section.Get<ParamSection>().param);
            break;
        }
        case StateSnapshotSectionKind::Macro: {
            auto const src = source_section.Get<MacroSection>().macro_index;
            auto const dst = target_section.Get<MacroSection>().macro_index;
            ASSERT(src < k_num_macros && dst < k_num_macros);

            set_param(ParamIndexFromMacroIndex(src), ParamIndexFromMacroIndex(dst));
            engine.macro_names[dst] = source.macro_names[src];
            engine.processor.main_macro_destinations[dst] = source.macro_destinations[src];
            for (auto const d : Range<u8>(k_max_macro_destinations))
                SendMacroDestination(engine.processor, dst, d);
            break;
        }
        case StateSnapshotSectionKind::Instrument: {
            auto const src = source_section.Get<InstrumentSection>().layer_index;
            auto const dst = target_section.Get<InstrumentSection>().layer_index;
            ASSERT(src < k_num_layers && dst < k_num_layers);
            LoadInstrument(engine, dst, source.inst_ids[src]);
            break;
        }
        case StateSnapshotSectionKind::VelocityCurve: {
            auto const src = source_section.Get<VelocityCurveSection>().layer_index;
            auto const dst = target_section.Get<VelocityCurveSection>().layer_index;
            ASSERT(src < k_num_layers && dst < k_num_layers);
            CopyLayerVelocity(engine, source, src, dst);
            break;
        }
        case StateSnapshotSectionKind::Envelope: {
            auto const src_sel = source_section.Get<EnvelopeSection>();
            auto const dst_sel = target_section.Get<EnvelopeSection>();
            ASSERT(src_sel.layer_index < k_num_layers && dst_sel.layer_index < k_num_layers);
            if (src_sel.kind != dst_sel.kind) break;

            auto const params = [&]() {
                switch (src_sel.kind) {
                    case EnvelopeSection::Kind::Volume:
                        return Array {LayerParamIndex::VolumeAttack,
                                      LayerParamIndex::VolumeDecay,
                                      LayerParamIndex::VolumeSustain,
                                      LayerParamIndex::VolumeRelease};
                    case EnvelopeSection::Kind::Filter:
                        return Array {LayerParamIndex::FilterAttack,
                                      LayerParamIndex::FilterDecay,
                                      LayerParamIndex::FilterSustain,
                                      LayerParamIndex::FilterRelease};
                }
            }();

            for (auto const lp : params)
                set_param(ParamIndexFromLayerParamIndex(src_sel.layer_index, lp),
                          ParamIndexFromLayerParamIndex(dst_sel.layer_index, lp));
            break;
        }
        case StateSnapshotSectionKind::Layer: {
            auto const src = source_section.Get<LayerSection>().layer_index;
            auto const dst = target_section.Get<LayerSection>().layer_index;
            ASSERT(src < k_num_layers && dst < k_num_layers);
            for (auto const i : Range(k_num_layer_parameters)) {
                auto const lp = (LayerParamIndex)i;
                set_param(ParamIndexFromLayerParamIndex(src, lp), ParamIndexFromLayerParamIndex(dst, lp));
            }
            CopyLayerArpState(engine, source, src, dst);
            CopyLayerVelocity(engine, source, src, dst);
            CopyLayerHarmony(engine, source, src, dst);
            LoadInstrument(engine, dst, source.inst_ids[src]);
            break;
        }
        case StateSnapshotSectionKind::ModuleTab: {
            auto const src_sel = source_section.Get<ModuleTabSection>();
            auto const dst_sel = target_section.Get<ModuleTabSection>();
            if (src_sel.subtab != dst_sel.subtab) break;

            auto const src_layer = LayerIndexFromModule(src_sel.scope);
            auto const dst_layer = LayerIndexFromModule(dst_sel.scope);
            if (src_sel.scope != dst_sel.scope && !(src_layer && dst_layer)) break;

            for (auto const i : Range(k_num_parameters)) {
                auto const& parts = k_param_descriptors[i].module_parts;
                if (parts[0] != src_sel.scope || parts[1] != src_sel.subtab) continue;
                auto dst_param = (ParamIndex)i;
                if (src_layer && dst_layer && *src_layer != *dst_layer) {
                    auto const info = LayerParamIndexAndLayerFor((ParamIndex)i);
                    if (!info) continue;
                    dst_param = ParamIndexFromLayerParamIndex(*dst_layer, info->param);
                }
                set_param((ParamIndex)i, dst_param);
            }

            if (src_layer && dst_layer) {
                switch (src_sel.subtab) {
                    case ParameterModule::Arp:
                        CopyLayerArpState(engine, source, *src_layer, *dst_layer);
                        break;
                    case ParameterModule::Config:
                        CopyLayerVelocity(engine, source, *src_layer, *dst_layer);
                        break;
                    case ParameterModule::Playback:
                        CopyLayerHarmony(engine, source, *src_layer, *dst_layer);
                        break;
                    default: break;
                }
            }
            break;
        }
        case StateSnapshotSectionKind::EqBand: {
            auto const src_sel = source_section.Get<EqBandSection>();
            auto const dst_sel = target_section.Get<EqBandSection>();
            auto const srcs = ResolveEqBand(src_sel.scope, src_sel.band);
            auto const dsts = ResolveEqBand(dst_sel.scope, dst_sel.band);
            set_param(srcs.type, dsts.type);
            set_param(srcs.freq, dsts.freq);
            set_param(srcs.reso, dsts.reso);
            set_param(srcs.gain, dsts.gain);
            break;
        }
    }

    NotifyListener(engine);
    engine.host.request_callback(&engine.host);
}

StateSnapshot const* PinnedPresetState(Engine const& engine) {
    if (engine.pinned_snapshot.state.extras.origin_preset_hash == 0) return nullptr;
    return &engine.pinned_snapshot.state;
}

bool StateModifiedFromPinned(Engine& engine) {
    auto const current = CurrentStateSnapshot(engine);
    bool const changed = current.extras.modified_from_origin_preset;

    if constexpr (!PRODUCTION_BUILD) {
        auto const& last = PinnedSnapshotForModificationCheck(engine);
        if (changed)
            AssignDiffDescription(engine.state_change_description, last, current);
        else
            dyn::Clear(engine.state_change_description);
    }

    return changed;
}

String IrName(Engine const& engine) {
    ASSERT(g_is_logical_main_thread);
    if (!engine.processor.convo.ir_id) return "None"_s;

    auto const& id = *engine.processor.convo.ir_id;
    auto lib = sample_lib_server::FindLibraryRetained(engine.shared_engine_systems.sample_library_server,
                                                      id.library);
    DEFER { lib.Release(); };
    if (!lib) return id.ir_id;

    auto const ir = lib->irs_by_id.Find(id.ir_id);
    if (!ir) return id.ir_id;

    return (*ir)->name;
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
        SetConvolutionIrAudioData(engine.processor, nullptr, {});
    }

    RecordUndoableStep(engine, ir_id ? "Load IR"_s : "Clear IR"_s);
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

    if (!engine.undoable_step_depth) RecordUndoableStep(engine, "Load instrument"_s);
}

void LoadInstruments(Engine& engine,
                     Array<Optional<sample_lib::InstrumentId>, k_num_layers> const& new_ids,
                     String undo_name) {
    ASSERT(g_is_logical_main_thread);

    bool any = false;
    for (auto const& id : new_ids)
        if (id) {
            any = true;
            break;
        }
    if (!any) return;

    auto snapshot = CurrentStateSnapshot(engine);
    for (auto const layer_index : Range(k_num_layers))
        if (auto const& id = new_ids[layer_index]) snapshot.inst_ids[layer_index] = *id;

    LoadState(engine, snapshot, {.source = StateSource::PresetFile, .update_pinned_snapshot = false});
    RecordUndoableStep(engine, undo_name);
}

bool ViewingPinnedSnapshot(Engine const& engine) { return engine.stashed_modifications.HasValue(); }

void TogglePinnedView(Engine& engine) {
    ASSERT(g_is_logical_main_thread);

    if (engine.stashed_modifications) {
        auto const stashed = *engine.stashed_modifications;
        engine.stashed_modifications.Clear();
        LoadState(engine, stashed, {.source = StateSource::PresetFile, .update_pinned_snapshot = false});
        RecordUndoableStep(engine, "View modifications"_s);
        return;
    }

    if (!StateModifiedFromPinned(engine)) return;

    auto const stash = CurrentStateSnapshot(engine);
    LoadState(engine,
              engine.pinned_snapshot.state,
              {
                  .source = StateSource::PresetFile,
                  .preset_path = engine.pinned_snapshot.preset_path,
                  .known_preset_id = engine.pinned_snapshot.known_preset_id,
                  .update_pinned_snapshot = false,
              });
    RecordUndoableStep(engine, "View preset"_s);
    // Must be set after RecordUndoableStep, which clears the stash.
    engine.stashed_modifications = stash;
}

void LoadPresetFromFile(Engine& engine, String path, u64 known_preset_id) {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator, Kb(16)};
    auto state_outcome = LoadPresetFile(path, scratch_arena);
    auto const error_id = HashMultiple(Array {"preset-load"_s, path});

    if (state_outcome.HasValue()) {
        auto& state = state_outcome.Value();
        FillStateExtrasFromPath(state, path);
        LoadState(engine,
                  state,
                  {
                      .source = StateSource::PresetFile,
                      .preset_path = path,
                      .known_preset_id = known_preset_id,
                  });
        engine.error_notifications.RemoveError(error_id);
        RecordUndoableStep(engine, path::FilenameWithoutExtension(path), true);
    } else if (auto err = engine.error_notifications.BeginWriteError(error_id)) {
        DEFER { engine.error_notifications.EndWriteError(*err); };
        dyn::AssignFitInCapacity(err->title, "Failed to load preset"_s);
        dyn::AssignFitInCapacity(err->message, path);
        err->error_code = state_outcome.Error();
    }
}

void SaveCurrentStateToFile(Engine& engine, String path) {
    ASSERT(path.size);
    ASSERT(IsValidUtf8(path));
    ASSERT(path::IsAbsolute(path));

    auto state = CurrentStateSnapshot(engine);

    auto const error_id = HashMultiple(Array {"preset-save"_s, path});
    if (auto const outcome = SavePresetFile(
            path,
            state,
            prefs::GetBool(engine.shared_engine_systems.prefs, ExperimentalParamsPreferenceDescriptor()));
        outcome.HasValue()) {
        // The file we just wrote is now this snapshot's origin: rebase extras so the GUI no longer
        // shows "modified" and the new hash identifies the saved preset.
        state.extras.origin_preset_hash = outcome.Value();
        state.extras.modified_from_origin_preset = false;
        auto const preserved_known_preset_id = path::Equal(engine.pinned_snapshot.preset_path, path)
                                                   ? engine.pinned_snapshot.known_preset_id
                                                   : 0;
        FillStateExtrasFromPath(state, path);
        SetPinnedSnapshot(engine, state, path, preserved_known_preset_id);
        RecordUndoableStep(engine, path::FilenameWithoutExtension(path), true);
        engine.error_notifications.RemoveError(error_id);
    } else if (auto err = engine.error_notifications.BeginWriteError(error_id)) {
        DEFER { engine.error_notifications.EndWriteError(*err); };
        dyn::AssignFitInCapacity(err->title, "Failed to save preset"_s);
        dyn::AssignFitInCapacity(err->message, path);
        err->error_code = outcome.Error();
    };
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

    if (HasLegacyFavourites(engine.shared_engine_systems.prefs)) {
        auto& server = engine.shared_engine_systems.sample_library_server;
        sample_lib_server::RequestScanningOfUnscannedFolders(server);
        if (!sample_lib_server::AreLibrariesScanning(server))
            MigrateLegacyFavourites(engine.shared_engine_systems.prefs, server);
        else
            // Let's try again in a bit.
            engine.request_main_thread_callback_at.Store(TimePoint::Now() + 2.0, StoreMemoryOrder::Release);
    }

    if (AutosaveNeeded(engine.autosave_state, engine.shared_engine_systems.prefs))
        QueueAutosave(engine.autosave_state, CurrentStateSnapshot(engine));
}

void Engine::OnProcessorChange(ChangeFlags flags) {
    if (flags & ProcessorListener::IrChanged) MarkNeedsAttributionTextUpdate(attribution_requirements);
    NotifyListener(*this);
    if (!g_is_logical_main_thread) host.request_callback(&host);
}

Engine::Engine(clap_host const& host,
               SharedEngineSystems& shared_engine_systems,
               FloeInstanceIndex instance_index)
    : host(host)
    , shared_engine_systems(shared_engine_systems)
    , instance_index(instance_index)
    , sample_lib_server_async_channel {sample_lib_server::OpenAsyncCommsChannel(
          shared_engine_systems.sample_library_server,
          {
              .error_notifications = error_notifications,
              .result_added_callback = [&engine = *this]() { engine.host.request_callback(&engine.host); },
              .library_changed_callback =
                  [&engine = *this](sample_lib::LibraryId lib_id) {
                      engine.main_thread_callbacks.Push(
                          [lib_id, &engine]() { SampleLibraryChanged(engine, lib_id); });
                  },
          })} {

    RefreshPresetDescriptionCache(*this);

    InitAutosaveState(autosave_state, shared_engine_systems.prefs, random_seed, pinned_snapshot.state);

    {
        UndoableStep seed {};
        seed.snapshot = pinned_snapshot.state;
        seed.is_pinned_snapshot_anchor = true;
        dyn::AssignFitInCapacity(seed.name, "Init");
        undo_history.Record(seed);
    }

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

    {
        auto const now = TimePoint::Now();
        auto t = engine.request_main_thread_callback_at.Load(LoadMemoryOrder::Acquire);
        while (true) {
            if (t.Raw() == 0) break;
            if (now < t) break;
            if (engine.request_main_thread_callback_at.CompareExchangeWeak(t,
                                                                           TimePoint {},
                                                                           RmwMemoryOrder::AcquireRelease,
                                                                           LoadMemoryOrder::Acquire)) {
                engine.host.request_callback(&engine.host);
                break;
            }
        }
    }

    AutosaveToFileIfNeeded(engine.autosave_state, engine.shared_engine_systems.paths);
}

static void PluginOnPreferenceChanged(Engine& engine, prefs::Key key, prefs::Value const* value) {
    ASSERT(g_is_logical_main_thread);
    OnPreferenceChanged(engine.autosave_state, key, value);

    if (prefs::Match(key, value, ExperimentalParamsPreferenceDescriptor())) {
        if (!value->Get<bool>()) {
            // Default-out all experimental params.
            for (auto const i : Range(k_num_parameters)) {
                auto const& desc = k_param_descriptors[i];
                if (desc.flags.experimental) {
                    SetParameterValue(engine.processor,
                                      (ParamIndex)i,
                                      desc.default_linear_value,
                                      {.host_should_not_record = true});
                }
            }
        }
    }
}

usize MegabytesUsedBySamples(Engine const& engine) {
    usize result = 0;
    for (auto& l : engine.processor.layer_processors) {
        if (auto i = l.instrument.TryGet<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>())
            for (auto& d : (*i)->audio_datas)
                result += d->RamUsageBytes();
    }

    return (result) / (1024 * 1024);
}

void SetToDefaultState(Engine& engine) {
    auto default_state = DefaultStateSnapshot();
    LoadState(engine, default_state, {.source = StateSource::PresetFile});
    RecordUndoableStep(engine, "Reset"_s, true);
}

static bool PluginSaveState(Engine& engine, clap_ostream const& stream) {
    auto state = CurrentStateSnapshot(engine);
    ASSERT(state.extras.instance_id.size);
    auto outcome = CodeState(
        state,
        CodeStateArguments {
            .mode = CodeStateArguments::Mode::Encode,
            .read_or_write_data = [&](void* data, usize bytes) -> ErrorCodeOr<void> {
                u64 bytes_written = 0;
                while (bytes_written != bytes) {
                    ASSERT(bytes_written < bytes);
                    auto const n =
                        stream.write(&stream, (u8 const*)data + bytes_written, bytes - bytes_written);
                    if (n < 0) return ErrorCode(CommonError::PluginHostError);
                    bytes_written += (u64)n;
                }
                return k_success;
            },
            .source = StateSource::Daw,
            .write_experimental_params =
                prefs::GetBool(engine.shared_engine_systems.prefs, ExperimentalParamsPreferenceDescriptor()),
        });

    auto const error_id = SourceLocationHash();

    if (outcome.HasError()) {
        if (auto err = engine.error_notifications.BeginWriteError(error_id)) {
            DEFER { engine.error_notifications.EndWriteError(*err); };
            dyn::AssignFitInCapacity(err->title, "Failed to save state for DAW"_s);
            err->error_code = outcome.Error();
        }
        return false;
    }

    engine.error_notifications.RemoveError(error_id);
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
                  });

    auto const error_id = SourceLocationHash();

    if (outcome.HasError()) {
        if (auto err = engine.error_notifications.BeginWriteError(error_id)) {
            DEFER { engine.error_notifications.EndWriteError(*err); };
            dyn::AssignFitInCapacity(err->title, "Failed to load state for DAW"_s);
            err->error_code = outcome.Error();
        }
        return false;
    }

    engine.error_notifications.RemoveError(error_id);
    // Fallback display name for legacy DAW state that doesn't carry one.
    if (state.extras.display_name.size == 0) dyn::Assign(state.extras.display_name, "DAW State"_s);
    LoadState(engine, state, {.source = StateSource::Daw});
    engine.undo_history.Clear();
    RecordUndoableStep(engine, state.extras.display_name, true);
    return true;
}

void RecordUndoableStep(Engine& engine, String name, bool is_pinned_snapshot_anchor) {
    ASSERT(g_is_logical_main_thread);
    engine.stashed_modifications.Clear();
    auto current = CurrentStateSnapshot(engine);
    UndoableStep step {
        .snapshot = current,
        .is_pinned_snapshot_anchor = is_pinned_snapshot_anchor,
    };
    dyn::AssignFitInCapacity(step.name, name);
    engine.undo_history.Record(step);
}

void Engine::OnParamChange(ProcessorListener::ParamChange change, ParamIndex param) {
    switch (change) {
        case ProcessorListener::ValueChanged:
            if (!undoable_step_depth) RecordUndoableStep(*this, k_param_descriptors[ToInt(param)].name);
            break;
        case ProcessorListener::GestureBegin:
            BeginUndoableStep(*this, k_param_descriptors[ToInt(param)].name);
            break;
        case ProcessorListener::GestureEnd: EndUndoableStep(*this); break;
    }
}

void BeginUndoableStep(Engine& engine, String name) {
    ASSERT(g_is_logical_main_thread);
    if (engine.undoable_step_depth == 0) dyn::AssignFitInCapacity(engine.pending_undoable_step_name, name);
    ++engine.undoable_step_depth;
}

void EndUndoableStep(Engine& engine) {
    ASSERT(g_is_logical_main_thread);
    ASSERT(engine.undoable_step_depth);
    if (--engine.undoable_step_depth == 0) RecordUndoableStep(engine, engine.pending_undoable_step_name);
}

// Walks the undo stack backwards from the current top to find the most recent pinned-snapshot-anchor
// entry, and sets the engine's pinned snapshot to that anchor's snapshot. The path is left empty so the
// existing preset_path_needs_lookup mechanism resolves it from origin_preset_hash. If no anchor is
// reachable (e.g. the oldest scrolled off the ring), the pinned snapshot is left untouched.
static void RestorePinnedSnapshotFromAnchor(Engine& engine) {
    auto const& undo = engine.undo_history.undo;
    for (auto i = undo.Size(); i > 0; --i) {
        auto const& step = undo.At(i - 1);
        if (step.is_pinned_snapshot_anchor) {
            SetPinnedSnapshot(engine, step.snapshot, ""_s, 0);
            return;
        }
    }
}

void Undo(Engine& engine) {
    ASSERT(g_is_logical_main_thread);
    ASSERT(engine.undo_history.CanUndo());
    engine.stashed_modifications.Clear();
    auto const entry = engine.undo_history.Undo();
    LoadState(engine, entry.snapshot, {.source = StateSource::PresetFile, .update_pinned_snapshot = false});
    RestorePinnedSnapshotFromAnchor(engine);
}

void Redo(Engine& engine) {
    ASSERT(g_is_logical_main_thread);
    ASSERT(engine.undo_history.CanRedo());
    engine.stashed_modifications.Clear();
    auto const entry = engine.undo_history.Redo();
    LoadState(engine, entry.snapshot, {.source = StateSource::PresetFile, .update_pinned_snapshot = false});
    RestorePinnedSnapshotFromAnchor(engine);
}

PluginCallbacks<Engine> const g_engine_callbacks {
    .on_main_thread = OnMainThread,
    .on_timer = PluginOnTimer,
    .on_poll_thread = PluginOnPollThread,
    .on_preference_changed = PluginOnPreferenceChanged,
    .save_state = PluginSaveState,
    .load_state = PluginLoadState,
};
