// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/preferences.hpp"

PUBLIC prefs::Descriptor ExperimentalParamsPreferenceDescriptor() {
    return {
        .key = "experimental-params"_s,
        .value_requirements = prefs::ValueType::Bool,
        .default_value = false,
        .gui_label = "Experimental parameters",
        .long_description =
            "Enable experimental parameters. These are not yet finalised and may change or be removed. It is not recommended saving presets when in this mode.",
    };
}

PUBLIC prefs::Descriptor AbbreviatedParamNamesPreferenceDescriptor() {
    return {
        .key = "abbreviated-param-names"_s,
        .value_requirements = prefs::ValueType::Bool,
        .default_value = false,
        .gui_label = "Abbreviate parameter names in DAW",
        .long_description =
            "Enable short parameters names in the DAW. e.g. instead of \"Effect Distortion On\", show \"FxDs On\". Restarting your DAW might be required for this change to take effect.",
    };
}
