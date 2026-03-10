// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/engine_prefs.hpp"

prefs::Descriptor ExperimentalFeaturesPreferenceDescriptor() {
    return {
        .key = "experimental-features"_s,
        .value_requirements = prefs::ValueType::Bool,
        .default_value = false,
        .gui_label = "Experimental features",
        .long_description =
            "Enable experimental features. These features are not yet finalised and may change or be removed in future versions. Presets or DAW projects that use experimental features may not be compatible with future versions of Floe.",
    };
}
