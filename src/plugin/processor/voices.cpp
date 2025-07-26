// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "voices.hpp"

#include <clap/ext/thread-pool.h>

#include "foundation/foundation.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "layer_processor.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processor/effect_stereo_widen.hpp"

static void FadeOutVoicesToEnsureMaxActive(VoicePool& pool, AudioProcessingContext const& context) {
    if (pool.num_active_voices.Load(LoadMemoryOrder::Relaxed) <= k_max_num_active_voices) return;

    auto time_started = LargestRepresentableValue<u64>();
    Voice* oldest_active_voice = nullptr;
    for (auto& v : pool.EnumerateActiveVoices()) {
        if (v.time_started < time_started && !v.volume_fade.IsFadingOut()) {
            time_started = v.time_started;
            oldest_active_voice = &v;
        }
    }

    // It's possible that all the voices are fading out already.
    if (!oldest_active_voice) return;

    // Fade out this voice.
    oldest_active_voice->volume_fade.SetAsFadeOut(context.sample_rate);
}

static Voice& FindVoice(VoicePool& pool, AudioProcessingContext const& context) {
    FadeOutVoicesToEnsureMaxActive(pool, context);

    // Easy case: find an inactive voice.
    for (auto& v : pool.voices)
        if (!v.is_active) return v;

    // All the voices are active, so we do a simple algorithm to find an appropriate voice to steal: quiet and
    // old.

    // Generate an array of the voice indexes, sorted by age. Where the first index in the array is an index
    // to the oldest voice.
    Array<u16, k_num_voices> old_index_to_index;
    for (auto [i, index] : Enumerate<u16>(old_index_to_index))
        index = i;
    Sort(old_index_to_index,
         [&voices = pool.voices](u16 a, u16 b) { return voices[a].time_started < voices[b].time_started; });

    ASSERT(pool.voices[old_index_to_index[0]].time_started <=
           pool.voices[old_index_to_index[1]].time_started);

    // We loop through the oldest 1/4 of the voices and find the quietest one to steal - this will hopefully
    // have the least obvious audible effect.
    auto quietest_gain = pool.voices[old_index_to_index[0]].current_gain;
    u16 quietest_voice_index = 0;
    for (auto const old_index : Range(1uz, old_index_to_index.size / 4)) {
        auto const voice_index = old_index_to_index[old_index];
        auto& v = pool.voices[voice_index];
        if (v.current_gain < quietest_gain) {
            quietest_gain = v.current_gain;
            quietest_voice_index = voice_index;
        }
    }

    auto& result = pool.voices[quietest_voice_index];
    ASSERT(result.is_active);

    EndVoiceInstantly(result);
    return result;
}

void UpdateLFOWaveform(Voice& v) {
    LFO::Waveform waveform {};
    switch (v.controller->lfo.shape) {
        case param_values::LfoShape::Sine: waveform = LFO::Waveform::Sine; break;
        case param_values::LfoShape::Triangle: waveform = LFO::Waveform::Triangle; break;
        case param_values::LfoShape::Sawtooth: waveform = LFO::Waveform::Sawtooth; break;
        case param_values::LfoShape::Square: waveform = LFO::Waveform::Square; break;
        case param_values::LfoShape::Count: PanicIfReached(); break;
    }
    if (waveform != v.lfo.waveform) v.lfo.SetWaveform(waveform);
}

void UpdateLFOTime(Voice& v, f32 sample_rate) { v.lfo.SetRate(sample_rate, v.controller->lfo.time_hz); }

static f64 MidiNoteToFrequency(f64 note) { return 440.0 * Exp2((note - 69.0) / 12.0); }

inline f64 CalculatePitchRatio(int note, VoiceSoundSource const& s, f32 pitch_semitones, f32 sample_rate) {
    switch (s.source_data.tag) {
        case InstrumentType::None: {
            PanicIfReached();
            break;
        }
        case InstrumentType::Sampler: {
            auto const& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();
            auto const source_root_note = sampler.region->root_key;
            auto const source_sample_rate = (f64)sampler.data->sample_rate;
            auto const pitch_delta =
                (((f64)note + (f64)pitch_semitones + ((f64)sampler.region->audio_props.tune_cents / 100.0)) -
                 source_root_note) /
                12.0;
            auto const exp = Exp2(pitch_delta);
            auto const result = exp * source_sample_rate / (f64)sample_rate;
            return result;
        }
        case InstrumentType::WaveformSynth: {
            auto const freq = MidiNoteToFrequency((f64)note + (f64)pitch_semitones);
            auto const result = freq / (f64)sample_rate;
            return result;
        }
    }

    return 1;
}

static int RootKey(Voice const& v, VoiceSoundSource const& s) {
    int k = v.note_num;
    if (s.source_data.tag == InstrumentType::Sampler) {
        auto const& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();
        switch (sampler.region->playback.keytrack_requirement) {
            case sample_lib::KeytrackRequirement::Default:
                if (v.controller->no_key_tracking) k = sampler.region->root_key;
                break;
            case sample_lib::KeytrackRequirement::Always: break;
            case sample_lib::KeytrackRequirement::Never: k = sampler.region->root_key; break;
            case sample_lib::KeytrackRequirement::Count: PanicIfReached(); break;
        }
    }
    return k;
}

void SetVoicePitch(Voice& v, f32 pitch_semitones, f32 sample_rate) {
    for (auto& s : v.sound_sources) {
        if (!s.is_active) continue;

        s.pitch_ratio = CalculatePitchRatio(RootKey(v, s), s, pitch_semitones, sample_rate);
    }
}

void UpdateXfade(Voice& v, f32 knob_pos_01, bool hard_set) {
    auto set_xfade_smoother = [&](VoiceSoundSource::SampleSource& s, f32 val) {
        ASSERT(val >= 0 && val <= 1);
        s.xfade_vol = val;
        if (hard_set) s.xfade_vol_smoother.Reset();
    };

    VoiceSoundSource::SampleSource* voice_sample_1 = nullptr;
    VoiceSoundSource::SampleSource* voice_sample_2 = nullptr;

    auto const knob_pos = knob_pos_01 * 99;

    for (auto& s : v.sound_sources) {
        if (!s.is_active) continue;
        if (s.source_data.tag != InstrumentType::Sampler) continue;
        auto& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();

        if (auto const r = sampler.region->timbre_layering.layer_range) {
            if (knob_pos >= r->start && knob_pos < r->end) {
                // NOTE: we don't handle the case if there is more than 2 overlapping regions. We should
                // ensure we can't get this point of the code with that being the case.
                if (!voice_sample_1)
                    voice_sample_1 = &sampler;
                else
                    voice_sample_2 = &sampler;
            } else {
                set_xfade_smoother(sampler, 0);
            }
        } else {
            set_xfade_smoother(sampler, 1);
        }
    }

    if (voice_sample_1 && !voice_sample_2)
        set_xfade_smoother(*voice_sample_1, 1);
    else if (voice_sample_1 && voice_sample_2) {
        auto const& r1 = *voice_sample_1->region->timbre_layering.layer_range;
        auto const& r2 = *voice_sample_2->region->timbre_layering.layer_range;
        if (r2.start < r1.start) Swap(voice_sample_1, voice_sample_2);
        auto const overlap_low = r2.start;
        auto const overlap_high = r1.end;
        ASSERT(overlap_high > overlap_low);
        auto const overlap_size = overlap_high - overlap_low;
        auto const pos = (knob_pos - overlap_low) / (f32)overlap_size;
        ASSERT(pos >= 0 && pos <= 1);
        set_xfade_smoother(*voice_sample_1, trig_table_lookup::SinTurns((1 - pos) * 0.25f));
        set_xfade_smoother(*voice_sample_2, trig_table_lookup::SinTurns(pos * 0.25f));
    }
}

static Optional<BoundsCheckedLoop> ConfigureLoop(param_values::LoopMode desired_mode,
                                                 sample_lib::Region::Loop const& region_loop,
                                                 u32 num_frames,
                                                 VoiceProcessingController::Loop const& custom_loop) {
    if (region_loop.builtin_loop) {
        auto result = CreateBoundsCheckedLoop(*region_loop.builtin_loop, num_frames);

        switch (desired_mode) {
            case param_values::LoopMode::InstrumentDefault: return result;
            case param_values::LoopMode::BuiltInLoopStandard:
                if (!region_loop.builtin_loop->lock_mode) result.mode = sample_lib::LoopMode::Standard;
                return result;
            case param_values::LoopMode::BuiltInLoopPingPong:
                if (!region_loop.builtin_loop->lock_mode) result.mode = sample_lib::LoopMode::PingPong;
                return result;
            case param_values::LoopMode::None:
                if (region_loop.loop_requirement == sample_lib::LoopRequirement::AlwaysLoop) return result;
                return k_nullopt;
            case param_values::LoopMode::Standard:
            case param_values::LoopMode::PingPong: {
                if (region_loop.builtin_loop->lock_loop_points) return result;
                break;
            }
            case param_values::LoopMode::Count: PanicIfReached(); break;
        }
    }

    switch (desired_mode) {
        case param_values::LoopMode::InstrumentDefault:
        case param_values::LoopMode::BuiltInLoopStandard:
        case param_values::LoopMode::BuiltInLoopPingPong:
        case param_values::LoopMode::None: {
            if (region_loop.loop_requirement == sample_lib::LoopRequirement::AlwaysLoop) {
                // This is a legacy option: we have to enforce some kind of looping behaviour.
                auto const n = (f32)num_frames;
                return CreateBoundsCheckedLoop(
                    {
                        .start_frame = 0,
                        .end_frame = (s64)(0.9f * n),
                        .crossfade_frames = (u32)(0.1f * n),
                        .mode = sample_lib::LoopMode::Standard,
                    },
                    num_frames);
            }
            return k_nullopt;
        }
        case param_values::LoopMode::Standard:
        case param_values::LoopMode::PingPong: {
            auto const n = (f32)num_frames;

            return CreateBoundsCheckedLoop(
                {
                    .start_frame = (s64)(custom_loop.start * n),
                    .end_frame = (s64)(custom_loop.end * n),
                    .crossfade_frames = (u32)(custom_loop.crossfade_size * n),
                    .mode = (desired_mode == param_values::LoopMode::PingPong)
                                ? sample_lib::LoopMode::PingPong
                                : sample_lib::LoopMode::Standard,
                },
                num_frames);

            break;
        }
        case param_values::LoopMode::Count: break;
    }

    return k_nullopt;
}

void UpdateLoopInfo(Voice& v) {
    for (auto& s : v.sound_sources) {
        if (!s.is_active) continue;
        if (s.source_data.tag != InstrumentType::Sampler) continue;
        auto& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();
        if (sampler.region->trigger.trigger_event == sample_lib::TriggerEvent::NoteOff) continue;

        sampler.loop = v.controller->vol_env_on ? ConfigureLoop(v.controller->loop_mode,
                                                                sampler.region->loop,
                                                                sampler.data->num_frames,
                                                                v.controller->loop)
                                                : k_nullopt;

        sampler.loop_and_reverse_flags = 0;
        if (v.controller->reverse) sampler.loop_and_reverse_flags = loop_and_reverse_flags::CurrentlyReversed;
        if (sampler.loop) {
            sampler.loop_and_reverse_flags =
                loop_and_reverse_flags::CorrectLoopFlagsIfNeeded(sampler.loop_and_reverse_flags,
                                                                 *sampler.loop,
                                                                 s.pos);
        }
    }
}

// These functions are from JUCE.
//
// Copyright (c) Raw Material Software Limited
// SPDX-License-Identifier: AGPL-3.0-only
//
// JUCE Pade approximation of sin valid from -PI to PI with max error of 1e-5 and average error of
// 5e-7
inline auto FastSin(ScalarOrVectorFloat auto x) {
    using T = Conditional<Vector<decltype(x)>, UnderlyingTypeOfVec<decltype(x)>, decltype(x)>;
    auto const x2 = x * x;
    auto const numerator =
        -x * (-T(11511339840) + x2 * (T(1640635920) + x2 * (-T(52785432) + x2 * T(479249))));
    auto const denominator = T(11511339840) + (x2 * (T(277920720) + x2 * (T(3177720) + x2 * T(18361))));
    return numerator / denominator;
}

// JUCE Pade approximation of cos valid from -PI to PI with max error of 1e-5 and average error of
// 5e-7
inline auto FastCos(ScalarOrVectorFloat auto x) {
    using T = Conditional<Vector<decltype(x)>, UnderlyingTypeOfVec<decltype(x)>, decltype(x)>;
    auto const x2 = x * x;
    auto const numerator = -(-T(39251520) + (x2 * (T(18471600) + x2 * (-1075032 + 14615 * x2))));
    auto const denominator = T(39251520) + (x2 * (1154160 + x2 * (16632 + x2 * 127)));
    return numerator / denominator;
}

// SIMD version where 2 pan positions are processed at once.
// The result is a vector of 4 floats: {left1, right1, left2, right2}.
inline f32x4 EqualPanGains2(f32x2 pan_pos) {
    auto const angle = pan_pos * (k_pi<f32> * 0.25f);
    auto const sinx = FastSin(angle);
    auto const cosx = FastCos(angle);

    constexpr auto k_root_2_over_2 = k_sqrt_two<> / 2;
    auto const left = k_root_2_over_2 * (cosx - sinx);
    auto const right = k_root_2_over_2 * (cosx + sinx);
    ASSERT_HOT(All(left >= -0.00001f) && All(right >= -0.00001f));

    return __builtin_shufflevector(left, right, 0, 2, 1, 3);
}

void StartVoice(VoicePool& pool,
                VoiceProcessingController& voice_controller,
                VoiceStartParams const& params,
                AudioProcessingContext const& audio_processing_state) {
    auto& voice = FindVoice(pool, audio_processing_state);

    auto const sample_rate = audio_processing_state.sample_rate;
    ASSERT(sample_rate != 0);

    voice.controller = &voice_controller;
    voice.lfo.phase = params.lfo_start_phase;

    UpdateLFOWaveform(voice);
    UpdateLFOTime(voice, audio_processing_state.sample_rate);

    voice.volume_fade.ForceSetAsFadeIn(sample_rate);
    voice.vol_env.Reset();
    voice.vol_env.Gate(true);
    voice.disable_vol_env = params.disable_vol_env;
    voice.fil_env.Reset();
    voice.fil_env.Gate(true);
    voice.time_started = voice.pool.voice_start_counter++;
    voice.id = voice.pool.voice_id_counter++;
    voice.midi_key_trigger = params.midi_key_trigger;
    voice.note_num = params.note_num;
    voice.frames_before_starting = params.num_frames_before_starting;
    voice.filters = {};
    voice.gain_smoother.Reset();
    voice.filter_linear_cutoff_smoother.Reset();
    voice.filter_mix_smoother.Reset();
    voice.filter_resonance_smoother.Reset();

    switch (params.params.tag) {
        case InstrumentType::None: {
            PanicIfReached();
            break;
        }
        case InstrumentType::Sampler: {
            auto const& sampler = params.params.Get<VoiceStartParams::SamplerParams>();
            voice.num_active_voice_samples = (u8)sampler.voice_sample_params.size;
            for (auto const i : Range(sampler.voice_sample_params.size)) {
                auto& s = voice.sound_sources[i];
                auto const& s_params = sampler.voice_sample_params[i];

                s.is_active = true;
                s.amp = s_params.amp * (f32)DbToAmpApprox((f64)s_params.region.audio_props.gain_db);

                s.source_data = VoiceSoundSource::SampleSource {};
                auto& s_sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();

                s_sampler.region = &s_params.region;
                s_sampler.data = &s_params.audio_data;
                s_sampler.loop = {};
                ASSERT(s_sampler.data != nullptr);

                s.pitch_ratio = CalculatePitchRatio(RootKey(voice, s), s, params.initial_pitch, sample_rate);
                s.pitch_ratio_smoother.Reset();

                auto const offs =
                    (f64)(sampler.initial_sample_offset_01 * ((f32)s_sampler.data->num_frames - 1)) +
                    s_params.region.audio_props.start_offset_frames;
                s.pos = offs;
                if (voice.controller->reverse) s.pos = (f64)(s_sampler.data->num_frames - Max(offs, 1.0));
            }
            for (u32 i = voice.num_active_voice_samples; i < k_max_num_voice_sound_sources; ++i)
                voice.sound_sources[i].is_active = false;

            UpdateLoopInfo(voice);
            UpdateXfade(voice, sampler.initial_timbre_param_value_01, true);
            break;
        }
        case InstrumentType::WaveformSynth: {
            auto const& waveform = params.params.Get<VoiceStartParams::WaveformParams>();
            voice.num_active_voice_samples = 1;
            for (u32 i = voice.num_active_voice_samples; i < k_max_num_voice_sound_sources; ++i)
                voice.sound_sources[i].is_active = false;

            auto& s = voice.sound_sources[0];
            s.is_active = true;
            s.amp = waveform.amp;
            s.pos = 0;
            s.source_data = waveform.type;
            s.pitch_ratio = CalculatePitchRatio(voice.note_num, s, params.initial_pitch, sample_rate);
            s.pitch_ratio_smoother.Reset();

            break;
        }
    }

    voice.is_active = true;
    voice.pool.num_active_voices.FetchAdd(1, RmwMemoryOrder::Relaxed);
    voice.pool.voices_per_midi_note_for_gui[voice.note_num].FetchAdd(1, RmwMemoryOrder::Relaxed);
    voice.pool.last_velocity[voice.controller->layer_index].Store(params.note_vel, StoreMemoryOrder::Relaxed);
}

void EndVoice(Voice& voice) {
    ASSERT(voice.is_active);
    voice.vol_env.Gate(false);
    voice.fil_env.Gate(false);
}

void VoicePool::EndAllVoicesInstantly() {
    for (auto& v : EnumerateActiveVoices())
        EndVoiceInstantly(v);
}

void VoicePool::PrepareToPlay() {
    decltype(Voice::index) index = 0;
    for (auto& v : voices)
        v.index = index++;
}

void NoteOff(VoicePool& pool, VoiceProcessingController& controller, MidiChannelNote note) {
    for (auto& v : pool.voices)
        if (v.is_active && v.midi_key_trigger == note && &controller == v.controller) EndVoice(v);
}

class VoiceProcessor {
  public:
    VoiceProcessor(Voice& voice, AudioProcessingContext const& audio_context)
        : m_filter_coeffs(voice.filter_coeffs)
        , m_filters(voice.filters)
        , m_audio_context(audio_context)
        , m_voice(voice)
        , m_buffer(m_voice.pool.buffer_pool[m_voice.index]) {}

    ~VoiceProcessor() {
        m_voice.filter_coeffs = m_filter_coeffs;
        m_voice.filters = m_filters;
    }

    bool Process(u32 num_frames) {
        ZoneNamedN(process, "Voice Process", true);
        u32 samples_written = 0;
        Span<f32> write_buffer = m_buffer;

        if (m_voice.frames_before_starting != 0) {
            auto const num_frames_to_remove = Min(num_frames, m_voice.frames_before_starting);
            auto const num_samples_to_remove = num_frames_to_remove * 2;
            ZeroMemory(write_buffer.SubSpan(0, num_samples_to_remove).ToByteSpan());
            write_buffer = write_buffer.SubSpan(num_samples_to_remove);
            samples_written = num_samples_to_remove;
            num_frames -= num_frames_to_remove;
            m_voice.frames_before_starting -= num_frames_to_remove;
        }

        m_frame_index = samples_written / 2;

        ZoneNamedN(chunk, "Voice Chunk", true);
        ZoneValueV(chunk, num_frames);

        FillLFOBuffer(num_frames);
        FillBufferWithSampleData(num_frames);

        auto num_valid_frames = ApplyGain(num_frames);
        ApplyFilter(num_valid_frames);

        auto const samples_to_write = num_valid_frames * 2;
        CheckSamplesAreValid(0, samples_to_write);
        samples_written += samples_to_write;

        if (num_valid_frames != num_frames || !m_voice.num_active_voice_samples) {
            write_buffer = write_buffer.SubSpan((usize)samples_to_write);
            // We can't do aligned zero because of frames_before_starting
            ZeroMemory(write_buffer.ToByteSpan());
            EndVoiceInstantly(m_voice);
            return samples_written != 0;
        }

        num_frames -= num_frames;
        m_frame_index += num_frames;

        m_voice.pool.voice_waveform_markers_for_gui.Write()[m_voice.index] = {
            .layer_index = (u8)m_voice.controller->layer_index,
            .position = (u16)(Clamp01(m_position_for_gui) * (f32)UINT16_MAX),
            .intensity = (u16)(Clamp01(m_voice.current_gain) * (f32)UINT16_MAX),
        };
        m_voice.pool.voice_vol_env_markers_for_gui.Write()[m_voice.index] = {
            .on = m_voice.controller->vol_env_on && !m_voice.disable_vol_env && !m_voice.vol_env.IsIdle(),
            .layer_index = (u8)m_voice.controller->layer_index,
            .state = m_voice.vol_env.state,
            .pos = (u16)(Clamp01(m_voice.vol_env.output) * (f32)UINT16_MAX),
            .sustain_level = (u16)(Clamp01(m_voice.controller->vol_env.sustain_amount) * (f32)UINT16_MAX),
            .id = m_voice.id,
        };
        m_voice.pool.voice_fil_env_markers_for_gui.Write()[m_voice.index] = {
            .on = m_voice.controller->fil_env_amount != 0 && !m_voice.fil_env.IsIdle(),
            .layer_index = (u8)m_voice.controller->layer_index,
            .state = m_voice.fil_env.state,
            .pos = (u16)(Clamp01(m_voice.fil_env.output) * (f32)UINT16_MAX),
            .sustain_level = (u16)(Clamp01(m_voice.controller->fil_env.sustain_amount) * (f32)UINT16_MAX),
            .id = m_voice.id,
        };

        m_voice.current_gain = 1;

        return samples_written != 0;
    }

  private:
    void CheckSamplesAreValid(usize buffer_pos, usize num) {
        ASSERT_HOT(buffer_pos + num <= m_buffer.size);
        for (usize i = buffer_pos; i < (buffer_pos + num); ++i)
            ASSERT_HOT(m_buffer[i] >= -k_erroneous_sample_value && m_buffer[i] <= k_erroneous_sample_value);
    }
    static void CheckSamplesAreValid(f32x4 samples) {
        ASSERT_HOT(All(samples >= -k_erroneous_sample_value && samples <= k_erroneous_sample_value));
    }

    bool HasPitchLfo() const {
        return m_voice.controller->lfo.on &&
               m_voice.controller->lfo.dest == param_values::LfoDestination::Pitch;
    }

    bool HasPanLfo() const {
        return m_voice.controller->lfo.on &&
               m_voice.controller->lfo.dest == param_values::LfoDestination::Pan;
    }

    bool HasFilterLfo() const {
        return m_voice.controller->lfo.on &&
               m_voice.controller->lfo.dest == param_values::LfoDestination::Filter;
    }

    bool HasVolumeLfo() const {
        return m_voice.controller->lfo.on &&
               m_voice.controller->lfo.dest == param_values::LfoDestination::Volume;
    }

    static u32 GetLastFrameInOddNumFrames(u32 const num_frames) {
        return ((num_frames % 2) != 0) ? (num_frames - 1) : UINT32_MAX;
    }

    void MultiplyVectorToBufferAtPos(usize const pos, f32x4 const& gain) {
        ASSERT_HOT(pos + 4 <= m_buffer.size);
        auto p = LoadUnalignedToType<f32x4>(&m_buffer[pos]);
        p *= gain;
        CheckSamplesAreValid(p);
        StoreToUnaligned(&m_buffer[pos], p);
    }

    void AddVectorToBufferAtPos(usize const pos, f32x4 const& addition) {
        ASSERT_HOT(pos + 4 <= m_buffer.size);
        auto p = LoadUnalignedToType<f32x4>(&m_buffer[pos]);
        p += addition;
        CheckSamplesAreValid(p);
        StoreToUnaligned(&m_buffer[pos], p);
    }

    void CopyVectorToBufferAtPos(usize const pos, f32x4 const& data) {
        ASSERT_HOT(pos + 4 <= m_buffer.size);
        CheckSamplesAreValid(data);
        StoreToUnaligned(&m_buffer[pos], data);
    }

    f64 GetPitchRatio(VoiceSoundSource& s, u32 frame) {
        auto pitch_ratio = s.pitch_ratio;
        if (HasPitchLfo()) {
            static constexpr f64 k_max_semitones = 1;
            auto const lfo_amp = (f64)m_voice.controller->lfo.amount;
            auto const pitch_addition_in_semitones =
                ((f64)m_lfo_amounts[(usize)frame] * lfo_amp * k_max_semitones);
            pitch_ratio *= Exp2(pitch_addition_in_semitones / 12.0);
        }
        return s.pitch_ratio_smoother.LowPass(pitch_ratio,
                                              (f64)m_audio_context.one_pole_smoothing_cutoff_0_2ms);
    }

    bool SampleGetAndInc(VoiceSoundSource& w, u32 frame, f32x2& out) {
        auto& sampler = w.source_data.Get<VoiceSoundSource::SampleSource>();
        out = SampleGetData(*sampler.data, sampler.loop, sampler.loop_and_reverse_flags, w.pos);
        if (!(sampler.loop_and_reverse_flags &
              (loop_and_reverse_flags::LoopedManyTimes | loop_and_reverse_flags::CurrentlyReversed))) {
            auto const pos = w.pos - sampler.region->audio_props.start_offset_frames;
            if (pos < sampler.region->audio_props.fade_in_frames) {
                auto const percent = pos / (f64)sampler.region->audio_props.fade_in_frames;
                // Quarter-sine fade in.
                auto const amount = trig_table_lookup::SinTurnsPositive((f32)percent * 0.25f);
                out *= amount;
            }
        }
        auto const pitch_ratio = GetPitchRatio(w, frame);
        return IncrementSamplePlaybackPos(sampler.loop,
                                          sampler.loop_and_reverse_flags,
                                          w.pos,
                                          pitch_ratio,
                                          (f64)sampler.data->num_frames);
    }

    bool SampleGetAndIncWithXFade(VoiceSoundSource& w, u32 frame, f32x2& out) {
        auto& sampler = w.source_data.Get<VoiceSoundSource::SampleSource>();
        bool sample_still_going = false;
        if (sampler.region->timbre_layering.layer_range) {
            if (auto const v =
                    sampler.xfade_vol_smoother.LowPass(sampler.xfade_vol,
                                                       m_audio_context.one_pole_smoothing_cutoff_10ms);
                v > 0.0001f) {
                sample_still_going = SampleGetAndInc(w, frame, out);
                out *= v;
            } else {
                auto const pitch_ratio1 = GetPitchRatio(w, frame);
                sample_still_going = IncrementSamplePlaybackPos(sampler.loop,
                                                                sampler.loop_and_reverse_flags,
                                                                w.pos,
                                                                pitch_ratio1,
                                                                (f64)sampler.data->num_frames);
            }
        } else {
            sample_still_going = SampleGetAndInc(w, frame, out);
        }
        return sample_still_going;
    }

    bool AddSampleDataOntoBuffer(VoiceSoundSource& w, u32 num_frames) {
        usize sample_pos = 0;
        for (u32 frame = 0; frame < num_frames; frame += 2) {
            f32x2 s1 {};
            f32x2 s2 {};

            bool sample_still_going = SampleGetAndIncWithXFade(w, frame, s1);

            auto const frame_p1 = frame + 1;
            if (sample_still_going && frame_p1 != num_frames)
                sample_still_going = SampleGetAndIncWithXFade(w, frame_p1, s2);

            // 's2' will be 0 if the second sample was not fetched so there is no harm in adding that too.
            auto v = __builtin_shufflevector(s1, s2, 0, 1, 2, 3);
            v *= w.amp;
            AddVectorToBufferAtPos(sample_pos, v);
            sample_pos += 4;

            if (!sample_still_going) return false;
        }
        return true;
    }

    void ConvertRandomNumsToWhiteNoiseInBuffer(u32 num_frames) {
        usize sample_pos = 0;
        f32x4 const randon_num_to_01_scale = 1.0f / (f32)0x7FFF;
        f32x4 const scale = 0.5f * 0.2f;
        for (u32 frame = 0; frame < num_frames; frame += 2) {
            auto buf = LoadAlignedToType<f32x4>(&m_buffer[sample_pos]);
            buf = ((buf * randon_num_to_01_scale) * 2 - 1) * scale;
            CheckSamplesAreValid(buf);
            StoreToAligned(&m_buffer[sample_pos], buf);
            sample_pos += 4;
        }
    }

    void FillBufferWithMonoWhiteNoise(u32 num_frames) {
        usize sample_pos = 0;
        for (u32 frame = 0; frame < num_frames; frame++) {
            auto const rand = (f32)FastRand(m_voice.pool.random_seed);
            m_buffer[sample_pos++] = rand;
            m_buffer[sample_pos++] = rand;
        }

        ConvertRandomNumsToWhiteNoiseInBuffer(num_frames);
    }

    void FillBufferWithStereoWhiteNoise(u32 num_frames) {
        auto const num_samples = num_frames * 2;
        for (auto const sample_pos : Range(num_samples))
            m_buffer[sample_pos] = (f32)FastRand(m_voice.pool.random_seed);

        ConvertRandomNumsToWhiteNoiseInBuffer(num_frames);

        for (usize sample = 0; sample < num_samples; sample += 2) {
            DoStereoWiden(0.7f,
                          m_buffer[sample],
                          m_buffer[sample + 1],
                          m_buffer[sample],
                          m_buffer[sample + 1]);
        }
    }

    void FillBufferWithSampleData(u32 num_frames) {
        ZoneScoped;
        ZeroChunkBuffer(num_frames);
        for (auto& s : m_voice.sound_sources) {
            if (!s.is_active) continue;
            switch (s.source_data.tag) {
                case InstrumentType::None: {
                    PanicIfReached();
                    break;
                }
                case InstrumentType::Sampler: {
                    auto const& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();
                    if (!AddSampleDataOntoBuffer(s, num_frames)) {
                        s.is_active = false;
                        m_voice.num_active_voice_samples--;
                    }
                    if (sampler.region->trigger.trigger_event == sample_lib::TriggerEvent::NoteOn)
                        m_position_for_gui = (f32)s.pos / (f32)sampler.data->num_frames;
                    break;
                }
                case InstrumentType::WaveformSynth: {
                    switch (s.source_data.Get<WaveformType>()) {
                        case WaveformType::Sine: {
                            usize sample_pos = 0;
                            for (u32 frame = 0; frame < num_frames; frame += 2) {
                                alignas(16) f32 samples[4];

                                samples[0] = trig_table_lookup::SinTurnsPositive((f32)s.pos);
                                samples[1] = samples[0];
                                s.pos += GetPitchRatio(s, frame);
                                if ((frame + 1) != num_frames) {
                                    samples[2] = trig_table_lookup::SinTurnsPositive((f32)s.pos);
                                    samples[3] = samples[2];
                                    s.pos += GetPitchRatio(s, frame + 1);
                                } else {
                                    samples[2] = 0;
                                    samples[3] = 0;
                                }

                                // prevent overflow
                                if (s.pos > (1 << 24)) [[unlikely]]
                                    s.pos -= (1 << 24);

                                // This is an arbitrary scale factor to make the sine more in-line with other
                                // waveform levels. It's important to keep this the same for backwards
                                // compatibility.
                                constexpr f32 k_sine_scale = 0.2f;

                                auto v = LoadAlignedToType<f32x4>(samples);
                                v *= s.amp * k_sine_scale;
                                CopyVectorToBufferAtPos(sample_pos, v);
                                sample_pos += 4;
                            }

                            break;
                        }
                        case WaveformType::WhiteNoiseMono: {
                            FillBufferWithMonoWhiteNoise(num_frames);
                            break;
                        }
                        case WaveformType::WhiteNoiseStereo: {
                            FillBufferWithStereoWhiteNoise(num_frames);
                            break;
                        }
                        case WaveformType::Count: PanicIfReached(); break;
                    }
                    break;
                }
            }
        }
    }

    u32 ApplyGain(u32 num_frames) {
        ZoneScoped;

        // Save envelope state for restoration
        auto vol_env = m_voice.vol_env;
        auto env_on = m_voice.controller->vol_env_on && !m_voice.disable_vol_env;
        auto vol_env_params = m_voice.controller->vol_env;
        DEFER { m_voice.vol_env = vol_env; };

        // LFO parameters
        auto const has_volume_lfo = HasVolumeLfo();
        auto const has_pan_lfo = HasPanLfo();
        auto const lfo_amp = (has_volume_lfo || has_pan_lfo) ? m_voice.controller->lfo.amount : 0.0f;
        auto const lfo_base = has_volume_lfo ? (1.0f - (Fabs(lfo_amp) / 2.0f)) : 1.0f;
        auto const lfo_half_amp = lfo_amp / 2.0f;

        usize sample_pos = 0;
        f32 final_gain1 = 1.0f;

        for (u32 frame = 0; frame < num_frames; frame += 2) {
            // Calculate envelope gain
            f32 env1 = env_on ? vol_env.Process(vol_env_params) : 1.0f;
            f32 env2 = 1.0f;
            auto const frame_p1 = frame + 1;
            auto const frame_p1_is_not_last = frame_p1 != num_frames;
            if (frame_p1_is_not_last) env2 = env_on ? vol_env.Process(vol_env_params) : 1.0f;

            // Calculate volume LFO gain
            f32 vol_lfo1 = 1.0f;
            f32 vol_lfo2 = 1.0f;
            if (has_volume_lfo) {
                vol_lfo1 = lfo_base + m_lfo_amounts[frame] * lfo_half_amp;
                vol_lfo2 =
                    (frame_p1_is_not_last) ? lfo_base + (m_lfo_amounts[frame_p1] * lfo_half_amp) : vol_lfo1;
            }

            // Calculate fade gain
            f32 fade1 = m_voice.volume_fade.GetFade() * m_voice.aftertouch_multiplier;
            f32 fade2 = 1.0f;
            if (frame_p1_is_not_last) fade2 = m_voice.volume_fade.GetFade() * m_voice.aftertouch_multiplier;

            // Calculate pan positions
            auto pan_pos1 = m_voice.controller->pan_pos;
            auto pan_pos2 = pan_pos1;
            if (has_pan_lfo) {
                pan_pos1 += (m_lfo_amounts[frame] * lfo_amp);
                pan_pos1 = Clamp(pan_pos1, -1.0f, 1.0f);
                if (frame_p1_is_not_last) {
                    pan_pos2 += (m_lfo_amounts[frame_p1] * lfo_amp);
                    pan_pos2 = Clamp(pan_pos2, -1.0f, 1.0f);
                }
            }

            // Get pan gains
            auto const pan_gains = EqualPanGains2(f32x2 {pan_pos1, pan_pos2});

            // Combine all gains
            final_gain1 = env1 * vol_lfo1 * fade1;
            f32 final_gain2 = env2 * vol_lfo2 * fade2;

            // Clamp volume LFO contribution
            if (has_volume_lfo) {
                final_gain1 = Clamp(final_gain1, 0.0f, 1.0f);
                final_gain2 = Clamp(final_gain2, 0.0f, 1.0f);
            }

            // Calculate final L/R gains
            auto const gain_1 = final_gain1 * pan_gains.xy;
            auto const gain_2 = final_gain2 * pan_gains.zw;

            // Apply smoothing to final gains
            auto const smooth_gain_1 =
                m_voice.gain_smoother.LowPass(gain_1, m_audio_context.one_pole_smoothing_cutoff_1ms);
            auto const smooth_gain_2 =
                m_voice.gain_smoother.LowPass(gain_2, m_audio_context.one_pole_smoothing_cutoff_1ms);

            // Apply smoothed gains to stereo sample pairs
            auto const gain = __builtin_shufflevector(smooth_gain_1, smooth_gain_2, 0, 1, 2, 3);
            MultiplyVectorToBufferAtPos(sample_pos, gain);
            sample_pos += 4;

            CheckSamplesAreValid(sample_pos - 4, 4);

            // Check for early termination conditions
            if (env_on && vol_env.IsIdle()) {
                m_voice.current_gain *= final_gain1;
                return frame;
            }
            if (m_voice.volume_fade.IsSilent()) {
                m_voice.current_gain *= final_gain1;
                return frame;
            }
        }

        m_voice.current_gain *= final_gain1;
        return num_frames;
    }

    void ApplyFilter(u32 num_frames) {
        ZoneScoped;
        auto const filter_type = m_voice.controller->filter_type;

        auto fil_env = m_voice.fil_env;
        auto fil_env_params = m_voice.controller->fil_env;
        DEFER { m_voice.fil_env = fil_env; };

        usize sample_pos = 0;
        for (u32 frame = 0; frame < num_frames; frame++) {
            auto env = fil_env.Process(fil_env_params);
            if (auto const filter_mix =
                    m_voice.filter_mix_smoother.LowPass((f32)m_voice.controller->filter_on,
                                                        m_audio_context.one_pole_smoothing_cutoff_10ms);
                filter_mix > 0.00001f) {

                auto cut = m_voice.controller->sv_filter_cutoff_linear +
                           ((env - 0.5f) * m_voice.controller->fil_env_amount);
                auto res = m_voice.controller->sv_filter_resonance;

                auto const has_filter_lfo = HasFilterLfo();
                if (has_filter_lfo) {
                    auto const& lfo_amp = m_voice.controller->lfo.amount;
                    cut += (m_lfo_amounts[(usize)frame] * lfo_amp) / 2;
                }

                f32 res_change {};
                res = m_voice.filter_resonance_smoother.LowPass(res,
                                                                m_audio_context.one_pole_smoothing_cutoff_1ms,
                                                                &res_change);
                f32 cut_change {};
                cut = m_voice.filter_linear_cutoff_smoother.LowPass(
                    cut,
                    m_audio_context.one_pole_smoothing_cutoff_1ms,
                    &cut_change);

                if (has_filter_lfo || cut_change > 0.00001f || res_change > 0.00001f) {
                    cut = sv_filter::LinearToHz(Clamp(cut, 0.0f, 1.0f));
                    m_filter_coeffs.Update(m_audio_context.sample_rate, cut, res);
                }

                if (filter_mix < 0.999f) {
                    auto const in = LoadUnalignedToType<f32x2>(&m_buffer[sample_pos]);
                    f32x2 wet_buf;
                    sv_filter::Process(in, wet_buf, m_filters, filter_type, m_filter_coeffs);

                    for (auto const i : Range(2u)) {
                        auto& samp = m_buffer[sample_pos + i];
                        samp = samp + filter_mix * (wet_buf[i] - samp);
                    }
                } else {
                    auto const in = LoadUnalignedToType<f32x2>(&m_buffer[sample_pos]);
                    f32x2 out;
                    sv_filter::Process(in, out, m_filters, filter_type, m_filter_coeffs);
                    StoreToUnaligned(&m_buffer[sample_pos], out);
                }

                CheckSamplesAreValid(sample_pos, 2);
                sample_pos += 2;
            } else {
                m_voice.filters = {};
                m_voice.filter_resonance_smoother.Reset();
                m_voice.filter_linear_cutoff_smoother.Reset();
            }
        }
    }

    void FillLFOBuffer(u32 num_frames) {
        ZoneScoped;
        for (auto const i : Range(num_frames)) {
            auto v = m_voice.lfo.Tick();
            m_lfo_amounts[i] = -v;
        }
    }

    void ZeroChunkBuffer(u32 num_frames) {
        auto num_samples = num_frames * 2;
        num_samples += num_samples % 2;
        SimdZeroAlignedBuffer(m_buffer.data, (usize)num_samples);
    }

    sv_filter::CachedHelpers m_filter_coeffs = {};
    decltype(Voice::filters) m_filters = {};

    AudioProcessingContext const& m_audio_context;
    Voice& m_voice;
    StaticSpan<f32, k_block_size_max * 2> m_buffer;

    u32 m_frame_index = 0;
    f32 m_position_for_gui = 0;

    alignas(16) Array<f32, k_block_size_max + 1> m_lfo_amounts;
};

inline void ProcessBuffer(Voice& voice, u32 num_frames, AudioProcessingContext const& context) {
    if (!voice.is_active) return;

    VoiceProcessor processor(voice, context);
    voice.written_to_buffer_this_block = processor.Process(num_frames);
}

void OnThreadPoolExec(VoicePool& pool, u32 task_index) {
    auto& voice = pool.voices[task_index];
    if (voice.is_active)
        ProcessBuffer(voice, voice.pool.multithread_processing.num_frames, *pool.audio_processing_context);
}

void Reset(VoicePool& pool) {
    auto& waveform_markers = pool.voice_waveform_markers_for_gui.Write();
    auto& vol_env_markers = pool.voice_vol_env_markers_for_gui.Write();
    auto& fil_env_markers = pool.voice_fil_env_markers_for_gui.Write();
    for (auto const i : Range(k_num_voices)) {
        waveform_markers[i] = {};
        vol_env_markers[i] = {};
        fil_env_markers[i] = {};
    }
    pool.voice_waveform_markers_for_gui.Publish();
    pool.voice_vol_env_markers_for_gui.Publish();
    pool.voice_fil_env_markers_for_gui.Publish();
}

Array<Span<f32>, k_num_layers>
ProcessVoices(VoicePool& pool, u32 num_frames, AudioProcessingContext const& context) {
    ZoneScoped;
    if (pool.num_active_voices.Load(LoadMemoryOrder::Relaxed) == 0) return {};

    auto const thread_pool =
        (clap_host_thread_pool const*)context.host.get_extension(&context.host, CLAP_EXT_THREAD_POOL);

    {

        bool failed_multithreaded_process = false;
        if (thread_pool && thread_pool->request_exec) {
            pool.multithread_processing.num_frames = num_frames;
            for (auto& v : pool.voices)
                v.written_to_buffer_this_block = false;

            pool.audio_processing_context = &context;
            failed_multithreaded_process = !thread_pool->request_exec(&context.host, k_num_voices);
        }

        if (!thread_pool || failed_multithreaded_process) {
            for (auto& v : pool.voices) {
                v.written_to_buffer_this_block = false;
                if (v.is_active) ProcessBuffer(v, num_frames, context);
            }
        }
    }

    Array<Span<f32>, k_num_layers> layer_buffers {};

    for (auto& v : pool.voices) {
        if (v.written_to_buffer_this_block) {
            if constexpr (RUNTIME_SAFETY_CHECKS_ON && PRODUCTION_BUILD) {
                for (auto const frame : Range(num_frames)) {
                    auto const& l = pool.buffer_pool[v.index][(frame * 2) + 0];
                    auto const& r = pool.buffer_pool[v.index][(frame * 2) + 1];
                    ASSERT(l >= -k_erroneous_sample_value && l <= k_erroneous_sample_value);
                    ASSERT(r >= -k_erroneous_sample_value && r <= k_erroneous_sample_value);
                }
            }

            auto const layer_index = (usize)v.controller->layer_index;
            if (!layer_buffers[layer_index].size) {
                layer_buffers[layer_index] = pool.buffer_pool[v.index];
            } else {
                SimdAddAlignedBuffer(layer_buffers[layer_index].data,
                                     pool.buffer_pool[v.index].data,
                                     (usize)num_frames * 2);
            }
        } else {
            pool.voice_waveform_markers_for_gui.Write()[v.index] = {};
            pool.voice_vol_env_markers_for_gui.Write()[v.index] = {};
            pool.voice_fil_env_markers_for_gui.Write()[v.index] = {};
        }
    }

    pool.voice_waveform_markers_for_gui.Publish();
    pool.voice_vol_env_markers_for_gui.Publish();
    pool.voice_fil_env_markers_for_gui.Publish();

    return layer_buffers;
}
