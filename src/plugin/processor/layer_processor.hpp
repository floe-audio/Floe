// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/instrument.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "atomic_bitset.hpp"
#include "clap/host.h"
#include "param.hpp"
#include "processing_utils/adsr.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/curve_map.hpp"
#include "processing_utils/filters.hpp"
#include "processing_utils/midi.hpp"
#include "processing_utils/peak_meter.hpp"
#include "processing_utils/synced_timings.hpp"
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

    param_values::PlayMode play_mode {};

    struct {
        f32 speed {};
        f32 position {};
        f32 density {};
        f32 length_ms {};
        f32 spread {};
        f32 smoothing {};
        f32 random_pan {};
        f32 random_detune {};
        f32 random_direction {};
        f32 harmony {};
        HarmonyIntervalsBitset harmony_intervals {};
    } granular {};

    bool no_key_tracking = false;

    f32 sample_offset_01 = 0;
};

struct VoicePool;

// Cross-thread layout:
//   - `steps` is shared. Each ArpStep is 8 bytes so Atomic<ArpStep> is lock-free; either thread may
//     load/store any element. GUI writes user edits; audio reads on each trigger and Load/modify/Store
//     `step.note` while recording.
//   - `current_step_for_gui`, `recording`, `effectively_on_for_gui`, `resolved_rate_for_gui` are
//     audio<->GUI atomics.
//   - `audio` holds everything the audio thread mutates internally (timing, cached params, slice cache).
//     Only the audio thread reads or writes inside `audio`. The GUI must use the published atomics or
//     read directly from params/instrument.
struct ArpeggiatorState {
    // Shared GUI <-> audio. GUI writes when user edits steps. Audio reads on trigger and
    // Load/modify/Stores step.note during recording.
    Array<Atomic<ArpStep>, k_arp_max_steps> steps {};

    // Written by audio thread, read by GUI. k_arp_max_steps means not playing/recording.
    Atomic<u32> current_step_for_gui {k_arp_max_steps};

    // Written by GUI to request recording (Fixed mode only); cleared by audio when recording finishes.
    Atomic<bool> recording {false};

    // Audio publishes (slice_mode || on). GUI reads via EffectivelyOnForGui().
    Atomic<bool> effectively_on_for_gui {false};

    // Audio publishes the effective rate (= user rate, or auto-resolved in slice mode). GUI reads to
    // display the active rate.
    Atomic<SyncedTimes> resolved_rate_for_gui {SyncedTimes::_1_8};

    // Audio-thread-only state. No atomicity: only the audio thread touches anything inside.
    struct AudioOnly {
        Array<Bitset<128>, 16> last_triggered_notes {};

        u32 frames_until_next_step {};
        u32 frames_into_current_step {};
        u32 frames_per_step {1};
        u32 current_step {};
        bool any_notes_held {};
        u32 gate_off_frame {}; // frames into current step at which to release notes (0 = no pending release)

        // Cached parameter values, refreshed in ProcessLayerChanges() from changed_params.
        bool on {};
        param_values::ArpMode type {};
        param_values::ArpNoteOrder note_order {};
        param_values::ArpTriggerMode trigger_mode {};
        SyncedTimes user_rate {SyncedTimes::_1_8}; // From ArpRate param
        SyncedTimes rate {SyncedTimes::_1_8}; // Effective rate (= user_rate, or auto-resolved in slice mode)
        bool auto_rate {};
        u32 length {8};
        f32 humanise {};

        // Slice-mode cache, set when the instrument changes. The user's `steps` are unaffected; tie is
        // derived from step_to_slice_index, and interval/note are zeroed by the audio thread.
        bool slice_mode {};
        u32 slice_length {};
        Array<u8, k_arp_max_steps> step_to_slice_index {};

        // Used to detect a GUI-initiated recording false->true transition so audio can reset
        // current_step itself (avoiding a cross-thread write race on it).
        bool was_recording_last_block {};

        bool EffectivelyOn() const { return slice_mode || on; }
    } audio;

    // GUI accessor: reads the published atomic. Audio code should use `audio.EffectivelyOn()` directly.
    bool EffectivelyOnForGui() const { return effectively_on_for_gui.Load(LoadMemoryOrder::Relaxed); }

    // Clears audio-thread playback timing (not user's `steps`). Audio thread only.
    void ResetAudioPlayback() {
        audio.last_triggered_notes = {};
        audio.frames_until_next_step = 0;
        audio.frames_into_current_step = 0;
        audio.current_step = 0;
        audio.any_notes_held = false;
        current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
    }
};

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
        , lfo_shape({
              .current_idx = ParamIndexFromLayerParamIndex(index, LayerParamIndex::LfoShape),
              .legacies = {{{
                  .idx = ParamIndexFromLayerParamIndex(index, LayerParamIndex::LegacyLfoShape),
                  .remap_table = param_values::k_legacy_lfo_shape_to_current,
              }}},
          })
        , lfo_dest({
              .current_idx = ParamIndexFromLayerParamIndex(index, LayerParamIndex::LfoDestination),
              .legacies = {{{
                  .idx = ParamIndexFromLayerParamIndex(index, LayerParamIndex::LegacyLfoDestination),
                  .remap_table = param_values::k_legacy_lfo_destination_to_current,
              }}},
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
                auto const& id = instrument_id.GetFromTag<InstrumentType::Sampler>();

                // Try to get the name from the loaded instrument if we can.
                if (auto const& s = instrument.TryGetFromTag<InstrumentType::Sampler>();
                    s && *s && (*s)->instrument.library.id == id.library && (*s)->instrument.id == id.inst_id)
                    return (*s)->instrument.name;

                return id.inst_id;
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
                if (s.regions.size == 1 && s.regions[0].slices.size) return "Sliced"_s;
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

    Optional<sample_lib::LibraryId> LibId() const {
        ASSERT(g_is_logical_main_thread);
        if (auto sampled_inst = instrument.TryGetFromTag<InstrumentType::Sampler>())
            return (*sampled_inst)->instrument.library.id;
        return k_nullopt;
    }

    param_values::VelocityMappingMode GetVelocityMode(Parameters const& params) const {
        return params.IntValue<param_values::VelocityMappingMode>(index,
                                                                  LayerParamIndex::LegacyVelocityMapping);
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
    param_values::MonophonicMode monophonic_mode {};
    bool monophonic_retrigger_legacy {}; // Legacy
    bool monophonic_latch {};

    bool vol_env_on_param = true;

    param_values::LfoRestartMode lfo_restart_mode {};
    param_values::LfoSyncedRate lfo_synced_time {};
    f32 lfo_unsynced_hz {};
    bool lfo_is_synced {};
    EnumParamWithLegacies<param_values::LfoShape> lfo_shape {};
    EnumParamWithLegacies<param_values::LfoDestination> lfo_dest {};

    EqBands eq_bands;

    ArpeggiatorState arp_state {};

    Optional<u32> pending_slice_start_frame {};
    Optional<u32> pending_slice_end_frame {};

    int num_velocity_regions = 1;
    Bitset<4> active_velocity_regions {};
    CurveMap velocity_curve_map = {};
    AtomicBitset<k_num_harmony_interval_bits> harmony_intervals {};

    StereoPeakMeter peak_meter = {};

    VolumeFade inst_change_fade {};
};

void SetSilent(LayerProcessor& layer, bool state);
bool ChangeInstrumentIfNeededAndReset(LayerProcessor& layer,
                                      VoicePool& voice_pool,
                                      AudioProcessingContext const& context);
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

void ProcessLayerPreVoices(LayerProcessor& layer,
                           AudioProcessingContext const& context,
                           VoicePool& voice_pool,
                           u32 num_frames);

void LayerApplyNewState(LayerProcessor& layer, StateSnapshot const& state, StateSource source);
