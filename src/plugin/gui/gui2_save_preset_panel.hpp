// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common_infrastructure/preferences.hpp"

#include "gui/gui_fwd.hpp"
#include "state/state_snapshot.hpp"

struct FilePickerState;
struct Engine;
struct GuiBoxSystem;
struct Box;
struct FloePaths;

struct SavePresetPanelContext {
    Engine& engine;
    FilePickerState& file_picker_state;
    FloePaths const& paths;
    prefs::Preferences& prefs;
};

struct SavePresetPanelState {
    bool open;
    StateMetadata metadata;
    bool scroll_to_start;
    bool modeless {};
};

void OnEngineStateChange(SavePresetPanelState& state, Engine const& engine);

void DoSavePresetPanel(GuiBoxSystem& box_system,
                       SavePresetPanelContext& context,
                       SavePresetPanelState& state);

bool DoTagsGui(GuiBoxSystem& box_system,
               DynamicArrayBounded<DynamicArrayBounded<char, k_max_tag_size>, k_max_num_tags>& tags,
               Box const& root);
