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
    Optional<ParamIndex> param_index {}; // nullopt if this is unused.
    f32 value = 0; // Bidirectional percentage from -1 to 1.
};

// Null-terminated array: all active destinations are packed at the beginning, followed by nullopt entries.
// We don't use a standard dynamic array with a size because it's trickier to transfer that via atomics:
// there's 2 separate bits of data: element and size.
struct MacroDestinationList {
    constexpr bool operator==(MacroDestinationList const& other) const = default;

    usize Size() const {
        for (auto const [i, d] : Enumerate(items))
            if (!d.param_index) return i;
        return k_max_macro_destinations;
    }

    Optional<usize> Append(MacroDestination const& d) {
        ASSERT(d.param_index);
        auto const i = Size();
        if (i >= k_max_macro_destinations) return k_nullopt;
        items[i] = d;
        return i;
    }

    void RemoveAt(usize index) {
        ASSERT(items[index].param_index);
        // Shift everything after index down by one to keep array packed.
        for (usize i = index; i < k_max_macro_destinations - 1; i++) {
            items[i] = items[i + 1];
            if (!items[i + 1].param_index) break;
        }
        items[k_max_macro_destinations - 1] = {};
    }

    Array<MacroDestination, k_max_macro_destinations> items;
};

using MacroName = DynamicArrayBounded<char, k_max_macro_name_length>;
using MacroNames = Array<MacroName, k_num_macros>;

constexpr auto DefaultMacroNames() {
    static auto const reuslt = ArrayT<MacroName>({
        "Macro 1"_s,
        "Macro 2"_s,
        "Macro 3"_s,
        "Macro 4"_s,
    });
    static_assert(reuslt.size == k_num_macros);
    return reuslt;
}

using MacroDestinations = Array<MacroDestinationList, k_num_macros>;

constexpr auto k_macro_params = ComptimeParamSearch<ComptimeParamSearchOptions {
    .modules = ParamModules {ParameterModule::Macro},
    .skip = {},
}>();

static_assert(k_macro_params.size == k_num_macros);
