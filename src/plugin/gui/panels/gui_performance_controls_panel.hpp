// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/performance_profile.hpp"
#include "common_infrastructure/preferences.hpp"

#include "gui/core/gui_fwd.hpp"
#include "gui/overlays/gui_confirmation_dialog.hpp"

struct AudioProcessor;

struct PerformanceControlsPanelState {
    static constexpr u64 k_panel_id = HashFnv1a("performance-controls-panel");

    enum class Tab : u8 { MidiCc, Reproducibility, Mpe, Count };
    enum class TextEntryMode : u8 { None, SaveAsNew, Rename };

    Tab tab {Tab::MidiCc};

    // Cached copy of the settings new instances start with, so the panel can show whether the current
    // settings match them without re-reading the file every frame. Loaded when the panel opens, updated when
    // we save, discarded when the panel closes.
    Optional<perf_profile::Profile> new_instance_defaults_cache {};

    TextEntryMode text_entry_mode {};
    // Only meaningful while text_entry_mode == Rename: the profile the rename applies to.
    DynamicArrayBounded<char, perf_profile::k_max_name_size> rename_target {};
    DynamicArrayBounded<char, perf_profile::k_max_name_size> name_input {};
    Optional<perf_profile::NameError> name_error {};

    // The "add a new MIDI CC assignment" control at the bottom of the MIDI CC tab.
    bool add_cc_expanded {};
    Optional<ParamIndex> add_cc_param {};
    DynamicArrayBounded<char, 100> add_cc_param_search {};
    s64 add_cc_number {1};
};

struct PerformanceControlsPanelContext {
    AudioProcessor& processor;
    prefs::Preferences& prefs;
    ConfirmationDialogState& confirmation_dialog_state;
};

void DoPerformanceControlsPanel(GuiBuilder& builder,
                                PerformanceControlsPanelContext& context,
                                PerformanceControlsPanelState& state);
