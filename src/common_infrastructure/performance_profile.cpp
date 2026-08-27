// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "performance_profile.hpp"

#include "tests/framework.hpp"

#include "descriptors/param_descriptors.hpp"

namespace perf_profile {

constexpr String k_name_key = "name"_s;
constexpr String k_reset_on_transport_key = "reset-on-transport"_s;
constexpr String k_mpe_enabled_key = "mpe-enabled"_s;
constexpr String k_mpe_smoothing_key = "mpe-smoothing-ms"_s;
constexpr String k_reset_keyswitch_key = "reset-keyswitch"_s;
constexpr String k_seed_key = "seed"_s;
constexpr String k_cc_key_prefix = "cc-"_s;

static bool IsIniValueSafeName(String name) {
    for (auto const c : name)
        if ((u8)c < 32 || (u8)c == 127) return false;
    return true;
}

static MutableString ProfileSection(ArenaAllocator& arena) {
    u64 seed = RandomSeed();
    return fmt::Format(arena, "{}.{06x}", k_profile_section_prefix, RandomU64(seed) & 0xffffff);
}

// Invokes 'callback(key, value_list)' for each key in the given section.
static void ForEachKeyInSection(prefs::PreferencesTable const& prefs, String section, auto&& callback) {
    for (auto const [key, value_list, _] : prefs) {
        auto const sectioned_key = key.TryGet<prefs::SectionedKey>();
        if (sectioned_key && sectioned_key->section == section) callback(sectioned_key->key, value_list);
    }
}

// Invokes 'callback(section, name)' once for each profile. Every profile section has exactly one "name" key,
// so iterating those visits each profile exactly once.
static void ForEachProfileName(prefs::PreferencesTable const& prefs, auto&& callback) {
    for (auto const [key, value_list, _] : prefs) {
        auto const sectioned_key = key.TryGet<prefs::SectionedKey>();
        if (!sectioned_key) continue;
        if (!StartsWithSpan(sectioned_key->section, k_profile_section_prefix)) continue;
        if (sectioned_key->key.tag != prefs::KeyValueType::String) continue;
        if (sectioned_key->key.Get<String>() != k_name_key) continue;
        for (auto value = value_list; value; value = value->next)
            if (value->tag == prefs::ValueType::String)
                callback(sectioned_key->section, value->Get<String>());
    }
}

// The section string of the profile whose name matches, cloned into 'arena', or nullopt.
static Optional<String>
FindProfileSection(prefs::PreferencesTable const& prefs, String name, ArenaAllocator& arena) {
    Optional<String> result;
    ForEachProfileName(prefs, [&](String section, String profile_name) {
        if (!result && profile_name == name) result = arena.Clone(section);
    });
    return result;
}

static bool SectionHasAnyKeys(prefs::PreferencesTable const& prefs, String section) {
    bool any = false;
    ForEachKeyInSection(prefs, section, [&](prefs::KeyValueUnion const&, prefs::Value const*) {
        any = true;
    });
    return any;
}

static Profile ReadProfileFromSection(prefs::PreferencesTable const& prefs, String section) {
    Profile profile {};

    if (auto const v = prefs::LookupString(prefs, prefs::SectionedKey {section, k_name_key}))
        dyn::AssignFitInCapacity(profile.name, *v);
    if (auto const v = prefs::LookupBool(prefs, prefs::SectionedKey {section, k_reset_on_transport_key}))
        profile.controls.settings.reset_on_transport = *v;
    if (auto const v = prefs::LookupBool(prefs, prefs::SectionedKey {section, k_mpe_enabled_key}))
        profile.controls.settings.mpe_enabled = *v;
    if (auto const v = prefs::LookupInt(prefs, prefs::SectionedKey {section, k_mpe_smoothing_key}))
        profile.controls.settings.mpe_smoothing_ms = (u16)Clamp(*v, (s64)0, (s64)500);
    if (auto const v = prefs::LookupInt(prefs, prefs::SectionedKey {section, k_reset_keyswitch_key}))
        if (*v >= 0 && *v <= 127) profile.controls.settings.reset_keyswitch = (u7)*v;
    if (auto const v = prefs::LookupInt(prefs, prefs::SectionedKey {section, k_seed_key}))
        profile.controls.settings.seed = (u8)Clamp(*v, (s64)0, (s64)99);

    ForEachKeyInSection(prefs, section, [&](prefs::KeyValueUnion const& key, prefs::Value const* value_list) {
        if (key.tag != prefs::KeyValueType::String) return;
        auto const key_string = key.Get<String>();
        if (!StartsWithSpan(key_string, k_cc_key_prefix)) return;
        auto const cc_num = ParseInt(key_string.SubSpan(k_cc_key_prefix.size), ParseIntBase::Decimal);
        if (!cc_num || *cc_num < 0 || *cc_num > 127) return;
        for (auto value = value_list; value; value = value->next) {
            if (value->tag != prefs::ValueType::String) continue;
            if (auto const param_index = ParamIndexFromIdString(value->Get<String>()))
                profile.controls.param_learned_ccs[ToInt(*param_index)].Set((usize)*cc_num);
        }
    });

    return profile;
}

static bool AnyProfileExists(prefs::PreferencesTable const& prefs) {
    bool any = false;
    prefs::ForEachSubsectionLeaf(prefs, k_profile_section_prefix, [&](String) { any = true; });
    return any;
}

static void ClearSection(prefs::Preferences& prefs, String section) {
    ArenaAllocatorWithInlineStorage<2000> scratch {PageAllocator::Instance()};
    DynamicArray<prefs::Key> to_remove {scratch};
    ForEachKeyInSection(prefs, section, [&](prefs::KeyValueUnion const& key, prefs::Value const*) {
        dyn::Append(to_remove, prefs::Key {prefs::SectionedKey {section, key}});
    });
    for (auto const& key : to_remove)
        prefs::Remove(prefs, key);
}

static void
WriteProfileToSection(prefs::Preferences& prefs, String section, Profile const& profile, bool include_name) {
    constexpr prefs::SetValueOptions k_opts {.clone_key_string = true};

    if (include_name)
        prefs::SetValue(prefs, prefs::SectionedKey {section, k_name_key}, (String)profile.name, k_opts);
    prefs::SetValue(prefs,
                    prefs::SectionedKey {section, k_reset_on_transport_key},
                    profile.controls.settings.reset_on_transport,
                    k_opts);
    prefs::SetValue(prefs,
                    prefs::SectionedKey {section, k_mpe_enabled_key},
                    profile.controls.settings.mpe_enabled,
                    k_opts);
    prefs::SetValue(prefs,
                    prefs::SectionedKey {section, k_mpe_smoothing_key},
                    (s64)profile.controls.settings.mpe_smoothing_ms,
                    k_opts);
    if (profile.controls.settings.reset_keyswitch.HasValue())
        prefs::SetValue(prefs,
                        prefs::SectionedKey {section, k_reset_keyswitch_key},
                        (s64)profile.controls.settings.reset_keyswitch.Value(),
                        k_opts);
    prefs::SetValue(prefs, prefs::SectionedKey {section, k_seed_key}, (s64)profile.controls.settings.seed, k_opts);

    for (auto const [param_index, param_ccs] : Enumerate(profile.controls.param_learned_ccs)) {
        if (!param_ccs.AnyValuesSet()) continue;
        auto const id_string = ParamDescriptorAt((ParamIndex)param_index).id_string;
        for (auto const cc_num : Range(128uz))
            if (param_ccs.Get(cc_num)) {
                auto const key_string = fmt::FormatInline<16>("{}{}", k_cc_key_prefix, cc_num);
                prefs::AddValue(prefs, prefs::SectionedKey {section, (String)key_string}, id_string, k_opts);
            }
    }
}

DynamicArray<String> ListProfileNames(prefs::PreferencesTable const& prefs, ArenaAllocator& arena) {
    DynamicArray<String> result {arena};
    ForEachProfileName(prefs,
                       [&](String, String name) { dyn::AppendIfNotAlreadyThere(result, arena.Clone(name)); });
    Sort(result);
    return result;
}

Optional<Profile> LookupProfile(prefs::PreferencesTable const& prefs, String name) {
    if (name.size > k_max_name_size) return k_nullopt;
    ArenaAllocatorWithInlineStorage<2000> scratch {PageAllocator::Instance()};
    auto const section = FindProfileSection(prefs, name, scratch);
    if (!section) return k_nullopt;
    return ReadProfileFromSection(prefs, *section);
}

Optional<Profile> LookupDefault(prefs::PreferencesTable const& prefs) {
    if (!SectionHasAnyKeys(prefs, k_defaults_section)) return k_nullopt;
    auto profile = ReadProfileFromSection(prefs, k_defaults_section);
    dyn::Clear(profile.name);
    return profile;
}

Profile FallbackProfile() {
    Profile result {};
    result.controls.param_learned_ccs[ToInt(ParamIndex::MasterTimbre)].Set(11);
    result.controls.param_learned_ccs[ToInt(ParamIndex::MasterVolume)].Set(7);
    result.controls.param_learned_ccs[ToInt(ParamIndex::Macro1)].Set(1);
    return result;
}

Profile DefaultOrFallback(prefs::PreferencesTable const& prefs) {
    if (auto const profile = LookupDefault(prefs)) return *profile;
    return FallbackProfile();
}

void SaveProfile(prefs::Preferences& prefs, Profile const& profile) {
    ASSERT(profile.name.size <= k_max_name_size);

    ArenaAllocatorWithInlineStorage<2000> scratch {PageAllocator::Instance()};
    auto section = FindProfileSection(prefs, profile.name, scratch);
    if (section)
        ClearSection(prefs, *section);
    else
        section = (String)ProfileSection(scratch);

    WriteProfileToSection(prefs, *section, profile, true);
}

void SaveDefault(prefs::Preferences& prefs, Profile const& profile) {
    ClearSection(prefs, k_defaults_section);
    WriteProfileToSection(prefs, k_defaults_section, profile, false);
}

void DeleteProfile(prefs::Preferences& prefs, String name) {
    if (name.size > k_max_name_size) return;
    ArenaAllocatorWithInlineStorage<2000> scratch {PageAllocator::Instance()};
    if (auto const section = FindProfileSection(prefs, name, scratch)) ClearSection(prefs, *section);
}

void RenameProfile(prefs::Preferences& prefs, String old_name, String new_name) {
    if (old_name == new_name) return;
    if (new_name.size > k_max_name_size) return;
    ArenaAllocatorWithInlineStorage<2000> scratch {PageAllocator::Instance()};
    if (auto const section = FindProfileSection(prefs, old_name, scratch))
        prefs::SetValue(prefs,
                        prefs::SectionedKey {*section, k_name_key},
                        (String)new_name,
                        {.clone_key_string = true});
}

Optional<NameError>
ValidateProfileName(prefs::PreferencesTable const& prefs, String name, Optional<String> ignore) {
    auto const trimmed = WhitespaceStripped(name);
    if (trimmed.size == 0) return NameError::Empty;
    if (trimmed.size > k_max_name_size) return NameError::TooLong;
    if (!IsValidUtf8(trimmed)) return NameError::InvalidCharacters;
    if (!IsIniValueSafeName(trimmed)) return NameError::InvalidCharacters;

    ArenaAllocatorWithInlineStorage<2000> scratch {PageAllocator::Instance()};
    for (auto const existing : ListProfileNames(prefs, scratch)) {
        if (ignore && *ignore == existing) continue;
        if (existing == trimmed) return NameError::AlreadyExists;
    }

    return k_nullopt;
}

void MigrateIfNeeded(prefs::Preferences& prefs) {
    if (LookupDefault(prefs)) return;
    if (AnyProfileExists(prefs)) return;

    Profile migrated {};

    ForEachKeyInSection(prefs,
                        prefs::key::section::k_cc_to_param_id_map_section,
                        [&](prefs::KeyValueUnion const& key, prefs::Value const* value_list) {
                            if (key.tag != prefs::KeyValueType::Int) return;
                            auto const cc_num = key.Get<s64>();
                            if (cc_num < 1 || cc_num > 127) return;
                            for (auto value = value_list; value; value = value->next) {
                                if (value->tag != prefs::ValueType::Int) continue;
                                if (auto const param_index = ParamIdToIndex((u32)value->Get<s64>()))
                                    migrated.controls.param_learned_ccs[ToInt(*param_index)].Set((usize)cc_num);
                            }
                        });

    if (prefs::LookupBool(prefs, prefs::key::k_default_cc_param_mappings).ValueOr(true))
        for (auto const [param_index, ccs] : Enumerate(FallbackProfile().controls.param_learned_ccs))
            migrated.controls.param_learned_ccs[param_index] |= ccs;

    // No section needed when the fallback already produces the same settings.
    if (migrated == FallbackProfile()) return;

    SaveDefault(prefs, migrated);
}

TEST_CASE(TestPerformanceProfiles) {
    SUBCASE("profile round trip") {
        prefs::Preferences prefs;
        Profile profile {
            .controls {
                .settings {
                    .reset_on_transport = false,
                    .mpe_enabled = true,
                    .mpe_smoothing_ms = 250,
                    .reset_keyswitch = (u7)60,
                    .seed = 42,
                },
            },
        };
        dyn::Assign(profile.name, "My controller! (main)");
        profile.controls.param_learned_ccs[ToInt(ParamIndex::MasterTimbre)].Set(11);
        profile.controls.param_learned_ccs[ToInt(ParamIndex::MasterVolume)].Set(7);
        profile.controls.param_learned_ccs[ToInt(ParamIndex::MasterVolume)].Set(74);
        profile.controls.param_learned_ccs[ToInt(ParamIndex::Macro1)].Set(1);

        SaveProfile(prefs, profile);

        auto const loaded = LookupProfile(prefs, profile.name);
        REQUIRE(loaded);
        CHECK(*loaded == profile);

        // The profile is serialised as a "performance-profile.<id>" section.
        {
            DynamicArray<char> buffer {tester.scratch_arena};
            TRY(prefs::WritePreferencesTable(prefs, dyn::WriterFor(buffer)));
            CHECK(ContainsSpan((String)buffer, "[performance-profile."_s));
            CHECK(ContainsSpan((String)buffer, "master.timbre"_s));
            CHECK(ContainsSpan((String)buffer, "cc-11"_s));
        }

        // A profile with an unset keyswitch omits the key entirely.
        Profile no_keyswitch {};
        dyn::Assign(no_keyswitch.name, "No Keyswitch");
        SaveProfile(prefs, no_keyswitch);
        auto const loaded_no_keyswitch = LookupProfile(prefs, "No Keyswitch"_s);
        REQUIRE(loaded_no_keyswitch);
        CHECK(!loaded_no_keyswitch->controls.settings.reset_keyswitch.HasValue());
        CHECK(loaded_no_keyswitch->controls.settings == PerformanceControls::Settings {});
    }

    SUBCASE("crud and enumeration") {
        prefs::Preferences prefs;
        SaveProfile(prefs, Profile {.name = {"Beta"}});
        SaveProfile(prefs, Profile {.name = {"Alpha"}});

        auto const names = ListProfileNames(prefs, tester.scratch_arena);
        CHECK_EQ(names.size, 2u);
        CHECK_EQ(names[0], "Alpha"_s);
        CHECK_EQ(names[1], "Beta"_s);

        // Saving an existing profile replaces it entirely.
        Profile alpha_v2 {.name = {"Alpha"}};
        alpha_v2.controls.settings.seed = 9;
        alpha_v2.controls.param_learned_ccs[ToInt(ParamIndex::Macro1)].Set(3);
        SaveProfile(prefs, alpha_v2);
        CHECK_EQ(ListProfileNames(prefs, tester.scratch_arena).size, 2u);
        {
            auto const alpha = LookupProfile(prefs, "Alpha"_s);
            REQUIRE(alpha);
            CHECK_EQ(alpha->controls.settings.seed, 9);
            CHECK_EQ(alpha->controls.param_learned_ccs[ToInt(ParamIndex::Macro1)].NumSet(), 1u);
        }

        RenameProfile(prefs, "Alpha", "Gamma");
        CHECK(!LookupProfile(prefs, "Alpha"_s));
        REQUIRE(LookupProfile(prefs, "Gamma"_s));

        DeleteProfile(prefs, "Beta"_s);
        CHECK_EQ(ListProfileNames(prefs, tester.scratch_arena).size, 1u);
    }

    SUBCASE("new-instance defaults") {
        prefs::Preferences prefs;
        CHECK(!LookupDefault(prefs));
        CHECK(DefaultOrFallback(prefs) == FallbackProfile());

        Profile snapshot {.name = {"Name Is Ignored"}};
        snapshot.controls.settings.seed = 5;
        snapshot.controls.param_learned_ccs[ToInt(ParamIndex::Macro1)].Set(20);
        SaveDefault(prefs, snapshot);

        auto const loaded = LookupDefault(prefs);
        REQUIRE(loaded);
        CHECK_EQ(loaded->name.size, 0u);
        CHECK(loaded->controls.settings == snapshot.controls.settings);
        CHECK(loaded->controls.param_learned_ccs == snapshot.controls.param_learned_ccs);
        CHECK(DefaultOrFallback(prefs) == *loaded);

        // Saving again replaces the snapshot entirely.
        Profile replacement {};
        replacement.controls.settings.mpe_enabled = true;
        SaveDefault(prefs, replacement);
        CHECK(DefaultOrFallback(prefs) == replacement);

        // The snapshot never appears in the profile list.
        CHECK_EQ(ListProfileNames(prefs, tester.scratch_arena).size, 0u);
    }

    SUBCASE("name validation") {
        prefs::Preferences prefs;
        SaveProfile(prefs, Profile {.name = {"Existing"}});

        CHECK(!ValidateProfileName(prefs, "Good name"_s));
        CHECK(!ValidateProfileName(prefs, "  Padded  "_s));
        CHECK(!ValidateProfileName(prefs, "Existing"_s, "Existing"_s));
        CHECK(!ValidateProfileName(prefs, "Other"_s, "Existing"_s));
        // Names are stored as values, not filenames, so path-like characters are allowed.
        CHECK(!ValidateProfileName(prefs, "path/name"_s));

        CHECK_EQ(ValidateProfileName(prefs, ""_s), NameError::Empty);
        CHECK_EQ(ValidateProfileName(prefs, "   "_s), NameError::Empty);
        CHECK_EQ(ValidateProfileName(prefs, "Bad\nname"_s), NameError::InvalidCharacters);
        CHECK_EQ(ValidateProfileName(prefs, "Existing"_s), NameError::AlreadyExists);
    }

    SUBCASE("migration") {
        auto const add_legacy_pin = [](prefs::Preferences& prefs, ParamIndex param, s64 cc) {
            prefs::AddValue(prefs,
                            prefs::SectionedKey {prefs::key::section::k_cc_to_param_id_map_section, cc},
                            (s64)ParamIndexToId(param));
        };

        SUBCASE("fresh prefs write nothing") {
            prefs::Preferences prefs;
            MigrateIfNeeded(prefs);
            CHECK(!LookupDefault(prefs));
            CHECK(DefaultOrFallback(prefs) == FallbackProfile());
            CHECK_EQ(ListProfileNames(prefs, tester.scratch_arena).size, 0u);
        }

        SUBCASE("legacy pins with toggle enabled") {
            prefs::Preferences prefs;
            add_legacy_pin(prefs, ParamIndex::MasterTimbre, 74);
            MigrateIfNeeded(prefs);

            auto const migrated = LookupDefault(prefs);
            REQUIRE(migrated);
            CHECK(migrated->controls.param_learned_ccs[ToInt(ParamIndex::MasterTimbre)].Get(74));
            for (auto const [param_index, ccs] : Enumerate(FallbackProfile().controls.param_learned_ccs))
                CHECK((migrated->controls.param_learned_ccs[param_index] & ccs) == ccs);

            // Migration never creates profiles.
            CHECK_EQ(ListProfileNames(prefs, tester.scratch_arena).size, 0u);

            // Legacy keys remain untouched.
            CHECK(prefs::LookupValues(
                prefs,
                prefs::SectionedKey {prefs::key::section::k_cc_to_param_id_map_section, (s64)74}));
        }

        SUBCASE("legacy pins with toggle disabled") {
            prefs::Preferences prefs;
            prefs::SetValue(prefs, prefs::key::k_default_cc_param_mappings, false);
            add_legacy_pin(prefs, ParamIndex::MasterTimbre, 74);
            MigrateIfNeeded(prefs);

            auto const migrated = LookupDefault(prefs);
            REQUIRE(migrated);
            CHECK(migrated->controls.param_learned_ccs[ToInt(ParamIndex::MasterTimbre)].Get(74));
            for (auto const [param_index, ccs] : Enumerate(FallbackProfile().controls.param_learned_ccs))
                CHECK(!(migrated->controls.param_learned_ccs[param_index] & ccs).AnyValuesSet());
        }

        SUBCASE("idempotent") {
            prefs::Preferences prefs;
            add_legacy_pin(prefs, ParamIndex::MasterTimbre, 74);
            MigrateIfNeeded(prefs);
            auto const first = LookupDefault(prefs);
            REQUIRE(first);
            MigrateIfNeeded(prefs);
            MigrateIfNeeded(prefs);

            auto const after = LookupDefault(prefs);
            REQUIRE(after);
            CHECK(*after == *first);
            CHECK_EQ(ListProfileNames(prefs, tester.scratch_arena).size, 0u);
        }

        SUBCASE("existing profiles prevent migration") {
            prefs::Preferences prefs;
            SaveProfile(prefs, Profile {.name = {"Custom"}});
            add_legacy_pin(prefs, ParamIndex::MasterTimbre, 74);
            MigrateIfNeeded(prefs);

            CHECK(!LookupDefault(prefs));
            CHECK_EQ(ListProfileNames(prefs, tester.scratch_arena).size, 1u);
        }

        SUBCASE("existing defaults prevent migration") {
            prefs::Preferences prefs;
            Profile existing {};
            existing.controls.settings.seed = 7;
            SaveDefault(prefs, existing);
            add_legacy_pin(prefs, ParamIndex::MasterTimbre, 74);
            MigrateIfNeeded(prefs);

            CHECK(DefaultOrFallback(prefs) == existing);
        }
    }

    return k_success;
}

} // namespace perf_profile

TEST_REGISTRATION(RegisterPerformanceProfileTests) { REGISTER_TEST(perf_profile::TestPerformanceProfiles); }
