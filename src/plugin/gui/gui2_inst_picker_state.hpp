// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui2_common_picker.hpp"

struct InstPickerState {
    CommonPickerState common_state {};
    bool scroll_to_show_selected = false;
};
