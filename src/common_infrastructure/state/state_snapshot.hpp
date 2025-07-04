// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/effect_descriptors.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "instrument.hpp"
#include "plugin/processing_utils/curve_map.hpp"

struct StateMetadata {
    bool operator==(StateMetadata const& other) const = default;
    bool operator!=(StateMetadata const& other) const = default;
    DynamicArrayBounded<DynamicArrayBounded<char, k_max_tag_size>, k_max_num_tags> tags {};
    DynamicArrayBounded<char, k_max_preset_author_size> author {};
    DynamicArrayBounded<char, k_max_preset_description_size> description {};
};

struct StateMetadataRef {
    Set<String> tags {};
    String author {};
    String description {};
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
    Array<Bitset<128>, k_num_parameters> param_learned_ccs {};
    StateMetadata metadata {};
    DynamicArrayBounded<char, k_max_instance_id_size> instance_id;
    Array<CurveMap::Points, k_num_layers> velocity_curve_points {};
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
        case InstrumentType::Sampler:
            fmt::Append(result,
                        "Sampler: {}/{}/{}"_s,
                        id.Get<sample_lib::InstrumentId>().library.author,
                        id.Get<sample_lib::InstrumentId>().library.name,
                        id.Get<sample_lib::InstrumentId>().inst_name);
            break;
    }
    return result;
}

PUBLIC void AssignDiffDescription(dyn::DynArray auto& diff_desc,
                                  StateSnapshot const& old_state,
                                  StateSnapshot const& new_state) {
    dyn::Clear(diff_desc);

    if (old_state.ir_id != new_state.ir_id) {
        fmt::Append(diff_desc,
                    "IR changed, old: {}:{} vs new: {}:{}\n"_s,
                    old_state.ir_id.HasValue() ? old_state.ir_id.Value().library.name.Items() : "null"_s,
                    old_state.ir_id.HasValue() ? old_state.ir_id.Value().ir_name.Items() : "null"_s,
                    new_state.ir_id.HasValue() ? new_state.ir_id.Value().library.name.Items() : "null"_s,
                    new_state.ir_id.HasValue() ? new_state.ir_id.Value().ir_name.Items() : "null"_s);
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
        for (auto const& tag : old_state.metadata.tags)
            fmt::Append(diff_desc, "  - {}\n"_s, tag);
        for (auto const& tag : new_state.metadata.tags)
            fmt::Append(diff_desc, "  + {}\n"_s, tag);
    }
}
