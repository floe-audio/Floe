// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/instrument.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/midi.hpp"
#include "processing_utils/synced_timings.hpp"
#include "sample_lib_server/sample_library_server.hpp"

struct Parameters;

struct ArpeggiatorState {
    Array<Atomic<ArpStep>, k_arp_max_steps> steps {};

    // k_arp_max_steps means not playing/recording.
    Atomic<u32> current_step_for_gui {k_arp_max_steps};

    // Written by GUI to request recording (Fixed mode only); cleared by audio when recording finishes.
    Atomic<bool> recording {false};

    Atomic<bool> on_for_gui {false};
    Atomic<SyncedTimes> resolved_rate_for_gui {SyncedTimes::_1_8};

    Atomic<u8> slice_start_offset {0};
    Atomic<u8> slice_loop_length {0}; // 0 = all remaining after start_offset

    // For supporting OctavePolyrate modes.
    static constexpr u32 k_octave_polyrate_num_playheads = 11;
    static constexpr int k_octave_polyrate_base_octave = 5; // C3 = MIDI 60, octave 5 = 1x rate

    struct AudioOnly {
        struct Playhead {
            u32 current_step {};
            u32 note_index {}; // counts only steps that trigger a note (excludes tied/off steps)
            u32 frames_until_next_step {};
            u32 frames_into_current_step {};
            u32 frames_per_step {1};
            u32 gate_off_frame {};
            bool one_shot_finished {};
            Array<Bitset<128>, 16> last_triggered_notes {};
            u32 last_random_note_index {LargestRepresentableValue<u32>()};
        };

        union {
            Playhead playhead; // Normal
            Array<Playhead, k_octave_polyrate_num_playheads>
                octave_polyrate_playheads; // For OctavePolyrate modes.
        };

        bool any_notes_held {};
        Bitset<k_octave_polyrate_num_playheads> prev_active_octaves {};

        param_values::ArpMode type {};
        param_values::ArpNoteOrder note_order {};
        param_values::ArpOctavePolyrate octave_polyrate {};
        param_values::ArpTriggerMode trigger_mode {};
        SyncedTimes user_rate {SyncedTimes::_1_8};
        SyncedTimes rate {SyncedTimes::_1_8}; // user_rate or resolved auto-rate
        bool auto_rate {};
        bool one_shot {};
        u32 length {8};
        f32 humanise {};

        // Used to detect a GUI-initiated recording false->true transition so audio can reset
        // current_step itself (avoiding a cross-thread write race on it).
        bool was_recording_last_block {};
    } audio;
};

struct ArpSliceMapping {
    u32 length {};
    Array<u8, k_arp_max_steps> step_to_slice_index {};

    Span<u8 const> AsSpan() const { return {step_to_slice_index.data, length}; }
};

ArpSliceMapping
ComputeArpSliceMapping(Span<sample_lib::Region::Slice const> slices, u8 start_offset, u8 loop_length);

inline ArpStep ArpStepAt(u32 i, ArpStep raw_step, Span<u8 const> slice_mapping) {
    if (slice_mapping.size) {
        raw_step.tie = (i > 0) && slice_mapping[i] == slice_mapping[i - 1];
        raw_step.interval = 0;
        raw_step.note = 0;
    }
    return raw_step;
}

// Helper for the GUI to understand what needs to be shown/editable.
struct ArpGuiSnapshot {
    enum class Activation : u8 {
        UserDefined, // User's params control everything
        ForcedBySlicing, // Instrument has slice markers; arp runs in a locked slice mode
    };

    Activation activation;
    bool on;
    param_values::ArpMode type;
    param_values::ArpNoteOrder note_order;

    u32 length;
    Array<ArpStep, k_arp_max_steps> user_steps;
    Array<u8, k_arp_max_steps> step_to_slice_index;
    u32 slice_index_length;

    struct {
        bool length;
        bool note_order;
        bool auto_rate_visible;
        bool step_velocity;
        bool step_gate;
        bool step_on;
        bool step_tie;
        bool step_note;
    } edit;

    ArpStep StepAt(u32 i) const {
        return ArpStepAt(i, user_steps[i], {step_to_slice_index.data, slice_index_length});
    }
};

ArpGuiSnapshot CreateArpGuiSnapshot(Parameters const& params,
                                    u8 layer_index,
                                    ArpeggiatorState const& arp,
                                    Instrument const& inst);

// Audio-thread types and functions
// =========================================================================================================

struct SliceRange {
    u32 start_frame;
    Optional<u32> end_frame; // nullopt = play to end of sample
};

struct ArpNoteCommand {
    enum class Type : u8 { NoteOn, NoteOff };
    Type type;
    MidiChannelNote note;
    f32 velocity;
    u32 offset;
    Optional<SliceRange> slice;
};

constexpr u32 k_max_arp_commands = 200;
using ArpNoteCommands = DynamicArrayBounded<ArpNoteCommand, k_max_arp_commands>;

bool ArpIsOn(param_values::ArpMode mode, InstrumentUnwrapped const& inst);

u32 ArpFramesPerStep(SyncedTimes rate, AudioProcessingContext const& context);

void ArpProcessBlock(ArpeggiatorState& arp,
                     AudioProcessingContext const& context,
                     InstrumentUnwrapped const& inst,
                     u64& random_seed,
                     u32 num_frames,
                     ArpNoteCommands& out_commands);

Optional<SyncedTimes> AutoSyncedTimeForLayer(InstrumentUnwrapped const& inst,
                                             AudioProcessingContext const& context);

void ResetArpAudioPlayback(ArpeggiatorState& arp);
