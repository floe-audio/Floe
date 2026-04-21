// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layer_processor.hpp"

#include "foundation/foundation.hpp"
#include "tests/framework.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

#include "param.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/filters.hpp"
#include "processing_utils/key_range.hpp"
#include "processing_utils/midi.hpp"
#include "processing_utils/peak_meter.hpp"
#include "processing_utils/synced_timings.hpp"
#include "processing_utils/volume_fade.hpp"
#include "voices.hpp"

static void UpdateLoopPointsForVoices(LayerProcessor& layer, VoicePool& voice_pool) {
    for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
        UpdateLoopInfo(v);
}

static void UpdateVolumeEnvelopeOn(LayerProcessor& layer, VoicePool& voice_pool) {
    layer.voice_controller.vol_env_on =
        layer.vol_env_on_param || layer.audio_thread_inst.tag == InstrumentType::WaveformSynth;
    if (layer.voice_controller.vol_env_on)
        for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
            v.vol_env.Gate(false);
    else
        UpdateLoopPointsForVoices(layer, voice_pool);
}

static Span<sample_lib::Region::Slice const> AudioThreadSlices(InstrumentUnwrapped const& inst) {
    if (auto p = inst.TryGet<sample_lib::LoadedInstrument const*>()) {
        auto const& i = **p;
        if (i.instrument.regions.size == 1 && i.instrument.regions[0].slices.size)
            return i.instrument.regions[0].slices;
    }
    return {};
}

static bool ArpIsOn(LayerProcessor const& layer) {
    return layer.arp_state.audio.type != param_values::ArpMode::Off ||
           AudioThreadSlices(layer.audio_thread_inst).size;
}

bool LayerHasAudioActivity(LayerProcessor const& layer) {
    return !layer.peak_meter.Silent() || (ArpIsOn(layer) && layer.arp_state.audio.any_notes_held);
}

struct VelocityRegion {
    u7 const point_most_intense;
    u7 const point_least_intense;
    int const no_fade_size; // Always fades down from the bottom.
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

static f32 AmplitudeScalingFromVelocity(LayerProcessor& layer, f32 velocity, f32 velocity_to_volume) {
    ASSERT_HOT(velocity >= 0);

    auto mod = MapFrom01(velocity, 1 - velocity_to_volume, 1);

    auto const& curve = layer.velocity_curve_map.lookup_table.Consume().data;
    auto value = curve[(usize)Round(velocity * (curve.size - 1))];

    // Since we're using this as an amplitude, we want to scale by a more pleasing value.
    value = value * value;

    mod *= value;

    // Velocity regions are a legacy features that will only be used if we're running DAW state from an older
    // version.
    if (layer.num_velocity_regions == 2) {
        mod *= ProcessVeloRegions(k_velo_regions_half.Items(), layer.active_velocity_regions, velocity * 127);
    } else if (layer.num_velocity_regions == 3) {
        mod *=
            ProcessVeloRegions(k_velo_regions_third.Items(), layer.active_velocity_regions, velocity * 127);
    }
    return mod;
}

void SetSilent(LayerProcessor& layer, bool state) { layer.mute_solo_gain = state ? 0.0f : 1.0f; }

static void
UpdateVoiceLfoTimes(LayerProcessor& layer, VoicePool& voice_pool, AudioProcessingContext const& context) {
    for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
        UpdateLFOTime(v, context.sample_rate);
}

void PrepareToPlay(LayerProcessor& layer, AudioProcessingContext const& context) {
    ResetLayerAudioProcessing(layer);
    layer.peak_meter.PrepareToPlay(context.sample_rate);
}

void LayerApplyNewState(LayerProcessor& layer, StateSnapshot const& state, StateSource) {
    ASSERT(g_is_logical_main_thread);

    layer.velocity_curve_map.SetNewPoints(state.velocity_curve_points[layer.index]);

    layer.harmony_intervals.AssignBlockwise(state.harmony_intervals[layer.index]);

    // Always load the user's arp steps. Slice-mode overrides are stored in separate fields and don't affect
    // the user's state.
    auto& arp = layer.arp_state;
    for (auto const step_index : Range(k_arp_max_steps))
        arp.steps[step_index].Store(state.arp_steps[layer.index][step_index], StoreMemoryOrder::Relaxed);

    auto const& slice_config = state.slice_arp_configs[layer.index];
    arp.slice_start_offset.Store(slice_config.start_offset, StoreMemoryOrder::Relaxed);
    arp.slice_loop_length.Store(slice_config.loop_length, StoreMemoryOrder::Relaxed);
}

//
// ==========================================================================================================

//
//
//

static param_values::MonophonicMode EffectiveMonophonicMode(LayerProcessor const& layer) {
    // The arp manages its own voice lifecycle, so monophonic modes don't apply.
    if (ArpIsOn(layer)) return param_values::MonophonicMode::Off;

    // We maintain backwards compatibility with DAW projects that may have been automating the legacy
    // monophonic switch by overwriting the mode if the legacy param is true.
    return layer.monophonic_retrigger_legacy ? param_values::MonophonicMode::Retrigger
                                             : layer.monophonic_mode;
}

struct SliceRange {
    u32 start_frame;
    Optional<u32> end_frame; // nullopt = play to end of sample
};

struct TriggerVoiceArgs {
    sample_lib::TriggerEvent trigger_event;
    MidiChannelNote note;
    f32 velocity;
    u32 offset;
    Optional<SliceRange> slice {};
};

static void TriggerVoicesIfNeeded(LayerProcessor& layer,
                                  AudioProcessingContext const& context,
                                  VoicePool& voice_pool,
                                  TriggerVoiceArgs args) {
    ZoneScoped;
    if (layer.audio_thread_inst.tag == InstrumentType::None) return;

    auto const key_range_low = layer.voice_controller.key_range_low;
    auto const key_range_high = Max(layer.voice_controller.key_range_high, key_range_low);

    if (args.note.note < key_range_low || args.note.note > key_range_high) return;

    ASSERT_HOT(args.velocity >= 0 && args.velocity <= 1);
    auto const note_vel = (u16)RoundPositiveFloat(args.velocity * 999);

    auto const note_for_samples = ({
        auto const n = args.note.note + layer.midi_transpose;
        if (n < 0 || n > 127) return;
        (u7) n;
    });

    auto const velocity_amp =
        AmplitudeScalingFromVelocity(layer, args.velocity, layer.shared_params.velocity_to_volume_01);

    auto const key_range_fade_amp =
        KeyRangeFadeInAmp(args.note.note, key_range_low, layer.voice_controller.key_range_low_fade) *
        KeyRangeFadeOutAmp(args.note.note, key_range_high, layer.voice_controller.key_range_high_fade);

    auto const amp = velocity_amp * key_range_fade_amp;

    VoiceStartParams p {.params = VoiceStartParams::SamplerParams {}};
    if (auto i_ptr = layer.audio_thread_inst.TryGet<sample_lib::LoadedInstrument const*>()) {
        auto const& inst = **i_ptr;
        p.params = VoiceStartParams::SamplerParams {
            .initial_timbre_param_value_01 = layer.shared_params.timbre_value_01,
            .voice_sample_params = {},
        };
        auto& sampler_params = p.params.Get<VoiceStartParams::SamplerParams>();

        auto& rr_pos = layer.rr_pos[ToInt(args.trigger_event)];

        for (auto [group_index, group] :
             Enumerate(inst.instrument.round_robin_sequence_groups[ToInt(args.trigger_event)])) {
            if (rr_pos[group_index] > group.max_rr_pos) rr_pos[group_index] = 0;
        }

        DEFER {
            for (auto const group_index :
                 Range(inst.instrument.round_robin_sequence_groups[ToInt(args.trigger_event)].size))
                ++rr_pos[group_index];
        };

        for (auto i : Range(inst.instrument.regions.size)) {
            auto const& region = inst.instrument.regions[i];
            auto const& audio_data = inst.audio_datas[i];
            if (region.trigger.key_range.Contains(note_for_samples) &&
                region.trigger.velocity_range.Contains(note_vel) &&
                (!region.trigger.round_robin_index ||
                 *region.trigger.round_robin_index == rr_pos[region.trigger.round_robin_sequencing_group]) &&
                region.trigger.trigger_event == args.trigger_event) {
                dyn::Append(sampler_params.voice_sample_params,
                            VoiceStartParams::SamplerParams::Region {
                                .region = region,
                                .audio_data = *audio_data,
                                .amp = amp,
                            });
            }
        }

        if (!sampler_params.voice_sample_params.size) return;

        // Do velocity feathering if needed.
        {
            VoiceStartParams::SamplerParams::Region* feather_region_1 = nullptr;
            VoiceStartParams::SamplerParams::Region* feather_region_2 = nullptr;
            for (auto& r : sampler_params.voice_sample_params) {
                if (r.region.trigger.feather_overlapping_velocity_layers) {
                    // NOTE, if there are more than 2 feather regions, then we only cross-fade 2 of them.
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
                auto const amp1 = QuarterSineFade(1 - pos);
                auto const amp2 = QuarterSineFade(pos);
                feather_region_1->amp *= amp1;
                feather_region_2->amp *= amp2;
            }
        }

    } else if (auto w = layer.audio_thread_inst.TryGet<WaveformType>();
               w && args.trigger_event == sample_lib::TriggerEvent::NoteOn) {
        p.params = VoiceStartParams::WaveformParams {};
        auto& waveform = p.params.Get<VoiceStartParams::WaveformParams>();
        waveform.amp = amp;
        waveform.type = *w;
    }

    p.disable_vol_env = args.trigger_event == sample_lib::TriggerEvent::NoteOff;
    p.initial_pitch = layer.voice_controller.tune_semitones;
    p.midi_key_trigger = args.note;
    p.note_num = note_for_samples;
    p.note_vel = args.velocity;
    p.lfo_start_state = {};
    p.num_frames_before_starting = args.offset;
    if (layer.lfo_restart_mode == param_values::LfoRestartMode::Free) {
        for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller)) {
            p.lfo_start_state.phase = v.lfo.phase;
            p.lfo_start_state.random_state = v.lfo.random_state; // already non-zero
            p.lfo_start_state.prev_random = v.lfo.prev_random;
            p.lfo_start_state.next_random = v.lfo.next_random;
            break;
        }
    }

    bool start_voice = true;

    if (args.trigger_event == sample_lib::TriggerEvent::NoteOn) {
        switch (EffectiveMonophonicMode(layer)) {
            case param_values::MonophonicMode::Off: break;

            case param_values::MonophonicMode::Retrigger:
                for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
                    if (!layer.voice_controller.vol_env_on)
                        v.volume_fade.SetAsFadeOutIfNotAlready(context.sample_rate, 5);
                    else
                        EndVoice(v);
                break;

            case param_values::MonophonicMode::Latch:
                if (!layer.monophonic_latch)
                    layer.monophonic_latch = true;
                else
                    start_voice = false;
                break;

            case param_values::MonophonicMode::Count: break;
        }
    }

    if (start_voice) {
        if (args.slice) {
            if (auto sp = p.params.TryGet<VoiceStartParams::SamplerParams>()) {
                sp->slice_start_frame = args.slice->start_frame;
                sp->slice_end_frame = args.slice->end_frame;
            }
        }
        StartVoice(voice_pool, layer.voice_controller, p, context);
    }
}

static bool NoNotesHeld(AudioProcessingContext const& context) {
    for (auto const chan : Range<u8>(16)) {
        auto const held_or_sustained = context.midi_note_state.NotesHeldIncludingSustained((u4)chan);
        if (held_or_sustained.AnyValuesSet()) return false;
    }
    return true;
}

static bool
ShouldEndNote(LayerProcessor& layer, AudioProcessingContext const& context, MidiChannelNote note) {
    // Don't end notes when we're in latch mode.
    if (layer.monophonic_latch) return false;

    // When the sustain pedal is held, don't end notes.
    if (context.midi_note_state.sustain_pedal_on.Get(note.channel)) return false;

    // If there's no volume envelope, voices play out fully.
    if (!layer.voice_controller.vol_env_on) return false;

    // Don't end a note that's actually still held down somehow.
    if (context.midi_note_state.keys_held[note.channel].Get(note.note)) return false;

    // End the note.
    return true;
}

static void LayerHandleNoteOff(LayerProcessor& layer,
                               AudioProcessingContext const& context,
                               VoicePool& voice_pool,
                               MidiChannelNote note,
                               f32 velocity,
                               bool triggered_by_cc64) {
    if (layer.monophonic_latch && NoNotesHeld(context)) {
        for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
            EndVoice(v);
        layer.monophonic_latch = {};
    }

    if (ShouldEndNote(layer, context, note)) NoteOff(voice_pool, layer.voice_controller, note);

    // Some voices trigger on note-off.
    if (!triggered_by_cc64)
        TriggerVoicesIfNeeded(layer,
                              context,
                              voice_pool,
                              {
                                  .trigger_event = sample_lib::TriggerEvent::NoteOff,
                                  .note = note,
                                  .velocity = velocity,
                                  .offset = 0,
                              });
}

static void LayerHandleNoteOn(LayerProcessor& layer,
                              AudioProcessingContext const& context,
                              VoicePool& voice_pool,
                              TriggerVoiceArgs args) {
    TriggerVoicesIfNeeded(layer, context, voice_pool, args);
}

// Arpeggiator
// =========================================================================================================

static u32 ArpFramesPerStep(SyncedTimes rate, AudioProcessingContext const& context) {
    auto const hz = SyncedTimeToHz(context.tempo, rate);
    return Max(1u, (u32)(context.sample_rate / hz));
}

static void
ReleaseNotes(Array<Bitset<128>, 16>& notes, VoicePool& voice_pool, VoiceProcessingController& vc) {
    for (auto const chan : Range<u8>(16))
        notes[chan].ForEachSetBit(
            [&](usize bit) { NoteOff(voice_pool, vc, {.note = (u7)bit, .channel = (u4)chan}); });
    notes = {};
}

struct ArpStepContext {
    u32& current_step;
    u32& gate_off_frame;
    u32 frames_per_step;
    Array<Bitset<128>, 16>& last_triggered_notes;
    param_values::ArpMode type;
    param_values::ArpNoteOrder note_order;
    bool publish_gui_step;
    Span<sample_lib::Region::Slice const> const& slices;
};

static void ArpExecuteStep(LayerProcessor& layer,
                           AudioProcessingContext const& context,
                           VoicePool& voice_pool,
                           u32 frame_offset,
                           ArpStepContext ctx,
                           Span<MidiChannelNote const> notes,
                           u32 num_notes) {
    auto& arp = layer.arp_state;

    if (num_notes == 0) return;

    u32 slice_total_steps = 0;
    Array<u8, k_arp_max_steps> step_to_slice {};
    if (ctx.slices.size) {
        auto const start_offset = arp.slice_start_offset.Load(LoadMemoryOrder::Relaxed);
        auto const loop_length = arp.slice_loop_length.Load(LoadMemoryOrder::Relaxed);
        auto const num_slices = (u32)ctx.slices.size;
        auto const effective_length = loop_length == 0 ? num_slices : Min((u32)loop_length, num_slices);
        for (u32 i = 0; i < effective_length; i++) {
            u32 const si = ((u32)start_offset + i) % num_slices;
            for (u32 j = 0; j < ctx.slices[si].length_proportion; j++)
                step_to_slice[slice_total_steps++] = (u8)si;
        }
    }

    auto const length = ctx.slices.size ? slice_total_steps : arp.audio.length;

    auto const step_at = [&](u32 step_index) {
        ArpStep s = arp.steps[step_index].Load(LoadMemoryOrder::Relaxed);
        if (ctx.slices.size) {
            s.tie = (step_index > 0) && step_to_slice[step_index] == step_to_slice[step_index - 1];
            s.interval = 0;
            s.note = 0;
        }
        return s;
    };

    auto const step = step_at(ctx.current_step % length);

    auto const advance_step = [&]() {
        if (ctx.publish_gui_step) arp.current_step_for_gui.Store(ctx.current_step, StoreMemoryOrder::Relaxed);
        ctx.current_step = (ctx.current_step + 1) % length;
    };

    if (step.tie) {
        advance_step();
        return;
    }

    ReleaseNotes(ctx.last_triggered_notes, voice_pool, layer.voice_controller);

    if (!step.on) {
        advance_step();
        return;
    }

    auto const slice = ({
        Optional<SliceRange> s {};
        if (ctx.slices.size) {
            auto const slice_idx = step_to_slice[ctx.current_step % length];
            s = SliceRange {
                .start_frame = ctx.slices[slice_idx].start_frame,
                .end_frame = (slice_idx + 1 < ctx.slices.size)
                                 ? Optional<u32>(ctx.slices[slice_idx + 1].start_frame)
                                 : k_nullopt,
            };
        }
        s;
    });

    auto const humanise_note_delay = [&]() -> u32 {
        constexpr f32 k_max_ms = 80;
        constexpr f32 k_max_fraction_of_rate = 0.2f;
        if (arp.audio.humanise <= 0 || !voice_pool.master_random_seed) return 0;
        auto max_delay = (f32)ctx.frames_per_step * k_max_fraction_of_rate * arp.audio.humanise;
        max_delay = Min(context.sample_rate * (k_max_ms / 1000.0f), max_delay);
        auto max_delay_u32 = (u32)max_delay;
        if (max_delay_u32 == 0) return 0;
        return RandomIntInRange<u32>(*voice_pool.master_random_seed, 0, max_delay_u32);
    };

    auto const humanise_velocity = [&](f32 vel) -> f32 {
        constexpr f32 k_max_bidirectional_jitter_fraction = 0.15f;
        if (arp.audio.humanise <= 0 || !voice_pool.master_random_seed) return vel;
        auto const jitter = RandomFloatInRange(*voice_pool.master_random_seed,
                                               -k_max_bidirectional_jitter_fraction,
                                               k_max_bidirectional_jitter_fraction) *
                            arp.audio.humanise;
        return Clamp(vel + jitter, 0.0f, 1.0f);
    };

    switch (ctx.type) {
        case param_values::ArpMode::Off: PanicIfReached(); break;
        case param_values::ArpMode::Fixed: {
            MidiChannelNote note {.note = step.note, .channel = 0};
            LayerHandleNoteOn(layer,
                              context,
                              voice_pool,
                              {
                                  .trigger_event = sample_lib::TriggerEvent::NoteOn,
                                  .note = note,
                                  .velocity = humanise_velocity(step.Velocity01()),
                                  .offset = frame_offset + humanise_note_delay(),
                                  .slice = slice,
                              });
            ctx.last_triggered_notes[0].Set(note.note);
            break;
        }
        case param_values::ArpMode::Played: {
            auto const trigger_note = [&](u32 idx) {
                auto const note = notes[idx % num_notes];
                auto const input_vel = context.midi_note_state.velocities[note.channel][note.note];
                auto const vel =
                    humanise_velocity(step.Velocity01() * LinearInterpolate(0.5f, 1.0f, input_vel));
                auto triggered_note = note;
                triggered_note.note = (u7)Clamp((int)note.note + (int)step.interval, 0, 127);
                LayerHandleNoteOn(layer,
                                  context,
                                  voice_pool,
                                  {
                                      .trigger_event = sample_lib::TriggerEvent::NoteOn,
                                      .note = triggered_note,
                                      .velocity = vel,
                                      .offset = frame_offset + humanise_note_delay(),
                                      .slice = slice,
                                  });
                ctx.last_triggered_notes[triggered_note.channel].Set(triggered_note.note);
            };

            switch (ctx.note_order) {
                case param_values::ArpNoteOrder::Chord: {
                    for (auto const i : Range(num_notes))
                        trigger_note(i);
                    break;
                }
                case param_values::ArpNoteOrder::Up: {
                    trigger_note(ctx.current_step % num_notes);
                    break;
                }
                case param_values::ArpNoteOrder::Down: {
                    trigger_note((num_notes - 1) - (ctx.current_step % num_notes));
                    break;
                }
                case param_values::ArpNoteOrder::UpDown: {
                    if (num_notes <= 1) {
                        trigger_note(0);
                    } else {
                        auto const cycle_len = 2 * (num_notes - 1);
                        auto const pos = ctx.current_step % cycle_len;
                        trigger_note(pos < num_notes ? pos : cycle_len - pos);
                    }
                    break;
                }
                case param_values::ArpNoteOrder::Count: PanicIfReached(); break;
            }
            break;
        }
        case param_values::ArpMode::Count: PanicIfReached();
    }

    // Schedule gate-off if gate < 1 and next step is not tied.
    auto const next_step_index = ctx.current_step % length;
    bool const next_is_tied = step_at(next_step_index).tie;
    auto const gate_01 = step.Gate01();
    if (gate_01 < 1.0f && !next_is_tied)
        ctx.gate_off_frame = Max(1u, (u32)((f32)ctx.frames_per_step * gate_01));
    else
        ctx.gate_off_frame = 0;

    advance_step();
}

static void ArpTriggerStep(LayerProcessor& layer,
                           AudioProcessingContext const& context,
                           VoicePool& voice_pool,
                           u32 frame_offset) {
    auto& arp = layer.arp_state;

    Array<MidiChannelNote, 128 * 16> notes;
    u32 num_notes = 0;
    for (auto const chan : Range<u8>(16)) {
        auto const held = context.midi_note_state.NotesHeldIncludingSustained((u4)chan);
        held.ForEachSetBit([&](usize bit) {
            WriteAndIncrement(num_notes, notes, MidiChannelNote {.note = (u7)bit, .channel = (u4)chan});
        });
    }
    Sort(Span {notes.data, num_notes}, [](auto const& a, auto const& b) { return a.note < b.note; });

    auto const slices = AudioThreadSlices(layer.audio_thread_inst);
    auto const effective_type = slices.size ? param_values::ArpMode::Played : arp.audio.type;

    ArpExecuteStep(layer,
                   context,
                   voice_pool,
                   frame_offset,
                   {
                       .current_step = arp.audio.playhead.current_step,
                       .gate_off_frame = arp.audio.playhead.gate_off_frame,
                       .frames_per_step = arp.audio.playhead.frames_per_step,
                       .last_triggered_notes = arp.audio.playhead.last_triggered_notes,
                       .type = effective_type,
                       .note_order = arp.audio.note_order,
                       .publish_gui_step = true,
                       .slices = slices,
                   },
                   Span<MidiChannelNote const> {notes.data, num_notes},
                   num_notes);
}

static f32 OctavePolyrateRatio(param_values::ArpOctavePolyrate mode) {
    switch (mode) {
        case param_values::ArpOctavePolyrate::Double: return 2.0f;
        case param_values::ArpOctavePolyrate::ThreeToTwo: return 1.5f;
        case param_values::ArpOctavePolyrate::FourToThree: return 4.0f / 3.0f;
        case param_values::ArpOctavePolyrate::Off:
        case param_values::ArpOctavePolyrate::Count: PanicIfReached(); break;
    }
    return 2.0f;
}

static u32 OctavePolyrateFramesPerStep(u32 base_frames_per_step, u32 octave_index, f32 ratio) {
    auto const offset = (s32)octave_index - ArpeggiatorState::k_octave_polyrate_base_octave;
    if (offset == 0) return base_frames_per_step;
    auto const multiplier = Pow(ratio, (f32)((offset > 0) ? offset : -offset));
    if (offset > 0)
        return Max(1u, (u32)((f32)base_frames_per_step / multiplier));
    else
        return (u32)((f32)base_frames_per_step * multiplier);
}

static void ArpOctavePolyrateHandleNoteStartEnd(LayerProcessor& layer,
                                                AudioProcessingContext const& context,
                                                VoicePool& voice_pool,
                                                u32 num_frames) {
    auto& arp = layer.arp_state;

    // Gather all held notes sorted by pitch.
    Array<MidiChannelNote, 128 * 16> all_notes;
    u32 num_all_notes = 0;
    for (auto const chan : Range<u8>(16)) {
        auto const held = context.midi_note_state.NotesHeldIncludingSustained((u4)chan);
        held.ForEachSetBit([&](usize bit) {
            WriteAndIncrement(num_all_notes,
                              all_notes,
                              MidiChannelNote {.note = (u7)bit, .channel = (u4)chan});
        });
    }
    Sort(Span {all_notes.data, num_all_notes}, [](auto const& a, auto const& b) { return a.note < b.note; });

    // Determine which octaves have held notes.
    Bitset<ArpeggiatorState::k_octave_polyrate_num_playheads> active_octaves {};
    for (u32 i = 0; i < num_all_notes; i++)
        active_octaves.Set((u32)(all_notes[i].note / 12));
    u32 const gui_playhead_oct = num_all_notes ? (u32)(all_notes[0].note / 12) : 0;

    auto const base_frames = ArpFramesPerStep(arp.audio.rate, context);
    auto const ratio = OctavePolyrateRatio(arp.audio.octave_polyrate);
    for (auto const oct : Range(ArpeggiatorState::k_octave_polyrate_num_playheads))
        arp.audio.octave_polyrate_playheads[oct].frames_per_step =
            OctavePolyrateFramesPerStep(base_frames, oct, ratio);

    auto const slices = AudioThreadSlices(layer.audio_thread_inst);
    auto const effective_type = slices.size ? param_values::ArpMode::Played : arp.audio.type;

    // In Free mode, sync newly-active octaves to existing playback position so they join in phase.
    // In Retrigger mode, all playheads are already reset to 0 on note-on.
    if (arp.audio.trigger_mode == param_values::ArpTriggerMode::Free) {
        auto const newly_active = active_octaves & ~arp.audio.prev_active_octaves;
        if (newly_active.AnyValuesSet()) {
            u32 ref_oct = ArpeggiatorState::k_octave_polyrate_num_playheads;
            for (auto const oct : Range(ArpeggiatorState::k_octave_polyrate_num_playheads))
                if (arp.audio.prev_active_octaves.Get(oct)) {
                    ref_oct = oct;
                    break;
                }

            if (ref_oct < ArpeggiatorState::k_octave_polyrate_num_playheads) {
                auto const& ref = arp.audio.octave_polyrate_playheads[ref_oct];
                u32 length = arp.audio.length;
                if (slices.size) {
                    length = 0;
                    for (auto const& s : slices)
                        length += s.length_proportion;
                }

                u64 const frames_elapsed =
                    (u64)ref.current_step * ref.frames_per_step + ref.frames_into_current_step;

                newly_active.ForEachSetBit([&](usize oct) {
                    auto& head = arp.audio.octave_polyrate_playheads[oct];
                    head.current_step = (u32)((frames_elapsed / head.frames_per_step) % length);
                    auto const remainder = (u32)(frames_elapsed % head.frames_per_step);
                    head.frames_into_current_step = remainder;
                    head.frames_until_next_step = head.frames_per_step - remainder;
                });
            }
        }
    }
    arp.audio.prev_active_octaves = active_octaves;

    for (auto const i : Range(num_frames)) {
        for (auto const oct : Range(ArpeggiatorState::k_octave_polyrate_num_playheads)) {
            if (!active_octaves.Get(oct)) continue;
            auto& head = arp.audio.octave_polyrate_playheads[oct];

            if (head.frames_until_next_step == 0) {
                // Filter notes to this octave.
                Array<MidiChannelNote, 128 * 16> oct_notes;
                u32 num_oct_notes = 0;
                auto const oct_low = (u7)(oct * 12);
                auto const oct_high = (u7)Min(127u, ((oct + 1) * 12) - 1);
                for (u32 j = 0; j < num_all_notes; j++)
                    if (all_notes[j].note >= oct_low && all_notes[j].note <= oct_high)
                        WriteAndIncrement(num_oct_notes, oct_notes, all_notes[j]);

                ArpExecuteStep(layer,
                               context,
                               voice_pool,
                               i,
                               {
                                   .current_step = head.current_step,
                                   .gate_off_frame = head.gate_off_frame,
                                   .frames_per_step = head.frames_per_step,
                                   .last_triggered_notes = head.last_triggered_notes,
                                   .type = effective_type,
                                   .note_order = arp.audio.note_order,
                                   .publish_gui_step = (oct == gui_playhead_oct),
                                   .slices = slices,
                               },
                               Span<MidiChannelNote const> {oct_notes.data, num_oct_notes},
                               num_oct_notes);

                head.frames_until_next_step = head.frames_per_step;
                head.frames_into_current_step = 0;
            }

            if (head.gate_off_frame > 0 && head.frames_into_current_step == head.gate_off_frame) {
                ReleaseNotes(head.last_triggered_notes, voice_pool, layer.voice_controller);
                head.gate_off_frame = 0;
            }

            --head.frames_until_next_step;
            ++head.frames_into_current_step;
        }
    }
}

static void ArpHandleRegularNoteStartEnd(LayerProcessor& layer,
                                         AudioProcessingContext const& context,
                                         VoicePool& voice_pool,
                                         u32 num_frames) {
    auto& arp = layer.arp_state;
    auto& playhead = arp.audio.playhead;
    for (auto const i : Range(num_frames)) {
        if (playhead.frames_until_next_step == 0) {
            ArpTriggerStep(layer, context, voice_pool, i);
            playhead.frames_until_next_step = playhead.frames_per_step;
            playhead.frames_into_current_step = 0;
        }

        // Gate-off: release notes mid-step.
        if (playhead.gate_off_frame > 0 && playhead.frames_into_current_step == playhead.gate_off_frame) {
            ReleaseNotes(playhead.last_triggered_notes, voice_pool, layer.voice_controller);
            playhead.gate_off_frame = 0;
        }

        --playhead.frames_until_next_step;
        ++playhead.frames_into_current_step;
    }
}

static void ArpHandleNoteStartEnd(LayerProcessor& layer,
                                  AudioProcessingContext const& context,
                                  VoicePool& voice_pool,
                                  u32 num_frames) {
    auto& arp = layer.arp_state;
    if (!ArpIsOn(layer)) return;
    if (!arp.audio.any_notes_held) return;
    if (num_frames == 0) return;

    if (arp.audio.octave_polyrate == param_values::ArpOctavePolyrate::Off)
        ArpHandleRegularNoteStartEnd(layer, context, voice_pool, num_frames);
    else
        ArpOctavePolyrateHandleNoteStartEnd(layer, context, voice_pool, num_frames);
}

void ProcessLayerPreVoices(LayerProcessor& layer,
                           AudioProcessingContext const& context,
                           VoicePool& voice_pool,
                           u32 num_frames) {
    // Harmony intervals are not a parameter - always sync from the AtomicBitset to the voice controller.
    layer.voice_controller.granular.harmony_intervals = layer.harmony_intervals.GetBlockwise();

    ArpHandleNoteStartEnd(layer, context, voice_pool, num_frames);
}

static Optional<SyncedTimes> AutoSyncedTimeForLayer(LayerProcessor const& layer,
                                                    AudioProcessingContext const& context) {
    auto const* inst_ptr = layer.audio_thread_inst.TryGet<sample_lib::LoadedInstrument const*>();
    if (!inst_ptr || !*inst_ptr) return k_nullopt;
    auto const& regions = (*inst_ptr)->instrument.regions;
    if (regions.size != 1) return k_nullopt;
    auto const& region = regions[0];
    if (!region.slices.size || !region.loop_beats || region.native_bpm <= 0) return k_nullopt;

    u32 total_prop = 0;
    for (auto const& s : region.slices)
        total_prop += s.length_proportion;
    if (!total_prop) return k_nullopt;

    // Native step duration in seconds: (beats * 60 / bpm) / total_prop. Convert to milliseconds.
    f64 const native_step_ms = ((f64)region.loop_beats * 60000.0 / (f64)region.native_bpm) / (f64)total_prop;
    return LargestSyncedTimeWithinTarget(native_step_ms, context.tempo, SyncedTimesType::Straight);
}

bool ChangeInstrumentIfNeededAndReset(LayerProcessor& layer,
                                      VoicePool& voice_pool,
                                      AudioProcessingContext const& context) {
    ZoneScoped;
    auto desired_inst = layer.desired_inst.Consume();

    DEFER { ResetLayerAudioProcessing(layer); };

    if (!desired_inst) return false;
    if (*desired_inst == layer.audio_thread_inst) return false;

    bool const arp_was_on = ArpIsOn(layer);

    // End all layer voices
    for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
        EndVoiceInstantly(v);

    layer.peak_meter.Zero();
    voice_pool.last_activated_audio_data_hash[layer.index].Store(0, StoreMemoryOrder::Relaxed);

    // Swap instrument
    layer.audio_thread_inst = *desired_inst;
    UpdateLoopPointsForVoices(layer, voice_pool);
    UpdateVolumeEnvelopeOn(layer, voice_pool);

    // Update arp state
    {
        auto& arp = layer.arp_state;
        arp.slice_start_offset.Store(0, StoreMemoryOrder::Relaxed);
        arp.slice_loop_length.Store(0, StoreMemoryOrder::Relaxed);

        bool const arp_now_on = ArpIsOn(layer);
        arp.on_for_gui.Store(arp_now_on, StoreMemoryOrder::Relaxed);

        if (arp.audio.auto_rate) {
            if (auto const auto_time = AutoSyncedTimeForLayer(layer, context)) arp.audio.rate = *auto_time;
            arp.resolved_rate_for_gui.Store(arp.audio.rate, StoreMemoryOrder::Relaxed);
        }

        if (!arp_now_on && arp_was_on) {
            ReleaseNotes(arp.audio.playhead.last_triggered_notes, voice_pool, layer.voice_controller);
            arp.ResetAudioPlayback();
        } else if (arp_now_on) {
            arp.audio.playhead.frames_per_step = ArpFramesPerStep(arp.audio.rate, context);
        }
    }

    return true;
}

template <typename Type>
static SyncedTimes SyncedTimesFromParam(Type param_rate) {
    // Remapping enum values like this allows us to separate values that cannot ever change (the parameter
    // value), with values that we have more control over (DSP code). It helps make things explicit when
    // things change and we need to maintain perfect backwards compatibility.
    switch (param_rate) {
        case Type::_1_64T: return SyncedTimes::_1_64T;
        case Type::_1_64: return SyncedTimes::_1_64;
        case Type::_1_64D: return SyncedTimes::_1_64D;
        case Type::_1_32T: return SyncedTimes::_1_32T;
        case Type::_1_32: return SyncedTimes::_1_32;
        case Type::_1_32D: return SyncedTimes::_1_32D;
        case Type::_1_16T: return SyncedTimes::_1_16T;
        case Type::_1_16: return SyncedTimes::_1_16;
        case Type::_1_16D: return SyncedTimes::_1_16D;
        case Type::_1_8T: return SyncedTimes::_1_8T;
        case Type::_1_8: return SyncedTimes::_1_8;
        case Type::_1_8D: return SyncedTimes::_1_8D;
        case Type::_1_4T: return SyncedTimes::_1_4T;
        case Type::_1_4: return SyncedTimes::_1_4;
        case Type::_1_4D: return SyncedTimes::_1_4D;
        case Type::_1_2T: return SyncedTimes::_1_2T;
        case Type::_1_2: return SyncedTimes::_1_2;
        case Type::_1_2D: return SyncedTimes::_1_2D;
        case Type::_1_1T: return SyncedTimes::_1_1T;
        case Type::_1_1: return SyncedTimes::_1_1;
        case Type::_1_1D: return SyncedTimes::_1_1D;
        case Type::_2_1T: return SyncedTimes::_2_1T;
        case Type::_2_1: return SyncedTimes::_2_1;
        case Type::_2_1D: return SyncedTimes::_2_1D;
        case Type::_4_1T: return SyncedTimes::_4_1T;
        case Type::_4_1: return SyncedTimes::_4_1;
        case Type::_4_1D: return SyncedTimes::_4_1D;
        case Type::Count: PanicIfReached(); break;
    }
    return SyncedTimes::_1_8;
}

void ProcessLayerChanges(LayerProcessor& layer,
                         AudioProcessingContext const& context,
                         ProcessBlockChanges changes,
                         VoicePool& voice_pool) {
    f32 const sample_rate = context.sample_rate;
    auto& vmst = layer.voice_controller;

    // Main controls
    // =======================================================================================================
    if (auto p = changes.changed_params.IntValue<param_values::VelocityMappingMode>(
            layer.index,
            LayerParamIndex::LegacyVelocityMapping))
        SetVelocityMapping(layer, *p);

    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::Volume)) layer.gain = *p;

    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::Pan)) vmst.pan_pos = *p;

    {
        bool set_tune = false;
        if (auto p = changes.changed_params.IntValue<int>(layer.index, LayerParamIndex::TuneSemitone)) {
            layer.tune_semitone = (f32)*p;
            set_tune = true;
        }
        if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::TuneCents)) {
            layer.tune_cents = *p;
            set_tune = true;
        }
        if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::PitchBendRange)) {
            layer.pitch_bend_range_semitone = *p;
            set_tune = true;
        }

        if (changes.pitchwheel_changed.AnyValuesSet()) {
            for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
                if (changes.pitchwheel_changed.Get(v.midi_key_trigger.channel)) {
                    set_tune = true;
                    break;
                }
        }

        if (set_tune) {
            auto const tune = layer.tune_semitone + (layer.tune_cents / 100.0f);
            layer.voice_controller.tune_semitones = tune;
            for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
                SetVoicePitch(v,
                              vmst.tune_semitones + (context.pitchwheel_position[v.midi_key_trigger.channel] *
                                                     layer.pitch_bend_range_semitone),
                              sample_rate);
        }
    }

    constexpr f32 k_min_envelope_ms = 0.2f;
    // Volume envelope
    // =======================================================================================================
    if (auto p = changes.changed_params.BoolValue(layer.index, LayerParamIndex::VolEnvOn)) {
        layer.vol_env_on_param = *p;
        UpdateVolumeEnvelopeOn(layer, voice_pool);
    }

    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::VolumeAttack))
        layer.voice_controller.vol_env.SetAttackSamples(Max(k_min_envelope_ms, *p) / 1000.0f * sample_rate,
                                                        2.0f);
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::VolumeDecay))
        layer.voice_controller.vol_env.SetDecaySamples(Max(k_min_envelope_ms, *p) / 1000.0f * sample_rate,
                                                       0.1f);
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::VolumeSustain))
        layer.voice_controller.vol_env.SetSustainAmp(*p);

    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::VolumeRelease))
        layer.voice_controller.vol_env.SetReleaseSamples(Max(k_min_envelope_ms, *p) / 1000.0f * sample_rate,
                                                         0.1f);

    // Filter
    // =======================================================================================================
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::FilterEnvAmount))
        vmst.fil_env_amount = *p;
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::FilterAttack))
        layer.voice_controller.fil_env.SetAttackSamples(Max(k_min_envelope_ms, *p) / 1000.0f * sample_rate,
                                                        2.0f);
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::FilterDecay))
        layer.voice_controller.fil_env.SetDecaySamples(Max(k_min_envelope_ms, *p) / 1000.0f * sample_rate,
                                                       0.1f);
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::FilterSustain))
        layer.voice_controller.fil_env.SetSustainAmp(*p);
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::FilterRelease))
        layer.voice_controller.fil_env.SetReleaseSamples(Max(k_min_envelope_ms, *p) / 1000.0f * sample_rate,
                                                         0.1f);
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::FilterCutoff))
        vmst.sv_filter_cutoff_linear = sv_filter::HzToLinear(*p);
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::FilterResonance))
        vmst.sv_filter_resonance = sv_filter::SkewResonance(*p);
    if (auto p = changes.changed_params.BoolValue(layer.index, LayerParamIndex::FilterOn))
        vmst.filter_on = *p;
    if (auto p =
            changes.changed_params.IntValue<param_values::LayerFilterType>(layer.index,
                                                                           LayerParamIndex::FilterType)) {
        sv_filter::Type sv_type {};
        // Remapping enum values like this allows us to separate values that cannot change (the parameter
        // value), with values that we have more control over (DSP code)
        switch (*p) {
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
    if (auto p = changes.changed_params.IntValue<int>(layer.index, LayerParamIndex::MidiTranspose))
        layer.midi_transpose = *p;
    if (auto p = changes.changed_params.BoolValue(layer.index, LayerParamIndex::Keytrack))
        vmst.no_key_tracking = !*p;

    // LFO
    // =======================================================================================================
    if (auto shape = layer.lfo_shape.Poll(changes.changed_params)) {
        vmst.lfo.shape = *shape;
        for (auto& v : voice_pool.EnumerateActiveLayerVoices(layer.voice_controller))
            UpdateLFOWaveform(v);
    }
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::LfoAmount))
        vmst.lfo.amount = *p;
    if (auto dest = layer.lfo_dest.Poll(changes.changed_params)) vmst.lfo.dest = *dest;
    if (auto p = changes.changed_params.BoolValue(layer.index, LayerParamIndex::LfoOn))
        layer.voice_controller.lfo.on = *p;

    {
        bool update_voice_controller_times = false;
        if (changes.tempo_changed) update_voice_controller_times = true;

        if (auto p = changes.changed_params.IntValue<param_values::LfoSyncedRate>(
                layer.index,
                LayerParamIndex::LfoRateTempoSynced)) {
            layer.lfo_synced_time = *p;
            update_voice_controller_times = true;
        }
        if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::LfoRateHz)) {
            layer.lfo_unsynced_hz = *p;
            update_voice_controller_times = true;
        }
        if (auto p = changes.changed_params.BoolValue(layer.index, LayerParamIndex::LfoSyncSwitch)) {
            layer.lfo_is_synced = *p;
            update_voice_controller_times = true;
        }
        if (update_voice_controller_times) {
            if (layer.lfo_is_synced) {
                auto const synced_time = SyncedTimesFromParam(layer.lfo_synced_time);
                vmst.lfo.time_hz = (f32)(1.0 / (SyncedTimeToMs(context.tempo, synced_time) / 1000.0));
            } else {
                vmst.lfo.time_hz = layer.lfo_unsynced_hz;
            }
            UpdateVoiceLfoTimes(layer, voice_pool, context);
        }
    }

    if (auto p = changes.changed_params.IntValue<param_values::LfoRestartMode>(layer.index,
                                                                               LayerParamIndex::LfoRestart))
        layer.lfo_restart_mode = *p;

    // Read legacy bool parameter for DAW automation compatibility
    if (auto p = changes.changed_params.BoolValue(layer.index, LayerParamIndex::LegacyMonophonicBool))
        layer.monophonic_retrigger_legacy = *p;

    if (auto p =
            changes.changed_params.IntValue<param_values::MonophonicMode>(layer.index,
                                                                          LayerParamIndex::MonophonicMode)) {
        layer.monophonic_mode = *p;

        if (*p != param_values::MonophonicMode::Latch) layer.monophonic_latch = false;
    }

    if (auto p = changes.changed_params.IntValue<u7>(layer.index, LayerParamIndex::KeyRangeLow))
        vmst.key_range_low = *p;
    if (auto p = changes.changed_params.IntValue<u7>(layer.index, LayerParamIndex::KeyRangeHigh))
        vmst.key_range_high = *p;
    if (auto p = changes.changed_params.IntValue<u7>(layer.index, LayerParamIndex::KeyRangeLowFade))
        vmst.key_range_low_fade = *p;
    if (auto p = changes.changed_params.IntValue<u7>(layer.index, LayerParamIndex::KeyRangeHighFade))
        vmst.key_range_high_fade = *p;

    // Loop
    // =======================================================================================================
    {
        bool update_loop_info = false;
        if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::LoopStart)) {
            vmst.loop.start = *p;
            update_loop_info = true;
        }
        if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::LoopEnd)) {
            vmst.loop.end = *p;
            update_loop_info = true;
        }
        if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::LoopCrossfade)) {
            vmst.loop.crossfade_size = *p;
            update_loop_info = true;
        }
        if (auto p = changes.changed_params.BoolValue(layer.index, LayerParamIndex::Reverse)) {
            update_loop_info = true;
            vmst.reverse = *p;
        }
        if (auto p = changes.changed_params.IntValue<param_values::LoopMode>(layer.index,
                                                                             LayerParamIndex::LoopMode)) {
            update_loop_info = true;
            vmst.loop_mode = *p;
        }
        if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::SampleOffset))
            vmst.sample_offset_01 = *p;

        if (update_loop_info) UpdateLoopPointsForVoices(layer, voice_pool);
    }

    // Playback / Granular
    // =======================================================================================================
    if (auto p =
            changes.changed_params.IntValue<param_values::PlayMode>(layer.index, LayerParamIndex::PlayMode))
        vmst.play_mode = *p;
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::GranularSpeed))
        vmst.granular.speed = *p;
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::GranularPosition))
        vmst.granular.position = *p;
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::GranularDensity))
        vmst.granular.density = *p;
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::GranularLength))
        vmst.granular.length_ms = *p;
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::GranularSpread))
        vmst.granular.spread = *p;
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::GranularSmoothing))
        vmst.granular.smoothing = *p;
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::GranularRandomPan))
        vmst.granular.random_pan = *p;
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::GranularRandomDetune))
        vmst.granular.random_detune = *p;
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::GranularRandomDirection))
        vmst.granular.random_direction = *p;
    if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::GranularHarmony))
        vmst.granular.harmony = *p;

    // EQ
    // =======================================================================================================
    if (auto p = changes.changed_params.BoolValue(layer.index, LayerParamIndex::EqOn))
        layer.eq_bands.SetOn(*p);

    for (auto const eq_band_index : Range(k_num_layer_eq_bands))
        layer.eq_bands.OnParamChange(eq_band_index, changes.changed_params, layer.index, sample_rate);

    // Arpeggiator
    // =======================================================================================================
    {
        auto& arp = layer.arp_state;
        bool rate_changed = false;

        // Always update user's arp config from params. Slice mode overrides are handled by
        // ActualArpBehaviour, not by overwriting user fields.
        if (auto p = changes.changed_params.IntValue<param_values::ArpMode>(layer.index,
                                                                            LayerParamIndex::ArpMode)) {
            if (*p != arp.audio.type) {
                bool const on_before = ArpIsOn(layer);
                arp.audio.type = *p;
                bool const on_after = ArpIsOn(layer);
                if (on_before != on_after) {
                    arp.on_for_gui.Store(on_after, StoreMemoryOrder::Relaxed);
                    if (on_after) {
                        arp.audio.playhead.frames_per_step = ArpFramesPerStep(arp.audio.rate, context);
                    } else {
                        ReleaseNotes(arp.audio.playhead.last_triggered_notes,
                                     voice_pool,
                                     layer.voice_controller);
                        arp.ResetAudioPlayback();
                    }
                }
            }
        }

        if (auto p =
                changes.changed_params.IntValue<param_values::ArpNoteOrder>(layer.index,
                                                                            LayerParamIndex::ArpNoteOrder))
            arp.audio.note_order = *p;

        if (auto p = changes.changed_params.IntValue<param_values::ArpOctavePolyrate>(
                layer.index,
                LayerParamIndex::ArpOctavePolyrate)) {
            bool const was_on = arp.audio.octave_polyrate != param_values::ArpOctavePolyrate::Off;
            bool const now_on = *p != param_values::ArpOctavePolyrate::Off;
            if (was_on != now_on) {
                for (auto& head : arp.audio.octave_polyrate_playheads)
                    ReleaseNotes(head.last_triggered_notes, voice_pool, layer.voice_controller);
                arp.audio.octave_polyrate_playheads = {};
                arp.audio.prev_active_octaves = {};
                arp.audio.playhead.frames_per_step = ArpFramesPerStep(arp.audio.rate, context);
            }
            arp.audio.octave_polyrate = *p;
        }

        if (auto p = changes.changed_params.IntValue<u32>(layer.index, LayerParamIndex::ArpLength))
            arp.audio.length = Clamp(*p, 1u, (u32)k_arp_max_steps);

        if (auto p = changes.changed_params.IntValue<param_values::ArpTriggerMode>(
                layer.index,
                LayerParamIndex::ArpTriggerMode))
            arp.audio.trigger_mode = *p;

        if (auto p = changes.changed_params.IntValue<param_values::ArpSyncedRate>(layer.index,
                                                                                  LayerParamIndex::ArpRate)) {
            arp.audio.user_rate = SyncedTimesFromParam(*p);
            rate_changed = true;
        }

        if (auto p = changes.changed_params.BoolValue(layer.index, LayerParamIndex::ArpAutoRate)) {
            if (*p != arp.audio.auto_rate) {
                arp.audio.auto_rate = *p;
                rate_changed = true;
            }
        }

        if (auto p = changes.changed_params.ProjectedValue(layer.index, LayerParamIndex::ArpHumanise))
            arp.audio.humanise = *p;

        if (rate_changed || changes.tempo_changed) {
            arp.audio.rate = arp.audio.user_rate;
            if (arp.audio.auto_rate)
                if (auto const auto_time = AutoSyncedTimeForLayer(layer, context))
                    arp.audio.rate = *auto_time;
            arp.audio.playhead.frames_per_step = ArpFramesPerStep(arp.audio.rate, context);
            arp.resolved_rate_for_gui.Store(arp.audio.rate, StoreMemoryOrder::Relaxed);
        }
    }

    // Detect recording false->true transition. The GUI only flips the `recording` atomic; audio is
    // responsible for resetting current_step so we don't have a cross-thread write race on it.
    {
        auto& arp = layer.arp_state;
        bool const recording_now = arp.recording.Load(LoadMemoryOrder::Relaxed);
        if (recording_now && !arp.audio.was_recording_last_block) {
            arp.audio.playhead.current_step = 0;
            arp.current_step_for_gui.Store(0, StoreMemoryOrder::Relaxed);
        }
        arp.audio.was_recording_last_block = recording_now;
    }

    // Start/end notes.
    // =======================================================================================================
    bool const arp_is_recording = layer.arp_state.recording.Load(LoadMemoryOrder::Relaxed);
    bool const arp_playback = ArpIsOn(layer) && !arp_is_recording;

    if (!arp_playback) {
        // Regular playback of notes.

        for (auto const& note : changes.note_events) {
            if (note.exclusively_for_layer != -1 && note.exclusively_for_layer != layer.index) continue;
            switch (note.type) {
                case NoteEvent::Type::On: {
                    LayerHandleNoteOn(layer,
                                      context,
                                      voice_pool,
                                      {
                                          .trigger_event = sample_lib::TriggerEvent::NoteOn,
                                          .note = note.note,
                                          .velocity = note.velocity,
                                          .offset = note.offset,
                                      });

                    // Also record note if in arp recording mode.
                    auto& arp = layer.arp_state;
                    if (arp_is_recording && arp.audio.playhead.current_step < arp.audio.length) {
                        auto& rec_step = arp.audio.playhead.current_step;
                        // Atomic Load-modify-Store: audio owns step.note here, but the GUI may be
                        // editing other steps concurrently. A racing GUI edit on this same step
                        // during recording would be lost; the GUI greys out step-note editing in
                        // recording mode so this is acceptable.
                        auto s = arp.steps[rec_step].Load(LoadMemoryOrder::Relaxed);
                        s.note = note.note.note;
                        arp.steps[rec_step].Store(s, StoreMemoryOrder::Relaxed);
                        rec_step++;
                        if (rec_step >= arp.audio.length) {
                            arp.recording.Store(false, StoreMemoryOrder::Relaxed);
                            arp.audio.was_recording_last_block = false;
                            arp.current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
                        } else {
                            arp.current_step_for_gui.Store(rec_step, StoreMemoryOrder::Relaxed);
                        }
                    }
                    break;
                }
                case NoteEvent::Type::Off: {
                    LayerHandleNoteOff(layer,
                                       context,
                                       voice_pool,
                                       note.note,
                                       note.velocity,
                                       note.created_by_cc64);
                    break;
                }
            }
        }
    } else {
        // Arpeggiator playback.

        auto& arp = layer.arp_state;
        bool const was_held = arp.audio.any_notes_held;

        // Update any_notes_held
        {
            arp.audio.any_notes_held = false;
            for (auto const chan : Range<u8>(16))
                if (context.midi_note_state.NotesHeldIncludingSustained((u4)chan).AnyValuesSet())
                    arp.audio.any_notes_held = true;
        }

        for (auto const& note : changes.note_events) {
            if (note.exclusively_for_layer != -1 && note.exclusively_for_layer != layer.index) continue;

            if (note.type == NoteEvent::Type::On &&
                arp.audio.trigger_mode == param_values::ArpTriggerMode::Retrigger) {
                for (auto& head : arp.audio.octave_polyrate_playheads) {
                    ReleaseNotes(head.last_triggered_notes, voice_pool, layer.voice_controller);
                    head.frames_until_next_step = 0;
                    head.current_step = 0;
                }
            }
        }

        if (!was_held && arp.audio.any_notes_held)
            for (auto& head : arp.audio.octave_polyrate_playheads)
                head.frames_until_next_step = 0;

        if (was_held && !arp.audio.any_notes_held) {
            for (auto& head : arp.audio.octave_polyrate_playheads) {
                ReleaseNotes(head.last_triggered_notes, voice_pool, layer.voice_controller);
                head.current_step = 0;
            }
            arp.audio.prev_active_octaves = {};
            arp.current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
        }
    }
}

LayerProcessResult ProcessLayer(LayerProcessor& layer,
                                AudioProcessingContext const& context,
                                VoicePool& voice_pool,
                                u32 num_frames,
                                bool start_fade_out) {
    ZoneScoped;
    ZoneValue(layer.index);

    constexpr f32 k_inst_change_fade_ms = 100;

    LayerProcessResult result {};

    for (auto& voice : voice_pool.voices) {
        if (voice.produced_audio_this_block && voice.controller == &layer.voice_controller) {
            if (!result.output) {
                // We can use the first voice's buffer as the output buffer.
                result.output = Span<f32x2>(voice.buffer.data, num_frames);
            } else {
                // Otherwise we combine the voice buffers.
                auto& out = *result.output;
                for (auto const i : Range(num_frames))
                    out[i] += voice.buffer[i];
            }
        }
    }

    // NOTE: we want to trigger a fade out regardless of whether or not this layer is actually processing
    // audio at the moment because we want the swapping of instruments to be in sync with any other layers
    if (start_fade_out)
        layer.inst_change_fade.SetAsFadeOutIfNotAlready(context.sample_rate, k_inst_change_fade_ms);

    if (!result.output || layer.audio_thread_inst.tag == InstrumentType::None) {
        if (layer.inst_change_fade.JumpMultipleSteps(num_frames) == VolumeFade::State::Silent)
            result.instrument_swapped = ChangeInstrumentIfNeededAndReset(layer, voice_pool, context);

        layer.peak_meter.Zero();
        return result;
    }

    if (result.output) {
        for (auto& frame : *result.output) {
            frame = layer.eq_bands.Process(context, frame);

            frame *= layer.gain_smoother.LowPass(layer.gain * layer.mute_solo_gain,
                                                 context.one_pole_smoothing_cutoff_10ms);

            if (!result.instrument_swapped) {
                auto const fade = layer.inst_change_fade.GetFadeAndStateChange();
                frame *= fade.value;
                if (fade.state_changed == VolumeFade::State::Silent)
                    result.instrument_swapped = ChangeInstrumentIfNeededAndReset(layer, voice_pool, context);
            } else {
                // If we have swapped we want to be silent for the remainder of this block - we will use the
                // new instrument next block
                frame = {};
            }
        }

        ASSERT_HOT(!layer.inst_change_fade.IsSilent());

        layer.peak_meter.AddBuffer(*result.output);
    }

    return result;
}

void ResetLayerAudioProcessing(LayerProcessor& layer) {
    for (auto& b : layer.eq_bands.eq_bands)
        b.eq_data = {};
    layer.inst_change_fade.ForceSetFullVolume();
    layer.eq_bands.Reset();
    layer.gain_smoother.Reset();
}

TEST_CASE(TestKeyRangeFade) {
    SUBCASE("fade in") {
        CHECK_EQ(KeyRangeFadeInAmp(0, 0, 0), 1.0f);

        auto const v = KeyRangeFadeInAmp(0, 0, 1);
        CHECK_GT(v, 0.0f);
        CHECK_LT(v, 1.0f);

        CHECK_EQ(KeyRangeFadeInAmp(-1, 0, 1), 0.0f);
        CHECK_EQ(KeyRangeFadeInAmp(1, 0, 1), 1.0f);

        for (auto const note : Range(8, 18))
            tester.log.Debug("[{}] = {}", note, KeyRangeFadeInAmp(note, 10, 5));
    }

    SUBCASE("fade out") {
        CHECK_EQ(KeyRangeFadeOutAmp(0, 0, 0), 1.0f);

        auto const v = KeyRangeFadeOutAmp(0, 0, 1);
        CHECK_GT(v, 0.0f);
        CHECK_LT(v, 1.0f);

        CHECK_EQ(KeyRangeFadeOutAmp(-1, 0, 1), 1.0f);
        CHECK_EQ(KeyRangeFadeOutAmp(1, 0, 1), 0.0f);

        for (auto const note : Range(0, 13))
            tester.log.Debug("[{}] = {}", note, KeyRangeFadeOutAmp(note, 10, 5));
    }
    return k_success;
}

TEST_REGISTRATION(RegisterLayerProcessorTests) { REGISTER_TEST(TestKeyRangeFade); }
