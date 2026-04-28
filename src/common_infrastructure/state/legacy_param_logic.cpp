// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "legacy_param_logic.hpp"

#include "state_snapshot.hpp"

namespace {

template <typename CurrentEnum, usize N>
f32 EnumLookup(Array<CurrentEnum, N> const& mapping, f32 legacy_linear, f32 fallback) {
    auto const i = (usize)Trunc(legacy_linear);
    return (i < mapping.size) ? (f32)mapping[i] : fallback;
}

f32 FrequencyRemap(ParamIndex legacy, ParamIndex modern, f32 legacy_linear) {
    auto const& legacy_desc = k_param_descriptors[ToInt(legacy)];
    auto const& modern_desc = k_param_descriptors[ToInt(modern)];
    auto const hz = legacy_desc.ProjectValue(legacy_linear);
    return modern_desc.LineariseValue(hz, true).ValueOr(modern_desc.default_linear_value);
}

f32 FilterGainRemap(ParamIndex legacy, ParamIndex modern, f32 legacy_linear) {
    // In the past the peak_gain was incorrectly used in the 2-passes of the filter, resulting in 2x the dB
    // change.
    auto const& legacy_desc = k_param_descriptors[ToInt(legacy)];
    auto const& modern_desc = k_param_descriptors[ToInt(modern)];
    auto const new_db = legacy_desc.ProjectValue(legacy_linear) * 2;
    return modern_desc.LineariseValue(new_db, true).ValueOr(modern_desc.default_linear_value);
}

Optional<ModernisedValue>
ModerniseLayerParam(LayerParamIndex legacy_layer, u32 layer_num, f32 legacy_linear) {
    auto const make = [&](LayerParamIndex modern_layer, f32 modern_linear) {
        return ModernisedValue {
            .modern_param = ParamIndexFromLayerParamIndex(layer_num, modern_layer),
            .modern_linear = modern_linear,
        };
    };
    auto const make_via_freq = [&](LayerParamIndex modern_layer) {
        auto const modern_pi = ParamIndexFromLayerParamIndex(layer_num, modern_layer);
        auto const legacy_pi = ParamIndexFromLayerParamIndex(layer_num, legacy_layer);
        return ModernisedValue {.modern_param = modern_pi,
                                .modern_linear = FrequencyRemap(legacy_pi, modern_pi, legacy_linear)};
    };

    switch (legacy_layer) {
        case LayerParamIndex::LegacyMonophonicBool:
            return make(LayerParamIndex::MonophonicMode,
                        legacy_linear >= 0.5f ? (f32)param_values::MonophonicMode::Retrigger
                                              : (f32)param_values::MonophonicMode::Off);
        case LayerParamIndex::LegacyLfoShape: {
            auto const modern_pi = ParamIndexFromLayerParamIndex(layer_num, LayerParamIndex::LfoShape);
            auto const modern_default = k_param_descriptors[ToInt(modern_pi)].default_linear_value;
            return make(LayerParamIndex::LfoShape,
                        EnumLookup(k_legacy_lfo_shape_to_current, legacy_linear, modern_default));
        }
        case LayerParamIndex::LegacyLfoShapeV2: {
            auto const modern_pi = ParamIndexFromLayerParamIndex(layer_num, LayerParamIndex::LfoShape);
            auto const modern_default = k_param_descriptors[ToInt(modern_pi)].default_linear_value;
            return make(LayerParamIndex::LfoShape,
                        EnumLookup(k_legacy_lfo_shape_v2_to_current, legacy_linear, modern_default));
        }
        case LayerParamIndex::LegacyLfoDestination: {
            auto const modern_pi = ParamIndexFromLayerParamIndex(layer_num, LayerParamIndex::LfoDestination);
            auto const modern_default = k_param_descriptors[ToInt(modern_pi)].default_linear_value;
            return make(LayerParamIndex::LfoDestination,
                        EnumLookup(k_legacy_lfo_destination_to_current, legacy_linear, modern_default));
        }
        case LayerParamIndex::LegacyFilterType: {
            auto const modern_pi = ParamIndexFromLayerParamIndex(layer_num, LayerParamIndex::FilterType);
            auto const modern_default = k_param_descriptors[ToInt(modern_pi)].default_linear_value;
            return make(LayerParamIndex::FilterType,
                        EnumLookup(k_legacy_layer_filter_type_to_current, legacy_linear, modern_default));
        }
        case LayerParamIndex::LegacyEqType1: {
            auto const modern_pi = ParamIndexFromLayerParamIndex(layer_num, LayerParamIndex::EqType1);
            auto const modern_default = k_param_descriptors[ToInt(modern_pi)].default_linear_value;
            return make(LayerParamIndex::EqType1,
                        EnumLookup(k_legacy_eq_type_to_current, legacy_linear, modern_default));
        }
        case LayerParamIndex::LegacyEqType2: {
            auto const modern_pi = ParamIndexFromLayerParamIndex(layer_num, LayerParamIndex::EqType2);
            auto const modern_default = k_param_descriptors[ToInt(modern_pi)].default_linear_value;
            return make(LayerParamIndex::EqType2,
                        EnumLookup(k_legacy_eq_type_to_current, legacy_linear, modern_default));
        }
        case LayerParamIndex::LegacyFilterResonance:
            return make(LayerParamIndex::FilterResonance, Clamp(Pow(legacy_linear, 4.0f), 0.0f, 1.0f));
        case LayerParamIndex::LegacyEqResonance1:
            return make(LayerParamIndex::EqResonance1, Clamp(Pow(legacy_linear, 2.5f), 0.0f, 1.0f));
        case LayerParamIndex::LegacyEqResonance2:
            return make(LayerParamIndex::EqResonance2, Clamp(Pow(legacy_linear, 2.5f), 0.0f, 1.0f));
        case LayerParamIndex::LegacyFilterCutoff: return make_via_freq(LayerParamIndex::FilterCutoff);
        case LayerParamIndex::LegacyEqFreq1: return make_via_freq(LayerParamIndex::EqFreq1);
        case LayerParamIndex::LegacyEqFreq2: return make_via_freq(LayerParamIndex::EqFreq2);
        case LayerParamIndex::LegacyEqFreq3: return make_via_freq(LayerParamIndex::EqFreq3);
        // LegacyVelocityMapping folds into a velocity curve, not a single modern parameter — no
        // 1:1 modernisation possible.
        case LayerParamIndex::LegacyVelocityMapping: return k_nullopt;
        default: return k_nullopt;
    }
}

Optional<ModernisedValue> ModerniseGlobalParam(ParamIndex legacy, f32 legacy_linear) {
    auto const make = [&](ParamIndex modern, f32 modern_linear) {
        return ModernisedValue {.modern_param = modern, .modern_linear = modern_linear};
    };
    switch (legacy) {
        case ParamIndex::LegacyFilterCutoff:
            return make(ParamIndex::FilterCutoff,
                        FrequencyRemap(legacy, ParamIndex::FilterCutoff, legacy_linear));
        case ParamIndex::LegacyFilterResonance:
            return make(ParamIndex::FilterResonance, Clamp(Pow(legacy_linear, 2.5f), 0.0f, 1.0f));
        case ParamIndex::LegacyFilterGain:
            return make(ParamIndex::FilterGain,
                        FilterGainRemap(legacy, ParamIndex::FilterGain, legacy_linear));
        case ParamIndex::LegacyFilterType: {
            auto const modern_default =
                k_param_descriptors[ToInt(ParamIndex::FilterType)].default_linear_value;
            return make(ParamIndex::FilterType,
                        EnumLookup(k_legacy_effect_filter_type_to_current, legacy_linear, modern_default));
        }
        case ParamIndex::LegacyChorusHighpass:
            return make(ParamIndex::ChorusHighpass,
                        FrequencyRemap(legacy, ParamIndex::ChorusHighpass, legacy_linear));
        case ParamIndex::LegacyConvolutionReverbHighpass:
            return make(ParamIndex::ConvolutionReverbHighpass,
                        FrequencyRemap(legacy, ParamIndex::ConvolutionReverbHighpass, legacy_linear));
        default: return k_nullopt;
    }
}

} // namespace

Optional<ModernisedValue> ModerniseLegacyValue(ParamIndex legacy, f32 legacy_linear) {
    ASSERT(k_param_descriptors[ToInt(legacy)].flags.legacy);

    if (auto const lp = LayerParamIndexAndLayerFor(legacy))
        return ModerniseLayerParam(lp->param, lp->layer_num, legacy_linear);

    return ModerniseGlobalParam(legacy, legacy_linear);
}

Optional<ParamIndex> ModernCounterpartOf(ParamIndex legacy) {
    auto const m = ModerniseLegacyValue(legacy, 0.0f);
    if (!m) return k_nullopt;
    return m->modern_param;
}

Optional<ParamIndex> LegacyCounterpartOf(ParamIndex modern) {
    if (auto const lp = LayerParamIndexAndLayerFor(modern)) {
        Optional<LayerParamIndex> legacy_layer_param;
        switch (lp->param) {
            case LayerParamIndex::FilterCutoff:
                legacy_layer_param = LayerParamIndex::LegacyFilterCutoff;
                break;
            case LayerParamIndex::FilterResonance:
                legacy_layer_param = LayerParamIndex::LegacyFilterResonance;
                break;
            case LayerParamIndex::EqFreq1: legacy_layer_param = LayerParamIndex::LegacyEqFreq1; break;
            case LayerParamIndex::EqFreq2: legacy_layer_param = LayerParamIndex::LegacyEqFreq2; break;
            case LayerParamIndex::EqFreq3: legacy_layer_param = LayerParamIndex::LegacyEqFreq3; break;
            case LayerParamIndex::EqResonance1:
                legacy_layer_param = LayerParamIndex::LegacyEqResonance1;
                break;
            case LayerParamIndex::EqResonance2:
                legacy_layer_param = LayerParamIndex::LegacyEqResonance2;
                break;
            case LayerParamIndex::EqType1: legacy_layer_param = LayerParamIndex::LegacyEqType1; break;
            case LayerParamIndex::EqType2: legacy_layer_param = LayerParamIndex::LegacyEqType2; break;
            case LayerParamIndex::FilterType: legacy_layer_param = LayerParamIndex::LegacyFilterType; break;
            case LayerParamIndex::LfoShape: legacy_layer_param = LayerParamIndex::LegacyLfoShape; break;
            case LayerParamIndex::LfoDestination:
                legacy_layer_param = LayerParamIndex::LegacyLfoDestination;
                break;
            case LayerParamIndex::MonophonicMode:
                legacy_layer_param = LayerParamIndex::LegacyMonophonicBool;
                break;
            default: return k_nullopt;
        }
        return ParamIndexFromLayerParamIndex(lp->layer_num, *legacy_layer_param);
    }

    switch (modern) {
        case ParamIndex::FilterCutoff: return ParamIndex::LegacyFilterCutoff;
        case ParamIndex::FilterResonance: return ParamIndex::LegacyFilterResonance;
        case ParamIndex::FilterGain: return ParamIndex::LegacyFilterGain;
        case ParamIndex::FilterType: return ParamIndex::LegacyFilterType;
        case ParamIndex::ChorusHighpass: return ParamIndex::LegacyChorusHighpass;
        case ParamIndex::ConvolutionReverbHighpass: return ParamIndex::LegacyConvolutionReverbHighpass;
        default: return k_nullopt;
    }
}

void ModerniseMacroDestinations(StateSnapshot& state, ParamIndex legacy, ParamIndex modern) {
    for (auto& dests : state.macro_destinations)
        for (auto& dest : dests.items)
            if (dest.param_index && *dest.param_index == legacy) dest.param_index = modern;
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
