// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

constexpr u32 k_max_macro_destinations = 6;
constexpr u32 k_max_macro_name_length = 20;
constexpr u32 k_num_macros = 4;

struct MacroDestination {
    constexpr bool operator==(MacroDestination const& other) const = default;
    f32 ProjectedValue() const {
        // It feels more useful to have more granularity with smaller values so we use a cubic projection.
        return Copysign(value * value, value);
    }
    ParamIndex param_index;
    f32 value = 0; // Bidirectional percentage from -1 to 1.
};

using MacroName = DynamicArrayBounded<char, k_max_macro_name_length>;
using MacroNames = Array<MacroName, k_num_macros>;

constexpr auto DefaultMacroNames() {
    auto const reuslt = ArrayT<MacroName>({
        "Macro 1"_s,
        "Macro 2"_s,
        "Macro 3"_s,
        "Macro 4"_s,
    });
    static_assert(reuslt.size == k_num_macros);
    return reuslt;
}

using MacroDestinations =
    Array<DynamicArrayBounded<MacroDestination, k_max_macro_destinations>, k_num_macros>;

constexpr auto k_macro_params = ComptimeParamSearch<ComptimeParamSearchOptions {
    .modules = ParamModules {ParameterModule::Macro},
    .skip = {},
}>();

static_assert(k_macro_params.size == k_num_macros);
