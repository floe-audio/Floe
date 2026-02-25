// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_live_edit.hpp"

LiveEditGui g_live_edit_values {
    .ui_col_map =
        {
#define X(cat, n, col_id, a, dm) {Col {.c = Col::col_id, .dark_mode = dm, .alpha = a}},
#include COLOUR_MAP_DEF_FILENAME
#undef X
        },
};
