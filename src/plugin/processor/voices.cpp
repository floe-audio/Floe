// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "voices.hpp"

#include <clap/ext/thread-pool.h>

#include "foundation/foundation.hpp"
#include "tests/framework.hpp"
#include "utils/debug/tracy_wrapped.hpp"

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
        set_xfade_smoother(*voice_sample_1, QuarterSineFade(1 - pos));
        set_xfade_smoother(*voice_sample_2, QuarterSineFade(pos));
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

static f64 EffectiveStartOffset(f32 sample_offset_01, u32 start_offset_frames, u32 num_frames) {
    return Max<f64>((f64)sample_offset_01 * (f64)(num_frames - 1),
                    Min<f64>((f64)(num_frames - 1), (f64)start_offset_frames));
}

static Optional<BoundsCheckedLoop> LoopForSource(VoiceProcessingController const& controller,
                                                 VoiceSoundSource::SampleSource const& sampler) {
    return controller.vol_env_on ? ConfigureLoop(controller.loop_mode,
                                                 sampler.region->loop,
                                                 sampler.data->num_frames,
                                                 controller.loop)
                                 : k_nullopt;
}

void UpdateLoopInfo(Voice& v) {
    for (auto& s : v.sound_sources) {
        if (!s.is_active) continue;
        if (s.source_data.tag != InstrumentType::Sampler) continue;

        auto& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();
        if (sampler.region->trigger.trigger_event == sample_lib::TriggerEvent::NoteOff) continue;

        UpdatePlayhead(sampler.playhead,
                       LoopForSource(*v.controller, sampler),
                       v.controller->reverse,
                       sampler.data->num_frames);
    }
}

// SIMD version where 2 pan positions are processed at once.
// The result is a vector of 4 floats: {left 1, right 1, left 2, right 2}.
// Constant power pan law (AKA -3dB centre).
// pan_pos is in [-1, 1] where -1 is hard left and 1 is hard right.
inline f32x4 EqualPanGains2(f32x2 pan_pos) {
    auto const half_pos = pan_pos * 0.5f;
    auto const right = QuarterSineFade(half_pos + 0.5f);
    auto const left = QuarterSineFade(0.5f - half_pos);
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

    voice.volume_fade.ForceSetFullVolume();
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
    voice.gain_smoother.prev_output = 0.0f;
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
                ASSERT(s_sampler.data != nullptr);

                s.pitch_ratio = CalculatePitchRatio(RootKey(voice, s), s, params.initial_pitch, sample_rate);
                s.pitch_ratio_smoother.Reset();

                if (voice_controller.play_mode == param_values::PlayMode::GranularFixed) {
                    // GranularFixed ignores loops and sample offset; position is user-controlled.
                    ResetPlayhead(s_sampler.playhead,
                                  0,
                                  k_nullopt,
                                  voice_controller.reverse,
                                  s_sampler.data->num_frames);
                } else {
                    auto const offs = EffectiveStartOffset(voice_controller.sample_offset_01,
                                                           s_params.region.audio_props.start_offset_frames,
                                                           s_sampler.data->num_frames);

                    ResetPlayhead(s_sampler.playhead,
                                  offs,
                                  ConfigureLoop(voice_controller.loop_mode,
                                                s_sampler.region->loop,
                                                s_sampler.data->num_frames,
                                                voice_controller.loop),
                                  voice_controller.reverse,
                                  s_sampler.data->num_frames);
                }
            }
            for (u32 i = voice.num_active_voice_samples; i < k_max_num_voice_sound_sources; ++i)
                voice.sound_sources[i].is_active = false;

            UpdateXfade(voice, sampler.initial_timbre_param_value_01, true);

            if (IsGranular(voice_controller.play_mode)) voice.grain_pool.Reset(sample_rate);

            if (sampler.voice_sample_params.size) {
                if (sampler.voice_sample_params[0].region.trigger.trigger_event ==
                    sample_lib::TriggerEvent::NoteOn)
                    pool.last_activated_audio_data_hash[voice_controller.layer_index].Store(
                        sampler.voice_sample_params[0].audio_data.hash,
                        StoreMemoryOrder::Relaxed);
            }

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
            s.source_data = VoiceSoundSource::WaveformSource {
                .type = waveform.type,
                .pos = 0,
            };
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
        ZoneTextVF(process, "Voice %u", voice.index);
        ASSERT_HOT(voice.is_active);
        ASSERT_HOT(!voice.processed_this_block);

        voice.processed_this_block = true;

        auto output = Span<f32x2> {voice.buffer.data, num_frames};
        Fill(output, 0.0f);

        if (voice.frames_before_starting != 0) {
            auto const silent_frames = Min(num_frames, voice.frames_before_starting);
            voice.frames_before_starting -= silent_frames;
            output.RemovePrefix(silent_frames);
        }

        Array<f32, k_block_size_max> lfo_amounts_buffer;
        auto lfo_amounts = Span<f32> {lfo_amounts_buffer}.SubSpan(0, num_frames);
        FillLfoBuffer(voice, lfo_amounts);

        FillBufferWithSampleData(voice, output, lfo_amounts, audio_context);

        auto const block_result = ApplyGain(voice, output, lfo_amounts, audio_context);
        ApplyFilter(voice, output, lfo_amounts, audio_context);

        {
            f64 position_for_gui = {};
            for (auto const& s : voice.sound_sources) {
                if (!s.is_active) continue;
                if (s.source_data.tag != InstrumentType::Sampler) continue;
                auto const& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();
                if (sampler.region->trigger.trigger_event == sample_lib::TriggerEvent::NoteOff) continue;
                position_for_gui = sampler.playhead.RealFramePos(sampler.data->num_frames)
                                       .ValueOr(sampler.data->num_frames) /
                                   (f64)sampler.data->num_frames;
            }

            constexpr f32 k_max_u16 = LargestRepresentableValue<u16>();

            // Compute spread bounds for granular modes.
            VoiceWaveformMarkerForGui::SpreadRegion spread_1 {};
            VoiceWaveformMarkerForGui::SpreadRegion spread_2 {};

            if (IsGranular(voice.controller->play_mode)) {
                for (auto const& s : voice.sound_sources) {
                    if (!s.is_active || s.source_data.tag != InstrumentType::Sampler) continue;
                    auto const& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();
                    if (sampler.region->trigger.trigger_event == sample_lib::TriggerEvent::NoteOff) continue;

                    auto const nf = sampler.data->num_frames;
                    auto const is_fixed =
                        voice.controller->play_mode == param_values::PlayMode::GranularFixed;
                    auto const bounds = ComputeGrainSpreadBounds(sampler.playhead.frame_pos,
                                                                 voice.controller->granular.spread,
                                                                 sampler.playhead.loop,
                                                                 nf,
                                                                 is_fixed);

                    // Convert from frame_pos space to audio-data 0-1 space.
                    auto to_audio_01 = [&](f64 fp) -> f64 {
                        auto const fi = (u32)Clamp(fp, 0.0, (f64)(nf - 1));
                        auto const real = sampler.playhead.inverse_data_lookup ? ((nf - 1) - fi) : fi;
                        return (f64)real / (f64)nf;
                    };

                    auto const s1 = to_audio_01(bounds.region_1.start);
                    auto const e1 = to_audio_01(bounds.region_1.end);
                    spread_1.start = (u16)(Min(s1, e1) * (f64)k_max_u16);
                    spread_1.end = (u16)(Max(s1, e1) * (f64)k_max_u16);

                    if (bounds.has_region_2) {
                        auto const s2 = to_audio_01(bounds.region_2.start);
                        auto const e2 = to_audio_01(bounds.region_2.end);
                        spread_2.start = (u16)(Min(s2, e2) * (f64)k_max_u16);
                        spread_2.end = (u16)(Max(s2, e2) * (f64)k_max_u16);
                    }
                    break; // Use the first active sampler source.
                }
            }

            voice.pool.voice_waveform_markers_for_gui.Write()[voice.index] = {
                .position = (u16)(Clamp01(position_for_gui) * (f64)k_max_u16),
                .intensity = (u16)(Clamp01(voice.current_gain) * k_max_u16),
                .layer_index = voice.controller->layer_index,
                .spread_region_1 = spread_1,
                .spread_region_2 = spread_2,
            };
            voice.pool.voice_vol_env_markers_for_gui.Write()[voice.index] = {
                .on = voice.controller->vol_env_on && !voice.disable_vol_env && !voice.vol_env.IsIdle(),
                .layer_index = voice.controller->layer_index,
                .state = voice.vol_env.state,
                .pos = (u16)(Clamp01(voice.vol_env.output) * k_max_u16),
                .sustain_level = (u16)(Clamp01(voice.controller->vol_env.sustain_amount) * k_max_u16),
                .id = voice.id,
            };
            voice.pool.voice_fil_env_markers_for_gui.Write()[voice.index] = {
                .on = voice.controller->fil_env_amount != 0 && !voice.fil_env.IsIdle(),
                .layer_index = voice.controller->layer_index,
                .state = voice.fil_env.state,
                .pos = (u16)(Clamp01(voice.fil_env.output) * k_max_u16),
                .sustain_level = (u16)(Clamp01(voice.controller->fil_env.sustain_amount) * k_max_u16),
                .id = voice.id,
            };

            // Publish grain markers for GUI.
            auto& grain_markers = voice.pool.grain_markers_for_gui.Write()[voice.index];
            grain_markers.num_active = 0;
            if (IsGranular(voice.controller->play_mode)) {
                grain_markers.layer_index = voice.controller->layer_index;
                grain_markers.intensity = (u16)(Clamp01(voice.current_gain) * k_max_u16);

                // Find first active sampler source for position normalization.
                u32 ref_num_frames = 0;
                for (auto const& s : voice.sound_sources) {
                    if (!s.is_active || s.source_data.tag != InstrumentType::Sampler) continue;
                    ref_num_frames = s.source_data.Get<VoiceSoundSource::SampleSource>().data->num_frames;
                    break;
                }

                if (ref_num_frames) {
                    for (auto const& grain : voice.grain_pool.grains) {
                        if (!grain.active || grain_markers.num_active >= k_max_grains_per_voice) continue;
                        auto const pos = grain.playhead.RealFramePos(ref_num_frames);
                        if (pos) {
                            grain_markers.grains[grain_markers.num_active++] = {
                                .position = (u16)((*pos / (f64)ref_num_frames) *
                                                  (f64)LargestRepresentableValue<u16>()),
                            };
                        }
                    }
                }
            }
        }

        if (block_result == VoiceBlockResult::End || !voice.num_active_voice_samples)
            EndVoiceInstantly(voice);

        voice.produced_audio_this_block = true;
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

    static f32x2 NextSampleFrame(Voice const& voice,
                                 VoiceSoundSource& s,
                                 f32 current_lfo_value,
                                 AudioProcessingContext const& context) {
        auto& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();

        auto out = GetSampleFrame(*sampler.data, sampler.playhead);

        // Do the sample fade-in if it's the first time the sample is played.
        if (!sampler.playhead.loop ||
            (sampler.playhead.loop && !sampler.playhead.loop->only_use_frames_within_loop)) {
            auto const real_pos = sampler.playhead.RealFramePos(sampler.data->num_frames);
            if (real_pos) {
                if (auto const pos = *real_pos - sampler.region->audio_props.start_offset_frames;
                    pos >= 0 && pos < sampler.region->audio_props.fade_in_frames) {
                    auto const percent = pos / (f64)sampler.region->audio_props.fade_in_frames;
                    // Quarter-sine fade in.
                    auto const amount = QuarterSineFade((f32)percent);
                    out *= amount;
                }
            }
        }

        IncrementPlaybackPos(sampler.playhead,
                             PitchRatio(voice, s, current_lfo_value, context),
                             sampler.data->num_frames);
        return out;
    }

    static bool AddSampleDataOntoBuffer(Voice const& voice,
                                        VoiceSoundSource& s,
                                        Span<f32x2> buffer,
                                        Span<f32 const> lfo_amounts,
                                        AudioProcessingContext const& context) {
        if (auto& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();
            sampler.region->timbre_layering.layer_range) {
            for (auto [frame_index, val] : Enumerate(buffer)) {
                if (PlaybackEnded(sampler.playhead, sampler.data->num_frames)) return false;

                auto const sample_frame = ({
                    f32x2 f;
                    if (auto const v =
                            sampler.xfade_vol_smoother.LowPass(sampler.xfade_vol,
                                                               context.one_pole_smoothing_cutoff_10ms);
                        v > 0.0001f) {
                        f = NextSampleFrame(voice, s, lfo_amounts[frame_index], context);
                        f *= v;
                    } else {
                        auto const pitch_ratio1 = PitchRatio(voice, s, lfo_amounts[frame_index], context);
                        f = 0.0f;
                        IncrementPlaybackPos(sampler.playhead, pitch_ratio1, sampler.data->num_frames);
                    }
                    f;
                });

                val += sample_frame * s.amp;
            }
        } else {
            for (auto [frame_index, val] : Enumerate(buffer)) {
                if (PlaybackEnded(sampler.playhead, sampler.data->num_frames)) return false;

                auto const sample_frame = NextSampleFrame(voice, s, lfo_amounts[frame_index], context);

                val += sample_frame * s.amp;
            }
        }

        return true;
    }

    // Returns false if playback has ended.
    static bool AddGranularSampleDataOntoBuffer(Voice& voice,
                                                VoiceSoundSource& s,
                                                u8 source_index,
                                                Span<f32x2> buffer,
                                                Span<f32 const> lfo_amounts,
                                                AudioProcessingContext const& context) {
        ZoneNamedN(granular_zone, "Granular Synthesis", true);
        auto const& ctrl = *voice.controller;
        auto const is_fixed = ctrl.play_mode == param_values::PlayMode::GranularFixed;
        auto& pool = voice.grain_pool;
        auto& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();
        auto const num_frames = sampler.data->num_frames;

#ifdef TRACY_ENABLE
        {
            u32 num_active = 0;
            for (auto const& g : pool.grains)
                if (g.active) num_active++;
            ZoneTextVF(granular_zone,
                       "%u active grains, %u frames, %u ch",
                       num_active,
                       num_frames,
                       sampler.data->channels);
            TracyPlot("Active Grains", (s64)num_active);
        }
#endif

        // Running grains start at frame 0, but for newly spawned grains there might be an offset.
        Array<u8, k_max_grains_per_voice> grain_start_frame {};
        static_assert(LargestRepresentableValue<decltype(grain_start_frame)::ValueType>() >=
                      k_block_size_max);

        // Pre-compute source-wide values - all grains will refer to these.
        Array<f64, k_block_size_max> pitch_ratios;
        alignas(alignof(f32x4)) Array<f32x2, k_block_size_max> xfade_vols;
        {
            ZoneNamedN(precompute, "Granular: Precompute", true);

            for (auto const frame_index : Range(buffer.size))
                pitch_ratios[frame_index] = PitchRatio(voice, s, lfo_amounts[frame_index], context);

            for (auto const frame_index : Range(buffer.size)) {
                xfade_vols[frame_index] =
                    sampler.xfade_vol_smoother.LowPass(sampler.xfade_vol,
                                                       context.one_pole_smoothing_cutoff_10ms);
            }
            for (auto const frame_index : Range<usize>(buffer.size, k_block_size_max))
                xfade_vols[frame_index] = 0;
        }

        // The frame at which the source becomes dead (past-end without a loop). If the source
        // stays alive for the entire block this remains == buffer.size.
        auto source_dead_frame = buffer.size;

        // --- Pass 1: advance main playhead and spawn grains. ---
        {
            ZoneNamedN(granular_pass1, "Granular: Spawn Grains", true);

            if (is_fixed) sampler.playhead.frame_pos = (f64)ctrl.granular.position * (f64)(num_frames - 1);
            auto const rate_ratio = (f64)sampler.data->sample_rate / (f64)context.sample_rate;

            for (auto const frame_index : Range(buffer.size)) {
                if (is_fixed) {
                    // Fixed mode: source never dies.
                } else if (!PlaybackEnded(sampler.playhead, num_frames)) {
                    IncrementPlaybackPos(sampler.playhead, (f64)ctrl.granular.speed * rate_ratio, num_frames);
                } else {
                    source_dead_frame = frame_index;
                    break;
                }

                // Spawn check for this source.
                if (pool.spawn_counters[source_index] == 0) {
                    // Find first inactive grain slot.
                    Grain* new_grain = nullptr;
                    usize new_grain_index = 0;
                    for (auto [gi, g] : Enumerate(pool.grains)) {
                        if (!g.active) {
                            new_grain = &g;
                            new_grain_index = gi;
                            break;
                        }
                    }

                    // It's unlikely we couldn't find an inactive grain since we have a stealing process that
                    // should have already run. However, if it got to this state then we just don't spawn and
                    // try again next block.
                    if (!new_grain) {
                        // Push the spawn counter to the next block since it's wasteful to keep checking for
                        // inactive grains every frame - activeness only changes at the end of this block.
                        pool.spawn_counters[source_index] = (u32)(buffer.size - frame_index);
                        continue;
                    }

                    // We need some random floats in a few places, we already have SIMD support for
                    // generating 4 randoms at once, so we can save a few instructions.
                    auto const r1 = FastRand01(voice.random_seed);
                    auto const r2 = FastRand01(voice.random_seed);
                    auto const spread_rand = r1[0];
                    auto const length_jitter_rand = r1[1];
                    auto const pan_rand = r1[2];
                    auto const detune_rand = r1[3];
                    auto const density_jitter_rand = r2[0];

                    auto const spread_fraction = GrainSpreadParamToFraction(ctrl.granular.spread);
                    auto const spread_offset = (f64)(spread_rand * spread_fraction) * (f64)num_frames;

                    auto const grain_pos = sampler.playhead.frame_pos + spread_offset;

                    if (InitGrainPlayhead(*new_grain, grain_pos, sampler.playhead, num_frames, is_fixed)) {
                        new_grain->source_index = source_index;
                        new_grain->env_phase_inc = ({
                            auto const base =
                                GrainLengthParamToSamples(ctrl.granular.length, context.sample_rate);
                            constexpr f32 k_jitter_amount = 0.15f;
                            auto const scale = 1.0f + ((length_jitter_rand * 2.0f - 1.0f) * k_jitter_amount);
                            1.0f / (f32)Max(1u, (u32)(scale * (f32)base));
                        });
                        new_grain->env_phase = 0;
                        new_grain->active = true;
                        new_grain->steal_fade = 1.0f;
                        new_grain->steal_fade_dec = 0;
                        pool.num_active_non_stealing++;
                        grain_start_frame[new_grain_index] = (u8)frame_index;

                        // Random pan.
                        {
                            new_grain->pan_pos = (pan_rand * 2.0f - 1.0f) * ctrl.granular.random_pan;
                        }

                        // Random detune.
                        if (ctrl.granular.random_detune > 0.0001f) {
                            auto const detune_semitones =
                                (f64)((detune_rand * 2.0f) - 1.0f) * (f64)ctrl.granular.random_detune;
                            new_grain->detune_ratio = Exp2(detune_semitones / 12.0);
                        } else {
                            new_grain->detune_ratio = 1.0;
                        }

                        // If the grain pool is nearing full, we initiate quick fade-outs for grains to that
                        // hopefully by the time we next want to spawn a grain, we have inactive ones to pick
                        // from.
                        if (pool.num_active_non_stealing > k_grain_steal_threshold) {
                            // We pick one randomly to avoid any unpleasant-sounding regularity.
                            auto const pick = FastRand(voice.random_seed).x % pool.num_active_non_stealing;
                            u32 index = 0;
                            for (auto& g : pool.grains) {
                                if (!g.active || g.IsStealing() || &g == new_grain) continue;
                                if (index == pick) {
                                    g.steal_fade_dec = pool.steal_fade_dec_value;
                                    pool.num_active_non_stealing--;
                                    break;
                                }
                                index++;
                            }
                        }
                    }

                    // Set the new spawn point.
                    {
                        // 'Density' controls how often grains are spawned, but we add a little random
                        // jitter too to avoid the machine-gun affect that could occur in some particular
                        // situations.
                        auto const base_interval =
                            GrainsParamToSpawnInterval(ctrl.granular.density, context.sample_rate);
                        constexpr f32 k_jitter_amount = 0.25f;
                        auto const scale = 1.0f + ((density_jitter_rand * 2.0f - 1.0f) * k_jitter_amount);
                        pool.spawn_counters[source_index] = Max(1u, (u32)(scale * (f32)base_interval));
                    }
                } else {
                    pool.spawn_counters[source_index]--;
                }
            }
        }

        // --- Pass 2: process each grain across its full range of the buffer. ---
        {
            ZoneNamedN(granular_pass2, "Granular: Process Grains", true);

            // Grain envelope shape constant: smoothing controls how much of the
            // grain's duration is used for fade-in/fade-out. Hoisted here because
            // it's the same for every grain in this voice.
            auto const env_inv_fade = 1.0f / Max(ctrl.granular.smoothing * 0.5f, 0.001f);

            for (auto [grain_index, grain] : Enumerate(pool.grains)) {
                if (!grain.active || grain.source_index != source_index) continue;

                auto const start = grain_start_frame[grain_index];
                auto end = buffer.size;

                // --- Fetch samples and advance playhead ---
                alignas(alignof(f32x4)) Array<f32x2, k_block_size_max> grain_samples {};
                {
                    ZoneNamedN(fetch_zone, "Grain: GetSampleFrame", true);

                    for (auto const i : Range<usize>(start, buffer.size)) {
                        if (!PlaybackEnded(grain.playhead, num_frames)) {
                            grain_samples[i] = GetSampleFrame(*sampler.data, grain.playhead);
                            IncrementPlaybackPos(grain.playhead,
                                                 pitch_ratios[i] * grain.detune_ratio,
                                                 num_frames);
                        } else {
                            end = i;
                            break;
                        }
                    }
                }

                alignas(alignof(f32x4)) Array<f32x2, k_block_size_max> grain_gains {};

                // --- Calculate gains ---
                {
                    ZoneNamedN(mix_zone, "Grain: Gain calc", true);

                    auto const amp = s.amp;
                    auto const grain_pan_gains = EqualPanGains2(f32x2(grain.pan_pos)).xy;

                    // Constants.
                    auto const amp4 =
                        __builtin_shufflevector(grain_pan_gains, grain_pan_gains, 0, 1, 0, 1) * amp;
                    for (u32 i = 0; i < k_block_size_max; i += 2)
                        *(f32x4*)(void*)(&grain_gains[i]) = amp4;

                    // Xfade.
                    for (u32 i = 0; i < k_block_size_max; i += 2)
                        *(f32x4*)(void*)(&grain_gains[i]) *= *(f32x4*)(void*)(&xfade_vols[i]);

                    // Envelopes.
                    {
                        static_assert(k_block_size_max % 4 == 0);
                        alignas(alignof(f32x4)) Array<f32, k_block_size_max> env_scalars;
                        {
                            auto const phase_inc = grain.env_phase_inc;
                            auto const steal_dec = grain.steal_fade_dec;
                            f32x4 phases = grain.env_phase + (phase_inc * f32x4 {0, 1, 2, 3});
                            f32x4 steals = grain.steal_fade - (steal_dec * f32x4 {0, 1, 2, 3});
                            auto const phase_inc4 = phase_inc * 4;
                            auto const steal_dec4 = steal_dec * 4;

                            for (u32 i = 0; i < k_block_size_max; i += 4) {
                                auto const rise = Clamp01(phases * env_inv_fade);
                                auto const fall = Clamp01((f32x4 {1, 1, 1, 1} - phases) * env_inv_fade);
                                auto const env = QuarterSineFade(rise) * QuarterSineFade(fall);
                                auto const fade = Max(steals, f32x4 {0, 0, 0, 0});
                                *(f32x4*)(void*)(&env_scalars[i]) = env * fade;
                                phases += phase_inc4;
                                steals -= steal_dec4;
                            }
                        }

                        // Zero out-of-range entries (before start and after end).
                        for (u32 i = 0; i < start; ++i)
                            env_scalars[i] = 0;
                        for (auto i = end; i < k_block_size_max; ++i)
                            env_scalars[i] = 0;

                        for (u32 i = 0; i < k_block_size_max; i += 2) {
                            auto const env_pair = *(f32x2*)(void*)(&env_scalars[i]);
                            auto const expanded = __builtin_shufflevector(env_pair, env_pair, 0, 0, 1, 1);
                            *(f32x4*)(void*)(&grain_gains[i]) *= expanded;
                        }
                    }

                    // Advance phases.
                    {
                        auto const frames_processed = (f32)(end - start);
                        grain.env_phase += grain.env_phase_inc * frames_processed;
                        grain.steal_fade -= grain.steal_fade_dec * frames_processed;
                        if (grain.env_phase >= 1.0f || grain.steal_fade <= 0.0f) {
                            grain.active = false;
                            if (grain.steal_fade > 0.0f) pool.num_active_non_stealing--;
                        }
                    }
                }

                // --- Mix into buffer ---
                {
                    ZoneNamedN(mix_zone, "Grain: Mix", true);

                    for (usize i = 0; i < buffer.size; i += 2) {
                        auto const samples = *(f32x4*)(void*)(&grain_samples[i]);
                        auto const gains = *(f32x4*)(void*)(&grain_gains[i]);
                        f32x4 buf;
                        __builtin_memcpy_inline(&buf, &buffer[i], sizeof(f32x4));
                        buf = Fma<f32x4>(samples, gains, buf);
                        __builtin_memcpy_inline(&buffer[i], &buf, sizeof(f32x4));
                    }
                }
            }
        }

        // --- Pass 3: check if the source should end. ---
        if (source_dead_frame < buffer.size) {
            bool any_grain_active = false;
            for (auto const& g : pool.grains) {
                if (g.active && g.source_index == source_index) {
                    any_grain_active = true;
                    break;
                }
            }
            if (!any_grain_active) return false;
        }

        return true;
    }

    static constexpr u32 k_max_fast_rand = 0x7FFF;

    static u32x4 FastRand(u32x4& seed) {
        seed = ((214013 * seed) + 2531011);
        return (seed >> 16) & k_max_fast_rand;
    }

    static f32x4 FastRand01(u32x4& seed) {
        return ConvertVector(FastRand(seed), f32x4) / (f32)k_max_fast_rand;
    }

    static f32x4 RandomWhiteNoiseSample(u32x4& seed) {
        auto constexpr k_scale = 0.5f * 0.2f;
        return ((FastRand01(seed) * 2.0f) - 1.0f) * k_scale;
    }

    static void FillBufferWithSampleData(Voice& voice,
                                         Span<f32x2> buffer,
                                         Span<f32 const> lfo_amounts,
                                         AudioProcessingContext const& context) {
        ZoneScoped;

        auto const is_granular = IsGranular(voice.controller->play_mode);
        u8 source_index = 0;
        for (auto& s : voice.sound_sources) {
            if (!s.is_active) continue;
            switch (s.source_data.tag) {
                case InstrumentType::Sampler: {
                    bool ok;
                    if (is_granular)
                        ok = AddGranularSampleDataOntoBuffer(voice,
                                                             s,
                                                             source_index,
                                                             buffer,
                                                             lfo_amounts,
                                                             context);
                    else
                        ok = AddSampleDataOntoBuffer(voice, s, buffer, lfo_amounts, context);
                    if (!ok) {
                        s.is_active = false;
                        voice.num_active_voice_samples--;
                    }
                    source_index++;
                    break;
                }
                case InstrumentType::WaveformSynth: {
                    ASSERT_HOT(voice.num_active_voice_samples == 1);
                    auto& waveform_source = s.source_data.Get<VoiceSoundSource::WaveformSource>();
                    switch (waveform_source.type) {
                        case WaveformType::Sine: {
                            for (auto [frame_index, val] : Enumerate(buffer)) {
                                // This is an arbitrary scale factor to make the sine more in-line with other
                                // waveform levels. It's important to keep this the same for backwards
                                // compatibility.
                                constexpr f32x2 k_sine_scale = 0.2f;

                                val = f32x2(trig_table_lookup::SinTurnsPositive((f32)waveform_source.pos)) *
                                      k_sine_scale;

                                waveform_source.pos +=
                                    PitchRatio(voice, s, lfo_amounts[frame_index], context);
                                if (waveform_source.pos > (1 << 24)) [[unlikely]] // prevent overflow
                                    waveform_source.pos -= (1 << 24);
                            }

                            break;
                        }
                        case WaveformType::WhiteNoiseMono: {
                            for (auto& val : buffer)
                                val = RandomWhiteNoiseSample(voice.random_seed).x;
                            break;
                        }
                        case WaveformType::WhiteNoiseStereo: {
                            for (auto& val : buffer)
                                val = RandomWhiteNoiseSample(voice.random_seed).xy;

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
                vol_lfo1 = lfo_base + (lfo_amounts[frame] * lfo_half_amp);
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

            // Apply gains to the buffer
            buffer[frame + 0] *= voice.gain_smoother.LowPass(gain_1, context.one_pole_smoothing_cutoff_0_2ms);
            if (frame_p1_is_valid)
                buffer[frame + 1] *=
                    voice.gain_smoother.LowPass(gain_2, context.one_pole_smoothing_cutoff_0_2ms);

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
                    val = val + (filter_mix * (wet_buf - val));
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
            amount = -voice.lfo.Tick();
    }
};

void OnThreadPoolExec(VoicePool& pool, u32 task_index) {
    pool.multithread_processing.fence.Load(LoadMemoryOrder::Acquire);

    if (task_index >= pool.multithread_processing.num_tasks)
        // If this happens the host is seriously misbehaving and there will be incorrect audio, but at least
        // we won't crash.
        return;

    auto& voice = pool.voices[pool.multithread_processing.task_index_to_voice_index[task_index]];
    VoiceProcessor::Process(voice,
                            *pool.multithread_processing.audio_processing_context,
                            pool.multithread_processing.num_frames);
}

void Reset(VoicePool& pool) {
    auto& waveform_markers = pool.voice_waveform_markers_for_gui.Write();
    auto& vol_env_markers = pool.voice_vol_env_markers_for_gui.Write();
    auto& fil_env_markers = pool.voice_fil_env_markers_for_gui.Write();
    auto& grain_markers = pool.grain_markers_for_gui.Write();
    for (auto const i : Range(k_num_voices)) {
        waveform_markers[i] = {};
        vol_env_markers[i] = {};
        fil_env_markers[i] = {};
        grain_markers[i] = {};
    }
    pool.voice_waveform_markers_for_gui.Publish();
    pool.voice_vol_env_markers_for_gui.Publish();
    pool.voice_fil_env_markers_for_gui.Publish();
    pool.grain_markers_for_gui.Publish();
}

void ProcessVoices(VoicePool& pool, u32 num_frames, AudioProcessingContext const& context) {
    ZoneScoped;
    for (auto& v : pool.voices) {
        v.processed_this_block = false;
        v.produced_audio_this_block = false;
    }

    if (pool.num_active_voices.Load(LoadMemoryOrder::Relaxed) == 0) return;

    if (auto const thread_pool =
            (clap_host_thread_pool const*)context.host.get_extension(&context.host, CLAP_EXT_THREAD_POOL);
        thread_pool && thread_pool->request_exec) {
        pool.multithread_processing.num_frames = num_frames;
        pool.multithread_processing.audio_processing_context = &context;
        pool.multithread_processing.num_tasks = 0;
        for (auto const& v : pool.voices)
            if (v.is_active)
                pool.multithread_processing
                    .task_index_to_voice_index[pool.multithread_processing.num_tasks++] = v.index;
        pool.multithread_processing.fence.Store(0, StoreMemoryOrder::Release);

        // NOTE: Bitwig 5.2 misbehaves with this: it doesn't call the on_thread_exec function for as many
        // tasks as we requested. It's fine though because we handle this by checking processed_this_block.
        thread_pool->request_exec(&context.host, pool.multithread_processing.num_tasks);
    }

    // Process all voices that haven't already been processed (possibly by the thread pool).
    for (auto& v : pool.voices)
        if (v.is_active && !v.processed_this_block) VoiceProcessor::Process(v, context, num_frames);

    for (auto& v : pool.voices) {
        if (v.produced_audio_this_block) {
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
            pool.grain_markers_for_gui.Write()[v.index] = {};
        }
    }

    pool.voice_waveform_markers_for_gui.Publish();
    pool.voice_vol_env_markers_for_gui.Publish();
    pool.voice_fil_env_markers_for_gui.Publish();
    pool.grain_markers_for_gui.Publish();
}

TEST_CASE(TestEqualPanGains) {
    constexpr f32 k_epsilon = 1e-3f;
    constexpr auto k_root_2_over_2 = k_sqrt_two<> / 2;

    SUBCASE("centre") {
        auto const gains = EqualPanGains2(f32x2 {0, 0});
        // {left1, right1, left2, right2}
        REQUIRE(ApproxEqual(gains[0], k_root_2_over_2, k_epsilon));
        REQUIRE(ApproxEqual(gains[1], k_root_2_over_2, k_epsilon));
        REQUIRE(ApproxEqual(gains[2], k_root_2_over_2, k_epsilon));
        REQUIRE(ApproxEqual(gains[3], k_root_2_over_2, k_epsilon));
    }

    SUBCASE("hard left") {
        auto const gains = EqualPanGains2(f32x2 {-1, -1});
        REQUIRE(ApproxEqual(gains[0], 1.0f, k_epsilon));
        REQUIRE(ApproxEqual(gains[1], 0.0f, k_epsilon));
    }

    SUBCASE("hard right") {
        auto const gains = EqualPanGains2(f32x2 {1, 1});
        REQUIRE(ApproxEqual(gains[0], 0.0f, k_epsilon));
        REQUIRE(ApproxEqual(gains[1], 1.0f, k_epsilon));
    }

    SUBCASE("constant power: left^2 + right^2 == 1") {
        for (int i = -10; i <= 10; i++) {
            auto const pan = (f32)i / 10.0f;
            auto const gains = EqualPanGains2(f32x2 {pan, pan});
            auto const left = gains[0];
            auto const right = gains[1];
            auto const power = (left * left) + (right * right);
            REQUIRE(ApproxEqual(power, 1.0f, k_epsilon));
        }
    }

    SUBCASE("symmetry: pan left == mirror of pan right") {
        for (int i = 0; i <= 10; i++) {
            auto const pan = (f32)i / 10.0f;
            auto const gains = EqualPanGains2(f32x2 {pan, -pan});
            // left of +pan should equal right of -pan
            REQUIRE(ApproxEqual(gains[0], gains[3], k_epsilon));
            REQUIRE(ApproxEqual(gains[1], gains[2], k_epsilon));
        }
    }

    return k_success;
}

TEST_REGISTRATION(RegisterVoiceTests) { REGISTER_TEST(TestEqualPanGains); }
