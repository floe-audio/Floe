// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "legacy_param_logic.hpp"

#include "state_snapshot.hpp"

// These tables remap legacy enum params to their modern equivalent. Index into them using the legacy value.

constexpr auto k_legacy_eq_type_to_current = ArrayT<param_values::EqType>({
    param_values::EqType::Peak,
    param_values::EqType::LowShelf,
    param_values::EqType::HighShelf,
});
static_assert(k_legacy_eq_type_to_current.size == ToInt(param_values::LegacyEqType::Count));

constexpr auto k_legacy_lfo_destination_to_current = ArrayT<param_values::LfoDestination>({
    param_values::LfoDestination::Volume,
    param_values::LfoDestination::Filter,
    param_values::LfoDestination::Pan,
    param_values::LfoDestination::Pitch,
});
static_assert(k_legacy_lfo_destination_to_current.size == ToInt(param_values::LegacyLfoDestination::Count));

// LFO shapes have had multiple revisions.
constexpr auto k_legacy_v1_lfo_shape_to_legacy_v2 = ArrayT<param_values::LegacyLfoShapeV2>({
    param_values::LegacyLfoShapeV2::Sine,
    param_values::LegacyLfoShapeV2::Triangle,
    param_values::LegacyLfoShapeV2::Sawtooth,
    param_values::LegacyLfoShapeV2::Square,
});
static_assert(k_legacy_v1_lfo_shape_to_legacy_v2.size == ToInt(param_values::LegacyLfoShapeV1::Count));

constexpr auto k_legacy_lfo_shape_v2_to_current = ArrayT<param_values::LfoShape>({
    param_values::LfoShape::Sine,
    param_values::LfoShape::Triangle,
    param_values::LfoShape::Sawtooth,
    param_values::LfoShape::Square,
    param_values::LfoShape::RandomSteps,
    param_values::LfoShape::RandomGlide,
});
static_assert(k_legacy_lfo_shape_v2_to_current.size == ToInt(param_values::LegacyLfoShapeV2::Count));

constexpr auto k_legacy_layer_filter_type_to_current = ArrayT<param_values::LayerFilterType>({
    param_values::LayerFilterType::Lowpass,
    param_values::LayerFilterType::BandpassResonant,
    param_values::LayerFilterType::Highpass,
    param_values::LayerFilterType::Bandpass,
    param_values::LayerFilterType::BandShelving,
    param_values::LayerFilterType::Notch,
    param_values::LayerFilterType::Lowpass,
    param_values::LayerFilterType::Peak,
});
static_assert(k_legacy_layer_filter_type_to_current.size ==
              ToInt(param_values::LegacyLayerFilterType::Count));

constexpr auto k_legacy_effect_filter_type_to_current = ArrayT<param_values::EffectFilterType>({
    param_values::EffectFilterType::LowPass24,
    param_values::EffectFilterType::HighPass24,
    param_values::EffectFilterType::BandPass,
    param_values::EffectFilterType::Notch,
    param_values::EffectFilterType::Peak,
    param_values::EffectFilterType::LowShelf,
    param_values::EffectFilterType::HighShelf,
});
static_assert(k_legacy_effect_filter_type_to_current.size ==
              ToInt(param_values::LegacyEffectFilterType::Count));

constexpr Optional<LayerParamIndex> SuccessorOfLegacyLayerParamIndex(LayerParamIndex legacy) {
    switch (legacy) {
        case LayerParamIndex::LegacyMonophonicBool: return LayerParamIndex::MonophonicMode;
        case LayerParamIndex::LegacyLfoShape: return LayerParamIndex::LegacyLfoShapeV2;
        case LayerParamIndex::LegacyLfoShapeV2: return LayerParamIndex::LfoShape;
        case LayerParamIndex::LegacyLfoDestination: return LayerParamIndex::LfoDestination;
        case LayerParamIndex::LegacyFilterType: return LayerParamIndex::FilterType;
        case LayerParamIndex::LegacyEqType1: return LayerParamIndex::EqType1;
        case LayerParamIndex::LegacyEqType2: return LayerParamIndex::EqType2;
        case LayerParamIndex::LegacyFilterResonance: return LayerParamIndex::FilterResonance;
        case LayerParamIndex::LegacyEqResonance1: return LayerParamIndex::EqResonance1;
        case LayerParamIndex::LegacyEqResonance2: return LayerParamIndex::EqResonance2;
        case LayerParamIndex::LegacyFilterCutoff: return LayerParamIndex::FilterCutoff;
        case LayerParamIndex::LegacyEqFreq1: return LayerParamIndex::EqFreq1;
        case LayerParamIndex::LegacyEqFreq2: return LayerParamIndex::EqFreq2;
        case LayerParamIndex::LegacyEqFreq3: return LayerParamIndex::EqFreq3;
        default: return k_nullopt;
    }
}

constexpr Optional<ParamIndex> SuccessorOfLegacyParamIndex(ParamIndex legacy) {
    ASSERT(!LayerParamIndexAndLayerFor(legacy), "use layer version of this function");
    switch (legacy) {
        case ParamIndex::LegacyFilterCutoff: return ParamIndex::FilterCutoff;
        case ParamIndex::LegacyFilterResonance: return ParamIndex::FilterResonance;
        case ParamIndex::LegacyFilterGain: return ParamIndex::FilterGain;
        case ParamIndex::LegacyFilterType: return ParamIndex::FilterType;
        case ParamIndex::LegacyChorusHighpass: return ParamIndex::ChorusHighpass;
        case ParamIndex::LegacyConvolutionReverbHighpass: return ParamIndex::ConvolutionReverbHighpass;
        default: return k_nullopt;
    }
}

// Build a lookup table for the predecessors (opposite directions to the SuccessorOf* functions).
consteval auto BuildLegacyPredecessorTable() {
    Array<Optional<ParamIndex>, k_num_parameters> table {};

    for (u32 layer = 0; layer < k_num_layers; ++layer) {
        for (usize i = 0; i < ToInt(LayerParamIndex::Count); ++i) {
            auto const legacy_layer = (LayerParamIndex)i;
            auto const succ_layer = SuccessorOfLegacyLayerParamIndex(legacy_layer);
            if (!succ_layer) continue;
            auto const legacy_pi = ParamIndexFromLayerParamIndex(layer, legacy_layer);
            auto const succ_pi = ParamIndexFromLayerParamIndex(layer, *succ_layer);
            if (table[ToInt(succ_pi)]) throw "Each successor must have a unique predecessor";
            table[ToInt(succ_pi)] = legacy_pi;
        }
    }
    for (u16 v = ToInt(ParamIndex::FirstNonLayerParam); v < ToInt(ParamIndex::CountHelper); ++v) {
        auto const legacy = (ParamIndex)v;
        auto const succ = SuccessorOfLegacyParamIndex(legacy);
        if (!succ) continue;
        if (table[ToInt(*succ)]) throw "Each successor must have a unique predecessor";
        table[ToInt(*succ)] = legacy;
    }
    return table;
}

constexpr auto k_legacy_predecessor_table = BuildLegacyPredecessorTable();

template <typename CurrentEnum, usize N>
static f32 EnumLookup(Array<CurrentEnum, N> const& mapping, f32 legacy_linear, f32 fallback) {
    auto const i = (usize)Trunc(legacy_linear);
    return (i < mapping.size) ? (f32)mapping[i] : fallback;
}

static f32 FrequencyRemap(ParamIndex legacy, ParamIndex modern, f32 legacy_linear) {
    auto const& legacy_desc = k_param_descriptors[ToInt(legacy)];
    auto const& modern_desc = k_param_descriptors[ToInt(modern)];

    ASSERT(legacy_desc.display_format == ParamDisplayFormat::Hz);
    ASSERT(modern_desc.display_format == ParamDisplayFormat::Hz);
    ASSERT(legacy_desc.flags.legacy);
    ASSERT(!modern_desc.flags.legacy);

    auto const hz = legacy_desc.ProjectValue(legacy_linear);
    return modern_desc.LineariseValue(hz, true).ValueOr(modern_desc.default_linear_value);
}

static f32 FilterGainRemap(ParamIndex legacy, ParamIndex modern, f32 legacy_linear) {
    // In the past the peak_gain was incorrectly used in the 2-passes of the filter, resulting in 2x the dB
    // change.
    auto const& legacy_desc = k_param_descriptors[ToInt(legacy)];
    auto const& modern_desc = k_param_descriptors[ToInt(modern)];

    ASSERT(legacy_desc.display_format == ParamDisplayFormat::VolumeDbRange);
    ASSERT(modern_desc.display_format == ParamDisplayFormat::VolumeDbRange);
    ASSERT(legacy_desc.flags.legacy);
    ASSERT(!modern_desc.flags.legacy);

    auto const new_db = legacy_desc.ProjectValue(legacy_linear) * 2;
    return modern_desc.LineariseValue(new_db, true).ValueOr(modern_desc.default_linear_value);
}

// Caller must ensure `legacy_layer` has a successor (per the structural switch).
static f32 RemapLegacyLayerValue(LayerParamIndex legacy_layer, u32 layer_num, f32 legacy_linear) {
    auto const successor_pi =
        ParamIndexFromLayerParamIndex(layer_num, *SuccessorOfLegacyLayerParamIndex(legacy_layer));
    auto const successor_default = k_param_descriptors[ToInt(successor_pi)].default_linear_value;

    switch (legacy_layer) {
        case LayerParamIndex::LegacyMonophonicBool:
            return legacy_linear >= 0.5f ? (f32)param_values::MonophonicMode::Retrigger
                                         : (f32)param_values::MonophonicMode::Off;
        case LayerParamIndex::LegacyLfoShape:
            return EnumLookup(k_legacy_v1_lfo_shape_to_legacy_v2, legacy_linear, successor_default);
        case LayerParamIndex::LegacyLfoShapeV2:
            return EnumLookup(k_legacy_lfo_shape_v2_to_current, legacy_linear, successor_default);
        case LayerParamIndex::LegacyLfoDestination:
            return EnumLookup(k_legacy_lfo_destination_to_current, legacy_linear, successor_default);
        case LayerParamIndex::LegacyFilterType:
            return EnumLookup(k_legacy_layer_filter_type_to_current, legacy_linear, successor_default);
        case LayerParamIndex::LegacyEqType1:
        case LayerParamIndex::LegacyEqType2:
            return EnumLookup(k_legacy_eq_type_to_current, legacy_linear, successor_default);
        case LayerParamIndex::LegacyFilterResonance: return Clamp(Pow(legacy_linear, 4.0f), 0.0f, 1.0f);
        case LayerParamIndex::LegacyEqResonance1:
        case LayerParamIndex::LegacyEqResonance2: return Clamp(Pow(legacy_linear, 2.5f), 0.0f, 1.0f);
        case LayerParamIndex::LegacyFilterCutoff:
        case LayerParamIndex::LegacyEqFreq1:
        case LayerParamIndex::LegacyEqFreq2:
        case LayerParamIndex::LegacyEqFreq3:
            return FrequencyRemap(ParamIndexFromLayerParamIndex(layer_num, legacy_layer),
                                  successor_pi,
                                  legacy_linear);
        default: PanicIfReached();
    }
    return 0;
}

static f32 RemapLegacyValue(ParamIndex legacy, f32 legacy_linear) {
    ASSERT(k_param_descriptors[ToInt(legacy)].flags.legacy);

    auto const successor_default =
        k_param_descriptors[ToInt(*SuccessorOfLegacyParamIndex(legacy))].default_linear_value;

    switch (legacy) {
        case ParamIndex::LegacyFilterCutoff:
            return FrequencyRemap(legacy, ParamIndex::FilterCutoff, legacy_linear);
        case ParamIndex::LegacyFilterResonance: return Clamp(Pow(legacy_linear, 2.5f), 0.0f, 1.0f);
        case ParamIndex::LegacyFilterGain:
            return FilterGainRemap(legacy, ParamIndex::FilterGain, legacy_linear);
        case ParamIndex::LegacyFilterType:
            return EnumLookup(k_legacy_effect_filter_type_to_current, legacy_linear, successor_default);
        case ParamIndex::LegacyChorusHighpass:
            return FrequencyRemap(legacy, ParamIndex::ChorusHighpass, legacy_linear);
        case ParamIndex::LegacyConvolutionReverbHighpass:
            return FrequencyRemap(legacy, ParamIndex::ConvolutionReverbHighpass, legacy_linear);
        default: PanicIfReached();
    }
    return 0;
}

Optional<SuccessorValue> SuccessorOfLegacyValue(ParamIndex legacy, f32 legacy_linear) {
    ASSERT(k_param_descriptors[ToInt(legacy)].flags.legacy);

    if (auto const lp = LayerParamIndexAndLayerFor(legacy)) {
        auto const succ_layer = TRY_OPT_OR(SuccessorOfLegacyLayerParamIndex(lp->param), return k_nullopt);
        return SuccessorValue {
            .successor_param = ParamIndexFromLayerParamIndex(lp->layer_num, succ_layer),
            .successor_linear = RemapLegacyLayerValue(lp->param, lp->layer_num, legacy_linear),
        };
    }

    auto const succ = TRY_OPT_OR(SuccessorOfLegacyParamIndex(legacy), return k_nullopt);
    return SuccessorValue {
        .successor_param = succ,
        .successor_linear = RemapLegacyValue(legacy, legacy_linear),
    };
}

Optional<SuccessorValue> TopmostSuccessorOfLegacyValue(ParamIndex legacy, f32 legacy_linear) {
    ASSERT(k_param_descriptors[ToInt(legacy)].flags.legacy);

    while (true) {
        auto const m = SuccessorOfLegacyValue(legacy, legacy_linear);
        if (!m) return k_nullopt;
        if (!k_param_descriptors[ToInt(m->successor_param)].flags.legacy) return m;
        legacy = m->successor_param;
        legacy_linear = m->successor_linear;
    }
}

static Optional<ParamIndex>
OldestOverridingAncestor(ParamIndex modern, StaticSpan<f32 const, k_num_parameters> linear_param_values) {
    Optional<ParamIndex> oldest_overriding;
    auto current = k_legacy_predecessor_table[ToInt(modern)];
    while (current) {
        auto const& desc = k_param_descriptors[ToInt(*current)];
        if (IsLegacyParamOverridingModern(desc, linear_param_values[ToInt(*current)]))
            oldest_overriding = current;
        current = k_legacy_predecessor_table[ToInt(*current)];
    }
    return oldest_overriding;
}

bool IsAnyLegacyOverriding(ParamIndex modern, StaticSpan<f32 const, k_num_parameters> linear_param_values) {
    return OldestOverridingAncestor(modern, linear_param_values).HasValue();
}

f32 ResolveLegacyAware(ParamIndex modern, StaticSpan<f32 const, k_num_parameters> linear_param_values) {
    if (auto const overriding = OldestOverridingAncestor(modern, linear_param_values))
        if (auto const m =
                TopmostSuccessorOfLegacyValue(*overriding, linear_param_values[ToInt(*overriding)]))
            return m->successor_linear;
    return linear_param_values[ToInt(modern)];
}

Optional<ParamIndex> LegacyPredecessor(ParamIndex param) { return k_legacy_predecessor_table[ToInt(param)]; }

void ModerniseMacroDestinations(StateSnapshot& state, ParamIndex legacy) {
    auto const successor = ({
        Optional<ParamIndex> s;
        if (auto const lp = LayerParamIndexAndLayerFor(legacy)) {
            if (auto const sl = SuccessorOfLegacyLayerParamIndex(lp->param))
                s = ParamIndexFromLayerParamIndex(lp->layer_num, *sl);
        } else {
            s = SuccessorOfLegacyParamIndex(legacy);
        }
        s;
    });
    if (!successor) return;
    for (auto& dests : state.macro_destinations)
        for (auto& dest : dests.items)
            if (dest.param_index && *dest.param_index == legacy) dest.param_index = *successor;
}

void ModerniseLegacyParamForDawState(StateSnapshot& state, ParamIndex legacy) {
    auto const& desc = k_param_descriptors[ToInt(legacy)];
    ASSERT(desc.flags.legacy);

    // For DAW, we retain the legacy parameter value - we can't detect if its safe to modernise.
    // Instead, we modify the _successor_ so that it matches the legacy's default value so that even if the
    // legacy parameter is automated by the DAW to equal it's default value (and therefore our DSP will detect
    // the legacy param as non-active and fallback to the sucessor value), it will sound identical and no pop
    // or jump will occur.
    if (auto const m = SuccessorOfLegacyValue(legacy, desc.default_linear_value))
        state.LinearParam(m->successor_param) = m->successor_linear;
}

void ModerniseLegacyParamForPresetState(StateSnapshot& state, ParamIndex legacy) {
    auto const& desc = k_param_descriptors[ToInt(legacy)];
    ASSERT(desc.flags.legacy);

    auto& legacy_val = state.LinearParam(legacy);

    // Set the successor param to match the legacy param.
    if (auto const m = SuccessorOfLegacyValue(legacy, legacy_val)) {
        state.LinearParam(m->successor_param) = m->successor_linear;
        ModerniseMacroDestinations(state, legacy);
    }

    // Clear the legacy param so DSP no longer considers it active.
    legacy_val = desc.default_linear_value;
}

CurveMap::Points ModerniseVelocityToCurve(param_values::VelocityMappingMode mode,
                                          f32 velocity_volume_strength) {
    CurveMap::Points points;
    switch (mode) {
        case param_values::VelocityMappingMode::None:
            // Flat at max volume.
            dyn::AssignAssumingAlreadyEmpty(points,
                                            Array {
                                                CurveMap::Point {0.0f, 1.0f, 0.0f},
                                                CurveMap::Point {1.0f, 1.0f, 0.0f},
                                            });
            break;
        case param_values::VelocityMappingMode::TopToBottom:
            // Linear.
            dyn::AssignAssumingAlreadyEmpty(points,
                                            Array {
                                                CurveMap::Point {0.0f, 0.0f, 0.0f},
                                                CurveMap::Point {1.0f, 1.0f, 0.0f},
                                            });
            break;
        case param_values::VelocityMappingMode::BottomToTop:
            // Inverse linear.
            dyn::AssignAssumingAlreadyEmpty(points,
                                            Array {
                                                CurveMap::Point {0.0f, 1.0f, 0.0f},
                                                CurveMap::Point {1.0f, 0.0f, 0.0f},
                                            });
            break;
        case param_values::VelocityMappingMode::TopToMiddle:
            dyn::AssignAssumingAlreadyEmpty(points,
                                            Array {
                                                CurveMap::Point {0.0f, 0.0f, 0.0f},
                                                CurveMap::Point {0.5f, 0.0f, 0.0f},
                                                CurveMap::Point {1.0f, 1.0f, 0.0f},
                                            });
            break;
        case param_values::VelocityMappingMode::MiddleOutwards:
            dyn::AssignAssumingAlreadyEmpty(points,
                                            Array {
                                                CurveMap::Point {0.0f, 0.0f, 0.0f},
                                                CurveMap::Point {0.5f, 1.0f, 0.0f},
                                                CurveMap::Point {1.0f, 0.0f, 0.0f},
                                            });
            break;
        case param_values::VelocityMappingMode::MiddleToBottom:
            dyn::AssignAssumingAlreadyEmpty(points,
                                            Array {
                                                CurveMap::Point {0.0f, 1.0f, 0.0f},
                                                CurveMap::Point {0.5f, 0.0f, 0.0f},
                                                CurveMap::Point {1.0f, 0.0f, 0.0f},
                                            });
            break;
        case param_values::VelocityMappingMode::Count: PanicIfReached();
    }

    // The stronger the velocity-volume strength, the more we bring down y values nearer to x=0
    // (low-velocity notes get quieter).
    for (auto& point : points)
        point.y = Max(point.y - (point.y * (1.0f - point.x) * velocity_volume_strength), 0.0f);

    return points;
}
