// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/arpeggiator.hpp"

#include "processor/param.hpp"
#include "sample_lib_server/sample_library_server.hpp"

ArpSliceMapping
ComputeArpSliceMapping(Span<sample_lib::Region::Slice const> slices, u8 start_offset, u8 loop_length) {
    if (!slices.size) return {};

    auto const num_slices = (u32)slices.size;
    auto const effective_length = loop_length == 0 ? num_slices : Min((u32)loop_length, num_slices);

    u32 step_idx = 0;
    ArpSliceMapping mapping {};
    for (auto const i : Range(effective_length)) {
        auto const slice_i = ((u32)start_offset + i) % num_slices;
        for (auto const _ : Range(slices[slice_i].length_proportion)) {
            if (step_idx >= k_arp_max_steps) break;
            mapping.step_to_slice_index[step_idx] = (u8)slice_i;
            step_idx++;
        }
    }
    mapping.length = step_idx;
    return mapping;
}

ArpGuiSnapshot CreateArpGuiSnapshot(Parameters const& params,
                                    u8 layer_index,
                                    ArpeggiatorState const& arp,
                                    Instrument const& inst) {
    // Detect slice mode from the loaded instrument (main thread view).
    auto const slices = ({
        Span<sample_lib::Region::Slice const> s {};
        if (auto sm = inst.TryGetFromTag<InstrumentType::Sampler>()) {
            if (*sm) {
                if ((*sm)->instrument.category == sample_lib::SamplerCategory::Sliced)
                    s = (*sm)->instrument.regions[0].slices;
            }
        }
        s;
    });

    auto const param_note_order =
        params.IntValue<param_values::ArpNoteOrder>(layer_index, LayerParamIndex::ArpNoteOrder);

    // Snapshot the user's steps once for the frame.
    auto const snapshot = ({
        Array<ArpStep, k_arp_max_steps> s {};
        for (auto const step_index : Range(k_arp_max_steps))
            s[step_index] = arp.steps[step_index].Load(LoadMemoryOrder::Relaxed);
        s;
    });

    // Slices mode.
    if (slices.size) {
        auto const start_offset = arp.slice_start_offset.Load(LoadMemoryOrder::Relaxed);
        auto const loop_length = arp.slice_loop_length.Load(LoadMemoryOrder::Relaxed);
        auto mapping = ComputeArpSliceMapping(slices, start_offset, loop_length);
        return {
            .activation = ArpGuiSnapshot::Activation::ForcedBySlicing,
            .on = true,
            .type = param_values::ArpMode::Played,
            .note_order = param_note_order, // User-selectable even in slice mode
            .length = mapping.length,
            .user_steps = snapshot,
            .step_to_slice_index = mapping.step_to_slice_index,
            .slice_index_length = mapping.length,
            .edit =
                {
                    .length = false,
                    .note_order = true,
                    .auto_rate_visible = true,
                    .step_velocity = true,
                    .step_gate = true,
                    .step_on = true,
                    .step_tie = false,
                    .step_note = false,
                },
        };
    }

    // Normal user-driven mode.
    auto const param_mode = params.IntValue<param_values::ArpMode>(layer_index, LayerParamIndex::ArpMode);
    auto const param_length =
        Clamp(params.IntValue<u32>(layer_index, LayerParamIndex::ArpLength), 1u, (u32)k_arp_max_steps);
    auto const param_on = param_mode != param_values::ArpMode::Off;
    auto const all_editable = param_on;
    return {
        .activation = ArpGuiSnapshot::Activation::UserDefined,
        .on = param_on,
        .type = param_mode,
        .note_order = param_note_order,
        .length = param_length,
        .user_steps = snapshot,
        .step_to_slice_index = {},
        .slice_index_length = 0,
        .edit =
            {
                .length = all_editable,
                .note_order = all_editable,
                .auto_rate_visible = false, // Not useful without slices
                .step_velocity = all_editable,
                .step_gate = all_editable,
                .step_on = all_editable,
                .step_tie = all_editable,
                .step_note = all_editable,
            },
    };
}

// Audio-thread functions
// =========================================================================================================

static Span<sample_lib::Region::Slice const> ArpSlices(InstrumentUnwrapped const& inst) {
    if (auto p = inst.TryGet<sample_lib::LoadedInstrument const*>()) {
        auto const& i = **p;
        if (i.instrument.category == sample_lib::SamplerCategory::Sliced)
            return i.instrument.regions[0].slices;
    }
    return {};
}

bool ArpIsOn(param_values::ArpMode mode, InstrumentUnwrapped const& inst) {
    return mode != param_values::ArpMode::Off || ArpSlices(inst).size;
}

u32 ArpFramesPerStep(SyncedTimes rate, AudioProcessingContext const& context) {
    auto const hz = SyncedTimeToHz(context.tempo, rate);
    return Max(1u, (u32)(context.sample_rate / hz));
}

void ResetArpAudioPlayback(ArpeggiatorState& arp) {
    arp.audio.octave_polyrate_playheads = {};
    arp.audio.any_notes_held = false;
    arp.audio.prev_active_octaves = {};
    arp.current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
}

Optional<SyncedTimes> AutoSyncedTimeForLayer(InstrumentUnwrapped const& inst,
                                             AudioProcessingContext const& context) {
    auto const* inst_ptr = inst.TryGet<sample_lib::LoadedInstrument const*>();
    if (!inst_ptr || !*inst_ptr) return k_nullopt;
    auto const& regions = (*inst_ptr)->instrument.regions;
    if (regions.size != 1) return k_nullopt;
    auto const& region = regions[0];
    if (!region.slices.size || !region.loop_beats || region.native_bpm <= 0) return k_nullopt;

    u32 total_prop = 0;
    for (auto const& s : region.slices)
        total_prop += s.length_proportion;
    if (!total_prop) return k_nullopt;

    auto const native_step_ms = ((f64)region.loop_beats * 60000.0 / (f64)region.native_bpm) / (f64)total_prop;
    return LargestSyncedTimeWithinTarget(native_step_ms, context.tempo, SyncedTimesType::Straight);
}

static void EmitReleaseNotes(Array<Bitset<128>, 16>& notes, ArpNoteCommands& out_commands) {
    for (auto const chan : Range<u8>(16))
        notes[chan].ForEachSetBit([&](usize bit) {
            dyn::Append(out_commands,
                        ArpNoteCommand {
                            .type = ArpNoteCommand::Type::NoteOff,
                            .note = {.note = (u7)bit, .channel = (u4)chan},
                            .velocity = 0,
                            .offset = 0,
                            .slice = k_nullopt,
                        });
        });
    notes = {};
}

struct ArpExecuteStepArgs {
    AudioProcessingContext const& context;
    u64& random_seed;
    ArpeggiatorState::AudioOnly::Playhead& playhead;
    param_values::ArpMode type;
    bool publish_gui_step;
    Span<sample_lib::Region::Slice const> slices;
    Span<MidiChannelNote const> notes;
    u32 frame_offset;
};

static void ArpExecuteStep(ArpeggiatorState& arp, ArpExecuteStepArgs args, ArpNoteCommands& out_commands) {
    auto const num_notes = (u32)args.notes.size;
    if (num_notes == 0) return;

    auto& head = args.playhead;
    if (head.one_shot_finished) return;

    ArpSliceMapping slice_mapping {};
    if (args.slices.size) {
        auto const start_offset = arp.slice_start_offset.Load(LoadMemoryOrder::Relaxed);
        auto const loop_length = arp.slice_loop_length.Load(LoadMemoryOrder::Relaxed);
        slice_mapping = ComputeArpSliceMapping(args.slices, start_offset, loop_length);
    }

    auto const length = slice_mapping.length ? slice_mapping.length : arp.audio.length;

    auto const step_at = [&](u32 step_index) {
        return ArpStepAt(step_index,
                         arp.steps[step_index].Load(LoadMemoryOrder::Relaxed),
                         slice_mapping.AsSpan());
    };

    auto const step = step_at(head.current_step % length);

    auto const advance_step = [&]() {
        if (args.publish_gui_step)
            arp.current_step_for_gui.Store(head.current_step, StoreMemoryOrder::Relaxed);
        auto const next = head.current_step + 1;
        if (arp.audio.one_shot && next >= length) {
            head.one_shot_finished = true;
            return;
        }
        head.current_step = next % length;
        if (head.current_step == 0) head.note_index = 0;
    };

    if (step.tie) {
        advance_step();
        if (head.one_shot_finished) {
            EmitReleaseNotes(head.last_triggered_notes, out_commands);
            head.gate_off_frame = 0;
            if (args.publish_gui_step)
                arp.current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
        }
        return;
    }

    EmitReleaseNotes(head.last_triggered_notes, out_commands);

    if (!step.on) {
        advance_step();
        if (head.one_shot_finished && args.publish_gui_step)
            arp.current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
        return;
    }

    auto const slice = ({
        Optional<SliceRange> s {};
        if (args.slices.size) {
            auto const slice_idx = slice_mapping.step_to_slice_index[head.current_step % length];
            s = SliceRange {
                .start_frame = args.slices[slice_idx].start_frame,
                .end_frame = (slice_idx + 1 < args.slices.size)
                                 ? Optional<u32>(args.slices[slice_idx + 1].start_frame)
                                 : k_nullopt,
            };
        }
        s;
    });

    auto const humanise_velocity = [&](f32 vel) -> f32 {
        constexpr f32 k_max_bidirectional_jitter_fraction = 0.15f;
        if (arp.audio.humanise <= 0) return vel;
        auto const jitter = RandomFloatInRange(args.random_seed,
                                               -k_max_bidirectional_jitter_fraction,
                                               k_max_bidirectional_jitter_fraction) *
                            arp.audio.humanise;
        return Clamp(vel + jitter, 0.0f, 1.0f);
    };

    auto const emit_note_on = [&](MidiChannelNote note, f32 velocity) {
        dyn::Append(
            out_commands,
            ArpNoteCommand {
                .type = ArpNoteCommand::Type::NoteOn,
                .note = note,
                .velocity = velocity,
                .offset = args.frame_offset + ({
                              u32 humanise_delay = 0;
                              if (arp.audio.humanise > 0) {
                                  constexpr f32 k_max_ms = 80;
                                  constexpr f32 k_max_fraction_of_rate = 0.2f;
                                  auto const max_delay_frames =
                                      args.context.sample_rate * (k_max_ms / 1000.0f);
                                  auto const max_delay_humanise_param =
                                      (f32)head.frames_per_step * k_max_fraction_of_rate * arp.audio.humanise;
                                  auto const max_delay = (u32)Min(max_delay_frames, max_delay_humanise_param);
                                  if (max_delay)
                                      humanise_delay = RandomIntInRange<u32>(args.random_seed, 0, max_delay);
                              }
                              humanise_delay;
                          }),
                .slice = slice,
            });
        head.last_triggered_notes[note.channel].Set(note.note);
    };

    switch (args.type) {
        case param_values::ArpMode::Off: PanicIfReached(); break;
        case param_values::ArpMode::Fixed: {
            MidiChannelNote note {.note = step.note, .channel = 0};
            emit_note_on(note, humanise_velocity(step.Velocity01()));
            break;
        }
        case param_values::ArpMode::Played: {
            auto const trigger_note_transposed = [&](u32 idx, int extra_semitones) {
                auto const note = args.notes[idx % num_notes];
                auto const input_vel = args.context.midi_note_state.velocities[note.channel][note.note];
                auto const vel =
                    humanise_velocity(step.Velocity01() * LinearInterpolate(0.5f, 1.0f, input_vel));
                auto triggered_note = note;
                triggered_note.note =
                    (u7)Clamp((int)note.note + (int)step.interval + extra_semitones, 0, 127);
                emit_note_on(triggered_note, vel);
            };
            auto const trigger_note = [&](u32 idx) { trigger_note_transposed(idx, 0); };

            switch (arp.audio.note_order) {
                case param_values::ArpNoteOrder::Chord: {
                    for (auto const note_index : Range(num_notes))
                        trigger_note(note_index);
                    break;
                }
                case param_values::ArpNoteOrder::Up: {
                    trigger_note(head.note_index % num_notes);
                    break;
                }
                case param_values::ArpNoteOrder::Down: {
                    trigger_note((num_notes - 1) - (head.note_index % num_notes));
                    break;
                }
                case param_values::ArpNoteOrder::UpDown: {
                    if (num_notes <= 1) {
                        trigger_note(0);
                    } else {
                        auto const cycle_len = 2 * (num_notes - 1);
                        auto const pos = head.note_index % cycle_len;
                        trigger_note(pos < num_notes ? pos : cycle_len - pos);
                    }
                    break;
                }
                case param_values::ArpNoteOrder::DownUp: {
                    if (num_notes <= 1) {
                        trigger_note(0);
                    } else {
                        auto const cycle_len = 2 * (num_notes - 1);
                        auto const pos = head.note_index % cycle_len;
                        auto const up_index = pos < num_notes ? pos : cycle_len - pos;
                        trigger_note((num_notes - 1) - up_index);
                    }
                    break;
                }
                case param_values::ArpNoteOrder::Random: {
                    auto const idx = RandomIntInRange<u32>(args.random_seed, 0, num_notes - 1);
                    trigger_note(idx);
                    break;
                }
                case param_values::ArpNoteOrder::RandomNoRepeat: {
                    if (num_notes > 1) {
                        u32 idx;
                        auto attempts = 0;
                        do {
                            idx = RandomIntInRange<u32>(args.random_seed, 0, num_notes - 1);
                        } while (idx == head.last_random_note_index && ++attempts < 3);
                        head.last_random_note_index = idx;
                        trigger_note(idx);
                    } else {
                        trigger_note(0);
                    }
                    break;
                }
                case param_values::ArpNoteOrder::UpX2: {
                    trigger_note((head.note_index / 2) % num_notes);
                    break;
                }
                case param_values::ArpNoteOrder::DownX2: {
                    trigger_note((num_notes - 1) - ((head.note_index / 2) % num_notes));
                    break;
                }
                case param_values::ArpNoteOrder::UpDownX2: {
                    if (num_notes <= 1) {
                        trigger_note(0);
                    } else {
                        auto const note_pos = head.note_index / 2;
                        auto const cycle_len = 2 * (num_notes - 1);
                        auto const pos = note_pos % cycle_len;
                        trigger_note(pos < num_notes ? pos : cycle_len - pos);
                    }
                    break;
                }
                case param_values::ArpNoteOrder::Converge: {
                    if (num_notes <= 1) {
                        trigger_note(0);
                    } else {
                        auto const pos = head.note_index % num_notes;
                        if (pos % 2 == 0)
                            trigger_note(pos / 2);
                        else
                            trigger_note((num_notes - 1) - (pos / 2));
                    }
                    break;
                }
                case param_values::ArpNoteOrder::Diverge: {
                    if (num_notes <= 1) {
                        trigger_note(0);
                    } else {
                        auto const mid = (num_notes - 1) / 2;
                        auto const pos = head.note_index % num_notes;
                        if (pos % 2 == 0)
                            trigger_note(mid - (pos / 2));
                        else
                            trigger_note(mid + 1 + (pos / 2));
                    }
                    break;
                }
                case param_values::ArpNoteOrder::Thumb: {
                    if (num_notes <= 1) {
                        trigger_note(0);
                    } else {
                        auto const cycle_len = 2 * (num_notes - 1);
                        auto const pos = head.note_index % cycle_len;
                        if (pos % 2 == 0)
                            trigger_note(0);
                        else
                            trigger_note(1 + (pos / 2));
                    }
                    break;
                }
                case param_values::ArpNoteOrder::UpPlus: {
                    auto const cycle_len = num_notes + 1;
                    auto const pos = head.note_index % cycle_len;
                    if (pos < num_notes)
                        trigger_note(pos);
                    else
                        trigger_note_transposed(num_notes - 1, 12);
                    break;
                }
                case param_values::ArpNoteOrder::Count: PanicIfReached(); break;
            }
            head.note_index++;
            break;
        }
        case param_values::ArpMode::Count: PanicIfReached();
    }

    // Schedule gate-off if gate < 1 and next step is not tied.
    auto const is_last_one_shot_step = arp.audio.one_shot && (head.current_step + 1) >= length;
    auto const next_step_index = head.current_step % length;
    auto const next_is_tied = !is_last_one_shot_step && step_at(next_step_index).tie;
    auto const gate_01 = step.Gate01();
    if (is_last_one_shot_step)
        head.gate_off_frame = Max(1u, (u32)((f32)head.frames_per_step * gate_01));
    else if (gate_01 < 1.0f && !next_is_tied)
        head.gate_off_frame = Max(1u, (u32)((f32)head.frames_per_step * gate_01));
    else
        head.gate_off_frame = 0;

    advance_step();
}

struct ArpSharedBlockData {
    DynamicArrayBounded<MidiChannelNote, 128 * 16> held_notes;
    Span<sample_lib::Region::Slice const> slices;
    param_values::ArpMode effective_type;
};

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

static void ArpProcessBlockPolyrate(ArpeggiatorState& arp,
                                    AudioProcessingContext const& context,
                                    u64& random_seed,
                                    ArpSharedBlockData const& block,
                                    u32 num_frames,
                                    ArpNoteCommands& out_commands) {
    // Determine which octaves have held notes.
    auto const active_octaves = ({
        Bitset<ArpeggiatorState::k_octave_polyrate_num_playheads> octs {};
        for (auto const note_index : Range(block.held_notes.size))
            octs.Set((u32)(block.held_notes[note_index].note / 12));
        octs;
    });

    auto const gui_playhead_oct = block.held_notes.size ? (u32)(block.held_notes[0].note / 12) : (u32)0;

    auto const base_frames = ArpFramesPerStep(arp.audio.rate, context);
    auto const ratio = OctavePolyrateRatio(arp.audio.octave_polyrate);
    for (auto const oct : Range(ArpeggiatorState::k_octave_polyrate_num_playheads))
        arp.audio.octave_polyrate_playheads[oct].frames_per_step =
            OctavePolyrateFramesPerStep(base_frames, oct, ratio);

    // In Free mode, sync newly-active octaves to existing playback position so they join in phase.
    if (arp.audio.trigger_mode == param_values::ArpTriggerMode::Free) {
        auto const newly_active = active_octaves & ~arp.audio.prev_active_octaves;
        if (newly_active.AnyValuesSet()) {
            auto const ref_oct = ({
                Optional<u32> r = {};
                for (auto const oct : Range(ArpeggiatorState::k_octave_polyrate_num_playheads))
                    if (arp.audio.prev_active_octaves.Get(oct)) {
                        r = oct;
                        break;
                    }
                r;
            });

            if (ref_oct) {
                auto const& ref = arp.audio.octave_polyrate_playheads[*ref_oct];
                auto const length = ({
                    u32 l = arp.audio.length;
                    if (block.slices.size) {
                        l = 0;
                        for (auto const& s : block.slices)
                            l += s.length_proportion;
                    }
                    l;
                });

                auto const frames_elapsed =
                    ((u64)ref.current_step * ref.frames_per_step) + ref.frames_into_current_step;

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

    for (auto const frame_index : Range(num_frames)) {
        for (auto const oct : Range(ArpeggiatorState::k_octave_polyrate_num_playheads)) {
            if (!active_octaves.Get(oct)) continue;
            auto& head = arp.audio.octave_polyrate_playheads[oct];

            if (!head.one_shot_finished && head.frames_until_next_step == 0) {
                auto const oct_notes = ({
                    DynamicArrayBounded<MidiChannelNote, 12 * 16> n;
                    auto const low = (u7)(oct * 12);
                    auto const high = (u7)Min(127u, ((oct + 1) * 12) - 1);
                    for (auto const note_index : Range(block.held_notes.size))
                        if (block.held_notes[note_index].note >= low &&
                            block.held_notes[note_index].note <= high)
                            n.data[n.size++] = block.held_notes[note_index];
                    n;
                });

                ArpExecuteStep(arp,
                               {
                                   .context = context,
                                   .random_seed = random_seed,
                                   .playhead = head,
                                   .type = block.effective_type,
                                   .publish_gui_step = (oct == gui_playhead_oct),
                                   .slices = block.slices,
                                   .notes = oct_notes,
                                   .frame_offset = frame_index,
                               },
                               out_commands);

                head.frames_until_next_step = head.frames_per_step;
                head.frames_into_current_step = 0;
            }

            if (head.gate_off_frame > 0 && head.frames_into_current_step == head.gate_off_frame) {
                EmitReleaseNotes(head.last_triggered_notes, out_commands);
                head.gate_off_frame = 0;
                if (head.one_shot_finished)
                    arp.current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
            }

            --head.frames_until_next_step;
            ++head.frames_into_current_step;
        }
    }
}

static void ArpProcessBlockRegular(ArpeggiatorState& arp,
                                   AudioProcessingContext const& context,
                                   u64& random_seed,
                                   ArpSharedBlockData const& block,
                                   u32 num_frames,
                                   ArpNoteCommands& out_commands) {
    auto& playhead = arp.audio.playhead;
    for (auto const frame_index : Range(num_frames)) {
        if (!playhead.one_shot_finished && playhead.frames_until_next_step == 0) {
            ArpExecuteStep(arp,
                           {
                               .context = context,
                               .random_seed = random_seed,
                               .playhead = playhead,
                               .type = block.effective_type,
                               .publish_gui_step = true,
                               .slices = block.slices,
                               .notes = block.held_notes,
                               .frame_offset = frame_index,
                           },
                           out_commands);
            playhead.frames_until_next_step = playhead.frames_per_step;
            playhead.frames_into_current_step = 0;
        }

        // Gate-off: release notes mid-step.
        if (playhead.gate_off_frame > 0 && playhead.frames_into_current_step == playhead.gate_off_frame) {
            EmitReleaseNotes(playhead.last_triggered_notes, out_commands);
            playhead.gate_off_frame = 0;
            if (playhead.one_shot_finished)
                arp.current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
        }

        --playhead.frames_until_next_step;
        ++playhead.frames_into_current_step;
    }
}

void ArpProcessBlock(ArpeggiatorState& arp,
                     AudioProcessingContext const& context,
                     InstrumentUnwrapped const& inst,
                     u64& random_seed,
                     u32 num_frames,
                     ArpNoteCommands& out_commands) {
    if (!ArpIsOn(arp.audio.type, inst)) return;
    if (!arp.audio.any_notes_held) return;
    if (num_frames == 0) return;

    auto const block = ({
        ArpSharedBlockData b {};
        for (auto const chan : Range<u8>(16)) {
            auto const held = context.midi_note_state.NotesHeldIncludingSustained((u4)chan);
            held.ForEachSetBit([&](usize bit) {
                b.held_notes.data[b.held_notes.size++] = {.note = (u7)bit, .channel = (u4)chan};
            });
        }
        Sort(b.held_notes, [](auto const& a, auto const& b) { return a.note < b.note; });
        b.slices = ArpSlices(inst);
        b.effective_type = b.slices.size ? param_values::ArpMode::Played : arp.audio.type;
        b;
    });

    if (arp.audio.octave_polyrate == param_values::ArpOctavePolyrate::Off)
        ArpProcessBlockRegular(arp, context, random_seed, block, num_frames, out_commands);
    else
        ArpProcessBlockPolyrate(arp, context, random_seed, block, num_frames, out_commands);
}
