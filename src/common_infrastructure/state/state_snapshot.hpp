// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/effect_descriptors.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/macros.hpp"
#include "common_infrastructure/tags.hpp"

#include "instrument.hpp"
#include "plugin/processing_utils/arpeggiator.hpp"
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

    bool reset_on_transport {true};
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
    Array<Array<ArpStep, k_arp_max_steps>, k_num_layers> arp_steps {};
    Array<SliceArpConfig, k_num_layers> slice_arp_configs {};
    MacroNames macro_names {};
    MacroDestinations macro_destinations {};
    InstanceConfig instance_config {};
};

enum class StateSource : u8 { PresetFile, Daw };

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

template <typename DynArrayT>
requires dyn::DynArray<DynArrayT>
void AssignDiffDescription(DynArrayT& diff_desc,
                           StateSnapshot const& old_state,
                           StateSnapshot const& new_state);

struct StateSnapshotSelector {
    ParamModules modules {};
    u8 macro_index {0};
};

struct StateSnapshotView {
    StateSnapshot const& snapshot;
    StateSnapshotSelector selector;
};

StateSnapshot OverlaySection(StateSnapshotView target, StateSnapshotView source);

StateSnapshot const& DefaultStateSnapshot();
