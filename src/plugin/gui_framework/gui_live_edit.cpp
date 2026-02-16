// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_live_edit.hpp"

LiveEditGui g_live_edit_values {
    .ui_sizes =
        {
#define GUI_SIZE(cat, n, v) v,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
        },
    .ui_sizes_names =
        {
#define GUI_SIZE(cat, n, v) #n,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
        },
    .ui_col_map =
        {
#define GUI_COL_MAP(cat, n, col_id, a, dm) {Col {.c = Col::col_id, .dark_mode = dm, .alpha = a}},
#include COLOUR_MAP_DEF_FILENAME
#undef GUI_COL_MAP
        },
};
