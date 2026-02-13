// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common_infrastructure/preferences.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "gui/gui_fwd.hpp"

struct FilePickerState;
struct Engine;
struct GuiBuilder;
struct Box;
struct FloePaths;

struct SavePresetPanelContext {
    Engine& engine;
    FilePickerState& file_picker_state;
    FloePaths const& paths;
    prefs::Preferences& prefs;
};

struct SavePresetPanelState {
    static constexpr u64 k_panel_id = HashFnv1a("save-preset-panel");
    StateMetadata metadata;
    bool scroll_to_start;
    bool modeless {};
};

void OnEngineStateChange(SavePresetPanelState& state, Engine const& engine);

void DoSavePresetPanel(GuiBuilder& builder, SavePresetPanelContext& context, SavePresetPanelState& state);

bool DoTagsGui(GuiBuilder& builder,
               DynamicArrayBounded<DynamicArrayBounded<char, k_max_tag_size>, k_max_num_tags>& tags,
               Box const& root);
