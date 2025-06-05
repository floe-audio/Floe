// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "os/misc.hpp"

#include "gui/gui2_common_picker.hpp"
#include "gui/gui_fwd.hpp"
#include "preset_server/preset_server.hpp"

struct GuiBoxSystem;
struct PresetServer;

// Ephemeral
struct PresetPickerContext {
    void Init(ArenaAllocator& arena) {
        if (init++) return;
        libraries = sample_lib_server::AllLibrariesRetained(sample_library_server, arena);
        Sort(libraries, [](auto const& a, auto const& b) { return a->name < b->name; });
        presets_snapshot = BeginReadFolders(preset_server, arena);
    }
    void Deinit() {
        if (--init != 0) return;
        EndReadFolders(preset_server);
        sample_lib_server::ReleaseAll(libraries);
    }

    sample_lib_server::Server& sample_library_server;
    PresetServer& preset_server;
    LibraryImagesArray& library_images;
    Engine& engine;
    Optional<graphics::ImageID> unknown_library_icon;

    u32 init = 0;
    Span<sample_lib_server::RefCounted<sample_lib::Library>> libraries;
    PresetsSnapshot presets_snapshot;
    PresetFolder::Preset const* hovering_preset = nullptr;
};

// Persistent
struct PresetPickerState {
    DynamicArray<u64> selected_author_hashes {Malloc::Instance()};
    bool scroll_to_show_selected = false;

    // This is contains PresetFormat as u64. We use a dynamic array of u64 so we can share the same code as
    // the other types of selected_* filters.
    DynamicArray<u64> selected_preset_types {Malloc::Instance()};

    CommonPickerState common_state {
        .other_selected_hashes = Array {&selected_author_hashes, &selected_preset_types},
    };
};

void LoadAdjacentPreset(PresetPickerContext const& context,
                        PresetPickerState& state,
                        SearchDirection direction);

void LoadRandomPreset(PresetPickerContext const& context, PresetPickerState& state);

void DoPresetPicker(GuiBoxSystem& box_system,
                    imgui::Id popup_id,
                    Rect absolute_button_rect,
                    PresetPickerContext& context,
                    PresetPickerState& state);
