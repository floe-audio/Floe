// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "voices.hpp"

#include <clap/ext/thread-pool.h>

#include "foundation/foundation.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/final_binary_type.hpp"

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
// The result is a vector of 4 floats: {left 1, right 1, left 2, right 2}.
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
                AudioProcessingContext const& audio_processing_context) {
    auto& voice = FindVoice(pool, audio_processing_context);

    auto const sample_rate = audio_processing_context.sample_rate;
    ASSERT(sample_rate != 0);

    voice.controller = &voice_controller;
    voice.lfo.phase = params.lfo_start_phase;

    UpdateLFOWaveform(voice);
    UpdateLFOTime(voice, audio_processing_context.sample_rate);

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

            if (g_final_binary_type == FinalBinaryType::Standalone) {
                DynamicArrayBounded<SampleLogItem, k_max_num_voice_sound_sources> sample_log_items;
                for (auto const& s : voice.sound_sources) {
                    if (!s.is_active) continue;
                    dyn::Append(sample_log_items,
                                SampleLogItem {
                                    .region = s.source_data.Get<VoiceSoundSource::SampleSource>().region,
                                });
                }
                pool.sample_log_queue.Push(sample_log_items);
                audio_processing_context.host.request_callback(&audio_processing_context.host);
            }

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
    voice.pool.voices_per_midi_note_for_gui[voice.midi_key_trigger.note].FetchAdd(1, RmwMemoryOrder::Relaxed);
    voice.pool.last_velocity[voice.controller->layer_index].Store(params.note_vel, StoreMemoryOrder::Relaxed);
}

void OnMainThread(VoicePool& pool) {
    if (g_final_binary_type == FinalBinaryType::Standalone) {
        auto const items = pool.sample_log_queue.PopAll();
        if (items.size) {
            auto const time = TimePoint::Now();
            for (auto const& item : items) {
                StdPrintF(StdStream::Err,
                          "Region triggered. Root {} ({}), {}, {}, time {}\n",
                          item.region->root_key,
                          NoteName(CheckedCast<u7>(item.region->root_key)),
                          item.region->path,
                          item.region->trigger.trigger_event,
                          time.Raw());
            }
        }
    }
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

struct VoiceProcessor {
    enum class VoiceBlockResult {
        Continue,
        End,
    };

    static void Process(Voice& voice, AudioProcessingContext const& audio_context, u32 num_frames) {
        ZoneNamedN(process, "Voice Process", true);
        ASSERT_HOT(voice.is_active);

        auto buffer = Span<f32x2> {voice.buffer.data, num_frames};

        for (auto& f : voice.buffer)
            f = 0.0f;

        if (voice.frames_before_starting != 0) {
            auto const silent_frames = Min(num_frames, voice.frames_before_starting);
            voice.frames_before_starting -= silent_frames;
            buffer.RemovePrefix(silent_frames);
            if (buffer.size == 0) return;
        }

        Array<f32, k_block_size_max> lfo_amounts_buffer;
        auto lfo_amounts = Span<f32> {lfo_amounts_buffer}.SubSpan(0, num_frames);
        FillLfoBuffer(voice, lfo_amounts);

        FillBufferWithSampleData(voice, buffer, lfo_amounts, audio_context);

        auto const block_result = ApplyGain(voice, buffer, lfo_amounts, audio_context);
        ApplyFilter(voice, buffer, lfo_amounts, audio_context);

        {
            f32 position_for_gui = {};
            for (auto const& s : voice.sound_sources) {
                if (!s.is_active) continue;
                if (s.source_data.tag != InstrumentType::Sampler) continue;
                auto const& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();
                if (sampler.region->trigger.trigger_event == sample_lib::TriggerEvent::NoteOff) continue;
                position_for_gui = (f32)(s.pos / sampler.data->num_frames);
            }

            constexpr f32 k_max_u16 = LargestRepresentableValue<u16>();
            voice.pool.voice_waveform_markers_for_gui.Write()[voice.index] = {
                .layer_index = (u8)voice.controller->layer_index,
                .position = (u16)(Clamp01(position_for_gui) * k_max_u16),
                .intensity = (u16)(Clamp01(voice.current_gain) * k_max_u16),
            };
            voice.pool.voice_vol_env_markers_for_gui.Write()[voice.index] = {
                .on = voice.controller->vol_env_on && !voice.disable_vol_env && !voice.vol_env.IsIdle(),
                .layer_index = (u8)voice.controller->layer_index,
                .state = voice.vol_env.state,
                .pos = (u16)(Clamp01(voice.vol_env.output) * k_max_u16),
                .sustain_level = (u16)(Clamp01(voice.controller->vol_env.sustain_amount) * k_max_u16),
                .id = voice.id,
            };
            voice.pool.voice_fil_env_markers_for_gui.Write()[voice.index] = {
                .on = voice.controller->fil_env_amount != 0 && !voice.fil_env.IsIdle(),
                .layer_index = (u8)voice.controller->layer_index,
                .state = voice.fil_env.state,
                .pos = (u16)(Clamp01(voice.fil_env.output) * k_max_u16),
                .sustain_level = (u16)(Clamp01(voice.controller->fil_env.sustain_amount) * k_max_u16),
                .id = voice.id,
            };
        }

        if (block_result == VoiceBlockResult::End || !voice.num_active_voice_samples)
            EndVoiceInstantly(voice);

        voice.written_to_buffer_this_block = true;
    }

    static bool HasPitchLfo(Voice const& v) {
        return v.controller->lfo.on && v.controller->lfo.dest == param_values::LfoDestination::Pitch;
    }

    static bool HasPanLfo(Voice const& v) {
        return v.controller->lfo.on && v.controller->lfo.dest == param_values::LfoDestination::Pan;
    }

    static bool HasFilterLfo(Voice const& v) {
        return v.controller->lfo.on && v.controller->lfo.dest == param_values::LfoDestination::Filter;
    }

    static bool HasVolumeLfo(Voice const& v) {
        return v.controller->lfo.on && v.controller->lfo.dest == param_values::LfoDestination::Volume;
    }

    static f64 PitchRatio(Voice const& voice,
                          VoiceSoundSource& s,
                          f32 current_lfo_value,
                          AudioProcessingContext const& context) {
        static constexpr f64 k_lfo_range_semitones = 1;

        auto pitch_ratio = s.pitch_ratio;
        if (HasPitchLfo(voice)) {
            auto const pitch_addition_in_semitones =
                (f64)current_lfo_value * (f64)voice.controller->lfo.amount * k_lfo_range_semitones;
            pitch_ratio *= Exp2(pitch_addition_in_semitones / 12.0);
        }
        return s.pitch_ratio_smoother.LowPass(pitch_ratio, (f64)context.one_pole_smoothing_cutoff_0_2ms);
    }

    struct SampleSourceReturnValue {
        f32x2 frame;
        SampleState state;
    };

    static SampleSourceReturnValue NextSampleFrame(Voice const& voice,
                                                   VoiceSoundSource& s,
                                                   f32 current_lfo_value,
                                                   AudioProcessingContext const& context) {
        auto& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();

        auto out = GetSampleFrame(*sampler.data, sampler.loop, sampler.loop_and_reverse_flags, s.pos);
        if (!(sampler.loop_and_reverse_flags &
              (loop_and_reverse_flags::LoopedManyTimes | loop_and_reverse_flags::CurrentlyReversed))) {
            auto const pos = s.pos - sampler.region->audio_props.start_offset_frames;
            if (pos < sampler.region->audio_props.fade_in_frames) {
                auto const percent = pos / (f64)sampler.region->audio_props.fade_in_frames;
                // Quarter-sine fade in.
                auto const amount = trig_table_lookup::SinTurnsPositive((f32)percent * 0.25f);
                out *= amount;
            }
        }

        return {
            .frame = out,
            .state = IncrementSamplePlaybackPos(sampler.loop,
                                                sampler.loop_and_reverse_flags,
                                                s.pos,
                                                PitchRatio(voice, s, current_lfo_value, context),
                                                (f64)sampler.data->num_frames),
        };
    }

    static SampleState AddSampleDataOntoBuffer(Voice const& voice,
                                               VoiceSoundSource& s,
                                               Span<f32x2> buffer,
                                               Span<f32 const> lfo_amounts,
                                               AudioProcessingContext const& context) {
        if (auto& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();
            sampler.region->timbre_layering.layer_range) {
            for (auto [frame_index, val] : Enumerate(buffer)) {
                auto const sample_frame = ({
                    SampleSourceReturnValue f;
                    if (auto const v =
                            sampler.xfade_vol_smoother.LowPass(sampler.xfade_vol,
                                                               context.one_pole_smoothing_cutoff_10ms);
                        v > 0.0001f) {
                        f = NextSampleFrame(voice, s, lfo_amounts[frame_index], context);
                        f.frame *= v;
                    } else {
                        auto const pitch_ratio1 = PitchRatio(voice, s, lfo_amounts[frame_index], context);
                        f.frame = 0.0f;
                        f.state = IncrementSamplePlaybackPos(sampler.loop,
                                                             sampler.loop_and_reverse_flags,
                                                             s.pos,
                                                             pitch_ratio1,
                                                             (f64)sampler.data->num_frames);
                    }
                    f;
                });

                val += sample_frame.frame * s.amp;

                if (sample_frame.state == SampleState::Ended) return sample_frame.state;
            }
        } else {
            for (auto [frame_index, val] : Enumerate(buffer)) {
                auto const sample_frame = NextSampleFrame(voice, s, lfo_amounts[frame_index], context);

                val += sample_frame.frame * s.amp;

                if (sample_frame.state == SampleState::Ended) return sample_frame.state;
            }
        }

        return SampleState::Alive;
    }

    static constexpr unsigned int k_max_fast_rand = 0x7FFF;

    static void ScaleDownRandom(f32x2& val) {
        f32x2 constexpr k_random_num_to_01_scale = 1.0f / (f32)k_max_fast_rand;
        f32x2 constexpr k_scale = 0.5f * 0.2f;
        val = ((val * k_random_num_to_01_scale) * 2 - 1) * k_scale;
    }

    static int FastRand(unsigned int& seed) {
        seed = (214013 * seed + 2531011);
        return (seed >> 16) & k_max_fast_rand;
    }

    static void FillBufferWithSampleData(Voice& voice,
                                         Span<f32x2> buffer,
                                         Span<f32 const> lfo_amounts,
                                         AudioProcessingContext const& context) {
        ZoneScoped;
        for (auto& s : voice.sound_sources) {
            if (!s.is_active) continue;
            switch (s.source_data.tag) {
                case InstrumentType::Sampler: {
                    if (AddSampleDataOntoBuffer(voice, s, buffer, lfo_amounts, context) ==
                        SampleState::Ended) {
                        s.is_active = false;
                        voice.num_active_voice_samples--;
                    }
                    break;
                }
                case InstrumentType::WaveformSynth: {
                    ASSERT_HOT(voice.num_active_voice_samples == 1);
                    switch (s.source_data.Get<WaveformType>()) {
                        case WaveformType::Sine: {
                            for (auto [frame_index, val] : Enumerate(buffer)) {
                                // This is an arbitrary scale factor to make the sine more in-line with other
                                // waveform levels. It's important to keep this the same for backwards
                                // compatibility.
                                constexpr f32x2 k_sine_scale = 0.2f;

                                val = f32x2(trig_table_lookup::SinTurnsPositive((f32)s.pos)) * k_sine_scale;

                                s.pos += PitchRatio(voice, s, lfo_amounts[frame_index], context);
                                if (s.pos > (1 << 24)) [[unlikely]] // prevent overflow
                                    s.pos -= (1 << 24);
                            }

                            break;
                        }
                        case WaveformType::WhiteNoiseMono: {
                            for (auto& val : buffer) {
                                val = (f32)FastRand(voice.pool.random_seed);
                                ScaleDownRandom(val);
                            }
                            break;
                        }
                        case WaveformType::WhiteNoiseStereo: {
                            for (auto& val : buffer) {
                                val = {(f32)FastRand(voice.pool.random_seed),
                                       (f32)FastRand(voice.pool.random_seed)};
                                ScaleDownRandom(val);
                            }

                            for (auto& val : buffer) {
                                alignas(16) f32 samples[2];
                                StoreToAligned(samples, val);
                                DoStereoWiden(0.7f, samples[0], samples[1], samples[0], samples[1]);
                                val = LoadAlignedToType<f32x2>(samples);
                            }
                            break;
                        }
                        case WaveformType::Count: PanicIfReached(); break;
                    }
                    break;
                }
                case InstrumentType::None: PanicIfReached(); break;
            }
        }
    }

    [[nodiscard]] static VoiceBlockResult ApplyGain(Voice& voice,
                                                    Span<f32x2>& buffer,
                                                    Span<f32 const> lfo_amounts,
                                                    AudioProcessingContext const& context) {
        ZoneScoped;

        auto const env_on = voice.controller->vol_env_on && !voice.disable_vol_env;

        // LFO parameters
        auto const has_volume_lfo = HasVolumeLfo(voice);
        auto const has_pan_lfo = HasPanLfo(voice);
        auto const lfo_amp = (has_volume_lfo || has_pan_lfo) ? voice.controller->lfo.amount : 0.0f;
        auto const lfo_base = has_volume_lfo ? (1.0f - (Fabs(lfo_amp) / 2.0f)) : 1.0f;
        auto const lfo_half_amp = lfo_amp / 2.0f;

        f32 final_gain1 = 1.0f;

        for (u32 frame = 0; frame < buffer.size; frame += 2) {
            // Calculate envelope gain
            f32 env1 = env_on ? voice.vol_env.Process(voice.controller->vol_env) : 1.0f;
            f32 env2 = 1.0f;
            auto const frame_p1 = frame + 1;
            auto const frame_p1_is_valid = frame_p1 != buffer.size;
            if (frame_p1_is_valid) env2 = env_on ? voice.vol_env.Process(voice.controller->vol_env) : 1.0f;

            // Calculate volume LFO gain
            f32 vol_lfo1 = 1.0f;
            f32 vol_lfo2 = 1.0f;
            if (has_volume_lfo) {
                vol_lfo1 = lfo_base + lfo_amounts[frame] * lfo_half_amp;
                vol_lfo2 = (frame_p1_is_valid) ? lfo_base + (lfo_amounts[frame_p1] * lfo_half_amp) : vol_lfo1;
            }

            // Calculate fade gain
            f32 fade1 = voice.volume_fade.GetFade() * voice.aftertouch_multiplier;
            f32 fade2 = 1.0f;
            if (frame_p1_is_valid) fade2 = voice.volume_fade.GetFade() * voice.aftertouch_multiplier;

            // Calculate pan positions
            auto pan_pos1 = voice.controller->pan_pos;
            auto pan_pos2 = pan_pos1;
            if (has_pan_lfo) {
                pan_pos1 += (lfo_amounts[frame] * lfo_amp);
                pan_pos1 = Clamp(pan_pos1, -1.0f, 1.0f);
                if (frame_p1_is_valid) {
                    pan_pos2 += (lfo_amounts[frame_p1] * lfo_amp);
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
                voice.gain_smoother.LowPass(gain_1, context.one_pole_smoothing_cutoff_1ms);
            auto const smooth_gain_2 =
                voice.gain_smoother.LowPass(gain_2, context.one_pole_smoothing_cutoff_1ms);

            // Apply gains to the buffer
            buffer[frame + 0] *= smooth_gain_1;
            if (frame_p1_is_valid) buffer[frame + 1] *= smooth_gain_2;

            // Check for early termination conditions
            if ((env_on && voice.vol_env.IsIdle()) || voice.volume_fade.IsSilent()) {
                for (auto const i : Range<usize>(frame + 2, buffer.size))
                    buffer[i] = 0.0f;
                voice.current_gain = final_gain2;
                buffer = buffer.SubSpan(0, frame_p1);
                return VoiceBlockResult::End;
            }
        }

        voice.current_gain = final_gain1;
        return VoiceBlockResult::Continue;
    }

    static void ApplyFilter(Voice& voice,
                            Span<f32x2>& buffer,
                            Span<f32 const> lfo_amounts,
                            AudioProcessingContext const& context) {
        ZoneScoped;
        auto const filter_type = voice.controller->filter_type;
        auto const has_filter_lfo = HasFilterLfo(voice);

        for (auto const [frame_index, val] : Enumerate(buffer)) {
            auto env = voice.fil_env.Process(voice.controller->fil_env);
            if (auto const filter_mix =
                    voice.filter_mix_smoother.LowPass((f32)voice.controller->filter_on,
                                                      context.one_pole_smoothing_cutoff_10ms);
                filter_mix > 0.00001f) {

                auto cut = voice.controller->sv_filter_cutoff_linear +
                           ((env - 0.5f) * voice.controller->fil_env_amount);
                auto res = voice.controller->sv_filter_resonance;

                if (has_filter_lfo) cut += (lfo_amounts[frame_index] * voice.controller->lfo.amount) / 2;

                f32 res_change {};
                res = voice.filter_resonance_smoother.LowPass(res,
                                                              context.one_pole_smoothing_cutoff_1ms,
                                                              &res_change);
                f32 cut_change {};
                cut = voice.filter_linear_cutoff_smoother.LowPass(cut,
                                                                  context.one_pole_smoothing_cutoff_1ms,
                                                                  &cut_change);

                if (has_filter_lfo || cut_change > 0.00001f || res_change > 0.00001f) {
                    cut = sv_filter::LinearToHz(Clamp(cut, 0.0f, 1.0f));
                    voice.filter_coeffs.Update(context.sample_rate, cut, res);
                }

                f32x2 wet_buf;
                sv_filter::Process(val, wet_buf, voice.filters, filter_type, voice.filter_coeffs);

                if (filter_mix < 0.999f)
                    val = val + filter_mix * (wet_buf - val);
                else
                    val = wet_buf;
            } else {
                voice.filters = {};
                voice.filter_resonance_smoother.Reset();
                voice.filter_linear_cutoff_smoother.Reset();
            }
        }
    }

    static void FillLfoBuffer(Voice& voice, Span<f32> lfo_amounts) {
        ZoneScoped;
        for (auto& amount : lfo_amounts)
            amount = voice.lfo.Tick();
    }
};

void OnThreadPoolExec(VoicePool& pool, u32 task_index) {
    auto& voice = pool.voices[task_index];
    if (!voice.is_active) return;
    VoiceProcessor::Process(voice,
                            *pool.multithread_processing.audio_processing_context,
                            pool.multithread_processing.num_frames);
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

void ProcessVoices(VoicePool& pool, u32 num_frames, AudioProcessingContext const& context) {
    ZoneScoped;
    for (auto& v : pool.voices)
        v.written_to_buffer_this_block = false;

    if (pool.num_active_voices.Load(LoadMemoryOrder::Relaxed) == 0) return;

    auto const thread_pool =
        (clap_host_thread_pool const*)context.host.get_extension(&context.host, CLAP_EXT_THREAD_POOL);

    bool failed_multithreaded_process = false;
    if (thread_pool && thread_pool->request_exec) {
        pool.multithread_processing.num_frames = num_frames;
        pool.multithread_processing.audio_processing_context = &context;
        failed_multithreaded_process = !thread_pool->request_exec(&context.host, k_num_voices);
    }

    if (!thread_pool || failed_multithreaded_process) {
        for (auto& v : pool.voices)
            if (v.is_active) VoiceProcessor::Process(v, context, num_frames);
    }

    for (auto& v : pool.voices) {
        if (v.written_to_buffer_this_block) {
            if constexpr (RUNTIME_SAFETY_CHECKS_ON && PRODUCTION_BUILD) {
                for (auto const frame : Range(num_frames)) {
                    auto const& val = v.buffer[frame];
                    ASSERT(All(val >= -k_erroneous_sample_value && val <= k_erroneous_sample_value));
                }
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
}
