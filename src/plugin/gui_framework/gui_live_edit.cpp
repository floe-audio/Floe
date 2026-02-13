// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_live_edit.hpp"

bool g_high_contrast_gui = false;

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
    .ui_cols =
        {
#define GUI_COL(name, val, based_on, bright, alpha) {String(name), val, String(based_on), bright, alpha},
#include COLOURS_DEF_FILENAME
#undef GUI_COL
        },
    .ui_col_map =
        {
#define GUI_COL_MAP(cat, n, col, high_contrast_col) {String(col), String(high_contrast_col)},
#include COLOUR_MAP_DEF_FILENAME
#undef GUI_COL_MAP
        },
};
