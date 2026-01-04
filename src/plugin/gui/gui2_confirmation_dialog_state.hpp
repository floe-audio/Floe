// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

enum class ConfirmationDialogResult {
    Cancel,
    Ok,
};

struct ConfirmationDialogState {
    bool open {};
    DynamicArrayBounded<char, 256> title {};
    DynamicArrayBounded<char, 512> body_text {};
    TrivialFixedSizeFunction<40, void(ConfirmationDialogResult)> callback {};
};
