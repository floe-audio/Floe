// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/paths.hpp"

#include "engine/package_installation.hpp"
#include "gui/core/gui_file_picker.hpp"
#include "gui/core/gui_fwd.hpp"
#include "sample_lib_server/sample_library_server.hpp"

struct PreferencesPanelState {
    enum class Tab : u32 {
        General,
        Folders,
        Packages,
        Count,
    };
    static constexpr u64 k_panel_id = HashFnv1a("prefs-panel");
    Tab tab {Tab::General};
};

struct PreferencesPanelContext {
    prefs::Preferences& prefs;
    FloePaths const& paths;
    sample_lib_server::Server& sample_lib_server;
    package::InstallJobs& package_install_jobs;
    ThreadPool& thread_pool;
    FilePickerState& file_picker_state;
    PresetServer& presets_server;
    Optional<BeginReadFoldersResult> presets {};
};

void DoPreferencesPanel(GuiBuilder& builder, PreferencesPanelContext& context, PreferencesPanelState& state);
