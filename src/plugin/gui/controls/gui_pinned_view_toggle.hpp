// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui/core/gui_fwd.hpp"
#include "gui_framework/gui_builder.hpp"

// Two-segment pill that toggles between viewing the pinned (original) state and the user's
// modifications. Reads the engine's pinned/modified state, calls TogglePinnedView on click. The pill
// renders dimmed and inert when there's nothing to compare against (i.e. unmodified and viewing the
// pinned state).
enum class PinnedViewToggleStyle : u8 {
    // Wide form: "Original | Modified". Used in the perform panel where there's room.
    Labeled,
    // Compact form: "A | B". Used in the top panel next to undo/redo.
    Compact,
};

void DoPinnedViewToggle(GuiState& g, Box parent, PinnedViewToggleStyle style);
