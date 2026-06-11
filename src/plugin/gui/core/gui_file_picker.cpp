// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_file_picker.hpp"

constexpr u64 k_save_preset_last_path_store_id = HashFnv1a("file_picker.save_preset_last_path");
constexpr u64 k_load_preset_last_path_store_id = HashFnv1a("file_picker.load_preset_last_path");
constexpr u64 k_install_package_last_path_store_id = HashFnv1a("file_picker.install_package_last_path");
constexpr u64 k_add_scan_libs_last_path_store_id =
    HashFnv1a("file_picker.add_scan_folder_libraries_last_path");
constexpr u64 k_add_scan_presets_last_path_store_id =
    HashFnv1a("file_picker.add_scan_folder_presets_last_path");

static u64 PresetLastPathStoreId(PresetFilePickerMode mode) {
    switch (mode) {
        case PresetFilePickerMode::Save: return k_save_preset_last_path_store_id;
        case PresetFilePickerMode::Load: return k_load_preset_last_path_store_id;
        case PresetFilePickerMode::Count: PanicIfReached();
    }
    return 0;
}

static u64 AddScanFolderStoreId(ScanFolderType type) {
    switch (type) {
        case ScanFolderType::Libraries: return k_add_scan_libs_last_path_store_id;
        case ScanFolderType::Presets: return k_add_scan_presets_last_path_store_id;
        case ScanFolderType::Count: PanicIfReached();
    }
    return 0;
}

static Optional<String> PersistedFolder(persistent_store::Store& store, u64 id) {
    auto const r = persistent_store::Get(store, id);
    if (r.tag != persistent_store::GetResult::Found) return k_nullopt;
    auto const& data = r.Get<persistent_store::Value const*>()->data;
    String const stored {(char const*)data.data, data.size};
    if (!path::IsAbsolute(stored)) return k_nullopt;
    return stored;
}

static void RememberFolder(persistent_store::Store& store, u64 id, String folder) {
    if (!folder.size) return;
    persistent_store::RemoveValue(store, id, k_nullopt);
    persistent_store::AddValue(store, id, Span<u8 const> {(u8 const*)folder.data, folder.size});
}

static void RememberPickedFileFolder(persistent_store::Store& store, u64 id, String picked_file_path) {
    auto const dir = path::Directory(picked_file_path);
    if (!dir) return;
    RememberFolder(store, id, *dir);
}

void OpenFilePickerAddExtraScanFolders(FilePickerState& state,
                                       prefs::Preferences const& prefs,
                                       FloePaths const& paths,
                                       persistent_store::Store& store,
                                       AddScanFolderFilePickerState data) {
    Optional<String> default_folder = PersistedFolder(store, AddScanFolderStoreId(data.type));
    if (!default_folder)
        if (auto const extra_paths = ExtraScanFolders(paths, prefs, data.type); extra_paths.size)
            default_folder = extra_paths[0];

    auto& out = GuiIo().out;

    out.file_picker_dialog =
        FilePickerDialogOptions {
            .type = FilePickerDialogOptions::Type::SelectFolder,
            .title = ({
                String s {};
                switch (data.type) {
                    case ScanFolderType::Libraries: s = "Select Libraries Folder"; break;
                    case ScanFolderType::Presets: s = "Select Presets Folder"; break;
                    case ScanFolderType::Count: PanicIfReached();
                }
                s;
            }),
            .default_folder = default_folder,
            .filters = {},
            .allow_multiple_selection = true,
        }
            .Clone(out.file_picker_options_arena);

    state.data = data;
}

void OpenFilePickerInstallPackage(FilePickerState& state, persistent_store::Store& store) {
    static constexpr auto k_package_wildcards = Array {"*.floe-pkg"_s, "*.floe-pkg-enc"_s, "*.zip"_s};
    static constexpr auto k_filters = ArrayT<FilePickerDialogOptions::FileFilter>({
        {
            .description = "Floe Package"_s,
            .wildcard_filters = k_package_wildcards,
        },
    });

    auto& out = GuiIo().out;

    String const default_folder = PersistedFolder(store, k_install_package_last_path_store_id)
                                      .ValueOr(KnownDirectory(out.file_picker_options_arena,
                                                              KnownDirectoryType::Downloads,
                                                              {.create = false}));
    out.file_picker_dialog = FilePickerDialogOptions {
        .type = FilePickerDialogOptions::Type::OpenFile,
        .title = "Select 1 or more Floe Package",
        .default_folder = default_folder,
        .filters = k_filters,
        .allow_multiple_selection = true,
    };

    state.data = FilePickerStateType::InstallPackage;
}

static String FirstTimeSavePresetFolder(FloePaths const& paths, ArenaAllocator& arena) {
    auto const base = paths.always_scanned_folder[ToInt(ScanFolderType::Presets)];
    auto const user_dir = path::Join(arena, Array {base, "User"_s});
    auto _ = CreateDirectory(user_dir, {.create_intermediate_directories = true});
    return user_dir;
}

void OpenFilePickerSavePreset(FilePickerState& state,
                              FloePaths const& paths,
                              persistent_store::Store& store) {
    static constexpr auto k_save_preset_wildcards = Array {String {"*" FLOE_PRESET_FILE_EXTENSION}};
    static constexpr auto k_filters = ArrayT<FilePickerDialogOptions::FileFilter>({
        {
            .description = "Floe Preset"_s,
            .wildcard_filters = k_save_preset_wildcards,
        },
    });

    auto& out = GuiIo().out;

    auto const persisted = PersistedFolder(store, PresetLastPathStoreId(PresetFilePickerMode::Save));
    auto const default_folder =
        persisted ? *persisted : FirstTimeSavePresetFolder(paths, out.file_picker_options_arena);

    out.file_picker_dialog =
        FilePickerDialogOptions {
            .type = FilePickerDialogOptions::Type::SaveFile,
            .title = "Save Floe Preset",
            .default_folder = default_folder,
            .default_filename = "untitled" FLOE_PRESET_FILE_EXTENSION,
            .filters = k_filters,
            .allow_multiple_selection = false,
            .force_default_folder = true,
        }
            .Clone(out.file_picker_options_arena);

    state.data = FilePickerStateType::SavePreset;
}

void OpenFilePickerLoadPreset(FilePickerState& state,
                              FloePaths const& paths,
                              persistent_store::Store& store) {
    static constexpr auto k_preset_wildcards = Array {"*.floe-*"_s, "*.mirage-*"_s};
    static constexpr auto k_filters = ArrayT<FilePickerDialogOptions::FileFilter>({
        {
            .description = "Floe Preset"_s,
            .wildcard_filters = k_preset_wildcards,
        },
    });

    auto& out = GuiIo().out;

    out.file_picker_dialog =
        FilePickerDialogOptions {
            .type = FilePickerDialogOptions::Type::OpenFile,
            .title = "Load Floe Preset",
            .default_folder = PersistedFolder(store, PresetLastPathStoreId(PresetFilePickerMode::Load))
                                  .ValueOr(paths.always_scanned_folder[ToInt(ScanFolderType::Presets)]),
            .filters = k_filters,
            .allow_multiple_selection = false,
            .force_default_folder = true,
        }
            .Clone(out.file_picker_options_arena);

    state.data = FilePickerStateType::LoadPreset;
}

void CheckForFilePickerResults(GuiFrameInput const& frame_input,
                               FilePickerState& state,
                               FilePickerContext const& context) {
    if (frame_input.file_picker_results.size == 0) return;
    auto& store = context.engine.shared_engine_systems.persistent_store;
    switch (state.data.tag) {
        case FilePickerStateType::None: break;
        case FilePickerStateType::AddScanFolder: {
            auto const data = state.data.Get<AddScanFolderFilePickerState>();
            for (auto const p : frame_input.file_picker_results) {
                if constexpr (!IS_LINUX) ASSERT(IsValidUtf8(p));
                prefs::AddValue(context.prefs,
                                ExtraScanFolderDescriptor(context.paths, data.type),
                                (String)p);
            }
            if (data.set_as_install_folder)
                prefs::SetValue(context.prefs,
                                InstallLocationDescriptor(context.paths, context.prefs, data.type),
                                (String)frame_input.file_picker_results.first->data);
            RememberFolder(store,
                           AddScanFolderStoreId(data.type),
                           frame_input.file_picker_results.first->data);
            break;
        }
        case FilePickerStateType::InstallPackage: {
            for (auto const path : frame_input.file_picker_results) {
                package::AddJob(context.package_install_jobs,
                                path,
                                context.prefs,
                                context.paths,
                                context.sample_lib_server,
                                context.preset_server);
            }
            RememberPickedFileFolder(store,
                                     k_install_package_last_path_store_id,
                                     frame_input.file_picker_results.first->data);
            break;
        }
        case FilePickerStateType::SavePreset: {
            auto const& saved_path = frame_input.file_picker_results.first->data;
            RememberPickedFileFolder(store, PresetLastPathStoreId(PresetFilePickerMode::Save), saved_path);
            SaveCurrentStateToFile(context.engine, saved_path);
            break;
        }
        case FilePickerStateType::LoadPreset: {
            auto const& loaded_path = frame_input.file_picker_results.first->data;
            RememberPickedFileFolder(store, PresetLastPathStoreId(PresetFilePickerMode::Load), loaded_path);
            LoadPresetFromFile(context.engine, loaded_path);
            break;
        }
    }
    state.data = FilePickerStateType::None;
}
