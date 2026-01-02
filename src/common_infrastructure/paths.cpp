// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "paths.hpp"

#include "os/filesystem.hpp"

#include "error_reporting.hpp"

static Span<String> PossiblePrefFilePaths(ArenaAllocator& arena) {
    DynamicArray<String> result {arena};
    result.Reserve(4);

    // Best path
    {
        String error_log {};
        dyn::Append(result, PreferencesFilepath(&error_log));
        if (error_log.size) {
            ReportError(ErrorLevel::Warning,
                        SourceLocationHash(),
                        "Failed to get known preferences directory {}\n{}",
                        Last(result),
                        error_log);
        }
    }

    // Paths that Mirage used.
    // Some of these are actually a bit problematic for reading/writing due to permissions but it doesn't
    // matter for this case. We're just doing our best to retain any existing preferences.
    {
        auto add_path = [&](KnownDirectoryType known_dir, Span<String const> sub_paths, String filename) {
            dyn::Append(result,
                        KnownDirectoryWithSubdirectories(arena,
                                                         known_dir,
                                                         sub_paths,
                                                         filename,
                                                         {.create = false, .error_log = nullptr}));
        };

        // C:/ProgramData/FrozenPlain/Mirage/mirage.json
        // /Library/Application Support/FrozenPlain/Mirage/mirage.json
        add_path(KnownDirectoryType::MirageGlobalPreferences,
                 Array {"FrozenPlain"_s, "Mirage", "Settings"},
                 "mirage.json"_s);

        // ~/AppData/Roaming/FrozenPlain/Mirage/mirage.json
        // ~/Music/Audio Music Apps/Plug-In Settings/FrozenPlain/mirage.json
        {
            DynamicArrayBounded<String, 2> sub_paths;
            dyn::Append(sub_paths, "FrozenPlain"_s);
            if constexpr (IS_WINDOWS) dyn::Append(sub_paths, "Mirage");
            add_path(KnownDirectoryType::MiragePreferences, sub_paths, "mirage.json"_s);
        }

        // macOS had an additional possible path.
        // ~/Library/Application Support/FrozenPlain/Mirage/mirage.json
        if constexpr (IS_MACOS) {
            add_path(KnownDirectoryType::MiragePreferencesAlternate,
                     Array {"FrozenPlain"_s, "Mirage"},
                     "mirage.json");
        }
    }

    return result.ToOwnedSpan();
}

static String AlwaysScannedFolder(ScanFolderType type, ArenaAllocator& allocator, bool create) {
    auto const dir_type = ({
        FloeKnownDirectoryType d {};
        switch (type) {
            case ScanFolderType::Libraries: d = FloeKnownDirectoryType::Libraries; break;
            case ScanFolderType::Presets: d = FloeKnownDirectoryType::Presets; break;
            case ScanFolderType::Count: PanicIfReached();
        }
        d;
    });
    DynamicArrayBounded<char, 500> error_log;
    auto error_writer = dyn::WriterFor(error_log);
    auto const result =
        FloeKnownDirectory(allocator, dir_type, k_nullopt, {.create = create, .error_log = &error_writer});
    if (error_log.size) {
        ReportError(ErrorLevel::Warning,
                    HashComptime("always scanned folder") + ToInt(dir_type),
                    "Failed to get always scanned folder\n{}",
                    error_log);
    }
    return result;
}

FloePaths CreateFloePaths(ArenaAllocator& arena, bool create_folders) {
    auto const possible_prefs_paths = PossiblePrefFilePaths(arena);

    FloePaths result {
        .preferences_path = possible_prefs_paths[0],
        .possible_preferences_paths = possible_prefs_paths,
        .file_picker_last_path =
            {
                Malloc::Instance(),
                Malloc::Instance(),
            },
    };

    for (auto const type : Range(ToInt(ScanFolderType::Count)))
        result.always_scanned_folder[type] = AlwaysScannedFolder((ScanFolderType)type, arena, create_folders);

    {
        DynamicArrayBounded<char, Kb(1)> error_log;
        auto writer = dyn::WriterFor(error_log);
        result.autosave_path = FloeKnownDirectory(arena,
                                                  FloeKnownDirectoryType::Autosaves,
                                                  k_nullopt,
                                                  {.create = create_folders, .error_log = &writer});
        if (error_log.size) {
            ReportError(ErrorLevel::Warning,
                        HashComptime("autosave path"),
                        "Failed to get autosave path\n{}",
                        error_log);
        }
    }

    {
        DynamicArrayBounded<char, Kb(1)> error_log;
        auto writer = dyn::WriterFor(error_log);
        result.persistent_store_path =
            KnownDirectoryWithSubdirectories(arena,
                                             KnownDirectoryType::UserData,
                                             Array {"Floe"_s},
                                             "persistent_store",
                                             {.create = create_folders, .error_log = &writer});
        if (error_log.size) {
            ReportError(ErrorLevel::Warning,
                        HashComptime("persistent store path"),
                        "Failed to get persistent store path {}\n{}",
                        result.persistent_store_path,
                        error_log);
        }
    }

    return result;
}

prefs::Descriptor ExtraScanFolderDescriptor(FloePaths const& paths, ScanFolderType type) {
    prefs::Descriptor result {
        .value_requirements =
            prefs::Descriptor::StringRequirements {
                .validator =
                    [&paths, type](String& value) {
                        if (value.size > 8000) return false;
                        if (!path::IsAbsolute(value)) return false;
                        if (!IsValidUtf8(value)) return false;
                        if (path::Equal(value, paths.always_scanned_folder[ToInt(type)])) return false;
                        value = path::TrimDirectorySeparatorsEnd(value);
                        return true;
                    },
            },
        .default_value = String {nullptr, 0xdeadc0de},
    };

    switch (type) {
        case ScanFolderType::Presets:
            result.key = prefs::key::k_extra_presets_folder;
            result.gui_label = "Extra Presets Folder"_s;
            break;
        case ScanFolderType::Libraries:
            result.key = prefs::key::k_extra_libraries_folder;
            result.gui_label = "Extra Libraries Folder"_s;
            break;
        case ScanFolderType::Count: PanicIfReached(); break;
    }

    return result;
}

prefs::Descriptor
InstallLocationDescriptor(FloePaths const& paths, prefs::PreferencesTable const& prefs, ScanFolderType type) {
    prefs::Descriptor result {
        .value_requirements =
            prefs::Descriptor::StringRequirements {
                .validator =
                    [&paths, &prefs, type](String& value) {
                        // Reject known invalid values
                        if (value.size > 8000) return false;
                        if (!path::IsAbsolute(value)) return false;
                        if (!IsValidUtf8(value)) return false;

                        // We require install locations to be part of the known folders.
                        if (value == paths.always_scanned_folder[ToInt(type)]) return true;
                        for (auto const& extra_folder : ExtraScanFolders(paths, prefs, type))
                            if (path::Equal(value, extra_folder)) return true;

                        // If we've got here, the path is not part of any of our known folders. We reject the
                        // value.
                        return false;
                    },
            },
        .default_value = paths.always_scanned_folder[ToInt(type)],
    };

    switch (type) {
        case ScanFolderType::Presets:
            result.key = "presets-install-location"_s;
            result.gui_label = "Presets Install Location"_s;
            break;
        case ScanFolderType::Libraries:
            result.key = "libraries-install-location"_s;
            result.gui_label = "Libraries Install Location"_s;
            break;
        case ScanFolderType::Count: PanicIfReached(); break;
    }

    return result;
}
