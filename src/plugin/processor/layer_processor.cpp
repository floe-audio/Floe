// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layer_processor.hpp"

#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

#include "param.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/filters.hpp"
#include "processing_utils/midi.hpp"
#include "processing_utils/peak_meter.hpp"
#include "processing_utils/stereo_audio_frame.hpp"
#include "processing_utils/synced_timings.hpp"
#include "processing_utils/volume_fade.hpp"
#include "voices.hpp"

static void UpdateLoopPointsForVoices(LayerProcessor& layer, VoicePool& voice_pool) {
    for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
        UpdateLoopInfo(v);
}

bool LayerProcessor::VolumeEnvelopeIsOn(bool is_audio_thread) {
    auto const param_is_on = params[ToInt(LayerParamIndex::VolEnvOn)].ValueAsBool();
    auto const is_waveform =
        (is_audio_thread ? inst.tag : instrument_id.tag) == InstrumentType::WaveformSynth;
    return param_is_on || is_waveform;
}

static void UpdateVolumeEnvelopeOn(LayerProcessor& layer, VoicePool& voice_pool) {
    layer.voice_controller.vol_env_on = layer.VolumeEnvelopeIsOn(true);
    if (layer.voice_controller.vol_env_on)
        for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
            v.vol_env.Gate(false);
    else
        UpdateLoopPointsForVoices(layer, voice_pool);
}

struct VelocityRegion {
    u7 const point_most_intense;
    u7 const point_least_intense;
    int const no_fade_size; // always fades down from the bottom
};

constexpr Array<VelocityRegion, 2> k_velo_regions_half {{{127, 20, 20}, {0, 107, 20}}};
constexpr Array<VelocityRegion, 4> k_velo_regions_third {
    {{127, 64, 20}, {64, 127, 0}, {64, 20, 0}, {0, 64, 20}}};

static f32 ProcessVeloRegion(VelocityRegion const& r, u7 velo) {
    auto min = Min(r.point_least_intense, r.point_most_intense);
    auto max = Max(r.point_least_intense, r.point_most_intense);
    if (velo < min || velo > max) return 0;

    if (r.point_most_intense > r.point_least_intense) {
        auto point_fad_end = r.point_most_intense - r.no_fade_size;
        if (velo > point_fad_end) return 1;
        auto new_top = point_fad_end;
        auto new_bot = r.point_least_intense;
        auto map = (f32)(velo - new_bot) / f32(new_top - new_bot);
        return map;
    } else if (r.point_least_intense > r.point_most_intense) {
        auto point_fad_end = r.point_most_intense + r.no_fade_size;
        if (velo < point_fad_end) return 1;
        auto point_at_greatest_intensity = point_fad_end;
        auto point_at_least_intensity = r.point_least_intense;
        auto d = point_at_least_intensity - point_at_greatest_intensity;
        auto inv = (f32)(velo - point_at_greatest_intensity) / (f32)d;
        auto map = 1 - inv;
        return map;
    }

    return 0;
}

static f32 ProcessVeloRegions(Span<VelocityRegion const> regions, Bitset<4> active_regions, u7 velo) {
    f32 sum = 0;
    for (auto [i, r] : Enumerate(regions))
        if (active_regions.Get(i)) sum += ProcessVeloRegion(r, velo);
    return sum;
}

static void SetVelocityMapping(LayerProcessor& layer, param_values::VelocityMappingMode mode) {
    layer.active_velocity_regions.ClearAll();
    switch (mode) {
        case param_values::VelocityMappingMode::None: {
            layer.num_velocity_regions = 1;
            break;
        }
        case param_values::VelocityMappingMode::TopToBottom: {
            layer.num_velocity_regions = 2;
            layer.active_velocity_regions.Set(0);
            break;
        }
        case param_values::VelocityMappingMode::BottomToTop: {
            layer.num_velocity_regions = 2;
            layer.active_velocity_regions.Set(1);
            break;
        }
        case param_values::VelocityMappingMode::TopToMiddle: {
            layer.num_velocity_regions = 3;
            layer.active_velocity_regions.Set(0);
            break;
        }
        case param_values::VelocityMappingMode::MiddleOutwards: {
            layer.num_velocity_regions = 3;
            layer.active_velocity_regions.Set(1);
            layer.active_velocity_regions.Set(2);
            break;
        }
        case param_values::VelocityMappingMode::MiddleToBottom: {
            layer.num_velocity_regions = 3;
            layer.active_velocity_regions.Set(3);
            break;
        }
        case param_values::VelocityMappingMode::Count: PanicIfReached(); break;
    }
}

static f32 GetVelocityRegionLevel(LayerProcessor& layer, f32 velocity, f32 velocity_to_volume) {
    auto mod = MapFrom01(velocity, (1 - velocity_to_volume), 1);
    if (layer.num_velocity_regions == 2) {
        mod *= ProcessVeloRegions(k_velo_regions_half.Items(), layer.active_velocity_regions, velocity * 127);
    } else if (layer.num_velocity_regions == 3) {
        mod *=
            ProcessVeloRegions(k_velo_regions_third.Items(), layer.active_velocity_regions, velocity * 127);
    }
    return mod;
}

void SetSilent(LayerProcessor& layer, bool state) {
    layer.smoothed_value_system.Set(layer.mute_solo_mix_smoother_id, state ? 0.0f : 1.0f, 10);
    layer.is_silent.Store(state, StoreMemoryOrder::Relaxed);
}

static void
UpdateVoiceLfoTimes(LayerProcessor& layer, VoicePool& voice_pool, AudioProcessingContext const& context) {
    for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
        UpdateLFOTime(v, context.sample_rate);
}

void SetTempo(LayerProcessor& layer, VoicePool& voice_pool, AudioProcessingContext const& context) {
    UpdateVoiceLfoTimes(layer, voice_pool, context);
}

void PrepareToPlay(LayerProcessor& layer, ArenaAllocator& allocator, AudioProcessingContext const& context) {
    ResetLayerAudioProcessing(layer);
    layer.peak_meter.PrepareToPlay(context.sample_rate, allocator);
}

void OnParamChange(LayerProcessor& layer,
                   AudioProcessingContext const& context,
                   VoicePool& voice_pool,
                   ChangedLayerParams changed_params) {
    f32 const sample_rate = context.sample_rate;
    auto& vmst = layer.voice_controller;

    // Main controls
    // =======================================================================================================
    if (auto p = changed_params.Param(LayerParamIndex::VelocityMapping))
        SetVelocityMapping(layer, p->ValueAsInt<param_values::VelocityMappingMode>());

    if (auto p = changed_params.Param(LayerParamIndex::Volume))
        layer.smoothed_value_system.SetVariableLength(layer.vol_smoother_id, p->ProjectedValue(), 3, 30, 1);

    if (auto p = changed_params.Param(LayerParamIndex::Pan))
        layer.smoothed_value_system.SetVariableLength(vmst.pan_pos_smoother_id,
                                                      p->ProjectedValue(),
                                                      3,
                                                      30,
                                                      2);

    {
        bool set_tune = false;
        if (auto p = changed_params.Param(LayerParamIndex::TuneSemitone)) {
            layer.tune_semitone = (f32)p->ValueAsInt<int>();
            set_tune = true;
        }
        if (auto p = changed_params.Param(LayerParamIndex::TuneCents)) {
            layer.tune_cents = p->ProjectedValue();
            set_tune = true;
        }
        if (set_tune) {
            auto const tune = layer.tune_semitone + (layer.tune_cents / 100.0f);
            layer.voice_controller.tune = tune;
            for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
                SetVoicePitch(v, vmst.tune, sample_rate);
        }
    }

    constexpr f32 k_min_envelope_ms = 0.2f;
    // Volume envelope
    // =======================================================================================================
    if (changed_params.Param(LayerParamIndex::VolEnvOn)) UpdateVolumeEnvelopeOn(layer, voice_pool);
    if (auto p = changed_params.Param(LayerParamIndex::VolumeAttack))
        layer.voice_controller.vol_env.SetAttackSamples(Max(k_min_envelope_ms, p->ProjectedValue()) /
                                                            1000.0f * sample_rate,
                                                        2.0f);
    if (auto p = changed_params.Param(LayerParamIndex::VolumeDecay))
        layer.voice_controller.vol_env.SetDecaySamples(Max(k_min_envelope_ms, p->ProjectedValue()) / 1000.0f *
                                                           sample_rate,
                                                       0.1f);
    if (auto p = changed_params.Param(LayerParamIndex::VolumeSustain))
        layer.voice_controller.vol_env.SetSustainAmp(p->ProjectedValue());

    if (auto p = changed_params.Param(LayerParamIndex::VolumeRelease))
        layer.voice_controller.vol_env.SetReleaseSamples(Max(k_min_envelope_ms, p->ProjectedValue()) /
                                                             1000.0f * sample_rate,
                                                         0.1f);

    // Filter
    // =======================================================================================================
    if (auto p = changed_params.Param(LayerParamIndex::FilterEnvAmount))
        vmst.fil_env_amount = p->ProjectedValue();
    if (auto p = changed_params.Param(LayerParamIndex::FilterAttack))
        layer.voice_controller.fil_env.SetAttackSamples(Max(k_min_envelope_ms, p->ProjectedValue()) /
                                                            1000.0f * sample_rate,
                                                        2.0f);
    if (auto p = changed_params.Param(LayerParamIndex::FilterDecay))
        layer.voice_controller.fil_env.SetDecaySamples(Max(k_min_envelope_ms, p->ProjectedValue()) / 1000.0f *
                                                           sample_rate,
                                                       0.1f);
    if (auto p = changed_params.Param(LayerParamIndex::FilterSustain))
        layer.voice_controller.fil_env.SetSustainAmp(p->ProjectedValue());
    if (auto p = changed_params.Param(LayerParamIndex::FilterRelease))
        layer.voice_controller.fil_env.SetReleaseSamples(Max(k_min_envelope_ms, p->ProjectedValue()) /
                                                             1000.0f * sample_rate,
                                                         0.1f);
    if (auto p = changed_params.Param(LayerParamIndex::FilterCutoff)) {
        vmst.sv_filter_cutoff_linear = sv_filter::HzToLinear(p->ProjectedValue());
        for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
            SetFilterCutoff(v, vmst.sv_filter_cutoff_linear);
    }
    if (auto p = changed_params.Param(LayerParamIndex::FilterResonance)) {
        vmst.sv_filter_resonance = sv_filter::SkewResonance(p->ProjectedValue());
        for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
            SetFilterRes(v, vmst.sv_filter_resonance);
    }
    if (auto p = changed_params.Param(LayerParamIndex::FilterOn)) {
        vmst.filter_on = p->ValueAsBool();
        for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
            SetFilterOn(v, vmst.filter_on);
    }
    if (auto p = changed_params.Param(LayerParamIndex::FilterType)) {
        sv_filter::Type sv_type {};
        // Remapping enum values like this allows us to separate values that cannot change (the parameter
        // value), with values that we have more control over (DSP code)
        switch (p->ValueAsInt<param_values::LayerFilterType>()) {
            case param_values::LayerFilterType::Lowpass: sv_type = sv_filter::Type::Lowpass; break;
            case param_values::LayerFilterType::Bandpass: sv_type = sv_filter::Type::Bandpass; break;
            case param_values::LayerFilterType::Highpass: sv_type = sv_filter::Type::Highpass; break;
            case param_values::LayerFilterType::UnitGainBandpass:
                sv_type = sv_filter::Type::UnitGainBandpass;
                break;
            case param_values::LayerFilterType::BandShelving: sv_type = sv_filter::Type::BandShelving; break;
            case param_values::LayerFilterType::Notch: sv_type = sv_filter::Type::Notch; break;
            case param_values::LayerFilterType::Allpass: sv_type = sv_filter::Type::Allpass; break;
            case param_values::LayerFilterType::Peak: sv_type = sv_filter::Type::Peak; break;
            case param_values::LayerFilterType::Count: PanicIfReached(); break;
        }
        vmst.filter_type = sv_type;
    }

    // Midi
    // =======================================================================================================
    if (auto p = changed_params.Param(LayerParamIndex::MidiTranspose))
        layer.midi_transpose = p->ValueAsInt<int>();
    if (auto p = changed_params.Param(LayerParamIndex::Keytrack)) vmst.no_key_tracking = !p->ValueAsBool();

    // LFO
    // =======================================================================================================
    if (auto p = changed_params.Param(LayerParamIndex::LfoShape)) {
        vmst.lfo.shape = p->ValueAsInt<param_values::LfoShape>();
        for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
            UpdateLFOWaveform(v);
    }
    if (auto p = changed_params.Param(LayerParamIndex::LfoAmount)) vmst.lfo.amount = p->ProjectedValue();
    if (auto p = changed_params.Param(LayerParamIndex::LfoDestination))
        layer.voice_controller.lfo.dest = p->ValueAsInt<param_values::LfoDestination>();
    if (auto p = changed_params.Param(LayerParamIndex::LfoOn))
        layer.voice_controller.lfo.on = p->ValueAsBool();

    {
        bool update_voice_controller_times = false;
        if (auto p = changed_params.Param(LayerParamIndex::LfoRateTempoSynced)) {
            layer.lfo_synced_time = p->ValueAsInt<param_values::LfoSyncedRate>();
            update_voice_controller_times = true;
        }
        if (auto p = changed_params.Param(LayerParamIndex::LfoRateHz)) {
            layer.lfo_unsynced_hz = p->ProjectedValue();
            update_voice_controller_times = true;
        }
        if (auto p = changed_params.Param(LayerParamIndex::LfoSyncSwitch)) {
            layer.lfo_is_synced = p->ValueAsBool();
            update_voice_controller_times = true;
        }
        if (update_voice_controller_times) {
            if (layer.lfo_is_synced) {
                SyncedTimes synced_time {};
                // Remapping enum values like this allows us to separate values that cannot change (the
                // parameter value), with values that we have more control over (DSP code)
                switch (layer.lfo_synced_time) {
                    case param_values::LfoSyncedRate::_1_64T: synced_time = SyncedTimes::_1_64T; break;
                    case param_values::LfoSyncedRate::_1_64: synced_time = SyncedTimes::_1_64; break;
                    case param_values::LfoSyncedRate::_1_64D: synced_time = SyncedTimes::_1_64D; break;
                    case param_values::LfoSyncedRate::_1_32T: synced_time = SyncedTimes::_1_32T; break;
                    case param_values::LfoSyncedRate::_1_32: synced_time = SyncedTimes::_1_32; break;
                    case param_values::LfoSyncedRate::_1_32D: synced_time = SyncedTimes::_1_32D; break;
                    case param_values::LfoSyncedRate::_1_16T: synced_time = SyncedTimes::_1_16T; break;
                    case param_values::LfoSyncedRate::_1_16: synced_time = SyncedTimes::_1_16; break;
                    case param_values::LfoSyncedRate::_1_16D: synced_time = SyncedTimes::_1_16D; break;
                    case param_values::LfoSyncedRate::_1_8T: synced_time = SyncedTimes::_1_8T; break;
                    case param_values::LfoSyncedRate::_1_8: synced_time = SyncedTimes::_1_8; break;
                    case param_values::LfoSyncedRate::_1_8D: synced_time = SyncedTimes::_1_8D; break;
                    case param_values::LfoSyncedRate::_1_4T: synced_time = SyncedTimes::_1_4T; break;
                    case param_values::LfoSyncedRate::_1_4: synced_time = SyncedTimes::_1_4; break;
                    case param_values::LfoSyncedRate::_1_4D: synced_time = SyncedTimes::_1_4D; break;
                    case param_values::LfoSyncedRate::_1_2T: synced_time = SyncedTimes::_1_2T; break;
                    case param_values::LfoSyncedRate::_1_2: synced_time = SyncedTimes::_1_2; break;
                    case param_values::LfoSyncedRate::_1_2D: synced_time = SyncedTimes::_1_2D; break;
                    case param_values::LfoSyncedRate::_1_1T: synced_time = SyncedTimes::_1_1T; break;
                    case param_values::LfoSyncedRate::_1_1: synced_time = SyncedTimes::_1_1; break;
                    case param_values::LfoSyncedRate::_1_1D: synced_time = SyncedTimes::_1_1D; break;
                    case param_values::LfoSyncedRate::_2_1T: synced_time = SyncedTimes::_2_1T; break;
                    case param_values::LfoSyncedRate::_2_1: synced_time = SyncedTimes::_2_1; break;
                    case param_values::LfoSyncedRate::_2_1D: synced_time = SyncedTimes::_2_1D; break;
                    case param_values::LfoSyncedRate::_4_1T: synced_time = SyncedTimes::_4_1T; break;
                    case param_values::LfoSyncedRate::_4_1: synced_time = SyncedTimes::_4_1; break;
                    case param_values::LfoSyncedRate::_4_1D: synced_time = SyncedTimes::_4_1D; break;
                    case param_values::LfoSyncedRate::Count: PanicIfReached(); break;
                }
                vmst.lfo.time_hz = (f32)(1.0 / (SyncedTimeToMs(context.tempo, synced_time) / 1000.0));
            } else {
                vmst.lfo.time_hz = layer.lfo_unsynced_hz;
            }
            UpdateVoiceLfoTimes(layer, voice_pool, context);
        }
    }

    if (auto p = changed_params.Param(LayerParamIndex::LfoRestart))
        layer.lfo_restart_mode = p->ValueAsInt<param_values::LfoRestartMode>();

    if (auto p = changed_params.Param(LayerParamIndex::Monophonic)) layer.monophonic = p->ValueAsBool();

    // Loop
    // =======================================================================================================
    {
        bool update_loop_info = false;
        if (auto p = changed_params.Param(LayerParamIndex::LoopStart)) {
            vmst.loop.start = p->ProjectedValue();
            update_loop_info = true;
        }
        if (auto p = changed_params.Param(LayerParamIndex::LoopEnd)) {
            vmst.loop.end = p->ProjectedValue();
            update_loop_info = true;
        }
        if (auto p = changed_params.Param(LayerParamIndex::LoopCrossfade)) {
            vmst.loop.crossfade_size = p->ProjectedValue();
            update_loop_info = true;
        }
        if (auto p = changed_params.Param(LayerParamIndex::Reverse)) {
            update_loop_info = true;
            vmst.reverse = p->ValueAsBool();
        }
        if (auto p = changed_params.Param(LayerParamIndex::LoopMode)) {
            update_loop_info = true;
            vmst.loop_mode = p->ValueAsInt<param_values::LoopMode>();
        }
        if (auto p = changed_params.Param(LayerParamIndex::SampleOffset))
            layer.sample_offset_01 = p->ProjectedValue();

        if (update_loop_info) UpdateLoopPointsForVoices(layer, voice_pool);
    }

    // EQ
    // =======================================================================================================
    if (auto p = changed_params.Param(LayerParamIndex::EqOn))
        layer.eq_bands.SetOn(layer.smoothed_value_system, p->ValueAsBool());

    for (auto const eq_band_index : Range(k_num_layer_eq_bands))
        layer.eq_bands.OnParamChange(eq_band_index, changed_params, layer.smoothed_value_system, sample_rate);
}

//
// ==========================================================================================================

//
//
//

static void TriggerVoicesIfNeeded(LayerProcessor& layer,
                                  AudioProcessingContext const& context,
                                  VoicePool& voice_pool,
                                  sample_lib::TriggerEvent trigger_event,
                                  MidiChannelNote note,
                                  f32 note_vel_float,
                                  u32 offset,
                                  f32 timbre_param_value_01,
                                  f32 velocity_to_volume_01) {
    ZoneScoped;
    if (layer.inst.tag == InstrumentType::None) return;

    ASSERT_HOT(note_vel_float >= 0 && note_vel_float <= 1);
    auto const note_vel = (u8)RoundPositiveFloat(note_vel_float * 99);

    auto const note_for_samples_unchecked = note.note + layer.midi_transpose + layer.multisample_transpose;
    if (note_for_samples_unchecked < 0 || note_for_samples_unchecked > 127) return;
    auto const note_for_samples = (u7)note_for_samples_unchecked;
    auto const velocity_volume_modifier =
        GetVelocityRegionLevel(layer, note_vel_float, velocity_to_volume_01);

    VoiceStartParams p {.params = VoiceStartParams::SamplerParams {}};
    if (auto i_ptr = layer.inst.TryGet<sample_lib::LoadedInstrument const*>()) {
        auto const& inst = **i_ptr;
        p.params = VoiceStartParams::SamplerParams {
            .initial_sample_offset_01 = layer.sample_offset_01,
            .initial_timbre_param_value_01 = timbre_param_value_01,
            .voice_sample_params = {},
        };
        auto& sampler_params = p.params.Get<VoiceStartParams::SamplerParams>();

        auto& rr_pos = layer.rr_pos[ToInt(trigger_event)];

        for (auto [group_index, group] :
             Enumerate(inst.instrument.round_robin_sequence_groups[ToInt(trigger_event)])) {
            if (rr_pos[group_index] > group.max_rr_pos) rr_pos[group_index] = 0;
        }

        DEFER {
            for (auto const group_index :
                 Range(inst.instrument.round_robin_sequence_groups[ToInt(trigger_event)].size))
                ++rr_pos[group_index];
        };

        for (auto i : Range(inst.instrument.regions.size)) {
            auto const& region = inst.instrument.regions[i];
            auto const& audio_data = inst.audio_datas[i];
            if (region.trigger.key_range.Contains(note_for_samples) &&
                region.trigger.velocity_range.Contains(note_vel) &&
                (!region.trigger.round_robin_index ||
                 *region.trigger.round_robin_index == rr_pos[region.trigger.round_robin_sequencing_group]) &&
                region.trigger.trigger_event == trigger_event) {
                dyn::Append(sampler_params.voice_sample_params,
                            VoiceStartParams::SamplerParams::Region {
                                .region = region,
                                .audio_data = *audio_data,
                                .amp = velocity_volume_modifier,
                            });
            }
        }

        if (!sampler_params.voice_sample_params.size) return;

        // do velocity feathering if needed
        {
            VoiceStartParams::SamplerParams::Region* feather_region_1 = nullptr;
            VoiceStartParams::SamplerParams::Region* feather_region_2 = nullptr;
            for (auto& r : sampler_params.voice_sample_params) {
                if (r.region.trigger.feather_overlapping_velocity_layers) {
                    // NOTE, if there are more than 2 feather regions, then we only crossfade 2 of them.
                    // Any others will play at normal volume.
                    if (!feather_region_1)
                        feather_region_1 = &r;
                    else
                        feather_region_2 = &r;
                }
            }
            if (feather_region_1 && feather_region_2) {
                if (feather_region_2->region.trigger.velocity_range.start <
                    feather_region_1->region.trigger.velocity_range.start)
                    Swap(feather_region_1, feather_region_2);
                auto const overlap_low = feather_region_2->region.trigger.velocity_range.start;
                auto const overlap_high = feather_region_1->region.trigger.velocity_range.end;
                ASSERT(overlap_high > overlap_low);
                auto const overlap_size = overlap_high - overlap_low;
                auto const pos = (note_vel - overlap_low) / (f32)overlap_size;
                ASSERT(pos >= 0 && pos <= 1);
                auto const amp1 = trig_table_lookup::SinTurnsPositive((1 - pos) * 0.25f);
                auto const amp2 = trig_table_lookup::SinTurnsPositive(pos * 0.25f);
                feather_region_1->amp *= amp1;
                feather_region_2->amp *= amp2;
            }
        }
    } else if (auto w = layer.inst.TryGet<WaveformType>();
               w && trigger_event == sample_lib::TriggerEvent::NoteOn) {
        p.params = VoiceStartParams::WaveformParams {};
        auto& waveform = p.params.Get<VoiceStartParams::WaveformParams>();
        waveform.amp = velocity_volume_modifier;
        waveform.type = *w;
    }

    p.disable_vol_env = trigger_event == sample_lib::TriggerEvent::NoteOff;
    p.initial_pitch = layer.voice_controller.tune;
    p.midi_key_trigger = note;
    p.note_num = (u7)Clamp(note.note + layer.midi_transpose, 0, 127);
    p.note_vel = note_vel_float;
    p.lfo_start_phase = 0;
    p.num_frames_before_starting = offset;
    if (layer.lfo_restart_mode == param_values::LfoRestartMode::Free) {
        for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller)) {
            p.lfo_start_phase = v.lfo.phase;
            break;
        }
    }

    if (layer.monophonic && trigger_event == sample_lib::TriggerEvent::NoteOn) {
        for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
            if (!layer.voice_controller.vol_env_on)
                v.volume_fade.SetAsFadeOutIfNotAlready(context.sample_rate, 5);
            else
                EndVoice(v);
    }

    StartVoice(voice_pool, layer.voice_controller, p, context);
}

void LayerHandleNoteOff(LayerProcessor& layer,
                        AudioProcessingContext const& context,
                        VoicePool& voice_pool,
                        MidiChannelNote note,
                        f32 velocity,
                        bool triggered_by_cc64,
                        f32 timbre_param_value_01,
                        f32 velocity_to_volume_01) {
    if (!context.midi_note_state.sustain_pedal_on.Get(note.channel) && layer.voice_controller.vol_env_on &&
        !context.midi_note_state.keys_held[note.channel].Get(note.note))
        NoteOff(voice_pool, layer.voice_controller, note);

    if (!triggered_by_cc64)
        TriggerVoicesIfNeeded(layer,
                              context,
                              voice_pool,
                              sample_lib::TriggerEvent::NoteOff,
                              note,
                              velocity,
                              0,
                              timbre_param_value_01,
                              velocity_to_volume_01);
}

void LayerHandleNoteOn(LayerProcessor& layer,
                       AudioProcessingContext const& context,
                       VoicePool& voice_pool,
                       MidiChannelNote note_num,
                       f32 note_vel,
                       u32 offset,
                       f32 timbre_param_value_01,
                       f32 velocity_to_volume_01) {
    TriggerVoicesIfNeeded(layer,
                          context,
                          voice_pool,
                          sample_lib::TriggerEvent::NoteOn,
                          note_num,
                          note_vel,
                          offset,
                          timbre_param_value_01,
                          velocity_to_volume_01);
}

bool ChangeInstrumentIfNeededAndReset(LayerProcessor& layer, VoicePool& voice_pool) {
    ZoneScoped;
    auto desired_inst = layer.desired_inst.Consume();

    DEFER { ResetLayerAudioProcessing(layer); };

    if (!desired_inst) return false;
    if (*desired_inst == layer.inst) return false;

    // End all layer voices
    for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
        EndVoiceInstantly(v);

    layer.peak_meter.Zero();

    // Swap instrument
    layer.inst = *desired_inst;
    UpdateLoopPointsForVoices(layer, voice_pool);
    UpdateVolumeEnvelopeOn(layer, voice_pool);

    return true;
}

LayerProcessResult ProcessLayer(LayerProcessor& layer,
                                AudioProcessingContext const& context,
                                VoicePool& voice_pool,
                                u32 num_frames,
                                bool start_fade_out,
                                Span<f32> buffer) {
    ZoneScoped;
    ZoneValue(layer.index);

    constexpr f32 k_inst_change_fade_ms = 100;

    LayerProcessResult result {};

    // NOTE: we want to trigger a fade out regardless of whether or not this layer is actually processing
    // audio at the moment because we want the swapping of instruments to be in sync with any other layers
    if (start_fade_out)
        layer.inst_change_fade.SetAsFadeOutIfNotAlready(context.sample_rate, k_inst_change_fade_ms);

    if (!buffer.size || layer.inst.tag == InstrumentType::None) {
        if (layer.inst_change_fade.JumpMultipleSteps(num_frames) == VolumeFade::State::Silent)
            result.instrument_swapped = ChangeInstrumentIfNeededAndReset(layer, voice_pool);

        layer.peak_meter.Zero();
        return result;
    }

    for (auto const i : Range(num_frames)) {
        StereoAudioFrame frame(buffer.data, i);
        frame = layer.eq_bands.Process(layer.smoothed_value_system, frame, i);

        frame *= layer.smoothed_value_system.Value(layer.vol_smoother_id, i) *
                 layer.smoothed_value_system.Value(layer.mute_solo_mix_smoother_id, i);

        if (!result.instrument_swapped) {
            auto const fade = layer.inst_change_fade.GetFadeAndStateChange();
            frame *= fade.value;
            if (fade.state_changed == VolumeFade::State::Silent)
                result.instrument_swapped = ChangeInstrumentIfNeededAndReset(layer, voice_pool);
        } else {
            // If we have swapped we want to be silent for the remainder of this block - we will use the
            // new instrument next block
            frame = {};
        }

        frame.Store(buffer.data, i);
    }

    ASSERT_HOT(!layer.inst_change_fade.IsSilent());

    layer.peak_meter.AddBuffer(ToStereoFramesSpan(buffer.data, num_frames));

    result.did_any_processing = true;
    return result;
}

void ResetLayerAudioProcessing(LayerProcessor& layer) {
    for (auto& b : layer.eq_bands.eq_bands)
        b.eq_data = {};
    layer.inst_change_fade.ForceSetFullVolume();
}
