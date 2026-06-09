// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "state_snapshot.hpp"

template <typename DynArrayT>
requires dyn::DynArray<DynArrayT>
void AssignDiffDescription(DynArrayT& diff_desc,
                           StateSnapshot const& old_state,
                           StateSnapshot const& new_state) {
    dyn::Clear(diff_desc);

    if (old_state.ir_id != new_state.ir_id) {
        auto const old_lib =
            old_state.ir_id.HasValue()
                ? sample_lib::LookupLibraryIdString(old_state.ir_id.Value().library).ValueOr("?"_s)
                : "null"_s;
        auto const new_lib =
            new_state.ir_id.HasValue()
                ? sample_lib::LookupLibraryIdString(new_state.ir_id.Value().library).ValueOr("?"_s)
                : "null"_s;
        fmt::Append(diff_desc,
                    "IR changed, old: {}:{} vs new: {}:{}\n"_s,
                    old_lib,
                    old_state.ir_id.HasValue() ? old_state.ir_id.Value().ir_id.Items() : "null"_s,
                    new_lib,
                    new_state.ir_id.HasValue() ? new_state.ir_id.Value().ir_id.Items() : "null"_s);
    }

    for (auto layer_index : Range(k_num_layers)) {
        if (old_state.inst_ids[layer_index] != new_state.inst_ids[layer_index]) {
            fmt::Append(diff_desc,
                        "Layer {}: {} vs {}\n"_s,
                        layer_index,
                        PrintInstrumentId(old_state.inst_ids[layer_index]),
                        PrintInstrumentId(new_state.inst_ids[layer_index]));
        }
    }

    for (auto param_index : Range(k_num_parameters)) {
        if (old_state.param_values[param_index] != new_state.param_values[param_index]) {
            fmt::Append(diff_desc,
                        "Param {}: {} vs {}\n"_s,
                        k_param_descriptors[param_index].id_string,
                        old_state.param_values[param_index],
                        new_state.param_values[param_index]);
        }
    }

    if (old_state.fx_order != new_state.fx_order) fmt::Append(diff_desc, "FX order changed\n"_s);
    if (old_state.fx_visible != new_state.fx_visible) fmt::Append(diff_desc, "FX visibility changed\n"_s);

    for (auto cc : Range<usize>(1, 128)) {
        for (auto param_index : Range(k_num_parameters)) {
            if (old_state.extras.param_learned_ccs[param_index].Get(cc) !=
                new_state.extras.param_learned_ccs[param_index].Get(cc)) {
                fmt::Append(diff_desc,
                            "CC {}: Param {}: {} vs {}\n"_s,
                            cc,
                            k_param_descriptors[param_index].id_string,
                            old_state.extras.param_learned_ccs[param_index].Get(cc),
                            new_state.extras.param_learned_ccs[param_index].Get(cc));
            }
        }
    }

    if (old_state.metadata.author != new_state.metadata.author)
        fmt::Append(diff_desc,
                    "Author changed: {} vs {}\n"_s,
                    old_state.metadata.author,
                    new_state.metadata.author);

    if (old_state.metadata.description != new_state.metadata.description)
        fmt::Append(diff_desc,
                    "Description changed: {} vs {}\n"_s,
                    old_state.metadata.description,
                    new_state.metadata.description);

    if (old_state.metadata.tags != new_state.metadata.tags) {
        fmt::Append(diff_desc, "Tags changed:\n"_s);
        old_state.metadata.tags.ForEachSetBit(
            [&](usize bit) { fmt::Append(diff_desc, "  - {}\n"_s, GetTagInfo((TagType)bit).name); });
        new_state.metadata.tags.ForEachSetBit(
            [&](usize bit) { fmt::Append(diff_desc, "  + {}\n"_s, GetTagInfo((TagType)bit).name); });
    }

    if (old_state.extras.instance_id != new_state.extras.instance_id)
        fmt::Append(diff_desc,
                    "instance ID changed: {} vs {}\n"_s,
                    old_state.extras.instance_id,
                    new_state.extras.instance_id);

    if (old_state.extras.display_name != new_state.extras.display_name)
        fmt::Append(diff_desc,
                    "display name changed: {} vs {}\n"_s,
                    old_state.extras.display_name,
                    new_state.extras.display_name);

    if (old_state.extras.display_category != new_state.extras.display_category)
        fmt::Append(diff_desc,
                    "display category changed: {} vs {}\n"_s,
                    old_state.extras.display_category,
                    new_state.extras.display_category);

    if (old_state.extras.origin_preset_hash != new_state.extras.origin_preset_hash)
        fmt::Append(diff_desc,
                    "origin preset hash changed: {} vs {}\n"_s,
                    old_state.extras.origin_preset_hash,
                    new_state.extras.origin_preset_hash);

    if (old_state.extras.modified_from_origin_preset != new_state.extras.modified_from_origin_preset)
        fmt::Append(diff_desc,
                    "modified from origin preset changed: {} vs {}\n"_s,
                    old_state.extras.modified_from_origin_preset,
                    new_state.extras.modified_from_origin_preset);

    for (auto layer_index : Range(k_num_layers))
        if (old_state.velocity_curve_points[layer_index] != new_state.velocity_curve_points[layer_index])
            fmt::Append(diff_desc, "Velocity curve points changed for layer {}\n"_s, layer_index);

    for (auto layer_index : Range(k_num_layers))
        if (old_state.harmony_intervals[layer_index] != new_state.harmony_intervals[layer_index])
            fmt::Append(diff_desc, "Harmony intervals changed for layer {}\n"_s, layer_index);

    for (auto layer_index : Range(k_num_layers))
        if (old_state.arp_steps[layer_index] != new_state.arp_steps[layer_index])
            fmt::Append(diff_desc, "Arp steps changed for layer {}\n"_s, layer_index);

    for (auto layer_index : Range(k_num_layers))
        if (old_state.slice_arp_configs[layer_index] != new_state.slice_arp_configs[layer_index])
            fmt::Append(diff_desc, "Slice arp config changed for layer {}\n"_s, layer_index);

    for (auto macro_index : Range(k_num_macros))
        if (old_state.macro_names[macro_index] != new_state.macro_names[macro_index])
            fmt::Append(diff_desc,
                        "Macro {} name changed: {} vs {}\n"_s,
                        macro_index,
                        old_state.macro_names[macro_index],
                        new_state.macro_names[macro_index]);

    for (auto macro_index : Range(k_num_macros))
        if (old_state.macro_destinations[macro_index] != new_state.macro_destinations[macro_index])
            fmt::Append(diff_desc, "Macro {} destinations changed\n"_s, macro_index);

    if (old_state.extras.instance_config != new_state.extras.instance_config)
        dyn::AppendSpan(diff_desc, "Instance config changed\n"_s);
}

template void AssignDiffDescription<DynamicArrayBounded<char, 0>>(DynamicArrayBounded<char, 0>&,
                                                                  StateSnapshot const&,
                                                                  StateSnapshot const&);
template void AssignDiffDescription<DynamicArrayBounded<char, 200>>(DynamicArrayBounded<char, 200>&,
                                                                    StateSnapshot const&,
                                                                    StateSnapshot const&);
template void AssignDiffDescription<DynamicArrayBounded<char, Kb(4)>>(DynamicArrayBounded<char, Kb(4)>&,
                                                                      StateSnapshot const&,
                                                                      StateSnapshot const&);
template void
AssignDiffDescription<DynamicArray<char>>(DynamicArray<char>&, StateSnapshot const&, StateSnapshot const&);

StateSnapshot const& DefaultStateSnapshot() {
    static StateSnapshot const state = ({
        StateSnapshot s {};
        for (auto const i : Range(k_num_parameters))
            s.param_values[i] = k_param_descriptors[i].default_linear_value;
        for (auto& velo : s.velocity_curve_points)
            velo = k_default_velocity_curve_points;
        s.macro_names = DefaultMacroNames();
        for (auto const i : Range<u8>(s.fx_order.size))
            s.fx_order[i] = (EffectType)i;
        s.extras.display_name = "Blank Preset"_s;
        s.extras.display_category = {};
        s;
    });
    return state;
}
