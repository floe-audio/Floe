// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arpeggiator.hpp"

#include "common_infrastructure/state/state_snapshot.hpp"

#include "processor/param.hpp"

static ArpSliceMapping
ComputeArpSliceMapping(Span<sample_lib::Region::Slice const> slices, u8 start_offset, u8 loop_length) {
    if (!slices.size) return {};

    auto const num_slices = (u32)slices.size;
    auto const effective_length = loop_length == 0 ? num_slices : Min((u32)loop_length, num_slices);

    ArpSliceMapping mapping {};
    for (auto const i : Range(effective_length)) {
        auto const slice_i = ((u32)start_offset + i) % num_slices;
        for (auto const _ : Range(slices[slice_i].length_proportion)) {
            if (mapping.size >= k_arp_max_steps) break;
            dyn::Append(mapping, (u8)slice_i);
        }
    }
    return mapping;
}

inline ArpStep ArpStepAt(u32 i, ArpStep raw_step, Span<u8 const> slice_mapping) {
    if (slice_mapping.size) {
        raw_step.tie = (i > 0) && slice_mapping[i] == slice_mapping[i - 1];
        raw_step.interval = 0;
        raw_step.note = 0;
    }
    return raw_step;
}

ArpStep ArpGuiSnapshot::StepAt(u32 i) const {
    ASSERT(g_is_logical_main_thread);

    return ArpStepAt(i, user_steps[i], slice_mapping);
}

ArpGuiSnapshot CreateArpGuiSnapshot(Parameters const& params,
                                    u8 layer_index,
                                    ArpeggiatorState const& arp,
                                    Span<sample_lib::Region::Slice const> slices) {
    ASSERT(g_is_logical_main_thread);

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
        auto const mapping = ComputeArpSliceMapping(slices, start_offset, loop_length);
        return {
            .activation = ArpGuiSnapshot::Activation::ForcedBySlicing,
            .on = true,
            .type = param_values::ArpMode::Played,
            .note_order = param_note_order, // User-selectable even in slice mode
            .length = (u32)mapping.size,
            .user_steps = snapshot,
            .slice_mapping = mapping,
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
    auto const param_on = params.BoolValue(layer_index, LayerParamIndex::ArpOn);
    auto const param_mode = params.IntValue<param_values::ArpMode>(layer_index, LayerParamIndex::ArpMode);
    auto const param_length =
        Clamp(params.IntValue<u32>(layer_index, LayerParamIndex::ArpLength), 1u, (u32)k_arp_max_steps);
    auto const all_editable = param_on;
    return {
        .activation = ArpGuiSnapshot::Activation::UserDefined,
        .on = param_on,
        .type = param_mode,
        .note_order = param_note_order,
        .length = param_length,
        .user_steps = snapshot,
        .slice_mapping = {},
        .edit =
            {
                .length = all_editable,
                .note_order = all_editable,
                .auto_rate_visible = false, // Not useful without slices
                .step_velocity = true,
                .step_gate = true,
                .step_on = true,
                .step_tie = true,
                .step_note = true,
            },
    };
}

void ArpApplyState(ArpeggiatorState& arp, StateSnapshot const& state, u8 layer_index) {
    ASSERT(g_is_logical_main_thread);

    for (auto const step_index : Range(k_arp_max_steps))
        arp.steps[step_index].Store(state.arp_steps[layer_index][step_index], StoreMemoryOrder::Relaxed);

    auto const& slice_config = state.slice_arp_configs[layer_index];
    arp.slice_start_offset.Store(slice_config.start_offset, StoreMemoryOrder::Relaxed);
    arp.slice_loop_length.Store(slice_config.loop_length, StoreMemoryOrder::Relaxed);
}

// Audio-thread functions
// =========================================================================================================

bool ArpIsOn(bool on, sample_lib::Region const* sliced_region) {
    ASSERT_HOT(!sliced_region ||
               (sliced_region->slices.size && sliced_region->loop_beats && sliced_region->native_bpm > 0));
    return on || sliced_region;
}

u32 ArpFramesPerStep(SyncedTimes rate, AudioProcessingContext const& context) {
    auto const hz = SyncedTimeToHz(context.tempo, rate);
    return Max(1u, (u32)(context.sample_rate / hz));
}

void ResetArpAudioPlayback(ArpeggiatorState& arp) {
    arp.audio.playheads = {};
    arp.audio.any_notes_held = false;
    arp.audio.prev_active_octaves = {};
    arp.current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
}

static void EmitReleaseNotes(Array<Bitset<128>, 16>& notes, ArpNoteCommands& out_commands) {
    for (auto const chan : Range<u8>(16))
        notes[chan].ForEachSetBit([&](usize bit) {
            dyn::Append(out_commands,
                        ArpNoteCommand {
                            {
                                .velocity = 0,
                                .offset = 0,
                                .note = {.note = (u7)bit, .channel = (u4)chan},
                                .type = NoteEvent::Type::Off,
                            },
                            k_nullopt,
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

    auto const slice_mapping = ({
        ArpSliceMapping m {};
        if (args.slices.size) {
            auto const start_offset = arp.slice_start_offset.Load(LoadMemoryOrder::Relaxed);
            auto const loop_length = arp.slice_loop_length.Load(LoadMemoryOrder::Relaxed);
            m = ComputeArpSliceMapping(args.slices, start_offset, loop_length);
        }
        m;
    });

    auto const length = slice_mapping.size ? (u32)slice_mapping.size : arp.audio.length;

    auto const step_at = [&](u32 step_index) {
        return ArpStepAt(step_index, arp.steps[step_index].Load(LoadMemoryOrder::Relaxed), slice_mapping);
    };

    auto const step = step_at(head.current_step % length);

    auto const advance_step = [&]() {
        if (args.publish_gui_step)
            arp.current_step_for_gui.Store(head.current_step, StoreMemoryOrder::Relaxed);
        auto const next = (u32)head.current_step + 1;
        if (arp.audio.one_shot && next >= length) {
            head.one_shot_finished = true;
            return;
        }
        head.current_step = (u8)(next % length);
        ASSERT_HOT(head.current_step < k_arp_max_steps);
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
            auto const slice_idx = slice_mapping[head.current_step % length];
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
        dyn::Append(out_commands,
                    ArpNoteCommand {
                        {
                            .velocity = velocity,
                            .offset = args.frame_offset + ({
                                          u32 humanise_delay = 0;
                                          if (arp.audio.humanise > 0) {
                                              constexpr f32 k_max_ms = 80;
                                              constexpr f32 k_max_fraction_of_rate = 0.2f;
                                              auto const max_delay_frames =
                                                  args.context.sample_rate * (k_max_ms / 1000.0f);
                                              auto const max_delay_humanise_param =
                                                  (f32)head.frames_per_step * k_max_fraction_of_rate *
                                                  arp.audio.humanise;
                                              auto const max_delay =
                                                  (u32)Min(max_delay_frames, max_delay_humanise_param);
                                              if (max_delay)
                                                  humanise_delay =
                                                      RandomIntInRange<u32>(args.random_seed, 0, max_delay);
                                          }
                                          humanise_delay;
                                      }),
                            .note = note,
                            .type = NoteEvent::Type::On,
                        },
                        slice,
                    });
        head.last_triggered_notes[note.channel].Set(note.note);
    };

    switch (args.type) {
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
                        u8 idx;
                        auto attempts = 0;
                        do {
                            idx = RandomIntInRange<u8>(args.random_seed, 0, (u8)(num_notes - 1));
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
    auto const next_step_index = (head.current_step + 1) % length;
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

struct ArpProcessBlockData {
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
                                    ArpProcessBlockData const& block,
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
        arp.audio.playheads[oct].frames_per_step = OctavePolyrateFramesPerStep(base_frames, oct, ratio);

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
                auto const& ref = arp.audio.playheads[*ref_oct];
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
                    auto& head = arp.audio.playheads[oct];
                    head.current_step = (u8)((frames_elapsed / head.frames_per_step) % length);
                    ASSERT_HOT(head.current_step < k_arp_max_steps);
                    auto const remainder = (u32)(frames_elapsed % head.frames_per_step);
                    head.frames_into_current_step = remainder;
                    head.frames_until_next_step = head.frames_per_step - remainder;
                });
            }
        }
    }
    arp.audio.prev_active_octaves = active_octaves;

    // In one-shot polyrate mode, all octaves keep looping until the slowest (lowest) octave
    // completes, then everything stops together.
    auto const slowest_active_oct = ({
        Optional<u32> s {};
        if (arp.audio.one_shot) s = (u32)active_octaves.FirstSetBit();
        s;
    });

    for (auto const frame_index : Range(num_frames)) {
        for (auto const oct : Range(ArpeggiatorState::k_octave_polyrate_num_playheads)) {
            if (!active_octaves.Get(oct)) continue;
            auto& head = arp.audio.playheads[oct];

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

                if (slowest_active_oct && head.one_shot_finished && oct != *slowest_active_oct) {
                    head.one_shot_finished = false;
                    head.current_step = 0;
                    head.note_index = 0;
                }

                head.frames_until_next_step = head.frames_per_step;
                head.frames_into_current_step = 0;
            }

            if (head.gate_off_frame > 0 && head.frames_into_current_step == head.gate_off_frame) {
                EmitReleaseNotes(head.last_triggered_notes, out_commands);
                head.gate_off_frame = 0;
                if (head.one_shot_finished)
                    arp.current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
            }

            if (slowest_active_oct && oct == *slowest_active_oct && head.one_shot_finished &&
                head.gate_off_frame == 0) {
                for (auto const o : Range(ArpeggiatorState::k_octave_polyrate_num_playheads)) {
                    if (o == oct || !active_octaves.Get(o)) continue;
                    EmitReleaseNotes(arp.audio.playheads[o].last_triggered_notes, out_commands);
                    arp.audio.playheads[o].one_shot_finished = true;
                    arp.audio.playheads[o].gate_off_frame = 0;
                }
                return;
            }

            --head.frames_until_next_step;
            ++head.frames_into_current_step;
        }
    }
}

static void ArpProcessBlockRegular(ArpeggiatorState& arp,
                                   AudioProcessingContext const& context,
                                   u64& random_seed,
                                   ArpProcessBlockData const& block,
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
                     sample_lib::Region const* sliced_region,
                     u64& random_seed,
                     u32 num_frames,
                     ArpNoteCommands& out_commands) {
    if (!ArpIsOn(arp.audio.on, sliced_region)) return;
    if (!arp.audio.any_notes_held) return;
    if (num_frames == 0) return;

    auto const block = ({
        ArpProcessBlockData b {};
        for (auto const chan : Range<u8>(16)) {
            auto const held = context.midi_note_state.NotesHeldIncludingSustained((u4)chan);
            held.ForEachSetBit([&](usize bit) {
                b.held_notes.data[b.held_notes.size++] = {.note = (u7)bit, .channel = (u4)chan};
            });
        }
        Sort(b.held_notes, [](auto const& a, auto const& b) { return a.note < b.note; });
        if (sliced_region) b.slices = sliced_region->slices;
        b.effective_type = b.slices.size ? param_values::ArpMode::Played : arp.audio.type;
        b;
    });

    if (arp.audio.octave_polyrate == param_values::ArpOctavePolyrate::Off)
        ArpProcessBlockRegular(arp, context, random_seed, block, num_frames, out_commands);
    else
        ArpProcessBlockPolyrate(arp, context, random_seed, block, num_frames, out_commands);
}

static SyncedTimes AutoSyncedTimeForSlicedRegion(sample_lib::Region const& sliced_region,
                                                 AudioProcessingContext const& context) {
    u32 total_prop = 0;
    for (auto const& s : sliced_region.slices)
        total_prop += s.length_proportion;

    ASSERT_HOT(total_prop);
    ASSERT_HOT(sliced_region.loop_beats);
    ASSERT_HOT(sliced_region.native_bpm > 0);

    auto const native_step_ms =
        ((f64)sliced_region.loop_beats * 60000.0 / (f64)sliced_region.native_bpm) / (f64)total_prop;

    return LargestSyncedTimeWithinTarget(native_step_ms, context.tempo, SyncedTimesType::Straight);
}

static void ArpUpdateRate(ArpeggiatorState& arp,
                          sample_lib::Region const* sliced_region,
                          AudioProcessingContext const& context) {
    arp.audio.rate = arp.audio.user_rate;

    if (sliced_region && arp.audio.auto_rate)
        arp.audio.rate = AutoSyncedTimeForSlicedRegion(*sliced_region, context);

    arp.resolved_rate_for_gui.Store(arp.audio.rate, StoreMemoryOrder::Relaxed);
}

void ArpHandleInstrumentChange(ArpeggiatorState& arp,
                               ArpInstrumentChangeArgs const& args,
                               ArpNoteCommands& out_commands) {
    arp.slice_start_offset.Store(0, StoreMemoryOrder::Relaxed);
    arp.slice_loop_length.Store(0, StoreMemoryOrder::Relaxed);

    auto const was_on = ArpIsOn(arp.audio.on, args.old_sliced_region);
    auto const now_on = ArpIsOn(arp.audio.on, args.new_sliced_region);

    arp.on_for_gui.Store(now_on, StoreMemoryOrder::Relaxed);

    if (arp.audio.auto_rate) ArpUpdateRate(arp, args.new_sliced_region, args.context);

    if (!now_on && was_on) {
        EmitReleaseNotes(arp.audio.playhead.last_triggered_notes, out_commands);
        ResetArpAudioPlayback(arp);
    } else if (now_on) {
        arp.audio.playhead.frames_per_step = ArpFramesPerStep(arp.audio.rate, args.context);
    }
}

ArpNoteHandling
ArpOnBlockChanges(ArpeggiatorState& arp, ArpBlockChangesArgs const& args, ArpNoteCommands& out_commands) {
    // Update params.
    // =======================================================================================================
    bool rate_changed = false;

    if (auto const p =
            args.changed_params.IntValue<param_values::ArpMode>(args.layer_index, LayerParamIndex::ArpMode))
        arp.audio.type = *p;

    if (auto const p = args.changed_params.BoolValue(args.layer_index, LayerParamIndex::ArpOn)) {
        if (*p != arp.audio.on) {
            bool const on_before = ArpIsOn(arp.audio.on, args.sliced_region);
            arp.audio.on = *p;
            bool const on_after = ArpIsOn(arp.audio.on, args.sliced_region);
            if (on_before != on_after) {
                arp.on_for_gui.Store(on_after, StoreMemoryOrder::Relaxed);
                if (on_after) {
                    arp.audio.playhead.frames_per_step = ArpFramesPerStep(arp.audio.rate, args.context);
                } else {
                    EmitReleaseNotes(arp.audio.playhead.last_triggered_notes, out_commands);
                    ResetArpAudioPlayback(arp);
                }
            }
        }
    }

    if (auto const p =
            args.changed_params.IntValue<param_values::ArpNoteOrder>(args.layer_index,
                                                                     LayerParamIndex::ArpNoteOrder))
        arp.audio.note_order = *p;

    if (auto const p = args.changed_params.IntValue<param_values::ArpOctavePolyrate>(
            args.layer_index,
            LayerParamIndex::ArpOctavePolyrate)) {
        if (*p != arp.audio.octave_polyrate) {
            for (auto& head : arp.audio.playheads)
                EmitReleaseNotes(head.last_triggered_notes, out_commands);
            arp.audio.playheads = {};
            arp.audio.prev_active_octaves = {};
            arp.audio.playhead.frames_per_step = ArpFramesPerStep(arp.audio.rate, args.context);
            arp.audio.octave_polyrate = *p;
        }
    }

    if (auto const p = args.changed_params.IntValue<u32>(args.layer_index, LayerParamIndex::ArpLength))
        arp.audio.length = Clamp(*p, 1u, (u32)k_arp_max_steps);

    if (auto const p =
            args.changed_params.IntValue<param_values::ArpTriggerMode>(args.layer_index,
                                                                       LayerParamIndex::ArpTriggerMode))
        arp.audio.trigger_mode = *p;

    if (auto const p = args.changed_params.IntValue<param_values::ArpSyncedRate>(args.layer_index,
                                                                                 LayerParamIndex::ArpRate)) {
        arp.audio.user_rate = SyncedTimesFromParam(*p);
        rate_changed = true;
    }

    if (auto const p = args.changed_params.BoolValue(args.layer_index, LayerParamIndex::ArpAutoRate)) {
        if (*p != arp.audio.auto_rate) {
            arp.audio.auto_rate = *p;
            rate_changed = true;
        }
    }

    if (auto const p = args.changed_params.BoolValue(args.layer_index, LayerParamIndex::ArpOneShot))
        arp.audio.one_shot = *p;

    if (auto const p = args.changed_params.ProjectedValue(args.layer_index, LayerParamIndex::ArpHumanise))
        arp.audio.humanise = *p;

    if (rate_changed || args.tempo_changed) {
        ArpUpdateRate(arp, args.sliced_region, args.context);
        arp.audio.playhead.frames_per_step = ArpFramesPerStep(arp.audio.rate, args.context);
    }

    // Recording.
    // =======================================================================================================
    {
        bool const recording_now = arp.recording.Load(LoadMemoryOrder::Relaxed);
        if (recording_now && !arp.audio.was_recording_last_block) {
            arp.audio.playhead.current_step = 0;
            arp.audio.playhead.note_index = 0;
            arp.audio.playhead.one_shot_finished = false;
            arp.current_step_for_gui.Store(0, StoreMemoryOrder::Relaxed);
        }
        arp.audio.was_recording_last_block = recording_now;
    }

    bool const is_recording = arp.recording.Load(LoadMemoryOrder::Relaxed);

    auto const handling = (ArpIsOn(arp.audio.on, args.sliced_region) && !is_recording)
                              ? ArpNoteHandling::ArpHandlesNotes
                              : ArpNoteHandling::LayerHandlesNotes;

    // Record notes into steps (notes still play normally via the layer).
    if (is_recording) {
        for (auto const& note : args.note_events) {
            if (note.exclusively_for_layer != -1 && note.exclusively_for_layer != args.layer_index) continue;
            if (note.type != NoteEvent::Type::On) continue;
            if (arp.audio.playhead.current_step >= arp.audio.length) continue;

            auto& rec_step = arp.audio.playhead.current_step;
            auto s = arp.steps[rec_step].Load(LoadMemoryOrder::Relaxed);
            s.note = note.note.note;
            arp.steps[rec_step].Store(s, StoreMemoryOrder::Relaxed);

            do {
                rec_step++;
            } while (rec_step < arp.audio.length && arp.steps[rec_step].Load(LoadMemoryOrder::Relaxed).tie);

            if (rec_step >= arp.audio.length) {
                arp.recording.Store(false, StoreMemoryOrder::Relaxed);
                arp.audio.was_recording_last_block = false;
                arp.current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
            } else {
                arp.current_step_for_gui.Store(rec_step, StoreMemoryOrder::Relaxed);
            }
        }
    }

    if (handling == ArpNoteHandling::ArpHandlesNotes) {
        bool const was_held = arp.audio.any_notes_held;

        arp.audio.any_notes_held = ({
            bool b = false;
            for (auto const chan : Range<u8>(16))
                if (args.context.midi_note_state.NotesHeldIncludingSustained((u4)chan).AnyValuesSet()) {
                    b = true;
                    break;
                }
            b;
        });

        for (auto const& note : args.note_events) {
            if (note.exclusively_for_layer != -1 && note.exclusively_for_layer != args.layer_index) continue;

            if (note.type == NoteEvent::Type::On &&
                arp.audio.trigger_mode == param_values::ArpTriggerMode::Retrigger) {
                for (auto& head : arp.audio.playheads) {
                    EmitReleaseNotes(head.last_triggered_notes, out_commands);
                    head.frames_until_next_step = 0;
                    head.current_step = 0;
                    head.note_index = 0;
                    head.one_shot_finished = false;
                }
            }
        }

        if (!was_held && arp.audio.any_notes_held)
            for (auto& head : arp.audio.playheads) {
                head.frames_until_next_step = 0;
                head.one_shot_finished = false;
            }

        if (was_held && !arp.audio.any_notes_held) {
            for (auto& head : arp.audio.playheads) {
                EmitReleaseNotes(head.last_triggered_notes, out_commands);
                head.current_step = 0;
                head.note_index = 0;
                head.one_shot_finished = false;
            }
            arp.audio.prev_active_octaves = {};
            arp.current_step_for_gui.Store(k_arp_max_steps, StoreMemoryOrder::Relaxed);
        }
    }

    return handling;
}

// Tests
// =============================================================================================================

#include "tests/framework.hpp"

static clap_host_t const k_test_host {
    .clap_version = CLAP_VERSION,
    .host_data = nullptr,
    .name = "Test",
    .vendor = "Test",
    .url = "",
    .version = "1",
    .get_extension = [](clap_host_t const*, char const*) -> void const* { return nullptr; },
    .request_restart = [](clap_host_t const*) {},
    .request_process = [](clap_host_t const*) {},
    .request_callback = [](clap_host_t const*) {},
};

TEST_CASE(TestArpOneShotPolyrateWaitsForSlowest) {
    sample_lib::Region const* const no_sliced_region = nullptr;

    ArpeggiatorState arp {.audio {
        .any_notes_held = true,
        .type = param_values::ArpMode::Played,
        .on = true,
        .note_order = param_values::ArpNoteOrder::Up,
        .octave_polyrate = param_values::ArpOctavePolyrate::Double,
        .rate = SyncedTimes::_1_4,
        .one_shot = true,
        .length = 2,
    }};

    AudioProcessingContext context {
        .sample_rate = 44100,
        .process_block_size_max = 512,
        .tempo = 120,
        .host = k_test_host,
    };

    // Hold C3 (MIDI 60, octave 5 = base) and C4 (MIDI 72, octave 6 = 2x speed).
    context.midi_note_state.NoteOn({.note = 60, .channel = 0}, 0.8f);
    context.midi_note_state.NoteOn({.note = 72, .channel = 0}, 0.8f);

    auto const base_step_frames = ArpFramesPerStep(arp.audio.rate, context);
    // Octave 5 = base, octave 6 = base/2 (Double ratio means higher octaves are faster).
    auto const oct5_step_frames = OctavePolyrateFramesPerStep(base_step_frames, 5, 2.0f);
    auto const oct6_step_frames = OctavePolyrateFramesPerStep(base_step_frames, 6, 2.0f);
    REQUIRE_EQ(oct5_step_frames, base_step_frames);
    REQUIRE_EQ(oct6_step_frames, base_step_frames / 2);

    // Step 0 fires at frame 0, step 1 (the last) fires at frame fps. So oct6 fires its last step
    // at frame oct6_step_frames and oct5 fires its last step at frame oct5_step_frames.
    REQUIRE(oct6_step_frames < oct5_step_frames);

    u64 random_seed = 1;
    ArpNoteCommands commands {};

    // Process to a point after octave 6's last step but before octave 5's last step.
    auto const mid_point = oct6_step_frames + ((oct5_step_frames - oct6_step_frames) / 2);
    ArpProcessBlock(arp, context, no_sliced_region, random_seed, mid_point, commands);

    // Octave 6 should NOT be finished - it should keep looping until the slowest (octave 5) is done.
    REQUIRE(!arp.audio.playheads[6].one_shot_finished);
    REQUIRE(!arp.audio.playheads[5].one_shot_finished);

    // Process well past when octave 5's gate-off fires (last step at oct5_step_frames + gate duration of
    // oct5_step_frames). The stop-all triggers when the slowest octave's gate-off completes.
    commands = {};
    ArpProcessBlock(arp, context, no_sliced_region, random_seed, (oct5_step_frames * 2) + 1, commands);

    // Now all playheads should be finished.
    REQUIRE(arp.audio.playheads[5].one_shot_finished);
    REQUIRE(arp.audio.playheads[6].one_shot_finished);

    return k_success;
}

TEST_REGISTRATION(RegisterArpeggiatorTests) { REGISTER_TEST(TestArpOneShotPolyrateWaitsForSlowest); }
