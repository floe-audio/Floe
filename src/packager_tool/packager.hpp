// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "utils/cli_arg_parse.hpp"

enum class PackagerVerb : u8 { Pack, Info, GenKey, Count };

enum class PackArgId : u8 {
    LibraryFolder,
    PresetFolder,
    InputPackages,
    Prune,
    PackageName,
    PackageKey,
    Count,
};

enum class InfoArgId : u8 {
    LibraryFolder,
    PresetFolder,
    InputPackages,
    Prune,
    PackageName,
    Count,
};

constexpr String k_library_folder_desc = "Library folder to include (repeatable)"_s;
constexpr String k_preset_folder_desc = "Preset folder to include (repeatable)"_s;
constexpr String k_input_package_desc =
    "Existing package file to merge into the output. Files from --library-folder and "
    "--preset-folder take precedence on conflict. (repeatable)"_s;
constexpr String k_prune_desc =
    "Silently drop files that aren't used: for libraries, files not referenced from Lua "
    "(samples, images, IRs) or the .lua/license files; for preset folders, files that "
    "aren't presets or preset-bank info files. Without this, such files are warned about "
    "but still included."_s;
constexpr String k_package_name_desc =
    "Override the auto-generated package name. Any file extension is stripped."_s;

auto constexpr k_pack_arg_defs = MakeCommandLineArgDefs<PackArgId>({
    {
        .id = (u32)PackArgId::LibraryFolder,
        .key = "library-folder",
        .description = k_library_folder_desc,
        .value_type = "path",
        .required = false,
        .num_values = -1,
        .short_key = 'l',
    },
    {
        .id = (u32)PackArgId::PresetFolder,
        .key = "preset-folder",
        .description = k_preset_folder_desc,
        .value_type = "path",
        .required = false,
        .num_values = -1,
        .short_key = 'p',
    },
    {
        .id = (u32)PackArgId::InputPackages,
        .key = "input-package",
        .description = k_input_package_desc,
        .value_type = "path",
        .required = false,
        .num_values = -1,
        .short_key = 'i',
    },
    {
        .id = (u32)PackArgId::Prune,
        .key = "prune",
        .description = k_prune_desc,
        .value_type = {},
        .required = false,
        .num_values = 0,
    },
    {
        .id = (u32)PackArgId::PackageName,
        .key = "package-name",
        .description = k_package_name_desc,
        .value_type = "name",
        .required = false,
        .num_values = 1,
        .short_key = 'n',
    },
    {
        .id = (u32)PackArgId::PackageKey,
        .key = "package-key",
        .description = "64-character hex key for encrypting the package (use gen-key to create one). "
                       "When present, produces an encrypted .floe-pkg-enc instead of a plain .floe-pkg. "
                       "Reuse the same key across versions so existing license keys stay valid.",
        .value_type = "hex",
        .required = false,
        .num_values = 1,
    },
});

auto constexpr k_info_arg_defs = MakeCommandLineArgDefs<InfoArgId>({
    {
        .id = (u32)InfoArgId::LibraryFolder,
        .key = "library-folder",
        .description = k_library_folder_desc,
        .value_type = "path",
        .required = false,
        .num_values = -1,
        .short_key = 'l',
    },
    {
        .id = (u32)InfoArgId::PresetFolder,
        .key = "preset-folder",
        .description = k_preset_folder_desc,
        .value_type = "path",
        .required = false,
        .num_values = -1,
        .short_key = 'p',
    },
    {
        .id = (u32)InfoArgId::InputPackages,
        .key = "input-package",
        .description = k_input_package_desc,
        .value_type = "path",
        .required = false,
        .num_values = -1,
        .short_key = 'i',
    },
    {
        .id = (u32)InfoArgId::Prune,
        .key = "prune",
        .description = k_prune_desc,
        .value_type = {},
        .required = false,
        .num_values = 0,
    },
    {
        .id = (u32)InfoArgId::PackageName,
        .key = "package-name",
        .description = k_package_name_desc,
        .value_type = "name",
        .required = false,
        .num_values = 1,
        .short_key = 'n',
    },
});

constexpr String k_pack_description =
    "Build a Floe package (.floe-pkg) from libraries and presets. Existing packages can be merged "
    "into the output."_s;

constexpr String k_info_description =
    "Write a JSON manifest describing the package's instruments, presets, tags, etc., "
    "without producing a package file."_s;

constexpr String k_pack_positional_name = "output-dir"_s;
constexpr String k_pack_positional_desc =
    "Directory to write the package into. Created if missing. The package filename is auto-generated."_s;

constexpr String k_info_positional_name = "output-json"_s;
constexpr String k_info_positional_desc =
    "JSON file path to write the manifest to. Use '-' to write to stdout."_s;

constexpr String k_packager_description =
    "Packages libraries and presets into a Floe package file (.floe-pkg or .floe-pkg-enc).\n"
    "Existing packages can be merged into the output. Multiple libraries and preset folders\n"
    "are supported. Additionally:\n"
    "- Validates any Lua files.\n"
    "- Ensures libraries have a License file.\n"
    "- Adds an 'About' document for each library.\n"
    "- Adds an 'Installation' document for the package.\n"
    "- Embeds a checksum file into the package for better change detection if the package\n"
    "  is installed manually.\n"
    "\n"
    "Encrypted workflow: run gen-key once per product line and store the key securely.\n"
    "Pass it to pack via --package-key for every version so that a single license key\n"
    "unlocks all of them.";

// Build the CommandLineSubcommand array. Pass non-null out pointers to capture positionals; pass
// nullptrs (the default) for help/docs rendering.
PUBLIC Array<CommandLineSubcommand, ToInt(PackagerVerb::Count)>
PackagerSubcommands(Span<String>* pack_positionals = nullptr, Span<String>* info_positionals = nullptr) {
    return {
        CommandLineSubcommand {
            .id = (u32)PackagerVerb::Pack,
            .name = "pack"_s,
            .description = k_pack_description,
            .args = k_pack_arg_defs,
            .positionals = {.name = k_pack_positional_name,
                            .description = k_pack_positional_desc,
                            .min_count = 1,
                            .max_count = 1,
                            .out = pack_positionals},
        },
        CommandLineSubcommand {
            .id = (u32)PackagerVerb::Info,
            .name = "info"_s,
            .description = k_info_description,
            .args = k_info_arg_defs,
            .positionals = {.name = k_info_positional_name,
                            .description = k_info_positional_desc,
                            .min_count = 1,
                            .max_count = 1,
                            .out = info_positionals},
        },
        CommandLineSubcommand {
            .id = (u32)PackagerVerb::GenKey,
            .name = "gen-key"_s,
            .description = "Generate a random package key and print it to stdout."_s,
            .args = {},
        },
    };
}
