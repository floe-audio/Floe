// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui_frame.hpp"
#include "gui_framework/colours.hpp"

// The live-edit system maintains global arrays of colours, colour-mapping and sizes for GUI to access
// anywhere. The data is stored in .def files which are compiled into the program. However, these .def files
// are easy to parse and write and so we can edit the global array, and write it back to the .def source file;
// we see real-time changes but also store the data in code. In production builds the live-edit nature can be
// turned off do looking up colours or sizes is just a simple, fast data lookup.
//
// This system is quite convenient but by no means perfect. The 2 main disadvantages:
// - Adding new values requires manually modifying the .def and recompiling.
// - The entire GUI codebase is littered with lookups into these tables

// The build system needs to -I the directory containing these files
#define SIZES_DEF_FILENAME      "gui_sizes.def"
#define COLOUR_MAP_DEF_FILENAME "gui_colour_map.def"

enum class UiSizeId : u16 {
#define X(cat, n, v) n,
#include SIZES_DEF_FILENAME
#undef X
    Count
};

enum class UiColMap : u16 {
#define X(cat, n, col_id, alpha, dark_mode) n,
#include COLOUR_MAP_DEF_FILENAME
#undef X
    Count
};

struct LiveEditColourMap {
    Col col;
};

struct LiveEditGui {
    f32 ui_sizes[ToInt(UiSizeId::Count)];
    String ui_sizes_names[ToInt(UiSizeId::Count)];
    LiveEditColourMap ui_col_map[ToInt(UiColMap::Count)];
};

extern LiveEditGui g_live_edit_values;

inline u32 LiveCol(UiColMap type) { return ToU32(g_live_edit_values.ui_col_map[ToInt(type)].col); }
inline Col LiveColStruct(UiColMap type) { return g_live_edit_values.ui_col_map[ToInt(type)].col; }

inline f32 LiveWw(UiSizeId size_id) { return g_live_edit_values.ui_sizes[ToInt(size_id)]; }
inline f32 LivePx(UiSizeId size_id) {
    return WwToPixels(g_live_edit_values.ui_sizes[ToInt(size_id)]);
}

inline f32 LiveRaw(UiSizeId size_id) { return g_live_edit_values.ui_sizes[ToInt(size_id)]; }
