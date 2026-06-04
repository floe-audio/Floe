// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/state/state_coding.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

struct lua_State;

struct BuildPresetLuaTableOptions {
    char const* global_name = "preset";
    Optional<StateVersions> file_versions {};
};

void BuildPresetLuaTable(lua_State* lua,
                         StateSnapshot const& preset_state,
                         BuildPresetLuaTableOptions options);
void ExtractPresetFromLuaTable(lua_State* lua, int table_index, StateSnapshot& preset_state);

// Append a human-readable description of every field in the 'preset' Lua table. Each handler owns
// its own help text, so read, write, and shape documentation live together. Text only (no JSON).
void AppendPresetLuaTableShape(DynamicArray<char>& out);

// Emit a JSON catalog of every parameter (id_string, default, range — both projected numeric and
// formatted display forms, legacy flag) plus the enum integer tables (InstrumentType, WaveformType,
// EffectType). Intended for piping into jq.
ErrorCodeOr<void> WriteParamsJson(Writer out);
