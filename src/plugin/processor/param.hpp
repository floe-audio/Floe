// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

struct Parameter {
    f32 LinearValue() const { return value.Load(LoadMemoryOrder::Relaxed); }
    f32 NormalisedLinearValue() const {
        return MapTo01(LinearValue(), info.linear_range.min, info.linear_range.max);
    }
    f32 ProjectedValue() const { return info.ProjectValue(value.Load(LoadMemoryOrder::Relaxed)); }
    template <typename Type>
    Type ValueAsInt() const {
        return ParamToInt<Type>(LinearValue());
    }
    bool ValueAsBool() const { return ParamToBool(LinearValue()); }

    bool SetLinearValue(f32 new_value) {
        ASSERT(info.linear_range.Contains(new_value));
        auto const t = value.Load(LoadMemoryOrder::Relaxed);
        value.Store(new_value, StoreMemoryOrder::Relaxed);
        return t != new_value;
    }

    f32 DefaultLinearValue() const { return info.default_linear_value; }
    f32 NormalisedDefaultLinearValue() const {
        return MapTo01(DefaultLinearValue(), info.linear_range.min, info.linear_range.max);
    }

    ParamDescriptor const& info;
    Atomic<f32> value;
};

template <usize k_num_params>
struct ChangedParamsTemplate {
    using IndexType = Conditional<k_num_params == k_num_parameters, ParamIndex, LayerParamIndex>;

    ChangedParamsTemplate(StaticSpan<Parameter const, k_num_params> params, Bitset<k_num_params> changed)
        : params(params)
        , changed(changed) {}

    Parameter const* Param(IndexType index) const {
        auto const i = ToInt(index);
        return changed.Get(i) ? &params[i] : nullptr;
    }

    template <usize k_result_size>
    ChangedParamsTemplate<k_result_size> Subsection(usize offset) {
        return {params.data + offset, changed.template Subsection<k_result_size>(offset)};
    }

    bool Changed(IndexType index) const { return changed.Get((int)index); }

    auto Params() { return params; }

    StaticSpan<Parameter const, k_num_params> params;
    Bitset<k_num_params> changed;
};

using ChangedParams = ChangedParamsTemplate<k_num_parameters>;
using ChangedLayerParams = ChangedParamsTemplate<k_num_layer_parameters>;
