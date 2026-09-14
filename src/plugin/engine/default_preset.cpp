// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "default_preset.hpp"

#include "tests/framework.hpp"

static bool ValidatePresetPath(String& value) {
    if (value.size > 8000) return false;
    if (!path::IsAbsolute(value)) return false;
    if (!IsValidUtf8(value)) return false;
    return true;
}

prefs::Descriptor DefaultPresetDescriptor() {
    return {
        .key = "default-preset"_s,
        .value_requirements = prefs::Descriptor::StringRequirements {.validator = ValidatePresetPath},
        .default_value = String {nullptr, 0xdeadc0de},
        .gui_label = "Default Preset"_s,
        .long_description =
            "The preset loaded when you open a new instance of Floe. Set one by right-clicking a preset in the preset browser."_s,
    };
}

prefs::Descriptor SuggestedDefaultPresetDescriptor() {
    return {
        .key = "suggested-default-preset"_s,
        .value_requirements = prefs::Descriptor::StringRequirements {.validator = ValidatePresetPath},
        .default_value = String {nullptr, 0xdeadc0de},
        .gui_label = "Suggested Default Preset"_s,
        .long_description = "A default preset suggested by the most recently installed preset bank."_s,
    };
}

Optional<DefaultPreset> ResolveDefaultPreset(prefs::PreferencesTable const& prefs) {
    if (auto const v = prefs::GetValue(prefs, DefaultPresetDescriptor()); !v.is_default)
        return DefaultPreset {.path = v.value.Get<String>(), .is_user_set = true};
    if (auto const v = prefs::GetValue(prefs, SuggestedDefaultPresetDescriptor()); !v.is_default)
        return DefaultPreset {.path = v.value.Get<String>(), .is_user_set = false};
    return k_nullopt;
}

void SetDefaultPreset(prefs::Preferences& prefs, String path) {
    prefs::SetValue(prefs, DefaultPresetDescriptor(), path);
}

void SetSuggestedDefaultPreset(prefs::Preferences& prefs, String path) {
    prefs::SetValue(prefs, SuggestedDefaultPresetDescriptor(), path);
}

void ClearDefaultPreset(prefs::Preferences& prefs) {
    prefs::Remove(prefs, DefaultPresetDescriptor().key);
    prefs::Remove(prefs, SuggestedDefaultPresetDescriptor().key);
}

bool IsDefaultPreset(prefs::PreferencesTable const& prefs, String path) {
    auto const resolved = ResolveDefaultPreset(prefs);
    return resolved && path::Equal(resolved->path, path);
}

TEST_CASE(TestDefaultPresetResolution) {
    prefs::Preferences prefs {};

    auto const user_path = IS_WINDOWS ? "C:/presets/User.floe-preset"_s : "/presets/User.floe-preset"_s;
    auto const suggested_path = IS_WINDOWS ? "C:/presets/Bank.floe-preset"_s : "/presets/Bank.floe-preset"_s;

    CHECK(!ResolveDefaultPreset(prefs));

    SetSuggestedDefaultPreset(prefs, suggested_path);
    {
        auto const resolved = ResolveDefaultPreset(prefs);
        REQUIRE(resolved);
        CHECK_EQ(resolved->path, suggested_path);
        CHECK(!resolved->is_user_set);
    }

    // An explicit choice wins over a suggestion.
    SetDefaultPreset(prefs, user_path);
    {
        auto const resolved = ResolveDefaultPreset(prefs);
        REQUIRE(resolved);
        CHECK_EQ(resolved->path, user_path);
        CHECK(resolved->is_user_set);
    }

    CHECK(IsDefaultPreset(prefs, user_path));
    CHECK(!IsDefaultPreset(prefs, suggested_path));

    ClearDefaultPreset(prefs);
    CHECK(!ResolveDefaultPreset(prefs));

    // Relative paths are rejected.
    SetDefaultPreset(prefs, "relative/Init.floe-preset"_s);
    CHECK(!ResolveDefaultPreset(prefs));

    return k_success;
}

TEST_REGISTRATION(RegisterDefaultPresetTests) { REGISTER_TEST(TestDefaultPresetResolution); }
