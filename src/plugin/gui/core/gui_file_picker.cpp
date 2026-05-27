// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_file_picker.hpp"

void OpenFilePickerAddExtraScanFolders(FilePickerState& state,
                                       prefs::Preferences const& prefs,
                                       FloePaths const& paths,
                                       AddScanFolderFilePickerState data) {
    Optional<String> default_folder {};
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

void OpenFilePickerInstallPackage(FilePickerState& state) {
    static constexpr auto k_package_wildcards = Array {"*.floe-pkg"_s, "*.floe-pkg-enc"_s, "*.zip"_s};
    static constexpr auto k_filters = ArrayT<FilePickerDialogOptions::FileFilter>({
        {
            .description = "Floe Package"_s,
            .wildcard_filters = k_package_wildcards,
        },
    });

    auto& out = GuiIo().out;

    out.file_picker_dialog = FilePickerDialogOptions {
        .type = FilePickerDialogOptions::Type::OpenFile,
        .title = "Select 1 or more Floe Package",
        .default_folder =
            KnownDirectory(out.file_picker_options_arena, KnownDirectoryType::Downloads, {.create = false}),
        .filters = k_filters,
        .allow_multiple_selection = true,
    };

    state.data = FilePickerStateType::InstallPackage;
}

constexpr u64 k_save_preset_last_path_store_id = HashFnv1a("file_picker.save_preset_last_path");
constexpr u64 k_load_preset_last_path_store_id = HashFnv1a("file_picker.load_preset_last_path");

static u64 PresetLastPathStoreId(PresetFilePickerMode mode) {
    switch (mode) {
        case PresetFilePickerMode::Save: return k_save_preset_last_path_store_id;
        case PresetFilePickerMode::Load: return k_load_preset_last_path_store_id;
        case PresetFilePickerMode::Count: PanicIfReached();
    }
    return 0;
}

static String PresetFileDefaultPath(FloePaths const& paths, PresetFilePickerMode mode) {
    String result = paths.always_scanned_folder[ToInt(ScanFolderType::Presets)];

    if (auto const& last_path = paths.file_picker_last_path[ToInt(mode)]; last_path.size)
        if (auto const dir = path::Directory(last_path)) result = *dir;

    ASSERT(path::IsAbsolute(result));

    return result;
}

static Optional<String> PersistedPresetFolder(persistent_store::Store& store, PresetFilePickerMode mode) {
    auto const r = persistent_store::Get(store, PresetLastPathStoreId(mode));
    if (r.tag != persistent_store::GetResult::Found) return k_nullopt;
    auto const& data = r.Get<persistent_store::Value const*>()->data;
    String const stored {(char const*)data.data, data.size};
    if (!path::IsAbsolute(stored)) return k_nullopt;
    return stored;
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

    auto const persisted = PersistedPresetFolder(store, PresetFilePickerMode::Save);
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
            .default_folder = PersistedPresetFolder(store, PresetFilePickerMode::Load)
                                  .ValueOr(PresetFileDefaultPath(paths, PresetFilePickerMode::Load)),
            .filters = k_filters,
            .allow_multiple_selection = false,
            .force_default_folder = true,
        }
            .Clone(out.file_picker_options_arena);

    state.data = FilePickerStateType::LoadPreset;
}

static void
RememberPresetPath(FilePickerContext const& context, String picked_path, PresetFilePickerMode mode) {
    dyn::Assign(context.paths.file_picker_last_path[ToInt(mode)], picked_path);
    auto const dir = path::Directory(picked_path);
    if (!dir) return;
    auto& store = context.engine.shared_engine_systems.persistent_store;
    auto const id = PresetLastPathStoreId(mode);
    persistent_store::RemoveValue(store, id, k_nullopt);
    persistent_store::AddValue(store, id, Span<u8 const> {(u8 const*)dir->data, dir->size});
}

void CheckForFilePickerResults(GuiFrameInput const& frame_input,
                               FilePickerState& state,
                               FilePickerContext const& context) {
    if (frame_input.file_picker_results.size == 0) return;
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
            break;
        }
        case FilePickerStateType::SavePreset: {
            auto const& saved_path = frame_input.file_picker_results.first->data;
            RememberPresetPath(context, saved_path, PresetFilePickerMode::Save);
            SaveCurrentStateToFile(context.engine, saved_path);
            break;
        }
        case FilePickerStateType::LoadPreset: {
            auto const& loaded_path = frame_input.file_picker_results.first->data;
            RememberPresetPath(context, loaded_path, PresetFilePickerMode::Load);
            LoadPresetFromFile(context.engine, loaded_path);
            break;
        }
    }
    state.data = FilePickerStateType::None;
}
