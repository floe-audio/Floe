// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "state_snapshot.hpp"

#define FLOE_PRESET_FILE_EXTENSION ".floe-preset"

struct CodeStateArguments {
    enum class Mode : u8 { Decode, Encode };

    Mode mode;
    FunctionRef<ErrorCodeOr<void>(void* data, usize bytes)> read_or_write_data;
    StateSource source;
    bool write_experimental_params;
    // When decoding, skip the legacy→modern param remapping. No effect when encoding.
    bool skip_param_adaptation;
};

// "Code" as in decode/encode
ErrorCodeOr<void> CodeState(StateSnapshot& state, CodeStateArguments const& args);

enum class PresetFormat : u8 { Floe, Mirage, Count };

Optional<PresetFormat> PresetFormatFromPath(String path);

ErrorCodeOr<void> DecodeMirageJsonState(StateSnapshot& state,
                                        ArenaAllocator& scratch_arena,
                                        String json_data,
                                        bool adapt_for_latest_version = true);

ErrorCodeOr<StateSnapshot>
DecodeFromMemory(Span<u8 const> data, StateSource source, bool skip_param_adaptation = false);

ErrorCodeOr<StateSnapshot>
LoadPresetFile(String filepath, ArenaAllocator& scratch_arena, bool skip_param_adaptation = false);

ErrorCodeOr<StateSnapshot> LoadPresetFile(PresetFormat format,
                                          Reader& reader,
                                          ArenaAllocator& scratch_arena,
                                          bool skip_param_adaptation = false);

ErrorCodeOr<void> SavePresetFile(String path, StateSnapshot const& state, bool write_experimental_params);
