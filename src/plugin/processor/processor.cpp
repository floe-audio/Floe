// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "processor.hpp"

#include "os/threading.hpp"

#include "common_infrastructure/cc_mapping.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/error_reporting.hpp"
#include "common_infrastructure/preferences.hpp"

#include "clap/ext/params.h"
#include "param.hpp"
#include "plugin/plugin.hpp"
#include "voices.hpp"

static auto HostsParamsExtension(AudioProcessor& processor) {
    return (clap_host_params const*)processor.host.get_extension(&processor.host, CLAP_EXT_PARAMS);
}

consteval auto PersistentDefaultCcParamMappingsString() {
    constexpr String k_start = "CC ";
    constexpr String k_middle = " -> ";
    constexpr String k_end = "\n";

    constexpr usize k_size = []() {
        usize size = 0;
        for (auto const m : k_default_cc_to_param_mapping) {
            size += k_start.size;

            if (m.cc < 10)
                size += 1;
            else if (m.cc < 100)
                size += 2;
            else
                size += 3;

            size += k_middle.size;

            auto const p = k_param_descriptors[ToInt(m.param)];
            for (auto const mod : p.module_parts) {
                if (mod == ParameterModule::None) break;
                size += k_parameter_module_strings[ToInt(mod)].size;
                size += 1; // ' '
            }
            size += p.name.size;

            size += k_end.size;
        }
        return size;
    }();

    Array<char, k_size> result {};
    usize i = 0;
    for (auto const m : k_default_cc_to_param_mapping) {
        WriteAndIncrement(i, result, k_start);
        i += fmt::IntToString(m.cc, result.data + i);
        WriteAndIncrement(i, result, k_middle);

        auto const p = k_param_descriptors[ToInt(m.param)];
        for (auto const mod : p.module_parts) {
            if (mod == ParameterModule::None) break;
            WriteAndIncrement(i, result, k_parameter_module_strings[ToInt(mod)]);
            WriteAndIncrement(i, result, ' ');
        }
        WriteAndIncrement(i, result, p.name);

        WriteAndIncrement(i, result, k_end);
    }

    return result;
}

constexpr auto k_str = PersistentDefaultCcParamMappingsString();
static_assert(k_str[0] == 'C');

prefs::Descriptor SettingDescriptor(ProcessorSetting s) {
    switch (s) {
        case ProcessorSetting::DefaultCcParamMappings: {
            static constexpr auto k_description =
                ConcatArrays("When Floe starts, map these MIDI CC to parameters:\n"_ca,
                             PersistentDefaultCcParamMappingsString());
            return {
                .key = "default-cc-param-mappings"_s,
                .value_requirements = prefs::ValueType::Bool,
                .default_value = true,
                .gui_label = "Start with default CC to param mappings"_s,
                .long_description = k_description,

            };
        }
    }
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

void AddPersistentCcToParamMapping(prefs::Preferences& prefs, u8 cc_num, u32 param_id) {
    ASSERT(g_is_logical_main_thread);
    ASSERT(cc_num > 0 && cc_num <= 127);
    ASSERT(ParamIdToIndex(param_id));
    prefs::AddValue(prefs,
                    prefs::SectionedKey {prefs::key::section::k_cc_to_param_id_map_section, (s64)cc_num},
                    (s64)param_id);
}

void RemovePersistentCcToParamMapping(prefs::Preferences& prefs, u8 cc_num, u32 param_id) {
    ASSERT(g_is_logical_main_thread);

    prefs::RemoveValue(prefs,
                       prefs::SectionedKey {prefs::key::section::k_cc_to_param_id_map_section, (s64)cc_num},
                       (s64)param_id);
}

Bitset<128> PersistentCcsForParam(prefs::PreferencesTable const& prefs, u32 param_id) {
    ASSERT(g_is_logical_main_thread);

    Bitset<128> result {};

    for (auto const [key_union, value_list, _] : prefs) {
        auto const sectioned_key = key_union.TryGet<prefs::SectionedKey>();
        if (!sectioned_key) continue;
        auto const [section, key] = *sectioned_key;
        if (section != prefs::key::section::k_cc_to_param_id_map_section) continue;
        if (key.tag != prefs::KeyValueType::Int) continue;

        auto const cc_num = key.Get<s64>();
        if (cc_num < 1 || cc_num > 127) continue;

        for (auto value = value_list; value; value = value->next) {
            if (*value == (s64)param_id) {
                result.Set((usize)cc_num);
                break;
            }
        }
    }

    return result;
}

void AppendMacroDestination(AudioProcessor& processor, struct AppendMacroDestination config) {
    ASSERT(g_is_logical_main_thread);

    dyn::Append(processor.main_macro_destinations[config.macro_index],
                MacroDestination {
                    .param_index = config.param,
                    .value = config.value,
                });

    processor.events_for_audio_thread.Push(config);
    processor.host.request_process(&processor.host);
}

void RemoveMacroDestination(AudioProcessor& processor, struct RemoveMacroDestination config) {
    ASSERT(g_is_logical_main_thread);

    dyn::Remove(processor.main_macro_destinations[config.macro_index], config.destination_index);

    processor.events_for_audio_thread.Push(config);
    processor.host.request_process(&processor.host);
}

void MacroDestinationValueChanged(AudioProcessor& processor, struct MacroDestinationValueChanged config) {
    ASSERT(g_is_logical_main_thread);

    processor.events_for_audio_thread.Push(config);
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

void SetAllParametersToDefaultValues(AudioProcessor& processor) {
    ASSERT(g_is_logical_main_thread);

    StateSnapshot state {};

    for (auto const fx_index : Range<u8>(state.fx_order.size))
        state.fx_order[fx_index] = (EffectType)fx_index;

    for (auto const param_index : Range(k_num_parameters))
        state.param_values[param_index] = k_param_descriptors[param_index].default_linear_value;

    for (auto& velo_curve : state.velocity_curve_points)
        velo_curve = k_default_velocity_curve_points;

    ApplyNewState(processor, state, StateSource::PresetFile);
}

static void ProcessorRandomiseAllParamsInternal(AudioProcessor& processor, bool only_effects) {
    ASSERT(g_is_logical_main_thread);

    RandomIntGenerator<int> int_gen;
    RandomFloatGenerator<f32> float_gen;
    auto seed = (u64)NanosecondsSinceEpoch();
    RandomNormalDistribution normal_dist {0.5, 0.20};
    RandomNormalDistribution normal_dist_strong {0.5, 0.10};

    StateSnapshot state {};
    state.param_values = processor.main_params.values;
    state.macro_destinations = processor.main_macro_destinations;
    for (auto const layer_index : Range(k_num_layers)) {
        state.velocity_curve_points[layer_index] =
            processor.layer_processors[layer_index].velocity_curve_map.points;
    }

    auto const set_param = [&](DescribedParamValue const& p, f32 v) {
        if (IsAnyOf(p.info.value_type, Array {ParamValueType::Int, ParamValueType::Bool})) v = Round(v);
        ASSERT(v >= p.info.linear_range.min && v <= p.info.linear_range.max);
        state.param_values[ToInt(p.info.index)] = v;
    };
    auto const set_any_random = [&](DescribedParamValue const& p) {
        set_param(p, float_gen.GetRandomInRange(seed, p.info.linear_range.min, p.info.linear_range.max));
    };

    enum class BiasType {
        Normal,
        Strong,
    };

    auto const randomise_near_to_linear_value =
        [&](DescribedParamValue const& p, BiasType bias, f32 linear_value) {
            (void)linear_value;
            f32 rand_v = 0;
            switch (bias) {
                case BiasType::Normal: {
                    rand_v = (f32)normal_dist.Next(seed);
                    break;
                }
                case BiasType::Strong: {
                    rand_v = (f32)normal_dist_strong.Next(seed);
                    break;
                }
                default: PanicIfReached();
            }

            auto const v = Clamp(rand_v, 0.0f, 1.0f);
            set_param(p, MapFrom01(v, p.info.linear_range.min, p.info.linear_range.max));
        };

    auto const randomise_near_to_default = [&](DescribedParamValue const& p,
                                               BiasType bias = BiasType::Normal) {
        randomise_near_to_linear_value(p, bias, p.DefaultLinearValue());
    };

    auto const randomise_button_preffering_default = [&](DescribedParamValue const& p,
                                                         BiasType bias = BiasType::Normal) {
        f32 new_param_val = p.DefaultLinearValue();
        auto const v = int_gen.GetRandomInRange(seed, 1, 100, false);
        if ((bias == BiasType::Normal && v <= 10) || (bias == BiasType::Strong && v <= 5))
            new_param_val = Abs(new_param_val - 1.0f);
        set_param(p, new_param_val);
    };

    auto const randomise_detune = [&](DescribedParamValue const& p) {
        bool const should_detune = int_gen.GetRandomInRange(seed, 1, 10) <= 2;
        if (!should_detune) {
            set_param(p, 0);
            return;
        }
        randomise_near_to_default(p);
    };

    auto const randomise_pitch = [&](DescribedParamValue const& p) {
        switch (int_gen.GetRandomInRange(seed, 1, 10)) {
            case 1:
            case 2:
            case 3:
            case 4:
            case 5: {
                set_param(p, 0);
                break;
            }
            case 6:
            case 7:
            case 8:
            case 9: {
                f32 const potential_vals[] = {-24, -12, -5, 7, 12, 19, 24, 12, -12};
                set_param(
                    p,
                    potential_vals[int_gen.GetRandomInRange(seed, 0, (int)ArraySize(potential_vals) - 1)]);
                break;
            }
            case 10: {
                randomise_near_to_default(p);
                break;
            }
            default: PanicIfReached();
        }
    };

    auto const randomise_pitch_bend_range = [&](DescribedParamValue const& p) {
        switch (int_gen.GetRandomInRange(seed, 1, 10)) {
            case 1:
            case 2:
            case 3:
            case 4:
            case 5: {
                set_param(p, 0);
                break;
            }
            case 6:
            case 7:
            case 8:
            case 9: {
                f32 const potential_vals[] = {1, 2, 6, 12, 4, 24, 12, 12, 48, 36};
                set_param(
                    p,
                    potential_vals[int_gen.GetRandomInRange(seed, 0, (int)ArraySize(potential_vals) - 1)]);
                break;
            }
            case 10: {
                randomise_near_to_default(p);
                break;
            }
            default: PanicIfReached();
        }
    };

    auto const randomise_pan = [&](DescribedParamValue const& p) {
        if (int_gen.GetRandomInRange(seed, 1, 10) < 4)
            set_param(p, 0);
        else
            randomise_near_to_default(p, BiasType::Strong);
    };

    auto const randomise_loop_start_and_end = [&](DescribedParamValue const& start,
                                                  DescribedParamValue const& end) {
        auto const mid = float_gen.GetRandomInRange(seed, 0, 1);
        auto const min_half_size = 0.1f;
        auto const max_half_size = Min(mid, 1 - mid);
        auto const half_size = float_gen.GetRandomInRange(seed, min_half_size, max_half_size);
        set_param(start, Clamp(mid - half_size, 0.0f, 1.0f));
        set_param(end, Clamp(mid + half_size, 0.0f, 1.0f));
    };

    //
    //
    //

    // Set all params to a random value
    for (auto const param_index : Range(k_num_parameters)) {
        auto p = processor.main_params.DescribedValue((ParamIndex)param_index);
        if ((!only_effects || (only_effects && p.info.IsEffectParam())) && !p.info.flags.hidden)
            set_any_random(p);
    }

    // Specialise the randomness of specific params for better results
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::BitCrushWet));
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::BitCrushDry));
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::CompressorThreshold),
                              BiasType::Strong);
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::CompressorRatio));
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::CompressorGain),
                              BiasType::Strong);
    set_param(processor.main_params.DescribedValue(ParamIndex::CompressorAutoGain), 1.0f);
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::FilterCutoff));
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::FilterResonance));
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::ChorusWet));
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::ChorusDry), BiasType::Strong);
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::ReverbMix));
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::PhaserMix));
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::DelayMix));
    randomise_near_to_linear_value(processor.main_params.DescribedValue(ParamIndex::ConvolutionReverbWet),
                                   BiasType::Strong,
                                   0.5f);
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::ConvolutionReverbDry),
                              BiasType::Strong);
    randomise_near_to_default(processor.main_params.DescribedValue(ParamIndex::ConvolutionReverbHighpass));

    {
        auto fx = processor.effects_ordered_by_type;
        Shuffle(fx, seed);
        for (auto i : Range(fx.size))
            state.fx_order[i] = fx[i]->type;
    }

    if (!only_effects) {
        set_param(processor.main_params.DescribedValue(ParamIndex::MasterVolume),
                  processor.main_params.DescribedValue(ParamIndex::MasterVolume).DefaultLinearValue());
        for (auto& l : processor.layer_processors) {
            randomise_near_to_linear_value(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::Volume),
                BiasType::Strong,
                0.6f);
            randomise_button_preffering_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::Mute));
            randomise_button_preffering_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::Solo));
            randomise_pan(processor.main_params.DescribedValue(l.index, LayerParamIndex::Pan));
            randomise_detune(processor.main_params.DescribedValue(l.index, LayerParamIndex::TuneCents));
            randomise_pitch(processor.main_params.DescribedValue(l.index, LayerParamIndex::TuneSemitone));
            randomise_pitch_bend_range(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::PitchBendRange));
            set_param(processor.main_params.DescribedValue(l.index, LayerParamIndex::VolEnvOn), 1.0f);

            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::VolumeAttack));
            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::VolumeDecay));
            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::VolumeSustain));
            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::VolumeRelease));

            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::FilterEnvAmount));
            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::FilterAttack));
            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::FilterDecay));
            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::FilterSustain));
            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::FilterRelease));

            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::FilterCutoff));
            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::FilterResonance));

            randomise_loop_start_and_end(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::LoopStart),
                processor.main_params.DescribedValue(l.index, LayerParamIndex::LoopEnd));

            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::EqGain1));
            randomise_near_to_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::EqGain2));

            if (int_gen.GetRandomInRange(seed, 1, 10) < 4) {
                set_param(processor.main_params.DescribedValue(l.index, LayerParamIndex::SampleOffset), 0);
            } else {
                randomise_near_to_default(
                    processor.main_params.DescribedValue(l.index, LayerParamIndex::SampleOffset),
                    BiasType::Strong);
            }
            randomise_button_preffering_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::Reverse));

            randomise_button_preffering_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::Keytrack),
                BiasType::Strong);
            randomise_button_preffering_default(
                processor.main_params.DescribedValue(l.index, LayerParamIndex::Monophonic),
                BiasType::Strong);
            set_param(processor.main_params.DescribedValue(l.index, LayerParamIndex::MidiTranspose), 0.0f);
            set_param(processor.main_params.DescribedValue(l.index, LayerParamIndex::VelocityMapping), 0.0f);
            set_param(processor.main_params.DescribedValue(l.index, LayerParamIndex::Mute), 0.0f);
            set_param(processor.main_params.DescribedValue(l.index, LayerParamIndex::Solo), 0.0f);

            set_param(processor.main_params.DescribedValue(l.index, LayerParamIndex::KeyRangeLow), 0);
            set_param(processor.main_params.DescribedValue(l.index, LayerParamIndex::KeyRangeHigh), 127);
            set_param(processor.main_params.DescribedValue(l.index, LayerParamIndex::KeyRangeLowFade), 0);
            set_param(processor.main_params.DescribedValue(l.index, LayerParamIndex::KeyRangeHighFade), 0);
        }
    }

    ApplyNewState(processor, state, StateSource::PresetFile);
}

void RandomiseAllEffectParameterValues(AudioProcessor& processor) {
    ProcessorRandomiseAllParamsInternal(processor, true);
}
void RandomiseAllParameterValues(AudioProcessor& processor) {
    ProcessorRandomiseAllParamsInternal(processor, false);
}

static ChangedParams UpdateMacroAdjustedValues(Parameters& macro_adjusted_params,
                                               ChangedParams const& params,
                                               MacroDestinations const& macros) {
    Bitset<k_num_parameters> needs_adjustment {};
    for (auto const [macro_index, macro] : Enumerate(macros)) {
        auto const macro_param_index = k_macro_params[macro_index];
        bool const macro_changed = params.Changed(macro_param_index);

        for (auto const& dest : macro)
            if (params.Changed(dest.param_index) || macro_changed)
                needs_adjustment.Set(ToInt(dest.param_index));
    }

    for (auto const param_index : Range(k_num_parameters)) {
        if (!needs_adjustment.Get(param_index)) {
            if (params.changed.Get(param_index))
                macro_adjusted_params.values[param_index] = params.params.values[param_index];
            continue;
        }

        macro_adjusted_params.values[param_index] = AdjustedLinearValue(params.params,
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
            UpdateXfade(voice, processor.shared_layer_params.timbre_value_01, false);
    }

    if (auto p = changes.changed_params.ProjectedValue(ParamIndex::MasterVelocity))
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

    for (auto [index, l] : Enumerate(processor.layer_processors))
        ProcessLayerChanges(l, processor.audio_processing_context, changes, processor.voice_pool);

    for (auto effect : processor.effects_ordered_by_type)
        effect->ProcessChanges(changes, processor.audio_processing_context);
}

void ParameterJustStartedMoving(AudioProcessor& processor, ParamIndex index) {
    ASSERT(g_is_logical_main_thread);

    processor.param_events_for_audio_thread.Push(GuiStartedChangingParam {.param = index});

    if (auto host_params = HostsParamsExtension(processor)) host_params->request_flush(&processor.host);
}

void ParameterJustStoppedMoving(AudioProcessor& processor, ParamIndex index) {
    ASSERT(g_is_logical_main_thread);

    processor.param_events_for_audio_thread.Push(GuiEndedChangingParam {.param = index});

    if (auto host_params = HostsParamsExtension(processor)) host_params->request_flush(&processor.host);
}

bool SetParameterValue(AudioProcessor& processor, ParamIndex index, f32 value, ParamChangeFlags flags) {
    ASSERT(g_is_logical_main_thread);

    bool const changed = processor.main_params.values[ToInt(index)] != value;
    processor.main_params.SetLinearValue(index, value);

    processor.param_events_for_audio_thread.Push(MainThreadChangedParam {
        .value = value,
        .param = index,
        .host_should_not_record = flags.host_should_not_record != 0,
        .send_to_host = true,
    });

    if (auto host_params = HostsParamsExtension(processor))
        host_params->request_flush(&processor.host);
    else
        processor.host.request_process(&processor.host);

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

f32 AdjustedLinearValue(Parameters const& params,
                        MacroDestinations const& macros,
                        f32 linear_value,
                        ParamIndex param_index) {
    auto const& descriptor = k_param_descriptors[ToInt(param_index)];

    for (auto const [macro_index, dests] : Enumerate(macros)) {
        for (auto const& dest : dests)
            if (dest.param_index == param_index) {
                auto const& macro_param = params.LinearValue(k_macro_params[macro_index]);
                linear_value += descriptor.linear_range.Delta() * (dest.ProjectedValue() * macro_param);
            }
    }

    // Clamp the value to the range of the parameter.
    linear_value = Clamp(linear_value, descriptor.linear_range.min, descriptor.linear_range.max);

    return linear_value;
}

static void FlushEventsForAudioThread(AudioProcessor& processor) {
    auto _ = processor.events_for_audio_thread.PopAll();
    auto _ = processor.param_events_for_audio_thread.PopAll();
}

static void Deactivate(AudioProcessor& processor) {
    ASSERT(g_is_logical_main_thread);

    if (processor.activated) {
        FlushEventsForAudioThread(processor);
        processor.voice_pool.EndAllVoicesInstantly();
        processor.activated = false;
    }
}

void SetInstrument(AudioProcessor& processor, u32 layer_index, Instrument const& instrument) {
    ASSERT(g_is_logical_main_thread);

    // If we currently have a sampler instrument, we keep it alive by storing it and releasing at a later
    // time.
    if (auto current = processor.layer_processors[layer_index]
                           .instrument.TryGet<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>())
        dyn::Append(processor.lifetime_extended_insts, *current);

    // Retain the new instrument
    if (auto sampled_inst = instrument.TryGet<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>())
        sampled_inst->Retain();

    processor.layer_processors[layer_index].instrument = instrument;

    switch (instrument.tag) {
        case InstrumentType::Sampler: {
            auto& sampler_inst =
                instrument.Get<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>>();
            processor.layer_processors[layer_index].desired_inst.Set(&*sampler_inst);
            break;
        }
        case InstrumentType::WaveformSynth: {
            auto& w = instrument.Get<WaveformType>();
            processor.layer_processors[layer_index].desired_inst.Set(w);
            break;
        }
        case InstrumentType::None: {
            processor.layer_processors[layer_index].desired_inst.SetNone();
            break;
        }
    }

    processor.events_for_audio_thread.Push(LayerInstrumentChanged {.layer_index = layer_index});
    processor.host.request_process(&processor.host);
}

void SetConvolutionIrAudioData(AudioProcessor& processor,
                               AudioData const* audio_data,
                               sample_lib::ImpulseResponse::AudioProperties const& audio_props) {
    ASSERT(g_is_logical_main_thread);
    processor.convo.ConvolutionIrDataLoaded(audio_data, audio_props);
    processor.events_for_audio_thread.Push(EventForAudioThreadType::ConvolutionIRChanged);
    processor.host.request_process(&processor.host);
}

void ApplyNewState(AudioProcessor& processor, StateSnapshot const& state, StateSource source) {
    ASSERT(g_is_logical_main_thread);

    if (source == StateSource::Daw)
        for (auto [i, cc] : Enumerate(processor.param_learned_ccs))
            cc.AssignBlockwise(state.param_learned_ccs[i]);

    processor.main_params.values = state.param_values;

    processor.desired_effects_order.Store(EncodeEffectsArray(state.fx_order), StoreMemoryOrder::Relaxed);

    // Velocity curves.
    for (auto const layer_index : Range(k_num_layers)) {
        processor.layer_processors[layer_index].velocity_curve_map.SetNewPoints(
            state.velocity_curve_points[layer_index]);
    }

    DynamicArrayBounded<EventForAudioThread, (k_num_macros * k_max_macro_destinations) + 4>
        events_for_audio_thread;

    // Macro destinations.
    {
        processor.main_macro_destinations = state.macro_destinations;

        // We need to tell the audio thread about the changes.
        //
        // Start with removing all macro destinations.
        dyn::Emplace(events_for_audio_thread, EventForAudioThreadType::RemoveAllMacroDestinations);

        // Then add all the new ones.
        for (auto const [macro_index, macro] : Enumerate<u8>(state.macro_destinations)) {
            for (auto const& dest : macro) {
                struct AppendMacroDestination const event = {
                    .value = dest.value,
                    .param = dest.param_index,
                    .macro_index = macro_index,
                };
                dyn::Emplace(events_for_audio_thread, event);
            }
        }
    }

    // Reload all parameters.
    {
        if (auto host_params = HostsParamsExtension(processor))
            host_params->rescan(&processor.host, CLAP_PARAM_RESCAN_VALUES);

        UninitialisedArray<ParamEventForAudioThread, k_num_parameters> param_events;
        for (auto const param_index : Range(k_num_parameters)) {
            PLACEMENT_NEW(&param_events[param_index])
            ParamEventForAudioThread {MainThreadChangedParam {
                .value = state.param_values[param_index],
                .param = (ParamIndex)param_index,
                .host_should_not_record = true,
                .send_to_host = false, // The host already knows because of the rescan above.
            }};
        }
        if (!processor.param_events_for_audio_thread.Push(param_events)) {
            ReportError(ErrorLevel::Warning,
                        SourceLocationHash(),
                        "ApplyNewState: failed to push all param events to audio thread");
        }
    }

    dyn::Emplace(events_for_audio_thread, EventForAudioThreadType::ReloadAllAudioState);

    if (!processor.events_for_audio_thread.Push(events_for_audio_thread)) {
        ReportError(ErrorLevel::Warning,
                    SourceLocationHash(),
                    "ApplyNewState: failed to push all non-param events to audio thread");
    }

    processor.host.request_process(&processor.host);
}

StateSnapshot MakeStateSnapshot(AudioProcessor const& processor) {
    StateSnapshot result {};
    auto const ordered_fx_pointers =
        DecodeEffectsArray(processor.desired_effects_order.Load(LoadMemoryOrder::Relaxed),
                           processor.effects_ordered_by_type);
    for (auto [i, fx_pointer] : Enumerate(ordered_fx_pointers))
        result.fx_order[i] = fx_pointer->type;

    for (auto const i : Range(k_num_layers)) {
        result.inst_ids[i] = processor.layer_processors[i].instrument_id;
        result.velocity_curve_points[i] = processor.layer_processors[i].velocity_curve_map.points;
    }

    result.ir_id = processor.convo.ir_id;

    result.param_values = processor.main_params.values;

    result.macro_destinations = processor.main_macro_destinations;

    for (auto [i, cc] : Enumerate(processor.param_learned_ccs))
        result.param_learned_ccs[i] = cc.GetBlockwise();

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

    // Reset layers
    for (auto& l : processor.layer_processors)
        ChangeInstrumentIfNeededAndReset(l, processor.voice_pool);

    Reset(processor.voice_pool);
}

static bool Activate(AudioProcessor& processor, PluginActivateArgs args) {
    ASSERT(g_is_logical_main_thread);

    ASSERT(args.sample_rate > 0);

    processor.audio_processing_context.process_block_size_max = args.max_block_size;
    processor.audio_processing_context.sample_rate = (f32)args.sample_rate;
    processor.audio_processing_context.pitchwheel_position = {};
    processor.audio_processing_context.midi_note_state = {};

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
        processor.events_for_audio_thread.PopAll();
        processor.param_events_for_audio_thread.PopAll();
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
            MidiChannelNote const chan_note {.note = (u7)note.key, .channel = (u4)note.channel};

            processor.audio_processing_context.midi_note_state.NoteOn(chan_note, (f32)note.velocity);

            dyn::Append(changes.note_events,
                        {
                            .velocity = (f32)note.velocity,
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

            processor.audio_processing_context.midi_note_state.NoteOff(chan_note);

            dyn::Append(changes.note_events,
                        {
                            .velocity = (f32)note.velocity,
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
            // IMPROVE: support expression.
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
                    processor.audio_processing_context.midi_note_state.NoteOn(chan_note,
                                                                              message.Velocity() / 127.0f);

                    dyn::Append(changes.note_events,
                                {
                                    .velocity = message.Velocity() / 127.0f,
                                    .offset = event.time - block_start_frame,
                                    .note = chan_note,
                                    .type = NoteEvent::Type::On,
                                });
                    break;
                }
                case MidiMessageType::NoteOff: {
                    processor.audio_processing_context.midi_note_state.NoteOff(message.ChannelNote());
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

                    if (cc_num == 64) {
                        if (cc_val < 64) {
                            auto const notes_to_end =
                                processor.audio_processing_context.midi_note_state.HandleSustainPedalOff(
                                    channel);
                            notes_to_end.ForEachSetBit([&](usize note) {
                                dyn::Append(changes.note_events,
                                            NoteEvent {
                                                .velocity = 0.0f,
                                                .offset = event.time - block_start_frame,
                                                .note = {CheckedCast<u7>(note), channel},
                                                .created_by_cc64 = true,
                                                .type = NoteEvent::Type::Off,
                                            });
                            });
                        } else {
                            processor.audio_processing_context.midi_note_state.HandleSustainPedalOn(channel);
                        }
                    }

                    if (k_midi_learn_controller_bitset.Get(cc_num)) {
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
                    if constexpr (false) {
                        auto const note = message.NoteNum();
                        auto const channel = message.ChannelNum();
                        auto const value = message.PolyAftertouch();
                        for (auto& v : processor.voice_pool.EnumerateActiveVoices()) {
                            if (v.midi_key_trigger.channel == channel && v.midi_key_trigger.note == note) {
                                v.aftertouch_multiplier =
                                    1 + trig_table_lookup::SinTurns(value / 127.0f / 4.0f) * 2;
                            }
                        }
                    }
                    break;
                }
                case MidiMessageType::ChannelAftertouch: {
                    // NOTE: not supported at the moment
                    if constexpr (false) {
                        auto const channel = message.ChannelNum();
                        auto const value = message.ChannelPressure();
                        for (auto& v : processor.voice_pool.EnumerateActiveVoices()) {
                            if (v.midi_key_trigger.channel == channel) {
                                v.aftertouch_multiplier =
                                    1 + trig_table_lookup::SinTurns(value / 127.0f / 4.0f) * 2;
                            }
                        }
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
            auto const range = k_param_descriptors[ToInt(*index)].linear_range;
            auto const clamped_value = Clamp((f32)value->value, range.min, range.max);
            params.values[ToInt(*index)] = clamped_value;
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
    for (auto const& e : processor.param_events_for_audio_thread.PopAll()) {
        switch (e.tag) {
            case ParamEventForAudioThreadType::ParamChanged: {
                auto const& value = e.Get<MainThreadChangedParam>();

                if (value.send_to_host) {
                    clap_event_param_value const event {
                        .header {
                            .size = sizeof(event),
                            .time = frame_index,
                            .type = CLAP_EVENT_PARAM_VALUE,
                            .flags = CLAP_EVENT_IS_LIVE |
                                     (value.host_should_not_record ? (u32)CLAP_EVENT_DONT_RECORD : 0),
                        },
                        .param_id = ParamIndexToId(value.param),
                        .note_id = -1,
                        .port_index = -1,
                        .channel = -1,
                        .key = -1,
                        .value = (f64)value.value,
                    };
                    out.try_push(&out, &event.header);
                }

                processor.audio_params.values[ToInt(value.param)] = value.value;
                changes.changed_params.changed.Set(ToInt(value.param));
                break;
            }
            case ParamEventForAudioThreadType::ParamGestureBegin: {
                auto const& gesture = e.Get<GuiStartedChangingParam>();

                clap_event_param_gesture const event {
                    .header {
                        .size = sizeof(event),
                        .time = frame_index,
                        .type = CLAP_EVENT_PARAM_GESTURE_BEGIN,
                        .flags = CLAP_EVENT_IS_LIVE,
                    },
                    .param_id = ParamIndexToId(gesture.param),
                };

                out.try_push(&out, &event.header);
                break;
            }
            case ParamEventForAudioThreadType::ParamGestureEnd: {
                auto const& gesture = e.Get<GuiEndedChangingParam>();

                clap_event_param_gesture const event {
                    .header {
                        .size = sizeof(event),
                        .time = frame_index,
                        .type = CLAP_EVENT_PARAM_GESTURE_END,
                        .flags = CLAP_EVENT_IS_LIVE,
                    },
                    .param_id = ParamIndexToId(gesture.param),
                };

                out.try_push(&out, &event.header);
                break;
            }
        }
    }
}

static void SendParamChangesToMainThread(AudioProcessor& processor, ChangedParams& changes_for_main_thread) {
    // Update the main-thread representation of the parameters if they have changed.
    if (!changes_for_main_thread.changed.AnyValuesSet()) return;

    DynamicArrayBounded<AudioProcessor::ChangedParam, k_num_parameters> events {};
    for (auto const param_index : Range(k_num_parameters)) {
        if (changes_for_main_thread.changed.Get(param_index)) {
            dyn::Append(events,
                        {
                            .value = processor.audio_params.LinearValue((ParamIndex)param_index),
                            .index = (ParamIndex)param_index,
                        });
        }
    }
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
        if (processor.audio_processing_context.tempo <= 0) {
            processor.audio_processing_context.tempo = 120;
            changes.tempo_changed = true;
        }
    }

    constexpr f32 k_fade_out_ms = 30;
    constexpr f32 k_fade_in_ms = 10;

    auto const internal_events = processor.events_for_audio_thread.PopAll();
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
    for (auto const& e : internal_events) {
        switch (e.tag) {
            case EventForAudioThreadType::LayerInstrumentChanged: {
                auto const& layer_changed = e.Get<LayerInstrumentChanged>();
                layers_changed.Set(layer_changed.layer_index);
                break;
            }
            case EventForAudioThreadType::FxOrderChanged: {
                if (!new_fade_type) new_fade_type = AudioProcessor::FadeType::OutAndIn;
                break;
            }
            case EventForAudioThreadType::ReloadAllAudioState: {
                changes.changed_params.changed.SetAll();
                new_fade_type = AudioProcessor::FadeType::OutAndRestartVoices;
                layers_changed.SetAll();
                break;
            }
            case EventForAudioThreadType::ConvolutionIRChanged: {
                mark_convolution_for_fade_out = true;
                break;
            }
            case EventForAudioThreadType::AppendMacroDestination: {
                auto const& add_dest = e.Get<struct AppendMacroDestination>();
                dyn::Append(processor.audio_macro_destinations[add_dest.macro_index],
                            {
                                .param_index = add_dest.param,
                                .value = add_dest.value,
                            });
                changes.changed_params.changed.Set(ToInt(add_dest.param));
                break;
            }
            case EventForAudioThreadType::RemoveMacroDestination: {
                auto const& remove_dest = e.Get<struct RemoveMacroDestination>();
                auto const dest_param =
                    processor.audio_macro_destinations[remove_dest.macro_index][remove_dest.destination_index]
                        .param_index;
                dyn::Remove(processor.audio_macro_destinations[remove_dest.macro_index],
                            remove_dest.destination_index);
                changes.changed_params.changed.Set(ToInt(dest_param));
                break;
            }
            case EventForAudioThreadType::MacroDestinationValueChanged: {
                auto const& change_dest = e.Get<struct MacroDestinationValueChanged>();
                auto const dest_param =
                    processor.audio_macro_destinations[change_dest.macro_index][change_dest.destination_index]
                        .param_index;
                auto& dest =
                    processor
                        .audio_macro_destinations[change_dest.macro_index][change_dest.destination_index];
                dest.value = change_dest.value;
                changes.changed_params.changed.Set(ToInt(dest_param));
                break;
            }
            case EventForAudioThreadType::RemoveAllMacroDestinations: {
                for (auto const& macro : processor.audio_macro_destinations)
                    for (auto const& dest : macro)
                        changes.changed_params.changed.Set(ToInt(dest.param_index));
                processor.audio_macro_destinations = {};
                break;
            }
            case EventForAudioThreadType::StartNote: break;
            case EventForAudioThreadType::EndNote: break;
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

        for (auto const& e : internal_events) {
            switch (e.tag) {
                case EventForAudioThreadType::StartNote: {
                    auto const start = e.Get<GuiNoteClicked>();
                    clap_event_note const note {
                        .header {
                            .size = sizeof(clap_event_note),
                            .time = frame_index,
                            .type = CLAP_EVENT_NOTE_ON,
                        },
                        .note_id = -1,
                        .key = start.key,
                        .velocity = (f64)start.velocity,
                    };
                    ProcessClapNoteOrMidi(processor,
                                          note.header,
                                          *process.out_events,
                                          frame_index,
                                          change_flags,
                                          changes,
                                          changes_for_main_thread);
                    break;
                }
                case EventForAudioThreadType::EndNote: {
                    auto const end = e.Get<GuiNoteClickReleased>();
                    clap_event_note const note {
                        .header {
                            .size = sizeof(clap_event_note),
                            .time = frame_index,
                            .type = CLAP_EVENT_NOTE_OFF,
                        },
                        .note_id = -1,
                        .key = end.key,
                        .velocity = 0.0,
                    };
                    ProcessClapNoteOrMidi(processor,
                                          note.header,
                                          *process.out_events,
                                          frame_index,
                                          change_flags,
                                          changes,
                                          changes_for_main_thread);
                    break;
                }
                default: break;
            }
        }
    }

    // Create new voices for layer if requested. We want to do this after parameters have been updated
    // so that the voices start with the most recent parameter values.
    if (auto restart_layer_bitset = Exchange(processor.restart_voices_for_layer_bitset, {});
        restart_layer_bitset.AnyValuesSet()) {
        for (u32 chan = 0; chan <= 15; ++chan) {
            auto const keys_to_start =
                processor.audio_processing_context.midi_note_state.NotesHeldIncludingSustained((u4)chan);
            if (keys_to_start.AnyValuesSet()) {
                for (auto [layer_index, layer] : Enumerate(processor.layer_processors)) {
                    if (restart_layer_bitset.Get(layer_index)) {
                        for (u8 note_num = 0; note_num <= 127; ++note_num) {
                            if (keys_to_start.Get(note_num)) {
                                dyn::Append(changes.note_events,
                                            NoteEvent {
                                                .velocity = processor.audio_processing_context.midi_note_state
                                                                .velocities[chan][note_num],
                                                .offset = 0,
                                                .note = {.note = (u7)note_num, .channel = (u4)chan},
                                                .type = NoteEvent::Type::On,
                                            });
                            }
                        }
                    }
                }
            }
        }
    }

    ProcessorHandleChanges(processor, changes);

    // Voices and layers
    // ======================================================================================================
    // IMPROVE: support sending the host CLAP_EVENT_NOTE_END events when voices end
    ProcessVoices(processor.voice_pool, sub_block_size, processor.audio_processing_context);

    Array<f32x2, k_block_size_max> output_buffer;
    auto const output = Span<f32x2>(output_buffer.data, sub_block_size);
    Fill(output, 0.0f);

    bool audio_was_generated_by_layers = false;
    for (auto const layer_index : Range(k_num_layers)) {
        auto const process_result = ProcessLayer(processor.layer_processors[layer_index],
                                                 processor.audio_processing_context,
                                                 processor.voice_pool,
                                                 sub_block_size,
                                                 layers_changed.Get(layer_index));

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
    if (process.audio_outputs->channel_count == 2 && process.audio_outputs->data32 &&
        (((uintptr)process.audio_outputs->data32 % alignof(f32*)) == 0) && process.audio_outputs->data32[0] &&
        process.audio_outputs->data32[1]) {
        static_assert(sizeof(f32x2) == (sizeof(f32) * 2));
        auto interleaved_outputs = (f32 const*)output.data;
        CopyInterleavedToSeparateChannels(process.audio_outputs->data32[0] + frame_index,
                                          process.audio_outputs->data32[1] + frame_index,
                                          interleaved_outputs,
                                          sub_block_size);
    }

    return result;
}

clap_process_status Process(AudioProcessor& processor, clap_process const& process) {
    ZoneScoped;
    ASSERT_EQ(process.audio_outputs_count, 1u);
    ASSERT_HOT(processor.activated);

    if (process.frames_count == 0) return CLAP_PROCESS_CONTINUE;

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
        if (!layer.peak_meter.Silent()) change_flags |= ProcessorListener::PeakMeterChanged;

    if (change_flags) processor.listener.OnProcessorChange(change_flags);
    SendParamChangesToMainThread(processor, changes_for_main_thread);

    return result;
}

// Audio-thread
static void Reset(AudioProcessor& processor) {
    FlushEventsForAudioThread(processor);
    processor.voice_pool.EndAllVoicesInstantly();
    processor.audio_processing_context.pitchwheel_position = {};
    ProcessBlockChanges changes {
        .changed_params = {processor.audio_params, Bitset<k_num_parameters>()},
    };
    changes.pitchwheel_changed.SetAll();
    ResetProcessor(processor, changes);
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
          &convo,
      })) {

    for (auto const i : Range(k_num_parameters))
        main_params.values[i] = k_param_descriptors[i].default_linear_value;

    if (auto host_params = HostsParamsExtension(*this)) host_params->rescan(&host, CLAP_PARAM_RESCAN_VALUES);

    for (u32 i = 0; i < k_num_parameters; ++i)
        param_learned_ccs[i].AssignBlockwise(PersistentCcsForParam(prefs, ParamIndexToId((ParamIndex)i)));

    if (prefs::GetBool(prefs, SettingDescriptor(ProcessorSetting::DefaultCcParamMappings)))
        for (auto const mapping : k_default_cc_to_param_mapping)
            param_learned_ccs[ToInt(mapping.param)].Set(mapping.cc);
}

AudioProcessor::~AudioProcessor() {
    for (auto& i : lifetime_extended_insts)
        i.Release();
}

PluginCallbacks<AudioProcessor> const g_processor_callbacks {
    .activate = Activate,
    .deactivate = Deactivate,
    .reset = Reset,
    .process = Process,
    .flush_parameter_events = FlushParameterEvents,
    .on_main_thread = OnMainThread,
    .on_thread_pool_exec = OnThreadPoolExec,
};
