// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

struct InfoPanelState {
    enum class Tab : u32 {
        Libraries,
        About,
        Metrics,
        Legal,
        Count,
    };
    bool open {};
    bool opened_before {};
    Tab tab {Tab::Libraries};
};
