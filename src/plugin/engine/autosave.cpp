// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autosave.hpp"

#include "tests/framework.hpp"

#include "common_infrastructure/error_reporting.hpp"

#include "state/state_coding.hpp"

constexpr auto k_autosave_filename_prefix = "autosave"_ca;

// background thread
static ErrorCodeOr<void>
CleanupExcessInstanceAutosaves(AutosaveState& state, FloePaths const& paths, ArenaAllocator& scratch_arena) {
    ZoneScoped;

    auto const wildcard = fmt::FormatInline<32>("*{}*", InstanceId(state));

    auto const entries = TRY(FindEntriesInFolder(scratch_arena,
                                                 paths.autosave_path,
                                                 {
                                                     .options {
                                                         .wildcard = wildcard,
                                                     },
                                                     .recursive = false,
                                                     .only_file_type = FileType::File,
                                                 }));

    auto const max_autosaves_per_instance = state.max_autosaves_per_instance.Load(LoadMemoryOrder::Relaxed);

    if (entries.size <= max_autosaves_per_instance) return k_success;

    struct EntryWithTime {
        dir_iterator::Entry const* entry;
        s128 last_modified_time;
    };
    auto entries_with_times = scratch_arena.AllocateExactSizeUninitialised<EntryWithTime>(entries.size);
    for (auto [i, entry] : Enumerate(entries)) {
        auto const path = path::Join(scratch_arena, Array {paths.autosave_path, entry.subpath});
        DEFER { scratch_arena.Free(path.ToByteSpan()); };
        PLACEMENT_NEW(&entries_with_times[i])
        EntryWithTime {.entry = &entry, .last_modified_time = TRY(LastModifiedTimeNsSinceEpoch(path))};
    }

    Sort(entries_with_times, [](EntryWithTime const& a, EntryWithTime const& b) {
        return a.last_modified_time < b.last_modified_time;
    });

    auto const excess_count = entries.size - max_autosaves_per_instance;
    for (auto i : Range(excess_count)) {
        auto const path =
            path::Join(scratch_arena, Array {paths.autosave_path, entries_with_times[i].entry->subpath});
        DEFER { scratch_arena.Free(path.ToByteSpan()); };
        auto _ = Delete(path, {.type = DeleteOptions::Type::File, .fail_if_not_exists = false});
    }

    return k_success;
}

// background thread
static ErrorCodeOr<void>
Autosave(AutosaveState& state, StateSnapshot const& snapshot, FloePaths const& paths) {
    ZoneScoped;
    PathArena arena {PageAllocator::Instance()};

    DynamicArrayBounded<char, 64> filename;
    {
        auto seed = RandomSeed();
        auto const date = LocalTimeNow();
        fmt::Assign(filename,
                    "{} {02}-{02}-{02} {} {} {} {} {} ({})" FLOE_PRESET_FILE_EXTENSION,
                    k_autosave_filename_prefix,
                    date.hour,
                    date.minute,
                    date.second,
                    date.DayName(),
                    date.day_of_month,
                    date.MonthName(),
                    date.year,
                    InstanceId(state),
                    RandomIntInRange<u32>(seed, 1000, 9999));
    }

    auto const path = path::Join(arena, Array {paths.autosave_path, filename});
    TRY(SavePresetFile(path, snapshot));

    return k_success;
}

// background thread
static ErrorCodeOr<void> CleanupOldAutosavesIfNeeded(FloePaths const& paths,
                                                     ArenaAllocator& scratch_arena,
                                                     u16 k_autosave_max_age_days) {
    ZoneScoped;
    constexpr auto k_wildcard =
        ConcatArrays("*"_ca, k_autosave_filename_prefix, "*"_ca, FLOE_PRESET_FILE_EXTENSION ""_ca);
    auto const entries = TRY(FindEntriesInFolder(scratch_arena,
                                                 paths.autosave_path,
                                                 {
                                                     .options {
                                                         .wildcard = k_wildcard,
                                                     },
                                                     .recursive = false,
                                                     .only_file_type = FileType::File,
                                                 }));

    for (auto const& entry : entries) {
        auto const file_time =
            TRY_OR(LastModifiedTimeNsSinceEpoch(
                       path::Join(scratch_arena, Array {paths.autosave_path, entry.subpath})),
                   continue);

        auto const now = NanosecondsSinceEpoch();

        auto const delta_ns = now - file_time;
        auto const delta_secs = delta_ns / 1'000'000'000;
        auto const max_age_secs = (s128)k_autosave_max_age_days * 24 * 60 * 60;
        if (delta_secs < max_age_secs) continue;

        auto const path = path::Join(scratch_arena, Array {paths.autosave_path, entry.subpath});
        DEFER { scratch_arena.Free(path.ToByteSpan()); };

        auto _ = Delete(path, {.type = DeleteOptions::Type::File, .fail_if_not_exists = false});
    }

    return k_success;
}

static s64 AutosaveSettingIntValue(AutosaveSetting setting, prefs::PreferencesTable const& preferences) {
    return prefs::GetValue(preferences, SettingDescriptor(setting)).value.Get<s64>();
}

void InitAutosaveState(AutosaveState& state,
                       prefs::PreferencesTable const& prefs,
                       u64& random_seed,
                       StateSnapshot const& initial_state) {
    ASSERT(g_is_logical_main_thread);
    constexpr auto k_instance_words = Array {
        "wave"_s, "pond",  "beam", "drift", "breeze", "flow",  "spark",  "glow",  "river",  "cloud",
        "stream", "rain",  "sun",  "moon",  "star",   "wind",  "storm",  "frost", "flame",  "mist",
        "ocean",  "peak",  "dawn", "dusk",  "leaf",   "stone", "spring", "sand",  "brook",  "lake",
        "cliff",  "pine",  "snow", "bird",  "reed",   "fog",   "bay",    "bloom", "branch", "creek",
        "cave",   "delta", "dew",  "elm",   "fern",   "grove", "glen",   "hill",  "isle",   "marsh",
        "meadow", "nest",  "opal", "path",  "reef",   "ridge", "sage",   "shell", "shore",  "slope",
        "swift",  "tide",  "vale", "vine",  "wood",   "ash",   "comet",  "dust",  "flash",  "haze",
        "light",  "nova",  "orb",  "plume", "ray",    "shade", "torch",  "void",  "wisp",   "zinc",
    };

    auto const word_index = RandomIntInRange<u32>(random_seed, 0, k_instance_words.size - 1);
    auto const number = RandomIntInRange<u32>(random_seed, 100, 999);
    fmt::Assign(state.instance_id, "{}-{}", k_instance_words[word_index], number);
    state.last_save_time = TimePoint::Now();
    state.state = AutosaveState::State::Saved;
    state.snapshot = initial_state;
    state.autosave_delete_after_days.raw =
        (u16)AutosaveSettingIntValue(AutosaveSetting::AutosaveDeleteAfterDays, prefs);
    state.max_autosaves_per_instance.raw =
        (u16)AutosaveSettingIntValue(AutosaveSetting::MaxAutosavesPerInstance, prefs);
}

// background thread
void AutosaveToFileIfNeeded(AutosaveState& state, FloePaths const& paths) {
    ZoneScoped;
    Optional<StateSnapshot> snapshot {};
    {
        state.mutex.Lock();
        DEFER { state.mutex.Unlock(); };
        switch (state.state) {
            case AutosaveState::State::Idle: return;
            case AutosaveState::State::SaveRequested:
                snapshot = state.snapshot;
                state.state = AutosaveState::State::Saved;
                break;
            case AutosaveState::State::Saved: return;
        }
    }
    if (snapshot) {
        TRY_OR(Autosave(state, *snapshot, paths),
               ReportError(ErrorLevel::Error, HashComptime("autosave"), "autosave failed: {}", error););
        ArenaAllocatorWithInlineStorage<1000> scratch_arena {PageAllocator::Instance()};
        static bool first_call = true;
        if (Exchange(first_call, false)) {
            TRY_OR(
                CleanupOldAutosavesIfNeeded(paths,
                                            scratch_arena,
                                            state.autosave_delete_after_days.Load(LoadMemoryOrder::Relaxed)),
                {
                    ReportError(ErrorLevel::Error,
                                HashComptime("autosave cleanup"),
                                "cleanup old autosaves failed: {}",
                                error);
                });
        }
        TRY_OR(CleanupExcessInstanceAutosaves(state, paths, scratch_arena),
               ReportError(ErrorLevel::Error,
                           HashComptime("autosave cleanup"),
                           "cleanup excess autosaves failed: {}",
                           error););
    }
}

prefs::Descriptor SettingDescriptor(AutosaveSetting setting) {
    ASSERT(g_is_logical_main_thread);
    switch (setting) {
        case AutosaveSetting::AutosaveIntervalSeconds:
            return {
                .key = "autosave-interval-seconds"_s,
                .value_requirements =
                    prefs::Descriptor::IntRequirements {
                        .validator =
                            [](s64& value) {
                                value = Clamp<s64>(value, 1, 60 * 60);
                                return true;
                            },
                    },
                .default_value = (s64)10,
                .gui_label = "Autosave interval (seconds)"_s,
                .long_description = "The interval in seconds between autosaves.",
            };
        case AutosaveSetting::MaxAutosavesPerInstance:
            return {
                .key = "max-autosaves-per-instance"_s,
                .value_requirements =
                    prefs::Descriptor::IntRequirements {
                        .validator =
                            [](s64& value) {
                                value = Clamp<s64>(value, 1, 100);
                                return true;
                            },
                    },
                .default_value = (s64)16,
                .gui_label = "Max autosaves per instance"_s,
                .long_description = "The maximum number of autosaves to keep per instance. "
                                    "Older autosaves will be deleted when this limit is reached.",
            };
        case AutosaveSetting::AutosaveDeleteAfterDays:
            return {
                .key = "autosave-delete-after-days"_s,
                .value_requirements =
                    prefs::Descriptor::IntRequirements {
                        .validator =
                            [](s64& value) {
                                value = Clamp<s64>(value, 1, 365);
                                return true;
                            },
                    },
                .default_value = (s64)7,
                .gui_label = "Autosave delete after days"_s,
                .long_description = "The number of days after which autosaves will be deleted.",
            };
        case AutosaveSetting::Count: break;
    }
    PanicIfReached();
}

void OnPreferenceChanged(AutosaveState& state, prefs::Key const& key, prefs::Value const* value) {
    ASSERT(g_is_logical_main_thread);
    for (auto const setting : EnumIterator<AutosaveSetting>()) {
        if (auto const v = prefs::Match(key, value, SettingDescriptor(setting))) {
            switch (setting) {
                case AutosaveSetting::AutosaveIntervalSeconds: break;
                case AutosaveSetting::MaxAutosavesPerInstance:
                    state.max_autosaves_per_instance.Store(CheckedCast<u16>(value->Get<s64>()),
                                                           StoreMemoryOrder::Relaxed);
                    break;
                case AutosaveSetting::AutosaveDeleteAfterDays:
                    state.autosave_delete_after_days.Store(CheckedCast<u16>(value->Get<s64>()),
                                                           StoreMemoryOrder::Relaxed);
                    break;
                case AutosaveSetting::Count: break;
            }
            return;
        }
    }
}

DynamicArrayBounded<char, k_max_instance_id_size> InstanceId(AutosaveState& state) {
    DynamicArrayBounded<char, k_max_instance_id_size> result;
    {
        state.instance_id_mutex.Lock();
        DEFER { state.instance_id_mutex.Unlock(); };
        result = state.instance_id;
    }
    return result;
}

void SetInstanceId(AutosaveState& state, String instance_id) {
    ASSERT(instance_id.size <= k_max_instance_id_size);
    ASSERT(instance_id.size);
    state.instance_id_mutex.Lock();
    DEFER { state.instance_id_mutex.Unlock(); };
    dyn::Assign(state.instance_id, instance_id);
}

bool AutosaveNeeded(AutosaveState const& state, prefs::Preferences const& preferences) {
    ASSERT(g_is_logical_main_thread);
    ZoneScoped;
    return state.last_save_time.SecondsFromNow() >=
           (f64)AutosaveSettingIntValue(AutosaveSetting::AutosaveIntervalSeconds, preferences);
}

void QueueAutosave(AutosaveState& state, StateSnapshot const& snapshot) {
    ASSERT(g_is_logical_main_thread);
    ZoneScoped;
    state.mutex.Lock();
    DEFER { state.mutex.Unlock(); };
    switch (state.state) {
        case AutosaveState::State::Idle:
        case AutosaveState::State::SaveRequested:
            state.snapshot = snapshot;
            state.state = AutosaveState::State::SaveRequested;
            break;
        case AutosaveState::State::Saved:
            // We only queue a new autosave if the snapshot has changed.
            if (state.snapshot != snapshot) {
                if constexpr (!PRODUCTION_BUILD) {
                    DynamicArrayBounded<char, Kb(4)> diff {};
                    AssignDiffDescription(diff, state.snapshot, snapshot);
                    if (EndsWith(diff, '\n')) --diff.size;
                    LogDebug(ModuleName::Main, "Autosave diff: {}", diff);
                }
                state.snapshot = snapshot;
                state.state = AutosaveState::State::SaveRequested;
            }
            break;
    }
    state.last_save_time = TimePoint::Now();
}

// Tests
// ============================================================

static String TestPresetPath(tests::Tester& tester, String filename) {
    return path::Join(tester.scratch_arena,
                      Array {TestFilesFolder(tester), tests::k_preset_test_files_subdir, filename});
}

TEST_CASE(TestAutosave) {
    AutosaveState state {};
    auto const paths = CreateFloePaths(tester.arena);
    prefs::Preferences preferences {};

    // We need to load some valid state to test autosave.
    auto snapshot =
        TRY(LoadPresetFile(TestPresetPath(tester, "sine.floe-preset"), tester.scratch_arena, false));

    InitAutosaveState(state, preferences, tester.random_seed, snapshot);

    // We don't need check the result since it's time-based and we don't want to wait in a test.
    AutosaveNeeded(state, preferences);

    // main thread
    snapshot.param_values[0] += 1;
    QueueAutosave(state, snapshot);

    // background thread
    AutosaveToFileIfNeeded(state, paths);

    // do it multiple time to check file rotation
    for (auto _ : Range(AutosaveSettingIntValue(AutosaveSetting::MaxAutosavesPerInstance, preferences) + 1)) {
        snapshot.param_values[0] += 1;
        QueueAutosave(state, snapshot);
        AutosaveToFileIfNeeded(state, paths);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterAutosaveTests) { REGISTER_TEST(TestAutosave); }
