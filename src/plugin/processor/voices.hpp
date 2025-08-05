// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"
#include "utils/thread_extra/atomic_swap_buffer.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/state/instrument.hpp"

#include "processing_utils/adsr.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/filters.hpp"
#include "processing_utils/lfo.hpp"
#include "processing_utils/midi.hpp"
#include "processing_utils/volume_fade.hpp"
#include "sample_processing.hpp"

constexpr u32 k_max_num_active_voices = 256;
constexpr u32 k_num_voices = 280;
constexpr u32 k_max_num_voice_sound_sources = 4;
constexpr f32 k_erroneous_sample_value = 1000.0f;

struct VoiceProcessingController;

struct VoiceSoundSource {
    NON_COPYABLE(VoiceSoundSource);
    VoiceSoundSource() = default;

    bool is_active {false};

    f64 pitch_ratio = 1;
    OnePoleLowPassFilter<f64> pitch_ratio_smoother = {};
    f64 pitch_ratio_mod = 0;
    f64 pos = 0;
    f32 amp = 1;

    struct SampleSource {
        sample_lib::Region const* region = nullptr;
        AudioData const* data = nullptr;
        f32 xfade_vol = 1;
        OnePoleLowPassFilter<f32> xfade_vol_smoother = {};
        u32 loop_and_reverse_flags {};
        Optional<BoundsCheckedLoop> loop {};
    };

    using SourceData = TaggedUnion<InstrumentType,
                                   TypeAndTag<SampleSource, InstrumentType::Sampler>,
                                   TypeAndTag<WaveformType, InstrumentType::WaveformSynth>>;

    SourceData source_data = InstrumentType::None;
};

struct VoicePool;

struct Voice {
    NON_COPYABLE_AND_MOVEABLE(Voice);

    Voice(VoicePool& p) : pool(p) {}

    static constexpr int k_fade_out_samples_max = 64;
    static constexpr int k_filter_fade_in_samples_max = 64;

    VoiceProcessingController* controller = {};
    u64 time_started = 0;
    u16 id {};
    u32 frames_before_starting {};
    f32 current_gain {};

    bool is_active {false};
    bool written_to_buffer_this_block = false;

    u8 num_active_voice_samples = 0;
    Array<VoiceSoundSource, k_max_num_voice_sound_sources> sound_sources {};

    VoicePool& pool;

    u16 index = 0;

    sv_filter::CachedHelpers filter_coeffs = {};
    sv_filter::Data<f32x2> filters = {};
    OnePoleLowPassFilter<f32> filter_mix_smoother = {};
    OnePoleLowPassFilter<f32> filter_linear_cutoff_smoother = {};
    OnePoleLowPassFilter<f32> filter_resonance_smoother = {};

    //
    u7 note_num = 0;
    MidiChannelNote midi_key_trigger = {};

    LFO lfo = {};

    OnePoleLowPassFilter<f32x2> gain_smoother;

    VolumeFade volume_fade;
    adsr::Processor vol_env = {};
    adsr::Processor fil_env = {};
    f32 aftertouch_multiplier = 1;
    bool disable_vol_env = false;
};

struct VoiceEnvelopeMarkerForGui {
    u8 on : 1 {};
    u8 layer_index : 7 {};
    adsr::State state {};
    u16 pos {};
    u16 sustain_level {};
    u16 id {};
};

struct VoiceWaveformMarkerForGui {
    u32 layer_index {};
    u16 position {};
    u16 intensity {};
};

template <typename Type>
concept ShouldSkipVoiceFunction = requires(Type function, Voice const& v) {
    { function(v) } -> Same<bool>;
};

struct VoicePool {
    template <bool k_early_out_if_none_active, ShouldSkipVoiceFunction Function>
    auto EnumerateVoices(Function&& should_skip_voice) {
        struct IterableWrapper {
            struct Iterator {
                constexpr bool operator!=(Iterator const& other) const { return index != other.index; }
                constexpr void operator++() {
                    ++index;
                    while (index < k_num_voices && wrapper.should_skip_voice(wrapper.pool.voices[index]))
                        ++index;
                }
                constexpr Voice& operator*() const { return wrapper.pool.voices[index]; }

                IterableWrapper const& wrapper;
                usize index;
            };

            constexpr Iterator begin() {
                if (k_early_out_if_none_active && pool.num_active_voices.Load(LoadMemoryOrder::Relaxed) == 0)
                    return end();

                usize i = 0;
                for (; i < k_num_voices; ++i)
                    if (!should_skip_voice(pool.voices[i])) break;
                return {*this, i};
            }

            constexpr Iterator end() { return {*this, k_num_voices}; }

            VoicePool& pool;
            Function should_skip_voice;
        };

        return IterableWrapper {*this, Forward<Function>(should_skip_voice)};
    }

    auto EnumerateActiveVoices() {
        return EnumerateVoices<true>([](Voice const& v) { return !v.is_active; });
    }

    auto EnumerateActiveLayerVoices(VoiceProcessingController const& controller) {
        return EnumerateVoices<true>(
            [&controller](Voice const& v) { return !v.is_active || v.controller != &controller; });
    }

    template <typename Function>
    void ForActiveSamplesInActiveVoices(Function&& f) {
        for (auto& v : voices) {
            if (v.is_active) {
                for (auto& s : v.sound_sources)
                    if (s.is_active) f(v, s);
            }
        }
    }

    void PrepareToPlay();
    void EndAllVoicesInstantly();

    u64 voice_start_counter = 0;
    u16 voice_id_counter = 0;
    Atomic<u32> num_active_voices = 0;
    Array<Voice, k_num_voices> voices {MakeInitialisedArray<Voice, k_num_voices>(*this)};
    static_assert(k_block_size_max % 16 == 0, "k_block_size_max must be a multiple of 16");
    alignas(16) Array<Array<f32, k_block_size_max * 2>, k_num_voices> buffer_pool {};

    AtomicSwapBuffer<Array<VoiceWaveformMarkerForGui, k_num_voices>, true> voice_waveform_markers_for_gui {};
    AtomicSwapBuffer<Array<VoiceEnvelopeMarkerForGui, k_num_voices>, true> voice_vol_env_markers_for_gui {};
    AtomicSwapBuffer<Array<VoiceEnvelopeMarkerForGui, k_num_voices>, true> voice_fil_env_markers_for_gui {};
    Array<Atomic<s16>, 128> voices_per_midi_note_for_gui {};
    Array<Atomic<f32>, k_num_layers> last_velocity = {};

    unsigned int random_seed = (unsigned)NanosecondsSinceEpoch();

    AudioProcessingContext const* audio_processing_context = nullptr; // temp for thread pool

    struct {
        u32 num_frames = 0;
    } multithread_processing;
};

inline void EndVoiceInstantly(Voice& voice) {
    ASSERT(voice.is_active);
    voice.pool.num_active_voices.FetchSub(1, RmwMemoryOrder::Relaxed);
    voice.pool.voices_per_midi_note_for_gui[voice.midi_key_trigger.note].FetchSub(1, RmwMemoryOrder::Relaxed);
    voice.is_active = false;
}
void EndVoice(Voice& voice);

void UpdateLFOWaveform(Voice& v);
void SetVoicePitch(Voice& v, f32 pitch, f32 sample_rate);
void UpdateLFOTime(Voice& v, f32 sample_rate);
void SetPan(Voice& v, f32 pan_pos);
void UpdateLoopInfo(Voice& v);
void UpdateXfade(Voice& v, f32 knob_pos_01, bool hard_set);

struct VoiceStartParams {
    struct SamplerParams {
        struct Region {
            sample_lib::Region const& region;
            AudioData const& audio_data;
            f32 amp {};
        };

        f32 initial_sample_offset_01 {};
        f32 initial_timbre_param_value_01 {};
        DynamicArrayBounded<Region, k_max_num_voice_sound_sources> voice_sample_params {};
    };

    struct WaveformParams {
        WaveformType type;
        f32 amp;
    };

    using Params = TaggedUnion<InstrumentType,
                               TypeAndTag<SamplerParams, InstrumentType::Sampler>,
                               TypeAndTag<WaveformParams, InstrumentType::WaveformSynth>>;

    f32 initial_pitch;
    MidiChannelNote midi_key_trigger;
    u7 note_num;
    f32 note_vel;
    unsigned int lfo_start_phase;
    u32 num_frames_before_starting;
    Params params;
    bool disable_vol_env;
};

void StartVoice(VoicePool& pool,
                VoiceProcessingController& voice_controller,
                VoiceStartParams const& params,
                AudioProcessingContext const& audio_processing_context);

void NoteOff(VoicePool& pool, VoiceProcessingController& controller, MidiChannelNote note);

Array<Span<f32>, k_num_layers>
ProcessVoices(VoicePool& pool, u32 num_frames, AudioProcessingContext const& context);

void OnThreadPoolExec(VoicePool& pool, u32 task_index);

void Reset(VoicePool& pool);
