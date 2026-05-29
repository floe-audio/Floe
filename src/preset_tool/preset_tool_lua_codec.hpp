// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/state/state_snapshot.hpp"

struct lua_State;

struct BuildPresetLuaTableOptions {
    // In example mode, each handler emits only the first entry of any long array/dict — the goal is to
    // show shape, not enumerate every value. Not round-trippable; for documentation output only.
    bool example_mode = false;
    char const* global_name = "preset";
};

void BuildPresetLuaTable(lua_State* lua,
                         StateSnapshot const& preset_state,
                         BuildPresetLuaTableOptions options);
void ExtractPresetFromLuaTable(lua_State* lua, int table_index, StateSnapshot& preset_state);
