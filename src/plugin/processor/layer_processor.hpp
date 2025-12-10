// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/instrument.hpp"

#include "clap/host.h"
#include "param.hpp"
#include "processing_utils/adsr.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/curve_map.hpp"
#include "processing_utils/filters.hpp"
#include "processing_utils/midi.hpp"
#include "processing_utils/peak_meter.hpp"
#include "processing_utils/volume_fade.hpp"
#include "sample_lib_server/sample_library_server.hpp"

// These are controlled at the master level, but they are used by the layer processor. We let the master
// processor manage them but each layer gets a reference.
struct SharedLayerParams {
    f32 timbre_value_01 {};
    f32 velocity_to_volume_01 {};
};

constexpr u32 k_num_layer_eq_bands = 2;

struct EqBand {
    f32x2 Process(f32x2 in) {
        auto const [coeffs, mix] = eq_coeffs.Value();
        return rbj_filter::Process(eq_data, coeffs, in * mix);
    }

    void OnParamChange(ChangedParams changed_params, u8 layer_index, f32 sample_rate, u32 band_num) {
        auto freq_param = LayerParamIndex::EqFreq1;
        auto reso_param = LayerParamIndex::EqResonance1;
        auto gain_param = LayerParamIndex::EqGain1;
        auto type_param = LayerParamIndex::EqType1;
        if (band_num == 1) {
            freq_param = LayerParamIndex::EqFreq2;
            reso_param = LayerParamIndex::EqResonance2;
            gain_param = LayerParamIndex::EqGain2;
            type_param = LayerParamIndex::EqType2;
        } else if (band_num != 0) {
            PanicIfReached();
        }

        bool changed = false;
        if (auto p = changed_params.ProjectedValue(layer_index, freq_param)) {
            eq_params.fs = sample_rate;
            eq_params.fc = *p;
            changed = true;
        }
        if (auto p = changed_params.ProjectedValue(layer_index, reso_param)) {
            eq_params.fs = sample_rate;
            eq_params.q = MapFrom01Skew(*p, 0.5f, 8, 5);
            changed = true;
        }
        if (auto p = changed_params.ProjectedValue(layer_index, gain_param)) {
            eq_params.fs = sample_rate;
            eq_params.peak_gain = *p;
            changed = true;
        }
        if (auto p = changed_params.IntValue<param_values::EqType>(layer_index, type_param)) {
            eq_params.fs = sample_rate;
            rbj_filter::Type type {rbj_filter::Type::Peaking};
            switch (*p) {
                case param_values::EqType::HighShelf: type = rbj_filter::Type::HighShelf; break;
                case param_values::EqType::LowShelf: type = rbj_filter::Type::LowShelf; break;
                case param_values::EqType::Peak: type = rbj_filter::Type::Peaking; break;
                case param_values::EqType::Count: PanicIfReached(); break;
            }
            eq_params.type = type;
            changed = true;
        }

        if (changed) eq_coeffs.Set(eq_params);
    }

    void Reset() { eq_coeffs.ResetSmoothing(); }

    rbj_filter::StereoData eq_data {};
    rbj_filter::Params eq_params {};
    rbj_filter::SmoothedCoefficients eq_coeffs {};
};

struct EqBands {
    void OnParamChange(u32 band_num, ChangedParams changed_params, u8 layer_index, f32 sample_rate) {
        eq_bands[band_num].OnParamChange(changed_params, layer_index, sample_rate, band_num);
    }

    void SetOn(bool on) { eq_mix = on ? 1.0f : 0.0f; }

    f32x2 Process(AudioProcessingContext const& context, f32x2 in) {
        f32x2 result = in;
        if (auto mix = eq_mix_smoother.LowPass(eq_mix, context.one_pole_smoothing_cutoff_10ms); mix != 0) {
            for (auto& eq_band : eq_bands)
                result = eq_band.Process(result);
            if (mix != 1) result = LinearInterpolate(mix, in, result);
        }
        return result;
    }

    void Reset() {
        for (auto& eq_band : eq_bands)
            eq_band.Reset();
        eq_mix_smoother.Reset();
    }

    Array<EqBand, k_num_layer_eq_bands> eq_bands;
    f32 eq_mix {};
    OnePoleLowPassFilter<f32> eq_mix_smoother {};
};

// Audio-thread data that voices use to control their sound.
struct VoiceProcessingController {
    f32 velocity_volume_modifier = 0.5f;
    u8 const layer_index;

    struct {
        bool on;
        param_values::LfoShape shape;
        param_values::LfoDestination dest;
        f32 amount;
        f32 time_hz;
    } lfo {};

    struct Loop {
        f32 start;
        f32 end;
        f32 crossfade_size;
    };
    Loop loop {};

    f32 tune_semitones = 1;
    f32 pan_pos = 0;

    f32 sv_filter_cutoff_linear = 0;
    f32 sv_filter_resonance = 0;
    sv_filter::Type filter_type = {};
    bool filter_on = false;

    bool vol_env_on = true;
    adsr::Params vol_env = {};

    adsr::Params fil_env = {};
    f32 fil_env_amount = 0;

    u7 key_range_low {};
    u7 key_range_low_fade {};
    u7 key_range_high {}; // Inclusive
    u7 key_range_high_fade {};

    param_values::LoopMode loop_mode {};
    bool reverse {};

    bool no_key_tracking = false;
};

struct VoicePool;

constexpr auto k_default_velocity_curve_points = Array {
    CurveMap::Point {0.0f, 0.3f, 0.0f},
    CurveMap::Point {1.0f, 1.0f, 0.0f},
};

struct LayerProcessor {
    LayerProcessor(u8 index, clap_host const& host, SharedLayerParams& shared_params)
        : host(host)
        , shared_params(shared_params)
        , index(index)
        , voice_controller({
              .layer_index = index,
          })
        , eq_bands() {
        velocity_curve_map.SetNewPoints(k_default_velocity_curve_points);
    }

    ~LayerProcessor() {
        if (auto sampled_inst =
                instrument.TryGet<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>())
            sampled_inst->Release();
    }

    String InstName() const {
        ASSERT(g_is_logical_main_thread);
        switch (instrument_id.tag) {
            case InstrumentType::WaveformSynth: {
                return k_waveform_type_names[ToInt(instrument_id.Get<WaveformType>())];
            }
            case InstrumentType::Sampler: {
                return instrument_id.Get<sample_lib::InstrumentId>().inst_name;
            }
            case InstrumentType::None: return "None"_s;
        }
        return {};
    }

    String InstTypeName() const {
        ASSERT(g_is_logical_main_thread);
        switch (instrument.tag) {
            case InstrumentType::WaveformSynth: return "Oscillator waveform"_s;
            case InstrumentType::Sampler: {
                auto const& s =
                    instrument.Get<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>()
                        ->instrument;
                if (s.regions.size == 0) return "Empty"_s;
                if (s.regions.size == 1) return "Single sample"_s;
                return "Multisample"_s;
            }
            case InstrumentType::None: return "None"_s;
        }
    }

    bool UsesTimbreLayering() const {
        ASSERT(g_is_logical_main_thread);
        switch (instrument.tag) {
            case InstrumentType::WaveformSynth: return false;
            case InstrumentType::Sampler: {
                auto const& s =
                    instrument.Get<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>()
                        ->instrument;
                return s.uses_timbre_layering;
            }
            case InstrumentType::None: return false;
        }
    }

    bool VolumeEnvelopeIsOn(Parameters const& params) {
        return params.BoolValue(index, LayerParamIndex::VolEnvOn) ||
               instrument.tag == InstrumentType::WaveformSynth;
    }

    Optional<sample_lib::LibraryIdRef> LibId() const {
        ASSERT(g_is_logical_main_thread);
        if (auto sampled_inst = instrument.TryGetFromTag<InstrumentType::Sampler>())
            return (*sampled_inst)->instrument.library.id;
        return k_nullopt;
    }

    param_values::VelocityMappingMode GetVelocityMode(Parameters const& params) const {
        return params.IntValue<param_values::VelocityMappingMode>(index, LayerParamIndex::VelocityMapping);
    }

    clap_host const& host;
    SharedLayerParams& shared_params;

    u8 const index;
    VoiceProcessingController voice_controller;

    Array<Array<u8, sample_lib::k_max_round_robin_sequence_groups>, ToInt(sample_lib::TriggerEvent::Count)>
        rr_pos = {};

    Instrument instrument {InstrumentType::None};
    InstrumentId instrument_id {InstrumentType::None};

    InstrumentUnwrapped audio_thread_inst = InstrumentType::None;

    // Encodes possible instruments into a single atomic u64. We use the fact that the pointer's value must be
    // aligned to the type they point to, and therefore we can use unaligned numbers to represent other
    // things.
    struct DesiredInst {
        static constexpr u64 k_consumed = 1;
        void Set(WaveformType w) { value.Store(ValForWaveform(w), StoreMemoryOrder::Release); }
        void Set(sample_lib::LoadedInstrument const* i) {
            value.Store((uintptr)i, StoreMemoryOrder::Release);
        }
        void SetNone() { value.Store(0, StoreMemoryOrder::Release); }
        Optional<InstrumentUnwrapped> Consume() {
            auto v = value.Exchange(k_consumed, RmwMemoryOrder::Relaxed);
            if (v == k_consumed) return k_nullopt;
            if (v == 0) return InstrumentType::None;
            for (auto const w : Range((u64)WaveformType::Count))
                if (v == ValForWaveform((WaveformType)w)) return (WaveformType)w;
            return (sample_lib::LoadedInstrument const*)v;
        }
        static constexpr u64 ValForWaveform(WaveformType w) {
            auto const v = 1 + (alignof(sample_lib::LoadedInstrument) * ((u64)w + 1));
            ASSERT_HOT(v % alignof(sample_lib::LoadedInstrument) != 0, "needs to be an invalid ptr");
            return v;
        }
        bool IsConsumed() const { return value.Load(LoadMemoryOrder::Acquire) == k_consumed; }

        Atomic<u64> value {0};
    };

    DesiredInst desired_inst {};

    f32 gain = 1;
    f32 mute_solo_gain = 1;
    OnePoleLowPassFilter<f32> gain_smoother = {};

    int midi_transpose = 0;
    f32 tune_semitone = 0;
    f32 tune_cents = 0;
    f32 pitch_bend_range_semitone = 0;
    f32 sample_offset_01 = 0;

    bool monophonic {};
    bool vol_env_on_param = true;

    param_values::LfoRestartMode lfo_restart_mode {};
    param_values::LfoSyncedRate lfo_synced_time {};
    f32 lfo_unsynced_hz {};
    bool lfo_is_synced {};

    EqBands eq_bands;

    int num_velocity_regions = 1;
    Bitset<4> active_velocity_regions {};
    CurveMap velocity_curve_map = {};

    StereoPeakMeter peak_meter = {};

    VolumeFade inst_change_fade {};
};

void SetSilent(LayerProcessor& layer, bool state);
bool ChangeInstrumentIfNeededAndReset(LayerProcessor& layer, VoicePool& voice_pool);
void PrepareToPlay(LayerProcessor& layer, AudioProcessingContext const& context);

struct LayerProcessResult {
    bool instrument_swapped;
    Optional<Span<f32x2>> output;
};

void ProcessLayerChanges(LayerProcessor& layer,
                         AudioProcessingContext const& context,
                         ProcessBlockChanges changes,
                         VoicePool& voice_pool);

LayerProcessResult ProcessLayer(LayerProcessor& layer,
                                AudioProcessingContext const& context,
                                VoicePool& voice_pool,
                                u32 num_frames,
                                bool start_fade_out);

void ResetLayerAudioProcessing(LayerProcessor& layer);
