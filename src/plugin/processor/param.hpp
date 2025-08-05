// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

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

// A convenience wrapper around an array of f32 parameter values. We use these in lots of places so it's very
// helpful to have convenient access to the various forms of parameter values.
struct Parameters {
    ALWAYS_INLINE f32 LinearValue(ParamIndex index) const { return values[ToInt(index)]; }
    ALWAYS_INLINE f32 LinearValue(u8 layer_index, LayerParamIndex index) const {
        return values[ToInt(ParamIndexFromLayerParamIndex(layer_index, index))];
    }

    f32 ProjectedValue(ParamIndex index) const {
        return k_param_descriptors[ToInt(index)].ProjectValue(LinearValue(index));
    }
    f32 ProjectedValue(u8 layer_index, LayerParamIndex index) const {
        return k_param_descriptors[ToInt(ParamIndexFromLayerParamIndex(layer_index, index))].ProjectValue(
            LinearValue(layer_index, index));
    }

    template <typename Type>
    Type IntValue(ParamIndex index) const {
        ASSERT_HOT(IsAnyOf(Info(index).value_type,
                           Array {ParamValueType::Int, ParamValueType::Bool, ParamValueType::Menu}));
        return ParamToInt<Type>(LinearValue(index));
    }
    template <typename Type>
    Type IntValue(u8 layer_index, LayerParamIndex index) const {
        ASSERT_HOT(IsAnyOf(Info(layer_index, index).value_type,
                           Array {ParamValueType::Int, ParamValueType::Bool, ParamValueType::Menu}));
        return ParamToInt<Type>(LinearValue(layer_index, index));
    }

    bool BoolValue(ParamIndex index) const {
        ASSERT_HOT(Info(index).value_type == ParamValueType::Bool);
        return ParamToBool(LinearValue(index));
    }
    bool BoolValue(u8 layer_index, LayerParamIndex index) const {
        ASSERT_HOT(Info(layer_index, index).value_type == ParamValueType::Bool);
        return ParamToBool(LinearValue(layer_index, index));
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
    Optional<f32> ProjectedValue(ParamIndex index) const {
        if (changed.Get(ToInt(index))) return params.ProjectedValue(index);
        return k_nullopt;
    }

    Optional<f32> ProjectedValue(u8 layer_index, LayerParamIndex index) const {
        auto const param_index = ParamIndexFromLayerParamIndex(layer_index, index);
        if (changed.Get(ToInt(param_index))) return params.ProjectedValue(param_index);
        return k_nullopt;
    }

    template <typename Type>
    Optional<Type> IntValue(ParamIndex index) const {
        if (changed.Get(ToInt(index))) return params.IntValue<Type>(index);
        return k_nullopt;
    }
    template <typename Type>
    Optional<Type> IntValue(u8 layer_index, LayerParamIndex index) const {
        auto const param_index = ParamIndexFromLayerParamIndex(layer_index, index);
        if (changed.Get(ToInt(param_index))) return params.IntValue<Type>(param_index);
        return k_nullopt;
    }

    Optional<bool> BoolValue(ParamIndex index) const {
        if (changed.Get(ToInt(index))) return params.BoolValue(index);
        return k_nullopt;
    }
    Optional<bool> BoolValue(u8 layer_index, LayerParamIndex index) const {
        auto const param_index = ParamIndexFromLayerParamIndex(layer_index, index);
        if (changed.Get(ToInt(param_index))) return params.BoolValue(param_index);
        return k_nullopt;
    }

    bool Changed(ParamIndex index) const { return changed.Get(ToInt(index)); }
    bool Changed(u8 layer_index, LayerParamIndex index) const {
        return changed.Get(ToInt(ParamIndexFromLayerParamIndex(layer_index, index)));
    }

    Parameters const& params;
    Bitset<k_num_parameters> changed;
};
