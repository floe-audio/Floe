// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/legacy_param_logic.hpp"

// It's sometimes very useful to pass around a parameter value with its descriptor.
struct DescribedParamValue {
    f32 LinearValue() const { return linear_value; }
    f32 NormalisedLinearValue() const {
        return MapTo01(LinearValue(), info.linear_range.min, info.linear_range.max);
    }
    f32 ProjectedValue() const { return info.ProjectValue(LinearValue()); }
    template <typename Type>
    Type IntValue() const {
        return ParamToInt<Type>(LinearValue());
    }
    bool BoolValue() const { return ParamToBool(LinearValue()); }
    f32 DefaultLinearValue() const { return info.default_linear_value; }
    f32 NormalisedDefaultLinearValue() const {
        return MapTo01(DefaultLinearValue(), info.linear_range.min, info.linear_range.max);
    }

    ParamDescriptor const& info;
    f32 const& linear_value;
};

ALWAYS_INLINE static void AssertNoUnhandledLegacyAncestor(ParamIndex index) {
    if constexpr (!PRODUCTION_BUILD) {
        if (!g_is_logical_main_thread) // Presumably audio thread only
            ASSERT_HOT(k_param_descriptors[ToInt(index)].flags.legacy || !LegacyPredecessor(index));
    }
}

// A convenience wrapper around an array of f32 parameter values. We use these in lots of places so it's very
// helpful to have convenient access to the various forms of parameter values.
struct Parameters {
    ALWAYS_INLINE f32 LinearValue(ParamIndex index) const {
        AssertNoUnhandledLegacyAncestor(index);
        return values[ToInt(index)];
    }
    ALWAYS_INLINE f32 LinearValue(u8 layer_index, LayerParamIndex index) const {
        return LinearValue(ParamIndexFromLayerParamIndex(layer_index, index));
    }

    ALWAYS_INLINE f32 LinearValueIgnoringLegacy(ParamIndex index) const { return values[ToInt(index)]; }

    f32 ProjectedValue(ParamIndex index) const {
        AssertNoUnhandledLegacyAncestor(index);
        return k_param_descriptors[ToInt(index)].ProjectValue(LinearValue(index));
    }
    f32 ProjectedValue(u8 layer_index, LayerParamIndex index) const {
        return ProjectedValue(ParamIndexFromLayerParamIndex(layer_index, index));
    }

    template <typename Type>
    Type IntValue(ParamIndex index) const {
        AssertNoUnhandledLegacyAncestor(index);
        ASSERT_HOT(IsAnyOf(Info(index).value_type,
                           Array {ParamValueType::Int, ParamValueType::Bool, ParamValueType::Menu}));
        return ParamToInt<Type>(LinearValue(index));
    }
    template <typename Type>
    Type IntValue(u8 layer_index, LayerParamIndex index) const {
        return IntValue<Type>(ParamIndexFromLayerParamIndex(layer_index, index));
    }

    bool BoolValue(ParamIndex index) const {
        AssertNoUnhandledLegacyAncestor(index);
        ASSERT_HOT(Info(index).value_type == ParamValueType::Bool);
        return ParamToBool(LinearValue(index));
    }
    bool BoolValue(u8 layer_index, LayerParamIndex index) const {
        return BoolValue(ParamIndexFromLayerParamIndex(layer_index, index));
    }

    f32 LinearValueLegacyAware(ParamIndex modern) const { return ResolveLegacyAware(modern, values); }
    f32 LinearValueLegacyAware(u8 layer_index, LayerParamIndex modern) const {
        return LinearValueLegacyAware(ParamIndexFromLayerParamIndex(layer_index, modern));
    }

    f32 ProjectedValueLegacyAware(ParamIndex modern) const {
        return k_param_descriptors[ToInt(modern)].ProjectValue(LinearValueLegacyAware(modern));
    }
    f32 ProjectedValueLegacyAware(u8 layer_index, LayerParamIndex modern) const {
        return ProjectedValueLegacyAware(ParamIndexFromLayerParamIndex(layer_index, modern));
    }

    template <typename Type>
    Type IntValueLegacyAware(ParamIndex modern) const {
        return ParamToInt<Type>(LinearValueLegacyAware(modern));
    }
    template <typename Type>
    Type IntValueLegacyAware(u8 layer_index, LayerParamIndex modern) const {
        return IntValueLegacyAware<Type>(ParamIndexFromLayerParamIndex(layer_index, modern));
    }

    static ParamDescriptor const& Info(ParamIndex index) { return k_param_descriptors[ToInt(index)]; }
    static ParamDescriptor const& Info(u8 layer_index, LayerParamIndex index) {
        return k_param_descriptors[ToInt(ParamIndexFromLayerParamIndex(layer_index, index))];
    }

    struct DescribedParamValue DescribedValue(ParamIndex index) {
        return {k_param_descriptors[ToInt(index)], values[ToInt(index)]};
    }
    struct DescribedParamValue DescribedValue(u8 layer_index, LayerParamIndex index) {
        auto const param_index = ParamIndexFromLayerParamIndex(layer_index, index);
        return {k_param_descriptors[ToInt(param_index)], values[ToInt(param_index)]};
    }

    void SetLinearValue(ParamIndex index, f32 value) {
        auto const& range = k_param_descriptors[ToInt(index)].linear_range;
        ASSERT(value >= range.min && value <= range.max);
        values[ToInt(index)] = value;
    }

    Array<f32, k_num_parameters> values; // Linear values.
};

struct ChangedParams {
    Optional<f32> LinearValue(ParamIndex index) const {
        AssertNoUnhandledLegacyAncestor(index);
        if (changed.Get(ToInt(index))) return params.LinearValue(index);
        return k_nullopt;
    }
    Optional<f32> LinearValue(u8 layer_index, LayerParamIndex index) const {
        return LinearValue(ParamIndexFromLayerParamIndex(layer_index, index));
    }

    Optional<f32> ProjectedValue(ParamIndex index) const {
        AssertNoUnhandledLegacyAncestor(index);
        if (changed.Get(ToInt(index))) return params.ProjectedValue(index);
        return k_nullopt;
    }
    Optional<f32> ProjectedValue(u8 layer_index, LayerParamIndex index) const {
        return ProjectedValue(ParamIndexFromLayerParamIndex(layer_index, index));
    }

    template <typename Type>
    Optional<Type> IntValue(ParamIndex index) const {
        AssertNoUnhandledLegacyAncestor(index);
        if (changed.Get(ToInt(index))) return params.IntValue<Type>(index);
        return k_nullopt;
    }
    template <typename Type>
    Optional<Type> IntValue(u8 layer_index, LayerParamIndex index) const {
        return IntValue<Type>(ParamIndexFromLayerParamIndex(layer_index, index));
    }

    Optional<bool> BoolValue(ParamIndex index) const {
        AssertNoUnhandledLegacyAncestor(index);
        if (changed.Get(ToInt(index))) return params.BoolValue(index);
        return k_nullopt;
    }
    Optional<bool> BoolValue(u8 layer_index, LayerParamIndex index) const {
        return BoolValue(ParamIndexFromLayerParamIndex(layer_index, index));
    }

    bool Changed(ParamIndex index) const {
        AssertNoUnhandledLegacyAncestor(index);
        return changed.Get(ToInt(index));
    }
    bool Changed(u8 layer_index, LayerParamIndex index) const {
        return Changed(ParamIndexFromLayerParamIndex(layer_index, index));
    }

    bool ChangedIgnoringLegacy(ParamIndex index) const { return changed.Get(ToInt(index)); }

    bool ChangedLegacyAware(ParamIndex modern) const {
        if (ChangedIgnoringLegacy(modern)) return true;
        auto ancestor = LegacyPredecessor(modern);
        while (ancestor) {
            if (ChangedIgnoringLegacy(*ancestor)) return true;
            ancestor = LegacyPredecessor(*ancestor);
        }
        return false;
    }
    bool ChangedLegacyAware(u8 layer_index, LayerParamIndex modern) const {
        return ChangedLegacyAware(ParamIndexFromLayerParamIndex(layer_index, modern));
    }

    Optional<f32> LinearValueLegacyAware(ParamIndex modern) const {
        if (!ChangedLegacyAware(modern)) return k_nullopt;
        return ResolveLegacyAware(modern, params.values);
    }
    Optional<f32> LinearValueLegacyAware(u8 layer_index, LayerParamIndex modern) const {
        return LinearValueLegacyAware(ParamIndexFromLayerParamIndex(layer_index, modern));
    }

    Optional<f32> ProjectedValueLegacyAware(ParamIndex modern) const {
        if (!ChangedLegacyAware(modern)) return k_nullopt;
        return k_param_descriptors[ToInt(modern)].ProjectValue(ResolveLegacyAware(modern, params.values));
    }
    Optional<f32> ProjectedValueLegacyAware(u8 layer_index, LayerParamIndex modern) const {
        return ProjectedValueLegacyAware(ParamIndexFromLayerParamIndex(layer_index, modern));
    }

    template <typename Type>
    Optional<Type> IntValueLegacyAware(ParamIndex modern) const {
        if (!ChangedLegacyAware(modern)) return k_nullopt;
        return ParamToInt<Type>(ResolveLegacyAware(modern, params.values));
    }
    template <typename Type>
    Optional<Type> IntValueLegacyAware(u8 layer_index, LayerParamIndex modern) const {
        return IntValueLegacyAware<Type>(ParamIndexFromLayerParamIndex(layer_index, modern));
    }

    Parameters const& params;
    Bitset<k_num_parameters> changed;
};
