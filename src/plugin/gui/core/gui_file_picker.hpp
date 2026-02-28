// Copyright 2025-2026 Sam Windell
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
    FilePickerUnion data;
};

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

void OpenFilePickerAddExtraScanFolders(FilePickerState& state,
                                       prefs::Preferences const& prefs,
                                       FloePaths const& paths,
                                       AddScanFolderFilePickerState data);

void OpenFilePickerInstallPackage(FilePickerState& state);

void OpenFilePickerSavePreset(FilePickerState& state, FloePaths const& paths);

void OpenFilePickerLoadPreset(FilePickerState& state, FloePaths const& paths);

void CheckForFilePickerResults(GuiFrameInput const& frame_input,
                               FilePickerState& state,
                               FilePickerContext const& context);
