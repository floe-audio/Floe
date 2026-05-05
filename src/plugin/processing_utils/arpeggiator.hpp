// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

#include "audio_processing_context.hpp"
#include "synced_timings.hpp"

struct Parameters;
struct ChangedParams;
struct StateSnapshot;

static_assert(k_arp_max_steps <= LargestRepresentableValue<u8>());

// Designed to fit in an Atomic<>.
struct ArpStep {
    bool operator==(ArpStep const&) const = default;

    static constexpr f32 k_max = LargestRepresentableValue<u16>();

    f32 Velocity01() const { return (f32)velocity / k_max; }
    f32 Gate01() const { return (f32)gate / k_max; }
    static u16 From01(f32 v) { return (u16)Round(Clamp(v, 0.0f, 1.0f) * k_max); }

    u16 velocity {(u16)(k_max * 0.8f)};
    u16 gate {(u16)k_max}; // fraction of step duration the note plays (u16 max == 1.0)
    bool on {true};
    bool tie {false}; // fuse with previous step to create a larger unified step
    s8 interval {0}; // for 'played input notes' mode
    u7 note {60}; // for 'fixed sequence' mode
};

// These 2 values are not typical audio parameters because they are not designed to be automated and we want
// to reset/change every time the instrument change. Simpler to not involve plugin->host communication for
// this type of value.
struct SliceArpConfig {
    bool operator==(SliceArpConfig const&) const = default;
    u8 start_offset {}; // First slice index to play (0 = beginning)
    u8 loop_length {}; // Number of slices to loop (0 = all remaining after start_offset)
};

struct ArpeggiatorState {
    Array<Atomic<ArpStep>, k_arp_max_steps> steps {};

    // k_arp_max_steps means not playing/recording.
    Atomic<u8> current_step_for_gui {k_arp_max_steps};

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
            u32 note_index {}; // counts only steps that trigger a note (excludes tied/off steps)
            u32 frames_until_next_step {};
            u32 frames_into_current_step {};
            u32 frames_per_step {1};
            u32 gate_off_frame {};
            Array<Bitset<128>, 16> last_triggered_notes {};
            u8 current_step {};
            u8 last_random_note_index {LargestRepresentableValue<u8>()};
            bool one_shot_finished {};
        };

        // We have multiple playheads to support the polyrate behaviour. With this union we can conveniently
        // access the array or just the single playhead used in 'normal' mode whilst simultaneously being able
        // to loop through all playheads (normal or polyrate).
        union {
            Playhead playhead;
            Array<Playhead, k_octave_polyrate_num_playheads> playheads {};
        };

        bool any_notes_held {};
        Bitset<k_octave_polyrate_num_playheads> prev_active_octaves {};

        param_values::ArpMode type {};
        bool on {};
        param_values::ArpNoteOrder note_order {};
        param_values::ArpOctavePolyrate octave_polyrate {};
        param_values::ArpTriggerMode trigger_mode {};
        SyncedTimes user_rate {SyncedTimes::_1_8};
        SyncedTimes rate {SyncedTimes::_1_8}; // user_rate or resolved auto-rate
        bool auto_rate {};
        bool one_shot {};
        u32 length {8};
        f32 humanise {};

        // Used to detect a GUI-initiated recording false->true transition so audio can reset current_step
        // itself (avoiding a cross-thread write race on it).
        bool was_recording_last_block {};
    } audio;
};

using ArpSliceMapping = DynamicArrayBounded<u8, k_arp_max_steps>;

// Helper for the GUI to understand what needs to be shown/editable.
struct ArpGuiSnapshot {
    enum class Activation : u8 {
        UserDefined, // User's params control everything
        ForcedBySlicing, // Instrument has slice markers; arp runs in a locked slice mode
    };

    ArpStep StepAt(u32 i) const;

    Activation activation;
    bool on;
    param_values::ArpMode type;
    param_values::ArpNoteOrder note_order;

    u32 length;
    Array<ArpStep, k_arp_max_steps> user_steps;
    ArpSliceMapping slice_mapping;

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
};

ArpGuiSnapshot CreateArpGuiSnapshot(Parameters const& params,
                                    u8 layer_index,
                                    ArpeggiatorState const& arp,
                                    Span<sample_lib::Region::Slice const> slices);

struct SliceRange {
    u32 start_frame;
    Optional<u32> end_frame; // nullopt = play to end of sample
};

struct ArpNoteCommand : NoteEvent {
    Optional<SliceRange> slice;
};

using ArpNoteCommands = DynamicArrayBounded<ArpNoteCommand, k_max_note_events>;

bool ArpIsOn(bool on, sample_lib::Region const* sliced_region);

u32 ArpFramesPerStep(SyncedTimes rate, AudioProcessingContext const& context);

void ArpProcessBlock(ArpeggiatorState& arp,
                     AudioProcessingContext const& context,
                     sample_lib::Region const* sliced_region,
                     u64& random_seed,
                     u32 num_frames,
                     ArpNoteCommands& out_commands);

void ResetArpAudioPlayback(ArpeggiatorState& arp);

void ArpApplyState(ArpeggiatorState& arp, StateSnapshot const& state, u8 layer_index);

struct ArpInstrumentChangeArgs {
    sample_lib::Region const* old_sliced_region;
    sample_lib::Region const* new_sliced_region;
    AudioProcessingContext const& context;
};

void ArpHandleInstrumentChange(ArpeggiatorState& arp,
                               ArpInstrumentChangeArgs const& args,
                               ArpNoteCommands& out_commands);

enum class ArpNoteHandling : u8 {
    LayerHandlesNotes,
    ArpHandlesNotes,
};

struct ArpBlockChangesArgs {
    ChangedParams const& changed_params;
    u8 layer_index;

    // When provided, this activates the arpeggiator's sliced playback mode, playing the slices of the region
    // as the arpeggiator steps.
    sample_lib::Region const* sliced_region;

    AudioProcessingContext const& context;
    bool tempo_changed;
    Span<NoteEvent const> note_events;
};

ArpNoteHandling
ArpOnBlockChanges(ArpeggiatorState& arp, ArpBlockChangesArgs const& args, ArpNoteCommands& out_commands);
