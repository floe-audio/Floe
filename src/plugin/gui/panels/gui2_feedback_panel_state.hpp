// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

struct FeedbackPanelState {
    static constexpr u64 k_panel_id = HashFnv1a("feedback-panel");
    DynamicArrayBounded<char, Kb(4)> description {};
    DynamicArrayBounded<char, 64> email {};
    bool send_diagnostic_data {true};
};
