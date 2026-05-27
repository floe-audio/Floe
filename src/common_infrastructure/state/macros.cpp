// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "macros.hpp"

f32 AdjustedLinearValue(Span<f32 const> param_values,
                        MacroDestinations const& macros,
                        f32 linear_value,
                        ParamIndex param_index) {
    auto const& descriptor = k_param_descriptors[ToInt(param_index)];

    for (auto const [macro_index, dests] : Enumerate(macros)) {
        for (auto const& dest : dests.items)
            if (dest.param_index == param_index) {
                auto const macro_param = param_values[ToInt(k_macro_params[macro_index])];
                linear_value += descriptor.linear_range.Delta() * (dest.ProjectedValue() * macro_param);
            }
    }

    return Clamp(linear_value, descriptor.linear_range.min, descriptor.linear_range.max);
}
