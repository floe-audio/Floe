// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/instrument.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "processor/layer_processor.hpp"
#include "processor/param.hpp"
#include "sample_lib_server/sample_library_server.hpp"

enum class ArpBehaviourId : u8 {
    UserDefined, // User's params control everything
    ForcedBySlicing, // Instrument has slice markers; arp runs in a locked slice mode
};

struct ArpBehaviour {
    // Which aspects of the arp the user can edit. When false, the GUI should also hide or dim the
    // corresponding control.
    struct EditFlags {
        bool mode; // The Off/Fixed/Played menu itself
        bool length; // Number of steps
        bool note_order; // Chord / Up / Down / UpDown
        bool auto_rate_visible; // Whether the auto-rate toggle is meaningful (only in slice mode)
        bool step_velocity;
        bool step_gate;
        bool step_on;
        bool step_tie; // Also controls tie row visibility
        bool step_note; // Also controls note/interval row visibility
    };

    struct Value {
        ArpBehaviourId id;
        bool on;
        param_values::ArpMode type;
        param_values::ArpNoteOrder note_order;

        // Iterate steps via StepAt(i) for 0 <= i < length. In slice mode the step's tie is derived from
        // step_to_slice_index, and interval/note are forced to 0. Velocity/gate/on always come from
        // user_steps.
        u32 length;
        // Snapshot of the user's steps (loaded from the atomic step array). The snapshot keeps reads
        // consistent during a single GUI frame even if the audio thread is recording into the steps.
        Array<ArpStep, k_arp_max_steps> user_steps;
        // Slice index per step. Only the first `slice_index_length` entries are valid; zero length
        // means non-slice mode (StepAt then leaves tie/interval/note from user_steps untouched).
        Array<u8, k_arp_max_steps> step_to_slice_index;
        u32 slice_index_length;

        EditFlags edit;
        String short_name;
        String description;

        ArpStep StepAt(u32 i) const {
            ArpStep s = user_steps[i];
            if (slice_index_length) {
                s.tie = (i > 0) && step_to_slice_index[i] == step_to_slice_index[i - 1];
                s.interval = 0;
                s.note = 0;
            }
            return s;
        }
    };
    Value value;
    String reason; // Human-readable explanation when overriding user intent
    bool is_desired; // Whether value matches the user's params
};

// Slice-mode step-to-slice mapping is derived from the instrument here on the GUI thread. The audio
// thread keeps its own equivalent cache in arp.audio.step_to_slice_index for playback.
struct ArpSliceMapping {
    u32 length {};
    Array<u8, k_arp_max_steps> step_to_slice_index {};
};

PUBLIC ArpSliceMapping ComputeArpSliceMapping(Span<sample_lib::Region::Slice const> slices,
                                              u8 start_offset,
                                              u8 loop_length) {
    ArpSliceMapping mapping {};
    if (!slices.size) return mapping;

    auto const num_slices = (u32)slices.size;
    u32 const effective_length = loop_length == 0 ? num_slices : Min((u32)loop_length, num_slices);

    u32 step_idx = 0;
    for (u32 i = 0; i < effective_length; i++) {
        u32 const slice_i = ((u32)start_offset + i) % num_slices;
        for (u32 j = 0; j < slices[slice_i].length_proportion; j++) {
            if (step_idx >= k_arp_max_steps) break;
            mapping.step_to_slice_index[step_idx] = (u8)slice_i;
            step_idx++;
        }
    }
    mapping.length = step_idx;
    return mapping;
}

PUBLIC ArpBehaviour ActualArpBehaviour(Parameters const& params,
                                       u8 layer_index,
                                       ArpeggiatorState const& arp,
                                       Instrument const& inst) {
    // Detect slice mode from the loaded instrument (main thread view).
    Span<sample_lib::Region::Slice const> slices {};
    if (auto s = inst.TryGetFromTag<InstrumentType::Sampler>()) {
        if (*s) {
            if ((*s)->instrument.category == sample_lib::SamplerCategory::Sliced)
                slices = (*s)->instrument.regions[0].slices;
        }
    }

    auto const param_mode = params.IntValue<param_values::ArpMode>(layer_index, LayerParamIndex::ArpMode);
    auto const param_note_order =
        params.IntValue<param_values::ArpNoteOrder>(layer_index, LayerParamIndex::ArpNoteOrder);
    auto const param_length =
        Clamp(params.IntValue<u32>(layer_index, LayerParamIndex::ArpLength), 1u, (u32)k_arp_max_steps);

    // Snapshot the user's steps once for the frame.
    Array<ArpStep, k_arp_max_steps> snapshot {};
    for (u32 i = 0; i < k_arp_max_steps; ++i)
        snapshot[i] = arp.steps[i].Load(LoadMemoryOrder::Relaxed);

    if (slices.size) {
        auto const start_offset = arp.slice_start_offset.Load(LoadMemoryOrder::Relaxed);
        auto const loop_length = arp.slice_loop_length.Load(LoadMemoryOrder::Relaxed);
        auto mapping = ComputeArpSliceMapping(slices, start_offset, loop_length);
        return {
            .value =
                {
                    .id = ArpBehaviourId::ForcedBySlicing,
                    .on = true,
                    .type = param_values::ArpMode::Played,
                    .note_order = param_note_order, // User-selectable even in slice mode
                    .length = mapping.length,
                    .user_steps = snapshot,
                    .step_to_slice_index = mapping.step_to_slice_index,
                    .slice_index_length = mapping.length,
                    .edit =
                        {
                            .mode = false,
                            .length = false,
                            .note_order = true,
                            .auto_rate_visible = true,
                            .step_velocity = true,
                            .step_gate = true,
                            .step_on = true,
                            .step_tie = false,
                            .step_note = false,
                        },
                    .short_name = "Sliced"_s,
                    .description = "Arpeggiator is driven by slice markers in this instrument."_s,
                },
            .reason = "This instrument contains slice markers, which drive the arpeggiator."_s,
            .is_desired = false,
        };
    }

    // Normal user-driven arp.
    bool const param_on = param_mode != param_values::ArpMode::Off;
    bool const all_editable = param_on;
    return {
        .value =
            {
                .id = ArpBehaviourId::UserDefined,
                .on = param_on,
                .type = param_mode,
                .note_order = param_note_order,
                .length = param_length,
                .user_steps = snapshot,
                .step_to_slice_index = {},
                .slice_index_length = 0,
                .edit =
                    {
                        .mode = true, // Mode menu itself is always editable
                        .length = all_editable,
                        .note_order = all_editable,
                        .auto_rate_visible = false, // Not useful without slices
                        .step_velocity = all_editable,
                        .step_gate = all_editable,
                        .step_on = all_editable,
                        .step_tie = all_editable,
                        .step_note = all_editable,
                    },
                .short_name = "User"_s,
                .description = {},
            },
        .reason = {},
        .is_desired = true,
    };
}
