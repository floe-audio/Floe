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
        case GuiSetting::ShowKeyboard:
            return {
                .key = prefs::key::k_show_keyboard,
                .value_requirements = prefs::ValueType::Bool,
                .default_value = true,
                .gui_label = "Show keyboard",
                .long_description = "Show the on-screen keyboard.",
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
                                value = Clamp<s64>(value, k_min_gui_width, k_largest_gui_size);
                                value =
                                    SizeWithAspectRatio((u16)value, k_aspect_ratio_without_keyboard).width;
                                return true;
                            },
                    },
                .default_value = (s64)k_default_gui_width,
                .gui_label = "Window width",
                .long_description = "The size and scaling of Floe's window.",
            };
        case GuiSetting::Count: PanicIfReached();
    }
}

UiSize DesiredAspectRatio(prefs::Preferences const& preferences) {
    ASSERT(g_is_logical_main_thread);
    return prefs::GetBool(preferences, SettingDescriptor(GuiSetting::ShowKeyboard))
               ? k_aspect_ratio_with_keyboard
               : k_aspect_ratio_without_keyboard;
}

UiSize DesiredWindowSize(prefs::Preferences const& preferences) {
    ASSERT(g_is_logical_main_thread);
    return SizeWithAspectRatio((u16)prefs::GetInt(preferences, SettingDescriptor(GuiSetting::WindowWidth)),
                               DesiredAspectRatio(preferences));
}

f32 KeyboardHeight(prefs::Preferences const& preferences) {
    ASSERT(g_is_logical_main_thread);
    static_assert(k_aspect_ratio_with_keyboard.height > k_aspect_ratio_without_keyboard.height);
    static_assert(k_aspect_ratio_with_keyboard.width == k_aspect_ratio_without_keyboard.width);
    auto const width = (u16)prefs::GetInt(preferences, SettingDescriptor(GuiSetting::WindowWidth));
    return (f32)(SizeWithAspectRatio(width, k_aspect_ratio_with_keyboard).height -
                 SizeWithAspectRatio(width, k_aspect_ratio_without_keyboard).height);
}
