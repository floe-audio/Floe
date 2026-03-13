// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/engine_prefs.hpp"

prefs::Descriptor ExperimentalParamsPreferenceDescriptor() {
    return {
        .key = "experimental-params"_s,
        .value_requirements = prefs::ValueType::Bool,
        .default_value = false,
        .gui_label = "Experimental parameters",
        .long_description =
            "Enable experimental parameters. These are not yet finalised and may change or be removed. It is not recommended saving presets when in this mode.",
    };
}
