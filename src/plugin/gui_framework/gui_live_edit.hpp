// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui_frame.hpp"

// The live-edit system maintains global arrays of colours, colour-mapping and sizes for GUI to access
// anywhere. The data is stored in .def files which are compiled into the program. However, these .def files
// are easy to parse and write and so we can edit the global array, and write it back to the .def source file;
// we see real-time changes but also store the data in code. In production builds the live-edit nature can be
// turned off do looking up colours or sizes is just a simple, fast data lookup.
//
// This system is quite convenient but by no means perfect. The 2 main disadvantages:
// - Adding new values requires manually modifying the .def and recompiling.
// - It's inevitable that the .def fills up with values not longer used in the codebase.
// - The entire GUI codecase is littered with lookups into these tables

// The build system needs to -I the directory containing these files
#define COLOURS_DEF_FILENAME    "gui_colours.def"
#define SIZES_DEF_FILENAME      "gui_sizes.def"
#define COLOUR_MAP_DEF_FILENAME "gui_colour_map.def"

enum class UiSizeId : u16 {
#define GUI_SIZE(cat, n, v) cat##n,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
    Count
};

static constexpr int k_max_num_colours = 74;

enum class UiColMap : u16 {
#define GUI_COL_MAP(cat, n, v, high_contrast_col) cat##n,
#include COLOUR_MAP_DEF_FILENAME
#undef GUI_COL_MAP
    Count
};

struct ColourString {
    constexpr ColourString() = default;
    constexpr ColourString(String s) : size(s.size) { __builtin_memcpy(data, s.data, s.size); }
    constexpr operator String() const { return {data, size}; }
    void NullTerminate() {
        ASSERT(size < ArraySize(data));
        data[size] = 0;
    }
    usize size {};
    char data[30] {};
};

struct LiveEditColour {
    ColourString name {};
    u32 col {};

    ColourString based_on {}; // empty for disabled
    f32 with_brightness = 0; // valid if based_on is not empty. 0 to disable
    f32 with_alpha = 0; // valid if based_on is not empty. 0 to disable
};

struct LiveEditColourMap {
    ColourString colour;
    ColourString high_contrast_colour;
};

struct LiveEditGui {
    f32 ui_sizes[ToInt(UiSizeId::Count)];
    String ui_sizes_names[ToInt(UiSizeId::Count)];
    LiveEditColour ui_cols[k_max_num_colours];
    LiveEditColourMap ui_col_map[ToInt(UiColMap::Count)];
};

extern LiveEditGui g_live_edit_values;

extern bool g_high_contrast_gui; // IMPROVE: this is hacky

// IMPROVE: separate out possibly constexpr lookup from runtime calculation for better performance
inline u32 LiveCol(UiColMap type) {
    auto const map_index = ToInt(type);

    String const col_string =
        (g_high_contrast_gui && g_live_edit_values.ui_col_map[map_index].high_contrast_colour.size)
            ? g_live_edit_values.ui_col_map[map_index].high_contrast_colour
            : g_live_edit_values.ui_col_map[map_index].colour;

    // NOTE: linear search but probably ok
    for (auto const i : Range(k_max_num_colours))
        if (String(g_live_edit_values.ui_cols[i].name) == col_string)
            return g_live_edit_values.ui_cols[i].col;

    return {};
}

// Returns pixels
inline f32 LiveSize(UiSizeId size_id) {
    return GuiIo().WwToPixels(g_live_edit_values.ui_sizes[ToInt(size_id)]);
}

// Returns WW
inline f32 LiveWw(UiSizeId size_id) { return g_live_edit_values.ui_sizes[ToInt(size_id)]; }

inline f32 LiveRaw(UiSizeId size_id) { return g_live_edit_values.ui_sizes[ToInt(size_id)]; }
