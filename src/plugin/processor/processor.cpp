// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "processor.hpp"

#include "os/threading.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/error_reporting.hpp"
#include "common_infrastructure/performance_profile.hpp"
#include "common_infrastructure/preferences.hpp"

#include "clap/ext/params.h"
#include "param.hpp"
#include "plugin/plugin.hpp"
#include "voices.hpp"

static auto HostsParamsExtension(AudioProcessor& processor) {
    return (clap_host_params const*)processor.host.get_extension(&processor.host, CLAP_EXT_PARAMS);
}

bool EffectIsOn(Parameters const& params, Effect* effect) {
    return params.BoolValue(k_effect_info[ToInt(effect->type)].on_param_index);
}

bool IsMidiCCLearnActive(AudioProcessor const& processor) {
    ASSERT(g_is_logical_main_thread);
    return processor.midi_learn_param_index.Load(LoadMemoryOrder::Relaxed).HasValue();
}

void LearnMidiCC(AudioProcessor& processor, ParamIndex param) {
    ASSERT(g_is_logical_main_thread);
    processor.midi_learn_param_index.Store((s32)param, StoreMemoryOrder::Relaxed);
}

void CancelMidiCCLearn(AudioProcessor& processor) {
    ASSERT(g_is_logical_main_thread);
    processor.midi_learn_param_index.Store(k_nullopt, StoreMemoryOrder::Relaxed);
}

void AddLearnedMidiCC(AudioProcessor& processor, ParamIndex param, u7 cc_num) {
    processor.param_learned_ccs[ToInt(param)].Set(cc_num);
}

void UnlearnMidiCC(AudioProcessor& processor, ParamIndex param, u7 cc_num_to_remove) {
    processor.param_learned_ccs[ToInt(param)].Clear(cc_num_to_remove);
}

Bitset<128> GetLearnedCCsBitsetForParam(AudioProcessor const& processor, ParamIndex param) {
    ASSERT(g_is_logical_main_thread);
    return processor.param_learned_ccs[ToInt(param)].GetBlockwise();
}

bool CcControllerMovedParamRecently(AudioProcessor const& processor, ParamIndex param) {
    ASSERT(g_is_logical_main_thread);
    return (processor.time_when_cc_moved_param[ToInt(param)].Load(LoadMemoryOrder::Relaxed) + 0.4) >
           TimePoint::Now();
}

void AppendMacroDestination(AudioProcessor& processor, AppendMacroDestinationConfig config) {
    ASSERT(g_is_logical_main_thread);

    auto const i = processor.main_macro_destinations[config.macro_index].Append({
        .param_index = config.param,
        .value = 0.0f,
    });

    processor.macro_dest_inbox[config.macro_index][*i].Produce({
        .new_value = 0.0f,
        .new_param_index = config.param,
    });

    processor.host.request_process(&processor.host);
}

void RemoveMacroDestination(AudioProcessor& processor, RemoveMacroDestinationConfig config) {
    ASSERT(g_is_logical_main_thread);

    auto& macro_dests = processor.main_macro_destinations[config.macro_index];

    macro_dests.RemoveAt(config.destination_index);

    // Update atomics for all shifted destinations.
    for (usize i = config.destination_index; i < k_max_macro_destinations; i++) {
        auto const& dest = macro_dests.items[i];
        processor.macro_dest_inbox[config.macro_index][i].Produce(
            !dest.param_index ? audio_thread_inbox::MacroDestinationUpdate::ProduceOptions {.clear = true}
                              : audio_thread_inbox::MacroDestinationUpdate::ProduceOptions {
                                    .new_value = dest.value,
                                    .new_param_index = dest.param_index,
                                });
        if (!dest.param_index) break;
    }

    processor.host.request_process(&processor.host);
}

void RetargetMacroDestinations(AudioProcessor& processor, ParamIndex from, ParamIndex to) {
    ASSERT(g_is_logical_main_thread);

    bool any_changed = false;
    for (auto const macro_index : Range(k_num_macros)) {
        auto& dests = processor.main_macro_destinations[macro_index];
        for (auto const dest_index : Range(k_max_macro_destinations)) {
            auto& dest = dests.items[dest_index];
            if (!dest.param_index) break;
            if (*dest.param_index != from) continue;
            dest.param_index = to;
            processor.macro_dest_inbox[macro_index][dest_index].Produce({
                .new_value = dest.value,
                .new_param_index = to,
            });
            any_changed = true;
        }
    }

    if (any_changed) processor.host.request_process(&processor.host);
}

void MacroDestinationValueChanged(AudioProcessor& processor, MacroDestinationValueChangedConfig config) {
    ASSERT(g_is_logical_main_thread);

    auto const val =
        processor.main_macro_destinations[config.macro_index].items[config.destination_index].value;

    processor.macro_dest_inbox[config.macro_index][config.destination_index].Produce({
        .new_value = val,
    });

    processor.host.request_process(&processor.host);
}

static Bitset<k_num_layers> LayerSilentState(Bitset<k_num_layers> solo, Bitset<k_num_layers> mute) {
    bool const any_solo = solo.AnyValuesSet();
    Bitset<k_num_layers> result {};

    for (auto const layer_index : Range(k_num_layers)) {
        bool state = any_solo;

        auto is_solo = solo.Get(layer_index);
        if (is_solo) {
            result.SetToValue(layer_index, false);
            continue;
        }

        auto is_mute = mute.Get(layer_index);
        if (is_mute) {
            result.SetToValue(layer_index, true);
            continue;
        }

        result.SetToValue(layer_index, state);
    }

    return result;
}

static void HandleMuteSolo(AudioProcessor& processor) {
    auto layer_silent_state = LayerSilentState(processor.solo, processor.mute);

    for (auto const layer_index : Range(k_num_layers)) {
        bool const is_silent = layer_silent_state.Get(layer_index);
        SetSilent(processor.layer_processors[layer_index], is_silent);
    }
}

bool LayerIsSilent(AudioProcessor const& processor, u32 layer_index) {
    ASSERT(g_is_logical_main_thread);

    Bitset<k_num_layers> solo;
    Bitset<k_num_layers> mute;
    for (auto const i : Range<u8>(k_num_layers)) {
        solo.SetToValue(i, processor.main_params.BoolValue(i, LayerParamIndex::Solo));
        mute.SetToValue(i, processor.main_params.BoolValue(i, LayerParamIndex::Mute));
    }

    return LayerSilentState(solo, mute).Get(layer_index);
}

static ChangedParams UpdateMacroAdjustedValues(Parameters& macro_adjusted_params,
                                               ChangedParams const& params,
                                               MacroDestinations const& macros) {
    Bitset<k_num_parameters> needs_adjustment {};
    for (auto const [macro_index, macro] : Enumerate(macros)) {
        auto const macro_param_index = k_macro_params[macro_index];
        bool const macro_changed = params.Changed(macro_param_index);

        for (auto const& dest : macro.items) {
            if (!dest.param_index) continue;
            if (params.ChangedIgnoringLegacy(*dest.param_index) || macro_changed)
                needs_adjustment.Set(ToInt(*dest.param_index));
        }
    }

    for (auto const param_index : Range(k_num_parameters)) {
        if (!needs_adjustment.Get(param_index)) {
            if (params.changed.Get(param_index))
                macro_adjusted_params.values[param_index] = params.params.values[param_index];
            continue;
        }

        macro_adjusted_params.values[param_index] = AdjustedLinearValue(params.params.values,
                                                                        macros,
                                                                        params.params.values[param_index],
                                                                        (ParamIndex)param_index);
    }

    return {
        .params = macro_adjusted_params,
        .changed = params.changed | needs_adjustment,
    };
}

static void ProcessorHandleChanges(AudioProcessor& processor, ProcessBlockChanges changes) {
    if (!changes.changed_params.changed.AnyValuesSet() && !changes.tempo_changed &&
        !changes.note_events.size && !changes.pitchwheel_changed.AnyValuesSet())
        return;

    ZoneScoped;
    ZoneTextF("Num changed params: %d", (int)changes.changed_params.changed.NumSet());
    ZoneTextF("Num note events: %d", (int)changes.note_events.size);

    // Before using any of the changed params, we need to update any macro-adjusted values and apply them so
    // any further processors use the adjusted values. The placement-new is a bit of a hack because
    // ChangedParams contains a const reference.
    PLACEMENT_NEW(&changes.changed_params)
    ChangedParams {UpdateMacroAdjustedValues(processor.audio_macro_adjusted_params,
                                             changes.changed_params,
                                             processor.audio_macro_destinations)};

    if (auto p = changes.changed_params.ProjectedValue(ParamIndex::MasterVolume)) processor.master_vol = *p;

    if (auto p = changes.changed_params.ProjectedValue(ParamIndex::MasterTimbre)) {
        processor.shared_layer_params.timbre_value_01 = *p;
        for (auto& voice : processor.voice_pool.EnumerateActiveVoices())
            UpdateXfade(voice, ExpressionAdjustedTimbre01(*p, voice), false);
    }

    if (auto p = changes.changed_params.ProjectedValue(ParamIndex::LegacyMasterVelocity))
        processor.shared_layer_params.velocity_to_volume_01 = *p;

    {
        bool mute_or_solo_changed = false;
        for (auto const layer_index : Range(k_num_layers)) {
            if (auto p = changes.changed_params.BoolValue(
                    ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::Mute))) {
                processor.mute.SetToValue(layer_index, *p);
                mute_or_solo_changed = true;
            }
            if (auto p = changes.changed_params.BoolValue(
                    ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::Solo))) {
                processor.solo.SetToValue(layer_index, *p);
                mute_or_solo_changed = true;
            }
        }
        if (mute_or_solo_changed) HandleMuteSolo(processor);
    }

    // Auto Rate needs a project-wide octave shift that's safe for every layer's anchor. Refresh it
    // before per-layer change processing so ArpUpdateRate (which reads it from the context) picks up
    // tempo or auto-rate-mode changes from this same block.
    RecomputeSharedArpAutoRateShift(processor.layer_processors,
                                    &changes.changed_params,
                                    processor.audio_processing_context);

    for (auto [index, l] : Enumerate(processor.layer_processors))
        ProcessLayerChanges(l, processor.audio_processing_context, changes, processor.voice_pool);

    for (auto effect : processor.effects_ordered_by_type)
        effect->ProcessChanges(changes, processor.audio_processing_context);
}

void ParameterJustStartedMoving(AudioProcessor& processor, ParamIndex index) {
    ASSERT(g_is_logical_main_thread);

    processor.param_change_inbox[ToInt(index)].AddGuiGesture(
        audio_thread_inbox::ParamChange::GuiGestureType::Begin);

    if (auto host_params = HostsParamsExtension(processor)) host_params->request_flush(&processor.host);

    processor.listener.OnParamChange(ProcessorListener::ParamChange::GestureBegin, index);
}

void ParameterJustStoppedMoving(AudioProcessor& processor, ParamIndex index) {
    ASSERT(g_is_logical_main_thread);

    processor.param_change_inbox[ToInt(index)].AddGuiGesture(
        audio_thread_inbox::ParamChange::GuiGestureType::End);

    if (auto host_params = HostsParamsExtension(processor)) host_params->request_flush(&processor.host);

    processor.listener.OnParamChange(ProcessorListener::ParamChange::GestureEnd, index);
}

bool SetParameterValue(AudioProcessor& processor, ParamIndex index, f32 value, ParamChangeFlags flags) {
    ASSERT(g_is_logical_main_thread);

    bool const changed = processor.main_params.values[ToInt(index)] != value;
    processor.main_params.SetLinearValue(index, value);

    processor.param_change_inbox[ToInt(index)].AddValueChanged(
        value,
        {
            .send_to_host = true,
            .host_should_record = !flags.host_should_not_record,
        });

    if (auto host_params = HostsParamsExtension(processor))
        host_params->request_flush(&processor.host);
    else
        processor.host.request_process(&processor.host);

    processor.listener.OnParamChange(ProcessorListener::ParamChange::ValueChanged, index);

    return changed;
}

void MoveEffectToNewSlot(EffectsArray& effects, Effect* effect_to_move, usize slot) {
    if (slot < 0 || slot >= k_num_effect_types) return;

    Optional<usize> original_slot = {};
    for (auto [index, fx] : Enumerate(effects)) {
        if (fx == effect_to_move) {
            original_slot = index;
            break;
        }
    }
    if (!original_slot) return;
    if (slot == *original_slot) return;

    // Remove the old location.
    for (usize i = *original_slot; i < (k_num_effect_types - 1); ++i)
        effects[i] = effects[i + 1];

    // Make room at the new location.
    for (usize i = k_num_effect_types - 1; i > slot; --i)
        effects[i] = effects[i - 1];

    // Fill the slot.
    effects[slot] = effect_to_move;
}

usize FindSlotInEffects(EffectsArray const& effects, Effect* fx) {
    if (auto index = Find(effects, fx)) return *index;
    PanicIfReached();
    return UINT64_MAX;
}

u64 EncodeEffectsArray(Array<EffectType, k_num_effect_types> const& arr) {
    static_assert(k_num_effect_types < 16, "The effect index is encoded into 4 bits");
    static_assert(k_num_effect_types * 4 <= sizeof(u64) * 8);
    u64 result {};
    for (auto [index, e] : Enumerate(arr)) {
        result |= (u64)e;
        if (index != k_num_effect_types - 1) result <<= 4;
    }
    return result;
}

u64 EncodeEffectsArray(EffectsArray const& arr) {
    Array<EffectType, k_num_effect_types> type_arr;
    for (auto [i, ptr] : Enumerate(arr))
        type_arr[i] = ptr->type;
    return EncodeEffectsArray(type_arr);
}

EffectsArray DecodeEffectsArray(u64 val, EffectsArray const& effects_ordered_by_type) {
    EffectsArray result {};
    for (int i = k_num_effect_types - 1; i >= 0; --i) {
        result[(usize)i] = effects_ordered_by_type[val & 0xf];
        val >>= 4;
    }
    return result;
}

static EffectsArray OrderEffectsToEnum(EffectsArray e) {
    if constexpr (!PRODUCTION_BUILD)
        for (auto effect : e)
            ASSERT(effect != nullptr);
    Sort(e, [](Effect const* a, Effect const* b) { return a->type < b->type; });
    return e;
}

static void ClearInbox(AudioProcessor& processor) {
    for (auto& dests : processor.macro_dest_inbox)
        for (auto& v : dests)
            v.Clear();
    for (auto& p : processor.param_change_inbox)
        p.Clear();
    processor.inbox_flags.Store(0, StoreMemoryOrder::Release);
}

static void Deactivate(AudioProcessor& processor) {
    ASSERT(g_is_logical_main_thread);

    if (processor.activated) {
        ClearInbox(processor);
        processor.voice_pool.EndAllVoicesInstantly();
        processor.activated = false;
    }
}

void SetInstrument(AudioProcessor& processor,
                   u32 layer_index,
                   Instrument const& instrument,
                   SetInstrumentOptions const& opts) {
    ASSERT(g_is_logical_main_thread);
    ASSERT(layer_index < k_num_layers);

    auto& layer = processor.layer_processors[layer_index];

    if (opts.wipe_arp_slice_config) {
        layer.arp_state.slice_loop_length.Store(0, StoreMemoryOrder::Relaxed);
        layer.arp_state.slice_start_offset.Store(0, StoreMemoryOrder::Relaxed);
    }

    // If we currently have a sampler instrument, we keep it alive by storing it and releasing at a later
    // time.
    if (auto const current =
            layer.instrument.TryGet<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>())
        dyn::Append(processor.lifetime_extended_insts, *current);

    // Retain the new instrument
    if (auto sampled_inst =
            instrument.TryGet<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>())
        sampled_inst->Retain();

    layer.instrument = instrument;

    switch (instrument.tag) {
        case InstrumentType::Sampler: {
            auto& sampler_inst =
                instrument.Get<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>();
            layer.desired_inst.Set(&*sampler_inst);
            break;
        }
        case InstrumentType::WaveformSynth: {
            auto& w = instrument.Get<WaveformType>();
            layer.desired_inst.Set(w);
            break;
        }
        case InstrumentType::None: {
            layer.desired_inst.SetNone();
            auto const layer_solo_index = ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::Solo);
            if (processor.main_params.BoolValue(layer_solo_index))
                SetParameterValue(processor, layer_solo_index, 0, {.host_should_not_record = true});
            break;
        }
    }

    processor.inbox_flags.FetchOr(CheckedCast<u8>(audio_thread_inbox::LayerInstrumentChanged << layer_index),
                                  RmwMemoryOrder::Release);
    processor.host.request_process(&processor.host);
}

void SetConvolutionIrAudioData(AudioProcessor& processor,
                               AudioData const* audio_data,
                               sample_lib::ImpulseResponse::AudioProperties const& audio_props) {
    ASSERT(g_is_logical_main_thread);
    processor.convo.ConvolutionIrDataLoaded(audio_data, audio_props);
    processor.inbox_flags.FetchOr(audio_thread_inbox::ConvolutionIRChanged, RmwMemoryOrder::Release);
    processor.host.request_process(&processor.host);
}

void ApplyState(AudioProcessor& processor, StateSnapshot const& state, StateSource source) {
    ASSERT(g_is_logical_main_thread);

    if (source == StateSource::Daw)
        for (auto [i, cc] : Enumerate(processor.param_learned_ccs))
            cc.AssignBlockwise(state.extras.performance_controls.param_learned_ccs[i]);

    processor.main_params.values = state.param_values;

    processor.desired_effects_order.Store(EncodeEffectsArray(state.fx_order), StoreMemoryOrder::Relaxed);

    // Layers.
    for (auto const layer_index : Range(k_num_layers))
        LayerApplyState(processor.layer_processors[layer_index], state, source);

    // Macro destinations.
    {
        processor.main_macro_destinations = state.macro_destinations;

        for (auto const macro_index : Range(k_num_macros)) {
            for (auto const dest_index : Range(k_max_macro_destinations)) {
                auto const& new_dest = state.macro_destinations[macro_index].items[dest_index];
                auto& event = processor.macro_dest_inbox[macro_index][dest_index];
                event.Produce(!new_dest.param_index
                                  ? audio_thread_inbox::MacroDestinationUpdate::ProduceOptions {.clear = true}
                                  : audio_thread_inbox::MacroDestinationUpdate::ProduceOptions {
                                        .new_value = new_dest.value,
                                        .new_param_index = new_dest.param_index,
                                    });
            }
        }
    }

    // Reload all parameters.
    {
        if (auto host_params = HostsParamsExtension(processor))
            host_params->rescan(&processor.host, CLAP_PARAM_RESCAN_VALUES);

        for (auto [param_index, p] : Enumerate(processor.param_change_inbox)) {
            p.AddValueChanged(
                state.param_values[param_index],
                {
                    .send_to_host = false, // The host already knows because of the rescan above.
                    .host_should_record = false,
                });
        }
    }

    if (source == StateSource::Daw)
        processor.performance_settings.Store(state.extras.performance_controls.settings,
                                              StoreMemoryOrder::Release);

    processor.inbox_flags.FetchOr(audio_thread_inbox::ReloadAllAudioState, RmwMemoryOrder::Release);

    processor.host.request_process(&processor.host);
}

StateSnapshot CaptureStateSnapshot(AudioProcessor const& processor) {
    StateSnapshot result {};
    auto const ordered_fx_pointers =
        DecodeEffectsArray(processor.desired_effects_order.Load(LoadMemoryOrder::Relaxed),
                           processor.effects_ordered_by_type);
    for (auto [i, fx_pointer] : Enumerate(ordered_fx_pointers))
        result.fx_order[i] = fx_pointer->type;

    for (auto const i : Range(k_num_layers)) {
        result.inst_ids[i] = processor.layer_processors[i].instrument_id;
        result.velocity_curve_points[i] = processor.layer_processors[i].velocity_curve_map.points;
        result.harmony_intervals[i] = processor.layer_processors[i].harmony_intervals.GetBlockwise();
        for (auto const step_index : Range(k_arp_max_steps))
            result.arp_steps[i][step_index] =
                processor.layer_processors[i].arp_state.steps[step_index].Load(LoadMemoryOrder::Relaxed);
        result.slice_arp_configs[i] = {
            .start_offset =
                processor.layer_processors[i].arp_state.slice_start_offset.Load(LoadMemoryOrder::Relaxed),
            .loop_length =
                processor.layer_processors[i].arp_state.slice_loop_length.Load(LoadMemoryOrder::Relaxed),
        };
    }

    result.ir_id = processor.convo.ir_id;

    result.param_values = processor.main_params.values;

    result.macro_destinations = processor.main_macro_destinations;

    for (auto [i, cc] : Enumerate(processor.param_learned_ccs))
        result.extras.performance_controls.param_learned_ccs[i] = cc.GetBlockwise();

    result.extras.performance_controls.settings =
        processor.performance_settings.Load(LoadMemoryOrder::Relaxed);

    return result;
}

void ApplyPerformanceProfile(AudioProcessor& processor, perf_profile::Profile const& profile) {
    ASSERT(g_is_logical_main_thread);

    for (auto const i : Range(k_num_parameters))
        processor.param_learned_ccs[i].AssignBlockwise(profile.controls.param_learned_ccs[i]);

    processor.performance_settings.Store(profile.controls.settings, StoreMemoryOrder::Release);
}

perf_profile::Profile CaptureCurrentPerformanceProfile(AudioProcessor const& processor) {
    ASSERT(g_is_logical_main_thread);

    perf_profile::Profile result {};

    for (auto [i, cc] : Enumerate(processor.param_learned_ccs))
        result.controls.param_learned_ccs[i] = cc.GetBlockwise();

    result.controls.settings = processor.performance_settings.Load(LoadMemoryOrder::Relaxed);

    return result;
}

inline void ResetProcessor(AudioProcessor& processor, ProcessBlockChanges& changes) {
    ZoneScoped;
    processor.whole_engine_volume_fade.ForceSetFullVolume();

    // Set pending parameter changes
    changes.changed_params.changed |= Exchange(processor.pending_param_changes, {});
    ProcessorHandleChanges(processor, changes);

    // Discard any smoothing
    processor.master_vol_smoother.Reset();

    // Set the convolution IR
    processor.convo.SwapConvolversIfNeeded();

    // Set the effects order
    processor.actual_fx_order =
        DecodeEffectsArray(processor.desired_effects_order.Load(LoadMemoryOrder::Relaxed),
                           processor.effects_ordered_by_type);

    // Reset the effects
    for (auto fx : processor.actual_fx_order)
        fx->Reset();
    processor.fx_need_another_frame_of_processing = false;

    for (auto [layer_index, l] : Enumerate(processor.layer_processors))
        if (ChangeInstrumentIfNeededAndReset(l, processor.voice_pool, processor.audio_processing_context))
            processor.restart_voices_for_layer_bitset.Set(layer_index);

    // Instrument swaps may have changed every layer's anchor — recompute so the next block's
    // ArpUpdateRate calls observe a coherent shift across the new layer set.
    RecomputeSharedArpAutoRateShift(processor.layer_processors, nullptr, processor.audio_processing_context);

    Reset(processor.voice_pool);
}

static bool Activate(AudioProcessor& processor, PluginActivateArgs args) {
    ASSERT(g_is_logical_main_thread);

    ASSERT(args.sample_rate > 0);

    processor.audio_processing_context.process_block_size_max = args.max_block_size;
    processor.audio_processing_context.sample_rate = (f32)args.sample_rate;
    processor.audio_processing_context.pitchwheel_position = {};
    processor.audio_processing_context.midi_note_state = {};

    processor.prev_transport_playing = false;
    processor.gui_note_currently_held = k_nullopt;
    processor.inbox_flags.Store(0, StoreMemoryOrder::Relaxed);

    processor.audio_processing_context.one_pole_smoothing_cutoff_0_2ms =
        OnePoleLowPassFilter<f32>::MsToCutoff(0.2f, (f32)args.sample_rate);
    processor.audio_processing_context.one_pole_smoothing_cutoff_1ms =
        OnePoleLowPassFilter<f32>::MsToCutoff(1, (f32)args.sample_rate);
    processor.audio_processing_context.one_pole_smoothing_cutoff_10ms =
        OnePoleLowPassFilter<f32>::MsToCutoff(10, (f32)args.sample_rate);

    for (auto& fx : processor.effects_ordered_by_type)
        fx->PrepareToPlay(processor.audio_processing_context);

    if (Exchange(processor.previous_block_size, processor.audio_processing_context.process_block_size_max) <
        processor.audio_processing_context.process_block_size_max) {

        processor.voice_pool.PrepareToPlay();

        for (auto [index, l] : Enumerate(processor.layer_processors))
            PrepareToPlay(l, processor.audio_processing_context);

        processor.peak_meter.PrepareToPlay(processor.audio_processing_context.sample_rate);
    }

    // Update the audio-thread representations of the parameters.
    {
        ClearInbox(processor);
        processor.audio_params = processor.main_params;
        processor.audio_macro_destinations = processor.main_macro_destinations;
        ProcessBlockChanges changes {
            .changed_params = {processor.audio_params, {}},
        };
        changes.changed_params.changed.SetAll();
        ResetProcessor(processor, changes);
    }

    processor.activated = true;
    return true;
}

static void ResetRandomState(AudioProcessor& processor, u8 seed_0_99) {
    u64 seed_bytes = seed_0_99;
    processor.master_random_seed = HashFnv1a(Span<u8 const> {(u8 const*)&seed_bytes, sizeof(seed_bytes)});

    // Use a copy of the master seed to derive RR starting positions so we don't consume the master seed state
    // that voices will use.
    u64 rr_seed = processor.master_random_seed;
    for (auto& layer : processor.layer_processors) {
        layer.rr_pos = {};
        if (auto inst_ptr = layer.audio_thread_inst.TryGet<sample_lib::LoadedInstrument const*>()) {
            auto const& inst = **inst_ptr;
            for (auto const trigger_event : Range(ToInt(sample_lib::TriggerEvent::Count))) {
                for (auto [group_index, group] :
                     Enumerate(inst.instrument.round_robin_sequence_groups[trigger_event])) {
                    auto const num_positions = (u8)(group.max_rr_pos + 1);
                    if (num_positions > 1)
                        layer.rr_pos[trigger_event][group_index] = (u8)(RandomU64(rr_seed) % num_positions);
                }
            }
        }
    }
}

// Returns true if the note was consumed as a keyswitch reset (and should not trigger voices).
static bool HandleResetKeyswitch(AudioProcessor& processor, u7 note_key) {
    auto const settings = processor.performance_settings.Load(LoadMemoryOrder::Acquire);
    if (settings.reset_keyswitch.HasValue() && note_key == settings.reset_keyswitch.Value()) {
        ResetRandomState(processor, settings.seed);
        return true;
    }
    return false;
}

// Voices eligible for incoming per-note expression: still tracking (key held) and matching the event's
// channel/key, where nullopt matches everything.
static void ForEachExpressionTrackedVoice(VoicePool& pool,
                                          Optional<u4> channel,
                                          Optional<u7> key,
                                          FunctionRef<void(Voice&)> func) {
    for (auto& v : pool.EnumerateActiveVoices()) {
        if (!v.track_expression) continue;
        if (channel && v.midi_key_trigger.channel != *channel) continue;
        if (key && v.midi_key_trigger.note != *key) continue;
        func(v);
    }
}

static void ProcessClapNoteOrMidi(AudioProcessor& processor,
                                  clap_event_header const& event,
                                  clap_output_events const& out,
                                  u32 block_start_frame,
                                  ProcessorListener::ChangeFlags& change_flags,
                                  ProcessBlockChanges& changes,
                                  ChangedParams& changes_for_main_thread) {
    // IMPROVE: support per-param modulation and automation - each param can opt-in individually.

    ASSERT_HOT(event.time >= block_start_frame);

    switch (event.type) {
        case CLAP_EVENT_NOTE_ON: {
            auto const note = (clap_event_note const&)event;

            if (note.key > MidiMessage::k_u7_max) break;
            if (note.channel > MidiMessage::k_u4_max) break;
            if (HandleResetKeyswitch(processor, (u7)note.key)) break;

            MidiChannelNote const chan_note {.note = (u7)note.key, .channel = (u4)note.channel};
            // MuLab 10 VST3 sent invalid values.
            auto const vel = __builtin_isnan(note.velocity) ? 1.0f : Clamp((f32)note.velocity, 0.0f, 1.0f);

            processor.audio_processing_context.midi_note_state.NoteOn(chan_note, vel);

            processor.uses_fractional_velocity_values.Store(true, StoreMemoryOrder::Relaxed);

            dyn::Append(changes.note_events,
                        {
                            .velocity = vel,
                            .offset = event.time - block_start_frame,
                            .note = chan_note,
                            .type = NoteEvent::Type::On,
                        });
            break;
        }

        case CLAP_EVENT_NOTE_OFF: {
            auto const note = (clap_event_note const&)event;

            if (note.key > MidiMessage::k_u7_max) break;
            if (note.channel > MidiMessage::k_u4_max) break;
            MidiChannelNote const chan_note {.note = (u7)note.key, .channel = (u4)note.channel};
            auto const vel = __builtin_isnan(note.velocity) ? 1.0f : Clamp((f32)note.velocity, 0.0f, 1.0f);

            processor.audio_processing_context.midi_note_state.NoteOff(chan_note);
            processor.audio_processing_context.mpe.ResetChannelExpression(chan_note.channel);
            if (processor.audio_processing_context.mpe.IsMemberChannel(chan_note.channel))
                processor.audio_processing_context.pitchwheel_position[chan_note.channel] = 0.0f;

            dyn::Append(changes.note_events,
                        {
                            .velocity = vel,
                            .offset = event.time - block_start_frame,
                            .note = chan_note,
                            .type = NoteEvent::Type::Off,
                        });
            break;
        }

        case CLAP_EVENT_NOTE_CHOKE: {
            auto const note = (clap_event_note const&)event;

            if (note.key == -1) {
                if (note.channel == -1) {
                    for (auto const chan : Range(16u)) {
                        processor.audio_processing_context.midi_note_state.keys_held[chan].ClearAll();
                        processor.audio_processing_context.midi_note_state.sustain_keys[chan].ClearAll();
                    }
                    processor.voice_pool.EndAllVoicesInstantly();
                } else if (note.channel >= 0 && note.channel < 16) {
                    processor.audio_processing_context.midi_note_state.keys_held[(usize)note.channel]
                        .ClearAll();
                    processor.audio_processing_context.midi_note_state.sustain_keys[(usize)note.channel]
                        .ClearAll();
                    for (auto& v : processor.voice_pool.EnumerateActiveVoices())
                        if (v.midi_key_trigger.channel == note.channel) EndVoiceInstantly(v);
                }
            } else if (note.key < 128 && note.key >= 0) {
                if (note.channel == -1) {
                    for (auto const chan : Range(16u)) {
                        processor.audio_processing_context.midi_note_state.keys_held[chan].Clear(
                            (usize)note.key);
                        processor.audio_processing_context.midi_note_state.sustain_keys[chan].Clear(
                            (usize)note.key);
                    }
                    for (auto& v : processor.voice_pool.EnumerateActiveVoices())
                        if (v.midi_key_trigger.note == note.key) EndVoiceInstantly(v);
                } else if (note.channel >= 0 && note.channel < 16) {
                    processor.audio_processing_context.midi_note_state.keys_held[(usize)note.channel].Clear(
                        (usize)note.key);
                    processor.audio_processing_context.midi_note_state.sustain_keys[(usize)note.channel]
                        .Clear((usize)note.key);
                    for (auto& v : processor.voice_pool.EnumerateActiveVoices())
                        if (v.midi_key_trigger.note == note.key && v.midi_key_trigger.channel == note.channel)
                            EndVoiceInstantly(v);
                }
            }

            break;
        }

        case CLAP_EVENT_NOTE_EXPRESSION: {
            auto const& expr = (clap_event_note_expression const&)event;
            if (!__builtin_isfinite(expr.value)) break;
            if (expr.channel > 15 || expr.key > 127) break;
            auto const& context = processor.audio_processing_context;

            ForEachExpressionTrackedVoice(processor.voice_pool,
                                          expr.channel >= 0 ? Optional<u4> {(u4)expr.channel} : k_nullopt,
                                          expr.key >= 0 ? Optional<u7> {(u7)expr.key} : k_nullopt,
                                          [&](Voice& v) {
                                              switch (expr.expression_id) {
                                                  case CLAP_NOTE_EXPRESSION_TUNING: {
                                                      v.expression_pitch_semitones =
                                                          Clamp((f32)expr.value, -120.0f, 120.0f);
                                                      UpdateVoicePitch(v, context);
                                                      break;
                                                  }
                                                  case CLAP_NOTE_EXPRESSION_PRESSURE: {
                                                      v.per_note_expression_active = true;
                                                      v.pressure_target_01 = Clamp01((f32)expr.value);
                                                      break;
                                                  }
                                                  case CLAP_NOTE_EXPRESSION_BRIGHTNESS: {
                                                      v.per_note_expression_active = true;
                                                      v.slide_pos_target_01 = Clamp01((f32)expr.value);
                                                      break;
                                                  }
                                                  default: break;
                                              }
                                          });
            break;
        }

        case CLAP_EVENT_MIDI: {
            auto const midi = (clap_event_midi const&)event;
            MidiMessage const message {
                .status = midi.data[0],
                .data1 = midi.data[1],
                .data2 = midi.data[2],
            };

            auto const type = message.Type();
            if (type == MidiMessageType::NoteOn || type == MidiMessageType::NoteOff ||
                type == MidiMessageType::ControlChange) {
                change_flags |= ProcessorListener::NotesChanged;
            }

            switch (message.Type()) {
                case MidiMessageType::NoteOn: {
                    auto const chan_note = message.ChannelNote();
                    if (HandleResetKeyswitch(processor, chan_note.note)) break;

                    ASSERT_HOT(message.Velocity() >= 1);
                    auto const velocity = ((f32)message.Velocity() - 1.0f) / 126.0f;
                    ASSERT_HOT(velocity >= 0 && velocity <= 1);
                    processor.audio_processing_context.midi_note_state.NoteOn(chan_note, velocity);

                    dyn::Append(changes.note_events,
                                {
                                    .velocity = velocity,
                                    .offset = event.time - block_start_frame,
                                    .note = chan_note,
                                    .type = NoteEvent::Type::On,
                                });
                    break;
                }
                case MidiMessageType::NoteOff: {
                    processor.audio_processing_context.midi_note_state.NoteOff(message.ChannelNote());
                    processor.audio_processing_context.mpe.ResetChannelExpression(message.ChannelNum());
                    if (processor.audio_processing_context.mpe.IsMemberChannel(message.ChannelNum()))
                        processor.audio_processing_context.pitchwheel_position[message.ChannelNum()] = 0.0f;
                    dyn::Append(changes.note_events,
                                {
                                    .velocity = message.Velocity() / 127.0f,
                                    .offset = event.time - block_start_frame,
                                    .note = message.ChannelNote(),
                                    .type = NoteEvent::Type::Off,
                                });
                    break;
                }
                case MidiMessageType::PitchWheel: {
                    auto const channel = message.ChannelNum();
                    auto const pitch_pos = (message.PitchBend() / 16383.0f - 0.5f) * 2.0f;
                    processor.audio_processing_context.pitchwheel_position[channel] = pitch_pos;
                    changes.pitchwheel_changed.Set(channel);
                    break;
                }
                case MidiMessageType::ControlChange: {
                    auto const cc_num = message.CCNum();
                    auto const cc_val = message.CCValue();
                    auto const channel = message.ChannelNum();
                    auto& context = processor.audio_processing_context;

                    auto const release_sustain = [&](u4 sustain_channel) {
                        auto const notes_to_end =
                            context.midi_note_state.HandleSustainPedalOff(sustain_channel);
                        notes_to_end.ForEachSetBit([&](usize note) {
                            dyn::Append(changes.note_events,
                                        NoteEvent {
                                            .velocity = 0.0f,
                                            .offset = event.time - block_start_frame,
                                            .note = {CheckedCast<u7>(note), sustain_channel},
                                            .created_by_cc64 = true,
                                            .type = NoteEvent::Type::Off,
                                        });
                        });
                    };

                    if (cc_num == 64) {
                        // In MPE mode, sustain on a zone's master channel applies to the whole zone.
                        auto const sustain_channels = ({
                            Bitset<16> b {};
                            b.Set(channel);
                            if (context.mpe.IsMasterChannel(channel)) b |= context.mpe.ZoneChannels(channel);
                            b;
                        });

                        if (cc_val < 64) {
                            sustain_channels.ForEachSetBit(
                                [&](usize sustain_channel) { release_sustain((u4)sustain_channel); });
                        } else {
                            sustain_channels.ForEachSetBit([&](usize sustain_channel) {
                                context.midi_note_state.HandleSustainPedalOn((u4)sustain_channel);
                            });
                        }
                    }

                    // MPE per-note slide: CC74 on a member channel modulates that channel's note instead
                    // of behaving like a regular CC.
                    bool consumed_as_mpe_slide = false;
                    if (cc_num == 74 && context.mpe.IsMemberChannel(channel)) {
                        consumed_as_mpe_slide = true;
                        auto const slide = (f32)cc_val / 127.0f;
                        context.mpe.slide_01[channel] = slide;
                        ForEachExpressionTrackedVoice(processor.voice_pool,
                                                      channel,
                                                      k_nullopt,
                                                      [&](Voice& v) {
                                                          v.per_note_expression_active = true;
                                                          v.slide_pos_target_01 = slide;
                                                      });
                    }

                    if (context.mpe.enabled) {
                        if (auto const rpn =
                                context.mpe.rpn_detectors[channel].DetectRpnFromCcMessage(message)) {
                            // A Zone reconfiguration can add or remove channels from MPE control; stop
                            // sounding notes and reset controls on every such channel so the sender can't
                            // leave us with hanging notes or misinterpreted pitch/expression state.
                            auto const prev_zone_channels = context.mpe.AllZoneChannels();
                            context.mpe.HandleRpn(channel, *rpn);
                            (prev_zone_channels ^ context.mpe.AllZoneChannels())
                                .ForEachSetBit([&](usize changed_channel) {
                                    auto const notes_to_end =
                                        context.midi_note_state.HandleAllNotesOff((u4)changed_channel);
                                    notes_to_end.ForEachSetBit([&](usize note) {
                                        dyn::Append(
                                            changes.note_events,
                                            NoteEvent {
                                                .velocity = 0.0f,
                                                .offset = event.time - block_start_frame,
                                                .note = {CheckedCast<u7>(note), (u4)changed_channel},
                                                .type = NoteEvent::Type::Off,
                                            });
                                    });
                                    context.mpe.ResetChannelExpression((u4)changed_channel);
                                    context.pitchwheel_position[changed_channel] = 0.0f;
                                });
                        }
                    }

                    if (!consumed_as_mpe_slide && k_midi_learn_controller_bitset.Get(cc_num)) {
                        if (auto param_index =
                                processor.midi_learn_param_index.Exchange(k_nullopt, RmwMemoryOrder::Relaxed);
                            param_index.HasValue()) {
                            processor.param_learned_ccs[(usize)param_index.Value()].Set(cc_num);
                        }

                        for (auto const [param_index, param_ccs] :
                             Enumerate<u16>(processor.param_learned_ccs)) {
                            if (!param_ccs.Get(cc_num)) continue;

                            processor.time_when_cc_moved_param[param_index].Store(TimePoint::Now(),
                                                                                  StoreMemoryOrder::Relaxed);

                            auto& info = k_param_descriptors[param_index];
                            auto const percent = (f32)cc_val / 127.0f;
                            auto const val = info.linear_range.min + (info.linear_range.Delta() * percent);

                            processor.audio_params.values[param_index] = val;
                            changes.changed_params.changed.Set(param_index);
                            changes_for_main_thread.changed.Set(param_index);

                            clap_event_param_value const value_event {
                                .header {
                                    .size = sizeof(value_event),
                                    .time = event.time,
                                    .type = CLAP_EVENT_PARAM_VALUE,
                                    .flags = CLAP_EVENT_IS_LIVE | CLAP_EVENT_DONT_RECORD,
                                },
                                .param_id = ParamIndexToId(ParamIndex {param_index}),
                                .note_id = -1,
                                .port_index = -1,
                                .channel = -1,
                                .key = -1,
                                .value = (f64)val,
                            };
                            out.try_push(&out, &value_event.header);
                        }
                    }
                    break;
                }
                case MidiMessageType::PolyAftertouch: {
                    // NOTE: not supported at the moment
                    break;
                }
                case MidiMessageType::ChannelAftertouch: {
                    // MPE per-note press: channel pressure on a member channel modulates that channel's
                    // note. Outside MPE mode channel pressure is not supported.
                    auto const channel = message.ChannelNum();
                    auto& context = processor.audio_processing_context;
                    if (context.mpe.IsMemberChannel(channel)) {
                        auto const pressure = (f32)message.ChannelPressure() / 127.0f;
                        context.mpe.pressure_01[channel] = pressure;
                        ForEachExpressionTrackedVoice(processor.voice_pool,
                                                      channel,
                                                      k_nullopt,
                                                      [&](Voice& v) {
                                                          v.per_note_expression_active = true;
                                                          v.pressure_target_01 = pressure;
                                                      });
                    }
                    break;
                }
                case MidiMessageType::SystemMessage: break;
                case MidiMessageType::ProgramChange: break;
                case MidiMessageType::None: PanicIfReached(); break;
            }

            break;
        }
    }
}

static void ConsumeParamEventsFromHost(Parameters& params,
                                       clap_input_events const& events,
                                       u32 frame_index,
                                       u32 block_size,
                                       ProcessBlockChanges& changes,
                                       ChangedParams& changes_for_main_thread) {
    ZoneScoped;
    // IMPROVE: support CLAP_EVENT_PARAM_MOD
    // IMPROVE: support polyphonic

    for (auto const event_index : Range(events.size(&events))) {
        auto e = events.get(&events, event_index);

        if (e->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (e->type != CLAP_EVENT_PARAM_VALUE) continue;

        if (e->time < frame_index || e->time >= (frame_index + block_size)) continue;

        auto value = CheckedPointerCast<clap_event_param_value const*>(e);

        if ((value->note_id != -1 && value->note_id != 0) || value->channel > 0 || value->key > 0) continue;

        if (auto const index = ParamIdToIndex(value->param_id)) {
            params.values[ToInt(*index)] =
                k_param_descriptors[ToInt(*index)].SanitiseLinearValue((f32)value->value);
            changes.changed_params.changed.Set(ToInt(*index));
            changes_for_main_thread.changed.Set(ToInt(*index));
        }
    }
}

static void ConsumeParamEventsFromMainThread(AudioProcessor& processor,
                                             clap_output_events const& out,
                                             u32 frame_index,
                                             ProcessBlockChanges& changes) {
    ZoneScoped;
    for (auto [param_index, e] : Enumerate(processor.param_change_inbox)) {
        auto const maybe_p = e.Consume();
        if (!maybe_p) continue;
        auto const& p = *maybe_p;

        ASSERT_HOT(p.active);

        if (p.gui_gesture_begin) {
            clap_event_param_gesture const event {
                .header {
                    .size = sizeof(event),
                    .time = frame_index,
                    .type = CLAP_EVENT_PARAM_GESTURE_BEGIN,
                    .flags = CLAP_EVENT_IS_LIVE,
                },
                .param_id = ParamIndexToId((ParamIndex)param_index),
            };

            out.try_push(&out, &event.header);
        }

        if (p.value_changed) {
            if (p.send_to_host) {
                clap_event_param_value const event {
                    .header {
                        .size = sizeof(event),
                        .time = frame_index,
                        .type = CLAP_EVENT_PARAM_VALUE,
                        .flags =
                            CLAP_EVENT_IS_LIVE | (p.host_should_record ? 0 : (u32)CLAP_EVENT_DONT_RECORD),
                    },
                    .param_id = ParamIndexToId((ParamIndex)param_index),
                    .note_id = -1,
                    .port_index = -1,
                    .channel = -1,
                    .key = -1,
                    .value = (f64)p.value,
                };
                out.try_push(&out, &event.header);
            }

            processor.audio_params.values[param_index] = p.value;
            changes.changed_params.changed.Set(param_index);
        }

        if (p.gui_gesture_end) {
            clap_event_param_gesture const event {
                .header {
                    .size = sizeof(event),
                    .time = frame_index,
                    .type = CLAP_EVENT_PARAM_GESTURE_END,
                    .flags = CLAP_EVENT_IS_LIVE,
                },
                .param_id = ParamIndexToId((ParamIndex)param_index),
            };

            out.try_push(&out, &event.header);
        }
    }
}

static void SendParamChangesToMainThread(AudioProcessor& processor, ChangedParams& changes_for_main_thread) {
    // Update the main-thread representation of the parameters if they have changed.
    if (!changes_for_main_thread.changed.AnyValuesSet()) return;

    DynamicArrayBounded<AudioProcessor::ChangedParam, k_num_parameters> events {};
    changes_for_main_thread.changed.ForEachSetBit([&](usize param_index) {
        dyn::Append(events,
                    {
                        .value = processor.audio_params.LinearValueIgnoringLegacy((ParamIndex)param_index),
                        .index = (ParamIndex)param_index,
                    });
    });
    processor.param_changes_for_main_thread.Push(events);

    processor.host.request_callback(&processor.host);
}

static void
FlushParameterEvents(AudioProcessor& processor, clap_input_events const& in, clap_output_events const& out) {
    auto& params = processor.activated ? processor.audio_params : processor.main_params;
    ProcessBlockChanges changes {
        .changed_params = {params, Bitset<k_num_parameters>()},
    };
    ChangedParams changes_for_main_thread {params};
    ConsumeParamEventsFromMainThread(processor, out, 0, changes);
    ConsumeParamEventsFromHost(params,
                               in,
                               0,
                               LargestRepresentableValue<u32>(),
                               changes,
                               changes_for_main_thread);

    if (processor.activated) {
        ProcessorHandleChanges(processor, changes);
        SendParamChangesToMainThread(processor, changes_for_main_thread);
    } else {
        // It not activated, we have just updated the main-thread parameters. The audio thread parameters will
        // be updated in the next time we are activated.
    }
}

// Audio-thread
static void AudioThreadReset(AudioProcessor& processor) {
    ClearInbox(processor);
    processor.voice_pool.EndAllVoicesInstantly();
    processor.audio_processing_context.pitchwheel_position = {};
    processor.audio_processing_context.midi_note_state = {};
    for (auto const channel : Range(16u))
        processor.audio_processing_context.mpe.ResetChannelExpression((u4)channel);
    ProcessBlockChanges changes {
        .changed_params = {processor.audio_params, Bitset<k_num_parameters>()},
    };
    changes.pitchwheel_changed.SetAll();
    ResetProcessor(processor, changes);
}

static clap_process_status ProcessSubBlock(AudioProcessor& processor,
                                           clap_process const& process,
                                           u32 frame_index,
                                           u32 sub_block_size,
                                           ProcessorListener::ChangeFlags& change_flags,
                                           ChangedParams& changes_for_main_thread) {
    clap_process_status result = CLAP_PROCESS_CONTINUE;

    DEFER {
        if (processor.previous_process_status != result) change_flags |= ProcessorListener::StatusChanged;
        processor.previous_process_status = result;
    };

    ProcessBlockChanges changes {
        .changed_params = {processor.audio_params, Bitset<k_num_parameters>()},
    };

    // Check for tempo changes.
    {
        // process.transport is only for frame 0.
        if (frame_index == 0 && process.transport) {
            if (process.transport->flags & CLAP_TRANSPORT_HAS_TEMPO &&
                process.transport->tempo != processor.audio_processing_context.tempo) {
                processor.audio_processing_context.tempo = process.transport->tempo;
                changes.tempo_changed = true;
            }
        }
        for (auto const event_index : Range(process.in_events->size(process.in_events))) {
            auto e = process.in_events->get(process.in_events, event_index);
            if (!e) continue;

            if (e->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
            if (e->type != CLAP_EVENT_TRANSPORT) continue;
            if (e->time < frame_index || e->time >= (frame_index + sub_block_size)) continue;

            auto const transport = CheckedPointerCast<clap_event_transport const*>(e);
            if (transport->tempo != processor.audio_processing_context.tempo) {
                processor.audio_processing_context.tempo = transport->tempo;
                changes.tempo_changed = true;
            }
        }
        if (!__builtin_isfinite(processor.audio_processing_context.tempo) ||
            processor.audio_processing_context.tempo < 0.01) {
            processor.audio_processing_context.tempo = 120;
            changes.tempo_changed = true;
        }
    }

    // Check for transport start to reset random state.
    if (frame_index == 0 && process.transport) {
        bool const is_playing = process.transport->flags & CLAP_TRANSPORT_IS_PLAYING;
        if (is_playing && !processor.prev_transport_playing) {
            auto const settings = processor.performance_settings.Load(LoadMemoryOrder::Acquire);
            if (settings.reset_on_transport) ResetRandomState(processor, settings.seed);
        }
        processor.prev_transport_playing = is_playing;
    }

    constexpr f32 k_fade_out_ms = 30;
    constexpr f32 k_fade_in_ms = 10;

    Bitset<k_num_layers> layers_changed {};
    bool mark_convolution_for_fade_out = false;

    ConsumeParamEventsFromMainThread(processor, *process.out_events, frame_index, changes);
    ConsumeParamEventsFromHost(processor.audio_params,
                               *process.in_events,
                               frame_index,
                               sub_block_size,
                               changes,
                               changes_for_main_thread);

    Optional<AudioProcessor::FadeType> new_fade_type {};

    if (auto const flags = processor.inbox_flags.Exchange(0, RmwMemoryOrder::Acquire)) {
        if (flags & audio_thread_inbox::FxOrderChanged) {
            if (!new_fade_type) new_fade_type = AudioProcessor::FadeType::OutAndIn;
        }

        if (flags & audio_thread_inbox::ReloadAllAudioState) {
            changes.changed_params.changed.SetAll();
            new_fade_type = AudioProcessor::FadeType::OutAndRestartVoices;
            layers_changed.SetAll();
        }

        if (flags & audio_thread_inbox::ConvolutionIRChanged) mark_convolution_for_fade_out = true;

        if (flags & audio_thread_inbox::ResetAudioProcessing) AudioThreadReset(processor);

        for (auto const layer_index : Range(k_num_layers))
            if (flags & ((u32)audio_thread_inbox::LayerInstrumentChanged << layer_index))
                layers_changed.Set(layer_index);
    }

    for (auto [macro_index, inbox_dests] : Enumerate(processor.macro_dest_inbox)) {
        for (auto [dest_index, inbox_dest] : Enumerate(inbox_dests)) {
            auto const maybe_item = inbox_dest.Consume();
            if (!maybe_item) continue;
            auto const& item = *maybe_item;

            auto& d = processor.audio_macro_destinations[macro_index].items[dest_index];
            if (item.value_changed) {
                d.value = item.value;
                if (d.param_index) changes.changed_params.changed.Set(ToInt(*d.param_index));
            }
            if (item.param_index_changed) {
                if (d.param_index) changes.changed_params.changed.Set(ToInt(*d.param_index));
                changes.changed_params.changed.Set(ToInt(item.param_index));
                d.param_index = item.param_index;
            }
            if (item.clear) {
                if (d.param_index) changes.changed_params.changed.Set(ToInt(*d.param_index));
                d = {};
            }
        }
    }

    if (changes.changed_params.changed.Get(ToInt(ParamIndex::ConvolutionReverbOn)))
        change_flags |= ProcessorListener::IrChanged;

    if (new_fade_type) {
        processor.whole_engine_volume_fade_type = *new_fade_type;
        processor.whole_engine_volume_fade.SetAsFadeOutIfNotAlready(
            processor.audio_processing_context.sample_rate,
            k_fade_out_ms);
    }

    if (processor.peak_meter.Silent() && !processor.fx_need_another_frame_of_processing) {
        ResetProcessor(processor, changes);
        changes.changed_params.changed.ClearAll();
    }

    switch (processor.whole_engine_volume_fade.GetCurrentState()) {
        case VolumeFade::State::Silent: {
            ResetProcessor(processor, changes);

            // We have just done a hard reset on everything, any other state changes are no longer valid.
            changes.changed_params.changed.ClearAll();

            if (processor.whole_engine_volume_fade_type == AudioProcessor::FadeType::OutAndRestartVoices) {
                processor.voice_pool.EndAllVoicesInstantly();
                for (auto& l : processor.layer_processors) {
                    ResetArpAudioPlayback(l.arp_state);
                    l.arp_state.audio.playhead.frames_per_step =
                        ArpFramesPerStep(l.arp_state.audio.rate, processor.audio_processing_context);
                }
                processor.restart_voices_for_layer_bitset.SetAll(); // restart all voices
            } else {
                processor.whole_engine_volume_fade.SetAsFadeIn(processor.audio_processing_context.sample_rate,
                                                               k_fade_in_ms);
            }

            ASSERT_EQ(processor.whole_engine_volume_fade.GetCurrentState(), VolumeFade::State::FullVolume);
            break;
        }
        case VolumeFade::State::FadeOut: {
            // If we are going to be fading out anyways, let's apply param changes at that time too to
            // avoid any pops.
            processor.pending_param_changes |= changes.changed_params.changed;
            changes.changed_params.changed.ClearAll();
            break;
        }
        default: break;
    }

    {
        for (auto const i : Range(process.in_events->size(process.in_events))) {
            auto e = process.in_events->get(process.in_events, i);
            if (!e) continue;
            if (e->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
            if (e->time < frame_index || e->time >= (frame_index + sub_block_size)) continue;
            ProcessClapNoteOrMidi(processor,
                                  *e,
                                  *process.out_events,
                                  frame_index,
                                  change_flags,
                                  changes,
                                  changes_for_main_thread);
        }

        {
            auto const gui_note = processor.gui_note_click_state.Load(LoadMemoryOrder::Acquire);

            if (gui_note.is_held && !processor.gui_note_currently_held) {
                clap_event_note const note {
                    .header {
                        .size = sizeof(clap_event_note),
                        .time = frame_index,
                        .type = CLAP_EVENT_NOTE_ON,
                    },
                    .note_id = -1,
                    .key = gui_note.key,
                    .velocity = (f64)gui_note.velocity,
                };
                ProcessClapNoteOrMidi(processor,
                                      note.header,
                                      *process.out_events,
                                      frame_index,
                                      change_flags,
                                      changes,
                                      changes_for_main_thread);
                processor.gui_note_currently_held = gui_note.key;
            } else if (!gui_note.is_held && processor.gui_note_currently_held) {
                clap_event_note const note {
                    .header {
                        .size = sizeof(clap_event_note),
                        .time = frame_index,
                        .type = CLAP_EVENT_NOTE_OFF,
                    },
                    .note_id = -1,
                    .key = *processor.gui_note_currently_held,
                    .velocity = 0.0,
                };
                ProcessClapNoteOrMidi(processor,
                                      note.header,
                                      *process.out_events,
                                      frame_index,
                                      change_flags,
                                      changes,
                                      changes_for_main_thread);
                processor.gui_note_currently_held = k_nullopt;
            }
        }
    }

    // Create new voices for layer if requested. We want to do this after parameters have been updated
    // so that the voices start with the most recent parameter values.
    if (auto const restart_layer_bitset = Exchange(processor.restart_voices_for_layer_bitset, {});
        restart_layer_bitset.AnyValuesSet()) {
        auto const all_layers_resetting = restart_layer_bitset.AllValuesSet();

        // Clear latch for layers that are restarting.
        for (auto [layer_index, layer] : Enumerate(processor.layer_processors))
            if (restart_layer_bitset.Get(layer_index)) layer.monophonic_latch = {};

        for (auto const chan : Range<u8>(16)) {
            auto const held_notes =
                processor.audio_processing_context.midi_note_state.NotesHeldIncludingSustained((u4)chan);
            if (!held_notes.AnyValuesSet()) continue;

            auto const add_note_events = [&](s8 exclusively_for_layer) {
                for (auto const note_num : Range<u8>(128)) {
                    if (held_notes.Get(note_num)) {
                        dyn::Append(changes.note_events,
                                    NoteEvent {
                                        .velocity = processor.audio_processing_context.midi_note_state
                                                        .velocities[chan][note_num],
                                        .offset = 0,
                                        .note = {.note = (u7)note_num, .channel = (u4)chan},
                                        .type = NoteEvent::Type::On,
                                        .exclusively_for_layer = exclusively_for_layer,
                                    });
                    }
                }
            };

            if (all_layers_resetting) {
                // If all layers are restarting we don't need to set 'exclusively_for_layer' and so we can
                // avoid putting the event in multiple times (one for each layer).
                add_note_events(-1);
            } else {
                // Otherwise, we need to add the notes for every layer that is restarting.
                for (auto [layer_index, layer] : Enumerate(processor.layer_processors))
                    if (restart_layer_bitset.Get(layer_index)) add_note_events(CheckedCast<s8>(layer.index));
            }
        }
    }

    ProcessorHandleChanges(processor, changes);

    for (auto& l : processor.layer_processors)
        ProcessLayerPreVoices(l, processor.audio_processing_context, processor.voice_pool, sub_block_size);

    // Voices and layers
    // ======================================================================================================
    // IMPROVE: support sending the host CLAP_EVENT_NOTE_END events when voices end
    ProcessVoices(processor.voice_pool, sub_block_size, processor.audio_processing_context);

    Array<f32x2, k_block_size_max> output_buffer;
    auto const output = Span<f32x2>(output_buffer.data, sub_block_size);
    Fill(output, 0.0f);

    // Safe to share between layers: each layer's result is consumed before the next layer runs.
    Array<f32x2, k_block_size_max> layer_scratch_buffer;
    auto const layer_scratch = Span<f32x2>(layer_scratch_buffer.data, sub_block_size);

    bool audio_was_generated_by_layers = false;
    for (auto const layer_index : Range(k_num_layers)) {
        auto const process_result = ProcessLayer(processor.layer_processors[layer_index],
                                                 processor.audio_processing_context,
                                                 processor.voice_pool,
                                                 sub_block_size,
                                                 layers_changed.Get(layer_index),
                                                 layer_scratch);

        if (process_result.output) {
            audio_was_generated_by_layers = true;
            auto const& layer_audio = *process_result.output;
            for (auto const frame : Range(sub_block_size))
                output[frame] += layer_audio[frame];
        }

        if (process_result.instrument_swapped) {
            change_flags |= ProcessorListener::InstrumentChanged;

            // Start new voices. We don't want to do that here because we want all parameter changes
            // to be applied beforehand.
            processor.restart_voices_for_layer_bitset.Set(layer_index);
        }
    }

    if constexpr (RUNTIME_SAFETY_CHECKS_ON && !PRODUCTION_BUILD) {
        for (auto const frame : Range(sub_block_size)) {
            auto const& val = output[frame];
            ASSERT(All(val >= -k_erroneous_sample_value && val <= k_erroneous_sample_value));
        }
    }

    if (audio_was_generated_by_layers || processor.fx_need_another_frame_of_processing) {
        // Effects
        // ==================================================================================================

        bool fx_need_another_frame_of_processing = false;
        for (auto fx : processor.actual_fx_order) {
            void* extra_context {};
            ConvolutionReverb::ConvoExtraContext convo_extra_context {
                .start_fade_out = mark_convolution_for_fade_out,
            };
            if (fx->type == EffectType::ConvolutionReverb) extra_context = &convo_extra_context;

            auto const r = fx->ProcessBlock(output, processor.audio_processing_context, extra_context);
            if (r == EffectProcessResult::ProcessingTail) fx_need_another_frame_of_processing = true;

            if (fx->type == EffectType::ConvolutionReverb) {
                if (convo_extra_context.changed_ir) change_flags |= ProcessorListener::IrChanged;
            }
        }
        processor.fx_need_another_frame_of_processing = fx_need_another_frame_of_processing;

        // Master
        // ==================================================================================================

        for (auto& frame : output) {
            frame *= processor.master_vol_smoother.LowPass(
                processor.master_vol,
                processor.audio_processing_context.one_pole_smoothing_cutoff_10ms);

            // frame = Clamp(frame, {-1, -1}, {1, 1}); // hard limit
            frame *= processor.whole_engine_volume_fade.GetFade();
        }
        processor.peak_meter.AddBuffer(output);
    } else {
        processor.peak_meter.Zero();
        for (auto& l : processor.layer_processors)
            l.peak_meter.Zero();
        result = CLAP_PROCESS_SLEEP;
    }

    //
    // ======================================================================================================
    if (process.audio_outputs->channel_count == 2 && process.audio_outputs->data32) {
        // On Windows Ableton Live 10, dereferencing process.audio_outputs->data32 (such as
        // process.audio_outputs->data32[0]) is an unaligned memory access and crashes when the undefined
        // behaviour sanitizer is enabled. This is the workaround. The extra cast to (void*) is needed for
        // some reason, despite it simply being an arg to memcpy.
        f32* dest[2];
        for (auto i : Range(2))
            __builtin_memcpy_inline(&dest[i], (void*)&process.audio_outputs->data32[i], sizeof(f32*));

        if (dest[0] && dest[1]) {
            static_assert(sizeof(f32x2) == (sizeof(f32) * 2));
            auto interleaved_outputs = (f32 const*)output.data;
            CopyInterleavedToSeparateChannels(dest[0] + frame_index,
                                              dest[1] + frame_index,
                                              interleaved_outputs,
                                              sub_block_size);
        }
    }

    return result;
}

clap_process_status Process(AudioProcessor& processor, clap_process const& process) {
    ZoneScoped;
    ASSERT_EQ(process.audio_outputs_count, 1u);
    ASSERT_HOT(processor.activated);

    if (process.frames_count == 0) return CLAP_PROCESS_CONTINUE;

    {
        auto const performance_settings = processor.performance_settings.Load(LoadMemoryOrder::Acquire);
        auto& mpe = processor.audio_processing_context.mpe;
        if (mpe.enabled != performance_settings.mpe_enabled) {
            // Live voices must not keep frozen per-note bend/press/slide across a mode change.
            for (auto& v : processor.voice_pool.EnumerateActiveVoices()) {
                if (!v.per_note_expression_active) continue;
                v.per_note_expression_active = false;
                v.mpe_bend_semitones = 0;
                UpdateVoicePitch(v, processor.audio_processing_context);
                UpdateXfade(v,
                            ExpressionAdjustedTimbre01(processor.shared_layer_params.timbre_value_01, v),
                            false);
            }
            for (auto const channel : Range(16u))
                mpe.ResetChannelExpression((u4)channel);
            processor.audio_processing_context.pitchwheel_position = {};
        }
        mpe.enabled = performance_settings.mpe_enabled;
        mpe.press_slide_smoothing_ms = (f32)performance_settings.mpe_smoothing_ms;
    }

    clap_process_status result = CLAP_PROCESS_CONTINUE;

    ProcessorListener::ChangeFlags change_flags = ProcessorListener::None;
    ChangedParams changes_for_main_thread {processor.audio_params};

    for (u32 frame_index = 0; frame_index < process.frames_count; frame_index += k_block_size_max) {
        auto const sub_block_size = Min(k_block_size_max, process.frames_count - frame_index);
        result = ProcessSubBlock(processor,
                                 process,
                                 frame_index,
                                 sub_block_size,
                                 change_flags,
                                 changes_for_main_thread);
        if (result == CLAP_PROCESS_ERROR) break;
    }

    processor.notes_currently_held.AssignBlockwise(
        processor.audio_processing_context.midi_note_state.NotesCurrentlyHeldAllChannels());

    if (!processor.peak_meter.Silent()) change_flags |= ProcessorListener::PeakMeterChanged;
    for (auto& layer : processor.layer_processors)
        if (LayerHasAudioActivity(layer)) change_flags |= ProcessorListener::PeakMeterChanged;

    if (change_flags) processor.listener.OnProcessorChange(change_flags);
    SendParamChangesToMainThread(processor, changes_for_main_thread);

    return result;
}

void ResetAudioProcessing(AudioProcessor& processor) {
    ASSERT(g_is_logical_main_thread);
    processor.inbox_flags.FetchOr(audio_thread_inbox::ResetAudioProcessing, RmwMemoryOrder::Release);
    processor.host.request_process(&processor.host);
}

static void OnMainThread(AudioProcessor& processor) {
    ZoneScoped;
    processor.convo.DeletedUnusedConvolvers();

    // Clear any instruments that aren't used anymore. The audio thread will request this callback after it
    // swaps any instruments.
    if (processor.lifetime_extended_insts.size) {
        bool all_layers_have_completed_swap = true;
        for (auto& l : processor.layer_processors) {
            if (!l.desired_inst.IsConsumed()) {
                all_layers_have_completed_swap = false;
                break;
            }
        }
        if (all_layers_have_completed_swap) {
            for (auto& i : processor.lifetime_extended_insts)
                i.Release();
            dyn::Clear(processor.lifetime_extended_insts);
        }
    }

    // Consume any parameter changes that were made from the audio thread.
    if (auto const param_changes = processor.param_changes_for_main_thread.PopAll(); param_changes.size) {
        for (auto const p : param_changes)
            processor.main_params.values[ToInt(p.index)] = p.value;
        processor.listener.OnProcessorChange(ProcessorListener::ParametersChanged);
    }

    OnMainThread(processor.voice_pool);
}

static void OnThreadPoolExec(AudioProcessor& processor, u32 index) {
    OnThreadPoolExec(processor.voice_pool, index);
}

AudioProcessor::AudioProcessor(clap_host const& host,
                               ProcessorListener& listener,
                               prefs::PreferencesTable const& prefs)
    : host(host)
    , audio_processing_context {.host = host}
    , listener(listener)
    , effects_ordered_by_type(OrderEffectsToEnum(EffectsArray {
          &distortion,
          &bit_crush,
          &compressor,
          &filter_effect,
          &stereo_widen,
          &chorus,
          &reverb,
          &delay,
          &phaser,
          &eq,
          &convo,
      })) {

    voice_pool.master_random_seed = &master_random_seed;
    voice_pool.master_timbre_01 = &shared_layer_params.timbre_value_01;
    ResetRandomState(*this, 0); // Initialise with default seed for deterministic starting state.

    for (auto const i : Range(k_num_parameters))
        main_params.values[i] = k_param_descriptors[i].default_linear_value;

    {
        auto const profile = perf_profile::DefaultOrFallback(prefs);
        for (auto const i : Range(k_num_parameters))
            param_learned_ccs[i].AssignBlockwise(profile.controls.param_learned_ccs[i]);
        performance_settings.Store(profile.controls.settings, StoreMemoryOrder::Relaxed);
    }
}

AudioProcessor::~AudioProcessor() {
    for (auto& i : lifetime_extended_insts)
        i.Release();
}

PluginCallbacks<AudioProcessor> const g_processor_callbacks {
    .activate = Activate,
    .deactivate = Deactivate,
    .reset = AudioThreadReset,
    .process = Process,
    .flush_parameter_events = FlushParameterEvents,
    .on_main_thread = OnMainThread,
    .on_thread_pool_exec = OnThreadPoolExec,
};
