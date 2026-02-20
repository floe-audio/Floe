// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "foundation/foundation.hpp"

#include "gui/panels/gui2_common_browser.hpp"

struct IrBrowserState {
    static constexpr u64 k_panel_id = SourceLocationHash();
    CommonBrowserState common_state;
    bool scroll_to_show_selected = false;
};
