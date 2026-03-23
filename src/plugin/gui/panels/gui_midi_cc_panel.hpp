// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/preferences.hpp"

#include "gui/core/gui_fwd.hpp"

struct AudioProcessor;

struct MidiCcPanelState {
    static constexpr u64 k_panel_id = HashFnv1a("midi-cc-panel");
};

struct MidiCcPanelContext {
    AudioProcessor& processor;
    prefs::Preferences& prefs;
};

void DoMidiCcPanel(GuiBuilder& builder, MidiCcPanelContext& context, MidiCcPanelState& state);
