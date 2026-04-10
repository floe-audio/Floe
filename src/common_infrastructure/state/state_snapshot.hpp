// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/effect_descriptors.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/macros.hpp"
#include "common_infrastructure/tags.hpp"

#include "instrument.hpp"
#include "plugin/processing_utils/curve_map.hpp"

struct StateMetadata {
    bool operator==(StateMetadata const& other) const = default;
    bool operator!=(StateMetadata const& other) const = default;
    TagsBitset tags {};
    DynamicArrayBounded<char, k_max_preset_author_size> author {};
    DynamicArrayBounded<char, k_max_preset_description_size> description {};
};

struct StateMetadataRef {
    TagsBitset tags {};
    String author {};
    String description {};
};

struct InstanceConfig {
    bool operator==(InstanceConfig const&) const = default;
    bool operator!=(InstanceConfig const&) const = default;

    bool reset_on_transport {false};
    Optional<u7> reset_keyswitch {}; // MIDI note that triggers a reset, or nullopt for disabled
    u8 seed {0}; // 0-99, determines what the master PRNG resets to
};

struct StateSnapshot {
    f32& LinearParam(ParamIndex index) { return param_values[ToInt(index)]; }
    f32 LinearParam(ParamIndex index) const { return param_values[ToInt(index)]; }

    bool operator==(StateSnapshot const& other) const = default;
    bool operator!=(StateSnapshot const& other) const = default;

    Optional<sample_lib::IrId> ir_id {};
    InitialisedArray<InstrumentId, k_num_layers> inst_ids {InstrumentType::None};
    Array<f32, k_num_parameters> param_values {};
    Array<EffectType, k_num_effect_types> fx_order {};
    Bitset<k_num_effect_types> fx_visible {};
    Array<Bitset<128>, k_num_parameters> param_learned_ccs {};
    StateMetadata metadata {};
    DynamicArrayBounded<char, k_max_instance_id_size> instance_id;
    Array<CurveMap::Points, k_num_layers> velocity_curve_points {};
    Array<HarmonyIntervalsBitset, k_num_layers> harmony_intervals {};
    MacroNames macro_names {};
    MacroDestinations macro_destinations {};
    InstanceConfig instance_config {};
};

enum class StateSource { PresetFile, Daw };

struct StateSnapshotName {
    StateSnapshotName Clone(Allocator& a, CloneType clone_type = CloneType::Shallow) const {
        auto _ = clone_type;
        return {
            .name_or_path = name_or_path.Clone(a, CloneType::Shallow),
        };
    }

    Optional<String> Path() const {
        if (path::IsAbsolute(name_or_path)) return name_or_path;
        return k_nullopt;
    }
    String Name() const { return path::FilenameWithoutExtension(name_or_path); }

    String name_or_path;
};

struct StateSnapshotWithName {
    StateSnapshot state;
    StateSnapshotName name;
};

[[maybe_unused]] static auto PrintInstrumentId(InstrumentId id) {
    DynamicArrayBounded<char, 100> result {};
    switch (id.tag) {
        case InstrumentType::None: fmt::Append(result, "None"_s); break;
        case InstrumentType::WaveformSynth:
            fmt::Append(result, "WaveformSynth: {}"_s, id.Get<WaveformType>());
            break;
        case InstrumentType::Sampler: {
            auto const& inst = id.Get<sample_lib::InstrumentId>();
            auto const lib_name = sample_lib::LookupLibraryIdString(inst.library).ValueOr("?"_s);
            fmt::Append(result, "Sampler: {}/{}"_s, lib_name, inst.inst_id);
            break;
        }
    }
    return result;
}

PUBLIC void AssignDiffDescription(dyn::DynArray auto& diff_desc,
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
                        k_param_descriptors[param_index].name,
                        old_state.param_values[param_index],
                        new_state.param_values[param_index]);
        }
    }

    if (old_state.fx_order != new_state.fx_order) fmt::Append(diff_desc, "FX order changed\n"_s);
    if (old_state.fx_visible != new_state.fx_visible) fmt::Append(diff_desc, "FX visibility changed\n"_s);

    for (auto cc : Range<usize>(1, 128)) {
        for (auto param_index : Range(k_num_parameters)) {
            if (old_state.param_learned_ccs[param_index].Get(cc) !=
                new_state.param_learned_ccs[param_index].Get(cc)) {
                fmt::Append(diff_desc,
                            "CC {}: Param {}: {} vs {}\n"_s,
                            cc,
                            k_param_descriptors[param_index].name,
                            old_state.param_learned_ccs[param_index].Get(cc),
                            new_state.param_learned_ccs[param_index].Get(cc));
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

    if (old_state.instance_id != new_state.instance_id) dyn::AppendSpan(diff_desc, "instance ID changes");

    for (auto layer_index : Range(k_num_layers))
        if (old_state.velocity_curve_points[layer_index] != new_state.velocity_curve_points[layer_index])
            fmt::Append(diff_desc, "Velocity curve points changed for layer {}\n"_s, layer_index);

    for (auto layer_index : Range(k_num_layers))
        if (old_state.harmony_intervals[layer_index] != new_state.harmony_intervals[layer_index])
            fmt::Append(diff_desc, "Harmony intervals changed for layer {}\n"_s, layer_index);

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

    if (old_state.instance_config != new_state.instance_config)
        dyn::AppendSpan(diff_desc, "Instance config changed\n"_s);
}
