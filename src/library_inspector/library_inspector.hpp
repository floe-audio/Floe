// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "utils/cli_arg_parse.hpp"

enum class LibraryInspectorCliArgId : u8 {
    LibraryPath,
    Format,
    Count,
};

auto constexpr k_library_inspector_command_line_args_defs = MakeCommandLineArgDefs<LibraryInspectorCliArgId>({
    {
        .id = (u32)LibraryInspectorCliArgId::LibraryPath,
        .key = "library",
        .description = "Path to the library file (floe.lua or .mdata) or to a directory containing one.",
        .value_type = "path",
        .required = true,
        .num_values = 1,
    },
    {
        .id = (u32)LibraryInspectorCliArgId::Format,
        .key = "format",
        .description = "Output format: 'json' (default) or 'lua'. Lua output is a single `return { ... }`\n"
                       "chunk loadable with Lua's load()/dofile().",
        .value_type = "json|lua",
        .required = false,
        .num_values = 1,
    },
});

constexpr String k_library_inspector_description =
    "Inspect a Floe sample library and dump structured information to stdout.\n"
    "The library may be supplied as a path to its floe.lua/mdata file, or as a path\n"
    "to a directory containing one. Output includes library metadata, captured Lua\n"
    "print() output, parse errors, instruments, regions, IRs, and orphan/missing\n"
    "sample files. Defaults to JSON; use --format=lua for a loadable Lua chunk.\n"
    "\n"
    "Useful when developing or debugging sample libraries: it surfaces parse errors\n"
    "alongside everything Floe actually loaded, so you can sanity-check the result\n"
    "of your Lua script. The JSON output is also well-suited as input to AI agents.";
