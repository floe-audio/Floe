// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "constants.hpp"
#include "preferences.hpp"

enum class ScanFolderType : u8 { Presets, Libraries, Count };

enum class PresetFilePickerMode : u8 { Load, Save, Count };

struct FloePaths {
    String always_scanned_folder[ToInt(ScanFolderType::Count)];
    String preferences_path; // path to write to
    Span<String> possible_preferences_paths; // sorted. the first is recommended path to read
    String autosave_path;
    String persistent_store_path;
    Array<DynamicArray<char>, ToInt(PresetFilePickerMode::Count)> file_picker_last_path;
};

FloePaths CreateFloePaths(ArenaAllocator& arena, bool create_folders);

// String. Use this with prefs::GetString and prefs::SetValue
prefs::Descriptor
InstallLocationDescriptor(FloePaths const& paths, prefs::PreferencesTable const& prefs, ScanFolderType type);

// String list. Use this with prefs::GetValues and prefs::AddValue, prefs::RemoveValue
prefs::Descriptor ExtraScanFolderDescriptor(FloePaths const& paths, ScanFolderType type);

inline DynamicArrayBounded<String, k_max_extra_scan_folders>
ExtraScanFolders(FloePaths const& paths, prefs::PreferencesTable const& prefs, ScanFolderType type) {
    return prefs::GetValues<String, k_max_extra_scan_folders>(prefs, ExtraScanFolderDescriptor(paths, type));
}
