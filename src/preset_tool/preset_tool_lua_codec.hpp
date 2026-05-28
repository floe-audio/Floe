// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/state/state_snapshot.hpp"

struct lua_State;

void BuildPresetLuaTable(lua_State* lua, StateSnapshot const& preset_state, bool pretty);
void ExtractPresetFromLuaTable(lua_State* lua, int table_index, StateSnapshot& preset_state);
