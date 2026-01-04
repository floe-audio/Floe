// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/paths.hpp"
#include "common_infrastructure/state/state_coding.hpp"

#include "engine/engine.hpp"
#include "engine/package_installation.hpp"
#include "gui_framework/gui_frame.hpp"

struct AddScanFolderFilePickerState {
    ScanFolderType type {};
    bool set_as_install_folder {};
};

enum class FilePickerStateType {
    None,
    AddScanFolder,
    InstallPackage,
    SavePreset,
    LoadPreset,
};

using FilePickerUnion =
    TaggedUnion<FilePickerStateType,
                TypeAndTag<AddScanFolderFilePickerState, FilePickerStateType::AddScanFolder>>;

struct FilePickerState {
    ArenaAllocator arena {Malloc::Instance(), 256};
    FilePickerUnion data;
};

PUBLIC void OpenFilePickerAddExtraScanFolders(FilePickerState& state,
                                              GuiFrameResult& frame_result,
                                              prefs::Preferences const& prefs,
                                              FloePaths const& paths,
                                              AddScanFolderFilePickerState data) {
    state.arena.ResetCursorAndConsolidateRegions();
    Optional<String> default_folder {};
    if (auto const extra_paths = ExtraScanFolders(paths, prefs, data.type); extra_paths.size)
        default_folder = state.arena.Clone(extra_paths[0]);

    frame_result.file_picker_dialog = FilePickerDialogOptions {
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
    };

    state.data = data;
}

PUBLIC void OpenFilePickerInstallPackage(FilePickerState& state, GuiFrameResult& frame_result) {
    state.arena.ResetCursorAndConsolidateRegions();

    static constexpr auto k_filters = ArrayT<FilePickerDialogOptions::FileFilter>({
        {
            .description = "Floe Package"_s,
            .wildcard_filter = "*.zip"_s,
        },
    });

    frame_result.file_picker_dialog = FilePickerDialogOptions {
        .type = FilePickerDialogOptions::Type::OpenFile,
        .title = "Select 1 or more Floe Package",
        .default_folder = KnownDirectory(state.arena, KnownDirectoryType::Downloads, {.create = false}),
        .filters = k_filters,
        .allow_multiple_selection = true,
    };

    state.data = FilePickerStateType::InstallPackage;
}

static String PresetFileDefaultPath(FloePaths const& paths, PresetFilePickerMode mode) {
    String result = paths.always_scanned_folder[ToInt(ScanFolderType::Presets)];

    if (auto const& last_path = paths.file_picker_last_path[ToInt(mode)]; last_path.size)
        if (auto const dir = path::Directory(last_path)) result = *dir;

    ASSERT(path::IsAbsolute(result));

    return result;
}

PUBLIC void
OpenFilePickerSavePreset(FilePickerState& state, GuiFrameResult& frame_result, FloePaths const& paths) {
    state.arena.ResetCursorAndConsolidateRegions();

    static constexpr auto k_filters = ArrayT<FilePickerDialogOptions::FileFilter>({
        {
            .description = "Floe Preset"_s,
            .wildcard_filter = "*" FLOE_PRESET_FILE_EXTENSION,
        },
    });

    frame_result.file_picker_dialog = FilePickerDialogOptions {
        .type = FilePickerDialogOptions::Type::SaveFile,
        .title = "Save Floe Preset",
        .default_folder = PresetFileDefaultPath(paths, PresetFilePickerMode::Save),
        .default_filename = "untitled" FLOE_PRESET_FILE_EXTENSION,
        .filters = k_filters,
        .allow_multiple_selection = false,
    };

    state.data = FilePickerStateType::SavePreset;
}

PUBLIC void
OpenFilePickerLoadPreset(FilePickerState& state, GuiFrameResult& frame_result, FloePaths const& paths) {
    state.arena.ResetCursorAndConsolidateRegions();
    static constexpr auto k_filters = ArrayT<FilePickerDialogOptions::FileFilter>({
        {
            .description = "Floe Preset"_s,
            .wildcard_filter = "*.floe-*"_s,
        },
        {
            .description = "Mirage Preset"_s,
            .wildcard_filter = "*.mirage-*"_s,
        },
    });

    frame_result.file_picker_dialog = FilePickerDialogOptions {
        .type = FilePickerDialogOptions::Type::OpenFile,
        .title = "Load Floe Preset",
        .default_folder = PresetFileDefaultPath(paths, PresetFilePickerMode::Load),
        .filters = k_filters,
        .allow_multiple_selection = false,
    };

    state.data = FilePickerStateType::LoadPreset;
}

// Ephemeral
struct FilePickerContext {
    prefs::Preferences& prefs;
    FloePaths& paths;
    package::InstallJobs& package_install_jobs;
    ThreadPool& thread_pool;
    ArenaAllocator& scratch_arena;
    sample_lib_server::Server& sample_lib_server;
    PresetServer& preset_server;
    Engine& engine;
};

PUBLIC void CheckForFilePickerResults(GuiFrameInput const& frame_input,
                                      FilePickerState& state,
                                      FilePickerContext const& context) {
    if (frame_input.file_picker_results.size == 0) return;
    switch (state.data.tag) {
        case FilePickerStateType::None: break;
        case FilePickerStateType::AddScanFolder: {
            auto const data = state.data.Get<AddScanFolderFilePickerState>();
            for (auto const p : frame_input.file_picker_results) {
                ASSERT(IsValidUtf8(p));
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
            dyn::Assign(context.paths.file_picker_last_path[ToInt(PresetFilePickerMode::Save)],
                        frame_input.file_picker_results.first->data);
            SaveCurrentStateToFile(context.engine, frame_input.file_picker_results.first->data);
            break;
        }
        case FilePickerStateType::LoadPreset: {
            dyn::Assign(context.paths.file_picker_last_path[ToInt(PresetFilePickerMode::Load)],
                        frame_input.file_picker_results.first->data);
            LoadPresetFromFile(context.engine, frame_input.file_picker_results.first->data);
            break;
        }
    }
    state.data = FilePickerStateType::None;
}
