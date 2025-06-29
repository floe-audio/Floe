// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "voices.hpp"

#include "foundation/foundation.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "layer_processor.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processor/effect_stereo_widen.hpp"

static constexpr u32 k_num_frames_in_voice_processing_chunk = 64;

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

void SetFilterOn(Voice& v, bool on) {
    v.smoothing_system.Set(v.filter_mix_smoother_id, on ? 1.0f : 0.0f, 10);
}

void SetFilterCutoff(Voice& v, f32 cutoff01) {
    v.filter_changed = true;
    v.smoothing_system.Set(v.sv_filter_linear_cutoff_smoother_id, cutoff01, 10);
}

void SetFilterRes(Voice& v, f32 res) {
    v.filter_changed = true;
    v.smoothing_system.Set(v.sv_filter_resonance_smoother_id, res, 10);
}

static f64 MidiNoteToFrequency(f64 note) { return 440.0 * Exp2((note - 69.0) / 12.0); }

inline f64 CalculatePitchRatio(int note, VoiceSample const* s, f32 pitch_semitones, f32 sample_rate) {
    switch (s->generator) {
        case InstrumentType::None: {
            PanicIfReached();
            break;
        }
        case InstrumentType::Sampler: {
            auto const& sampler = s->sampler;
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

static int RootKey(Voice const& v, VoiceSample const& s) {
    int k = v.note_num;
    if (s.generator == InstrumentType::Sampler) {
        switch (s.sampler.region->playback.keytrack_requirement) {
            case sample_lib::KeytrackRequirement::Default:
                if (v.controller->no_key_tracking) k = s.sampler.region->root_key;
                break;
            case sample_lib::KeytrackRequirement::Always: break;
            case sample_lib::KeytrackRequirement::Never: k = s.sampler.region->root_key; break;
            case sample_lib::KeytrackRequirement::Count: PanicIfReached(); break;
        }
    }
    return k;
}

void SetVoicePitch(Voice& v, f32 pitch_semitones, f32 sample_rate) {
    for (auto& s : v.voice_samples) {
        if (!s.is_active) continue;

        v.smoothing_system.Set(s.pitch_ratio_smoother_id,
                               CalculatePitchRatio(RootKey(v, s), &s, pitch_semitones, sample_rate),
                               10);
    }
}

void UpdateXfade(Voice& v, f32 knob_pos_01, bool hard_set) {
    auto set_xfade_smoother = [&](VoiceSample& s, f32 val) {
        ASSERT(val >= 0 && val <= 1);
        if (!hard_set)
            v.smoothing_system.Set(s.sampler.xfade_vol_smoother_id, val, 10);
        else
            v.smoothing_system.HardSet(s.sampler.xfade_vol_smoother_id, val);
    };

    VoiceSample* voice_sample_1 = nullptr;
    VoiceSample* voice_sample_2 = nullptr;

    auto const knob_pos = knob_pos_01 * 99;

    for (auto& s : v.voice_samples) {
        if (!s.is_active) continue;
        if (s.generator != InstrumentType::Sampler) continue;
        auto& sampler = s.sampler;

        if (auto const r = sampler.region->timbre_layering.layer_range) {
            if (knob_pos >= r->start && knob_pos < r->end) {
                // NOTE: we don't handle the case if there is more than 2 overlapping regions. We should
                // ensure we can't get this point of the code with that being the case.
                if (!voice_sample_1)
                    voice_sample_1 = &s;
                else
                    voice_sample_2 = &s;
            } else {
                set_xfade_smoother(s, 0);
            }
        } else {
            set_xfade_smoother(s, 1);
        }
    }

    if (voice_sample_1 && !voice_sample_2)
        set_xfade_smoother(*voice_sample_1, 1);
    else if (voice_sample_1 && voice_sample_2) {
        auto const& r1 = *voice_sample_1->sampler.region->timbre_layering.layer_range;
        auto const& r2 = *voice_sample_2->sampler.region->timbre_layering.layer_range;
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
    for (auto& s : v.voice_samples) {
        if (!s.is_active) continue;
        if (s.generator != InstrumentType::Sampler) continue;
        if (s.sampler.region->trigger.trigger_event == sample_lib::TriggerEvent::NoteOff) continue;
        auto& sampler = s.sampler;

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

inline f32x2 EqualPanGains(f32 pan_pos) {
    auto const angle = pan_pos * 0.125f;
    f32 const sinx = trig_table_lookup::SinTurns(angle);
    f32 const cosx = trig_table_lookup::CosTurns(angle);

    constexpr auto k_root_2_over_2 = k_sqrt_two<> / 2;
    auto const left = k_root_2_over_2 * (cosx - sinx);
    auto const right = k_root_2_over_2 * (cosx + sinx);
    ASSERT_HOT(left >= 0 && right >= 0);

    return {left, right};
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
    voice.filter_changed = true;
    voice.filters = {};
    voice.smoothing_system.HardSet(voice.sv_filter_resonance_smoother_id,
                                   voice.controller->sv_filter_resonance);
    voice.smoothing_system.HardSet(voice.sv_filter_linear_cutoff_smoother_id,
                                   voice.controller->sv_filter_cutoff_linear);
    voice.smoothing_system.HardSet(voice.filter_mix_smoother_id, voice.controller->filter_on ? 1.0f : 0.0f);

    switch (params.params.tag) {
        case InstrumentType::None: {
            PanicIfReached();
            break;
        }
        case InstrumentType::Sampler: {
            auto const& sampler = params.params.Get<VoiceStartParams::SamplerParams>();
            voice.num_active_voice_samples = (u8)sampler.voice_sample_params.size;
            for (auto const i : Range(sampler.voice_sample_params.size)) {
                auto& s = voice.voice_samples[i];
                auto const& s_params = sampler.voice_sample_params[i];

                s.generator = InstrumentType::Sampler;

                s.is_active = true;
                s.amp = s_params.amp * (f32)DbToAmpApprox((f64)s_params.region.audio_props.gain_db);
                s.sampler.region = &s_params.region;
                s.sampler.data = &s_params.audio_data;
                s.sampler.loop = {};
                ASSERT(s.sampler.data != nullptr);

                voice.smoothing_system.HardSet(
                    s.pitch_ratio_smoother_id,
                    CalculatePitchRatio(RootKey(voice, s), &s, params.initial_pitch, sample_rate));
                auto const offs =
                    (f64)(sampler.initial_sample_offset_01 * ((f32)s.sampler.data->num_frames - 1)) +
                    s_params.region.audio_props.start_offset_frames;
                s.pos = offs;
                if (voice.controller->reverse) s.pos = (f64)(s.sampler.data->num_frames - Max(offs, 1.0));
            }
            for (u32 i = voice.num_active_voice_samples; i < k_max_num_voice_samples; ++i)
                voice.voice_samples[i].is_active = false;

            UpdateLoopInfo(voice);
            UpdateXfade(voice, sampler.initial_timbre_param_value_01, true);
            break;
        }
        case InstrumentType::WaveformSynth: {
            auto const& waveform = params.params.Get<VoiceStartParams::WaveformParams>();
            voice.num_active_voice_samples = 1;
            for (u32 i = voice.num_active_voice_samples; i < k_max_num_voice_samples; ++i)
                voice.voice_samples[i].is_active = false;

            auto& s = voice.voice_samples[0];
            s.generator = InstrumentType::WaveformSynth;
            s.is_active = true;
            s.amp = waveform.amp;
            s.pos = 0;
            s.waveform = waveform.type;
            voice.smoothing_system.HardSet(
                s.pitch_ratio_smoother_id,
                CalculatePitchRatio(voice.note_num, &s, params.initial_pitch, sample_rate));

            break;
        }
    }

    voice.is_active = true;
    voice.pool.num_active_voices.FetchAdd(1, RmwMemoryOrder::Relaxed);
    voice.pool.voices_per_midi_note_for_gui[voice.note_num].FetchAdd(1, RmwMemoryOrder::Relaxed);
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

void VoicePool::PrepareToPlay(ArenaAllocator& arena, AudioProcessingContext const& context) {
    auto const buffer_size_bytes = AlignForward((usize)context.process_block_size_max * 2, 4) * sizeof(f32);
    for (auto [index, buf] : Enumerate(buffer_pool)) {
        auto const alloc = arena.Allocate({
            .size = buffer_size_bytes,
            .alignment = 16,
            .allow_oversized_result = false,
        });
        buf = Span<f32> {CheckedPointerCast<f32*>(alloc.data), alloc.size / sizeof(f32)};
    }

    decltype(Voice::index) index = 0;
    for (auto& v : voices) {
        v.index = index++;
        v.smoothing_system.PrepareToPlay(k_num_frames_in_voice_processing_chunk, context.sample_rate, arena);
    }

    constexpr f32 k_time_constant_ms = 1.0f;
    smoothing_cutoff = 1.0f - Exp(-1.0f / (k_time_constant_ms * 0.001f * context.sample_rate));
}

void NoteOff(VoicePool& pool, VoiceProcessingController& controller, MidiChannelNote note) {
    for (auto& v : pool.voices)
        if (v.is_active && v.midi_key_trigger == note && &controller == v.controller) EndVoice(v);
}

class ChunkwiseVoiceProcessor {
  public:
    ChunkwiseVoiceProcessor(Voice& voice, AudioProcessingContext const& audio_context)
        : m_filter_coeffs(voice.filter_coeffs)
        , m_filters(voice.filters)
        , m_audio_context(audio_context)
        , m_voice(voice) {}

    ~ChunkwiseVoiceProcessor() {
        m_voice.filter_coeffs = m_filter_coeffs;
        m_voice.filters = m_filters;
    }

    bool Process(u32 num_frames) {
        ZoneNamedN(process, "Voice Process", true);
        u32 samples_written = 0;
        Span<f32> write_buffer = m_voice.pool.buffer_pool[m_voice.index];

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

        while (num_frames) {
            u32 const chunk_size = Min(num_frames, k_num_frames_in_voice_processing_chunk);
            ZoneNamedN(chunk, "Voice Chunk", true);
            ZoneValueV(chunk, chunk_size);

            m_voice.smoothing_system.ProcessBlock(chunk_size);

            FillLFOBuffer(chunk_size);
            FillBufferWithSampleData(chunk_size);

            auto num_valid_frames = ApplyGain(chunk_size);
            ApplyFilter(num_valid_frames);

            auto const samples_to_write = num_valid_frames * 2;
            CheckSamplesAreValid(0, samples_to_write);
            // We can't do aligned copy because of frames_before_starting
            CopyMemory(write_buffer.data, m_buffer.data, (usize)samples_to_write * sizeof(f32));
            samples_written += samples_to_write;
            write_buffer = write_buffer.SubSpan((usize)samples_to_write);

            if (num_valid_frames != chunk_size || !m_voice.num_active_voice_samples) {
                // We can't do aligned zero because of frames_before_starting
                ZeroMemory(write_buffer.ToByteSpan());
                EndVoiceInstantly(m_voice);
                break;
            }

            num_frames -= chunk_size;
            m_frame_index += chunk_size;

            m_voice.pool.voice_waveform_markers_for_gui.Write()[m_voice.index] = {
                .layer_index = (u8)m_voice.controller->layer_index,
                .position = (u16)(Clamp01(m_position_for_gui) * (f32)UINT16_MAX),
                .intensity = (u16)(Clamp01(m_voice.current_gain) * (f32)UINT16_MAX),
            };
            m_voice.pool.voice_vol_env_markers_for_gui.Write()[m_voice.index] = {
                .on = m_voice.controller->vol_env_on && !m_voice.disable_vol_env && !m_voice.vol_env.IsIdle(),
                .layer_index = (u8)m_voice.controller->layer_index,
                .state = (u8)m_voice.vol_env.state,
                .pos = (u16)(Clamp01(m_voice.vol_env.output) * (f32)UINT16_MAX),
                .sustain_level = (u16)(Clamp01(m_voice.controller->vol_env.sustain_amount) * (f32)UINT16_MAX),
                .id = m_voice.id,
            };
            m_voice.pool.voice_fil_env_markers_for_gui.Write()[m_voice.index] = {
                .on = m_voice.controller->fil_env_amount != 0 && !m_voice.fil_env.IsIdle(),
                .layer_index = (u8)m_voice.controller->layer_index,
                .state = (u8)m_voice.fil_env.state,
                .pos = (u16)(Clamp01(m_voice.fil_env.output) * (f32)UINT16_MAX),
                .sustain_level = (u16)(Clamp01(m_voice.controller->fil_env.sustain_amount) * (f32)UINT16_MAX),
                .id = m_voice.id,
            };

            m_voice.current_gain = 1;
        }

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
        f32x4 p;
        p = LoadUnalignedToType<f32x4>(&m_buffer[pos]);
        p += addition;
        CheckSamplesAreValid(p);
        StoreToUnaligned(&m_buffer[pos], p);
    }

    void CopyVectorToBufferAtPos(usize const pos, f32x4 const& data) {
        ASSERT_HOT(pos + 4 <= m_buffer.size);
        CheckSamplesAreValid(data);
        StoreToUnaligned(&m_buffer[pos], data);
    }

    f64 GetPitchRatio(VoiceSample& w, u32 frame) {
        auto pitch_ratio = m_voice.smoothing_system.Value(w.pitch_ratio_smoother_id, frame);
        if (HasPitchLfo()) {
            static constexpr f64 k_max_semitones = 1;
            auto const lfo_amp = (f64)m_voice.controller->lfo.amount;
            auto const pitch_addition_in_semitones =
                ((f64)m_lfo_amounts[(usize)frame] * lfo_amp * k_max_semitones);
            pitch_ratio *= Exp2(pitch_addition_in_semitones / 12.0);
        }
        return pitch_ratio;
    }

    bool SampleGetAndInc(VoiceSample& w, u32 frame, f32& out_l, f32& out_r) {
        SampleGetData(*w.sampler.data, w.sampler.loop, w.sampler.loop_and_reverse_flags, w.pos, out_l, out_r);
        if (!(w.sampler.loop_and_reverse_flags &
              (loop_and_reverse_flags::LoopedManyTimes | loop_and_reverse_flags::CurrentlyReversed))) {
            auto const pos = w.pos - w.sampler.region->audio_props.start_offset_frames;
            if (pos < w.sampler.region->audio_props.fade_in_frames) {
                auto const percent = pos / (f64)w.sampler.region->audio_props.fade_in_frames;
                // Quarter-sine fade in.
                auto const amount = trig_table_lookup::SinTurnsPositive((f32)percent * 0.25f);
                out_l *= amount;
                out_r *= amount;
            }
        }
        auto const pitch_ratio = GetPitchRatio(w, frame);
        return IncrementSamplePlaybackPos(w.sampler.loop,
                                          w.sampler.loop_and_reverse_flags,
                                          w.pos,
                                          pitch_ratio,
                                          (f64)w.sampler.data->num_frames);
    }

    bool SampleGetAndIncWithXFade(VoiceSample& w, u32 frame, f32& out_l, f32& out_r) {
        bool sample_still_going = false;
        if (w.sampler.region->timbre_layering.layer_range) {
            if (auto const v = m_voice.smoothing_system.Value(w.sampler.xfade_vol_smoother_id, frame);
                v != 0) {
                sample_still_going = SampleGetAndInc(w, frame, out_l, out_r);
                out_l *= v;
                out_r *= v;
            } else {
                auto const pitch_ratio1 = GetPitchRatio(w, frame);
                sample_still_going = IncrementSamplePlaybackPos(w.sampler.loop,
                                                                w.sampler.loop_and_reverse_flags,
                                                                w.pos,
                                                                pitch_ratio1,
                                                                (f64)w.sampler.data->num_frames);
            }
        } else {
            sample_still_going = SampleGetAndInc(w, frame, out_l, out_r);
        }
        return sample_still_going;
    }

    bool AddSampleDataOntoBuffer(VoiceSample& w, u32 num_frames) {
        usize sample_pos = 0;
        for (u32 frame = 0; frame < num_frames; frame += 2) {
            f32 sl1 {};
            f32 sr1 {};
            f32 sl2 {};
            f32 sr2 {};

            bool sample_still_going = SampleGetAndIncWithXFade(w, frame, sl1, sr1);

            auto const frame_p1 = frame + 1;
            if (sample_still_going && frame_p1 != num_frames)
                sample_still_going = SampleGetAndIncWithXFade(w, frame_p1, sl2, sr2);

            // sl2 and sl2 will be 0 if the second sample was not fetched so there is no harm in
            // adding that too
            f32x4 v {sl1, sr1, sl2, sr2};
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
        for (auto& s : m_voice.voice_samples) {
            if (!s.is_active) continue;
            switch (s.generator) {
                case InstrumentType::None: {
                    PanicIfReached();
                    break;
                }
                case InstrumentType::Sampler: {
                    if (!AddSampleDataOntoBuffer(s, num_frames)) {
                        s.is_active = false;
                        m_voice.num_active_voice_samples--;
                    }
                    if (s.sampler.region->trigger.trigger_event == sample_lib::TriggerEvent::NoteOn)
                        m_position_for_gui = (f32)s.pos / (f32)s.sampler.data->num_frames;
                    break;
                }
                case InstrumentType::WaveformSynth: {
                    switch (s.waveform) {
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
            if (frame_p1 != num_frames) env2 = env_on ? vol_env.Process(vol_env_params) : 1.0f;

            // Calculate volume LFO gain
            f32 vol_lfo1 = 1.0f;
            f32 vol_lfo2 = 1.0f;
            if (has_volume_lfo) {
                vol_lfo1 = lfo_base + m_lfo_amounts[frame] * lfo_half_amp;
                vol_lfo2 =
                    (frame_p1 != num_frames) ? lfo_base + (m_lfo_amounts[frame_p1] * lfo_half_amp) : vol_lfo1;
            }

            // Calculate fade gain
            f32 fade1 = m_voice.volume_fade.GetFade() * m_voice.aftertouch_multiplier;
            f32 fade2 = 1.0f;
            if (frame_p1 != num_frames) fade2 = m_voice.volume_fade.GetFade() * m_voice.aftertouch_multiplier;

            // Calculate pan positions
            auto pan_pos1 = m_voice.controller->pan_pos;
            auto pan_pos2 = pan_pos1;
            if (has_pan_lfo) {
                pan_pos1 += (m_lfo_amounts[frame] * lfo_amp);
                pan_pos1 = Clamp(pan_pos1, -1.0f, 1.0f);
                if (frame_p1 != num_frames) {
                    pan_pos2 += (m_lfo_amounts[frame_p1] * lfo_amp);
                    pan_pos2 = Clamp(pan_pos2, -1.0f, 1.0f);
                }
            }

            // Get pan gains
            auto const pan_gains1 = EqualPanGains(pan_pos1);
            auto const pan_gains2 = EqualPanGains(pan_pos2);

            // Combine all gains
            final_gain1 = env1 * vol_lfo1 * fade1;
            f32 final_gain2 = env2 * vol_lfo2 * fade2;

            // Clamp volume LFO contribution (preserving original behavior)
            if (has_volume_lfo) {
                final_gain1 = Clamp(final_gain1, 0.0f, 1.0f);
                final_gain2 = Clamp(final_gain2, 0.0f, 1.0f);
            }

            // Calculate final L/R gains
            auto const gain_1 = final_gain1 * pan_gains1;
            auto const gain_2 = final_gain2 * pan_gains2;

            // Apply smoothing to final gains
            auto const smooth_gain_1 = m_voice.gain_smoother.LowPass(gain_1, m_voice.pool.smoothing_cutoff);
            auto const smooth_gain_2 = m_voice.gain_smoother.LowPass(gain_2, m_voice.pool.smoothing_cutoff);

            // Apply smoothed gains to stereo sample pairs
            f32x4 const gain {smooth_gain_1.x, smooth_gain_1.y, smooth_gain_2.x, smooth_gain_2.y};
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
            if (auto filter_mix = m_voice.smoothing_system.Value(m_voice.filter_mix_smoother_id, frame);
                filter_mix != 0) {
                m_voice.filter_changed |=
                    m_voice.smoothing_system.IsSmoothing(m_voice.sv_filter_linear_cutoff_smoother_id,
                                                         frame) ||
                    m_voice.smoothing_system.IsSmoothing(m_voice.sv_filter_resonance_smoother_id, frame);

                auto cut =
                    m_voice.smoothing_system.Value(m_voice.sv_filter_linear_cutoff_smoother_id, frame) +
                    ((env - 0.5f) * m_voice.controller->fil_env_amount);
                auto const res =
                    m_voice.smoothing_system.Value(m_voice.sv_filter_resonance_smoother_id, frame);

                if (HasFilterLfo()) {
                    m_voice.filter_changed = true;
                    auto const& lfo_amp = m_voice.controller->lfo.amount;
                    cut += (m_lfo_amounts[(usize)frame] * lfo_amp) / 2;
                }

                if (fil_env.state != adsr::State::Sustain && m_voice.controller->fil_env_amount != 0)
                    m_voice.filter_changed = true;

                if (m_voice.filter_changed) {
                    cut = sv_filter::LinearToHz(Clamp(cut, 0.0f, 1.0f));
                    m_filter_coeffs.Update(m_audio_context.sample_rate, cut, res);
                    m_voice.filter_changed = false;
                }

                if (filter_mix != 1) {
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

    u32 m_frame_index = 0;
    f32 m_position_for_gui = 0;

    alignas(16) Array<f32, k_num_frames_in_voice_processing_chunk + 1> m_lfo_amounts;
    alignas(16) Array<f32, (k_num_frames_in_voice_processing_chunk * 2) + 2> m_buffer;
};

inline void ProcessBuffer(Voice& voice, u32 num_frames, AudioProcessingContext const& context) {
    if (!voice.is_active) return;

    ChunkwiseVoiceProcessor processor(voice, context);
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
