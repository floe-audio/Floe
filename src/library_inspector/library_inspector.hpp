// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "utils/cli_arg_parse.hpp"

enum class LibraryInspectorCliArgId : u8 {
    Format,
    Count,
};

auto constexpr k_library_inspector_command_line_args_defs = MakeCommandLineArgDefs<LibraryInspectorCliArgId>({
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

constexpr String k_library_inspector_positional_description =
    "Path to a floe.lua/mdata file, or to a directory containing one.";

constexpr String k_library_inspector_description =
    "Inspect a Floe sample library and dump structured information to stdout.\n"
    "Output includes library metadata, captured Lua print() output, parse errors,\n"
    "instruments, regions, IRs, and orphan/missing sample files. Defaults to JSON;\n"
    "use --format=lua for a loadable Lua chunk.\n"
    "\n"
    "Useful when developing or debugging sample libraries: it surfaces parse errors\n"
    "alongside everything Floe actually loaded, so you can sanity-check the result\n"
    "of your Lua script. The JSON output is also well-suited as input to AI agents.\n"
    "\n"
    "Examples (piping JSON output into jq):\n"
    "  # Check whether the library parsed and show any error:\n"
    "  floe-library-inspector ./my-lib | jq '.parse'\n"
    "\n"
    "  # Top-level library metadata (name, author, version, counts):\n"
    "  floe-library-inspector ./my-lib | jq '.library'\n"
    "\n"
    "  # List orphan and missing sample files:\n"
    "  floe-library-inspector ./my-lib | jq '.samples'\n"
    "\n"
    "  # List every instrument id and how many regions it has:\n"
    "  floe-library-inspector ./my-lib \\\n"
    "    | jq '.instruments[] | {id, num_regions}'\n"
    "\n"
    "  # List every sample path referenced by an instrument:\n"
    "  floe-library-inspector ./my-lib \\\n"
    "    | jq -r '.instruments[].regions[].sample_path'\n"
    "\n"
    "  # Show the key/velocity range of every region of an instrument:\n"
    "  floe-library-inspector ./my-lib \\\n"
    "    | jq '.instruments[] | select(.id==\"My Inst\") | .regions[].trigger'\n"
    "\n"
    "  # List IR ids and their file paths:\n"
    "  floe-library-inspector ./my-lib | jq '.irs[] | {id, path}'";
