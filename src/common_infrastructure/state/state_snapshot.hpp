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

// Fields that aren't part of the audible patch. Typically excluded from state equality checks.
struct StateExtras {
    bool operator==(StateExtras const& other) const = default;
    bool operator!=(StateExtras const& other) const = default;

    DynamicArrayBounded<char, k_max_instance_id_size> instance_id;
    Array<Bitset<128>, k_num_parameters> param_learned_ccs {};

    // For DAW state only. Preset files always get their name from the filename.
    DynamicArrayBounded<char, k_max_preset_name_size> display_name {};
    DynamicArrayBounded<char, k_max_preset_name_size> display_category {};

    // The hash of the preset when it was loaded from file - if any. This allows us to identify the preset
    // this state is based on and track if state has been modified since loading.
    u64 origin_preset_hash {}; // 0 == unknown. Filled automatically when decoding a preset.
    bool modified_from_origin_preset {};
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
    StateMetadata metadata {};
    Array<CurveMap::Points, k_num_layers> velocity_curve_points {};
    Array<HarmonyIntervalsBitset, k_num_layers> harmony_intervals {};
    Array<Array<ArpStep, k_arp_max_steps>, k_num_layers> arp_steps {};
    Array<SliceArpConfig, k_num_layers> slice_arp_configs {};
    MacroNames macro_names {};
    MacroDestinations macro_destinations {};
    InstanceConfig instance_config {};
    StateExtras extras {};
};

enum class StateSource : u8 {
    PresetFile,
    Daw,
    InMemorySource, // Such as from an undo system.
    GeneratedVariation, // A new snapshot generated programmatically.
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

struct MacroSection {
    bool operator==(MacroSection const&) const = default;
    u8 macro_index;
};

struct InstrumentSection {
    bool operator==(InstrumentSection const&) const = default;
    u8 layer_index;
};

struct ParamSection {
    bool operator==(ParamSection const&) const = default;
    ParamIndex param;
};

struct VelocityCurveSection {
    bool operator==(VelocityCurveSection const&) const = default;
    u8 layer_index;
};

struct EnvelopeSection {
    enum class Kind : u8 { Volume, Filter };
    bool operator==(EnvelopeSection const&) const = default;
    u8 layer_index;
    Kind kind;
};

struct LayerSection {
    bool operator==(LayerSection const&) const = default;
    u8 layer_index;
};

struct ModuleTabSection {
    bool operator==(ModuleTabSection const&) const = default;
    ParameterModule scope;
    ParameterModule subtab;
};

struct EqBandSection {
    bool operator==(EqBandSection const&) const = default;
    ParameterModule scope;
    u8 band;
};

enum class StateSnapshotSectionKind : u8 {
    Param,
    Macro,
    Instrument,
    VelocityCurve,
    Envelope,
    Layer,
    ModuleTab,
    EqBand,
};

using StateSnapshotSection =
    TaggedUnion<StateSnapshotSectionKind,
                TypeAndTag<ParamSection, StateSnapshotSectionKind::Param>,
                TypeAndTag<MacroSection, StateSnapshotSectionKind::Macro>,
                TypeAndTag<InstrumentSection, StateSnapshotSectionKind::Instrument>,
                TypeAndTag<VelocityCurveSection, StateSnapshotSectionKind::VelocityCurve>,
                TypeAndTag<EnvelopeSection, StateSnapshotSectionKind::Envelope>,
                TypeAndTag<LayerSection, StateSnapshotSectionKind::Layer>,
                TypeAndTag<ModuleTabSection, StateSnapshotSectionKind::ModuleTab>,
                TypeAndTag<EqBandSection, StateSnapshotSectionKind::EqBand>>;

StateSnapshot const& DefaultStateSnapshot();
