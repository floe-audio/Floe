// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui/core/gui_fwd.hpp"

struct AudioProcessor;

struct InstanceConfigPanelState {
    static constexpr u64 k_panel_id = HashFnv1a("instance-config-panel");
};

struct InstanceConfigPanelContext {
    AudioProcessor& processor;
};

void DoInstanceConfigPanel(GuiBuilder& builder,
                           InstanceConfigPanelContext& context,
                           InstanceConfigPanelState& state);
