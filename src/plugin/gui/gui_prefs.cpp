// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_prefs.hpp"

#include "gui_framework/gui_platform.hpp"

prefs::Descriptor SettingDescriptor(GuiSetting setting) {
    ASSERT(g_is_logical_main_thread);
    switch (setting) {
        case GuiSetting::ShowTooltips:
            return {
                .key = prefs::key::k_show_tooltips,
                .value_requirements = prefs::ValueType::Bool,
                .default_value = true,
                .gui_label = "Show tooltips",
                .long_description = "Show descriptions when hovering over controls.",
            };
        case GuiSetting::HighContrastGui:
            return {
                .key = prefs::key::k_high_contrast_gui,
                .value_requirements = prefs::ValueType::Bool,
                .default_value = false,
                .gui_label = "High contrast GUI",
                .long_description = "Use a high contrast colour scheme.",
            };
        case GuiSetting::ShowInstanceName:
            return {
                .key = "show-instance-name"_s,
                .value_requirements = prefs::ValueType::Bool,
                .default_value = true,
                .gui_label = "Show instance name",
                .long_description = "Show the name of the instance in the top panel GUI.",
            };
        case GuiSetting::WindowWidth:
            return {
                .key = prefs::key::k_window_width,
                .value_requirements =
                    prefs::Descriptor::IntRequirements {
                        .validator =
                            [](s64& value) {
                                value = Clamp<s64>(value, k_min_gui_width, k_max_gui_width);
                                value = SizeWithAspectRatio((u16)value, k_gui_aspect_ratio).width;
                                return true;
                            },
                    },
                .default_value = (s64)0,
                .gui_label = "Window width",
                .long_description = "The size and scaling of Floe's window.",
            };
        case GuiSetting::Count: PanicIfReached();
    }
}

Optional<UiSize> DesiredWindowSize(prefs::Preferences const& preferences) {
    ASSERT(g_is_logical_main_thread);
    auto const val = prefs::GetValue(preferences, SettingDescriptor(GuiSetting::WindowWidth));
    if (val.is_default) return k_nullopt;
    auto const int_val = val.value.Get<s64>();
    return SizeWithAspectRatio((u16)int_val, k_gui_aspect_ratio);
}
