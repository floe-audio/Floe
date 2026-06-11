// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "utils/cli_arg_parse.hpp"

enum class PackagerCliArgId : u8 {
    LibraryFolder,
    PresetFolder,
    InputPackages,
    OutputPackageFolder,
    PackageName,
    OutputPackageInfoJsonFile,
    Encrypt,
    OmitUnreferenced,
    Count,
};

auto constexpr k_packager_command_line_args_defs = MakeCommandLineArgDefs<PackagerCliArgId>({
    {
        .id = (u32)PackagerCliArgId::LibraryFolder,
        .key = "library-folder",
        .description = "Library folder to include (repeatable)",
        .value_type = "path",
        .required = false,
        .num_values = -1,
        .short_key = 'l',
    },
    {
        .id = (u32)PackagerCliArgId::PresetFolder,
        .key = "preset-folder",
        .description = "Preset folder to include (repeatable)",
        .value_type = "path",
        .required = false,
        .num_values = -1,
        .short_key = 'p',
    },
    {
        .id = (u32)PackagerCliArgId::InputPackages,
        .key = "input-package",
        .description = "Existing package file to merge into the output. Files from --library-folder and "
                       "--preset-folder take precedence on conflict. (repeatable)",
        .value_type = "path",
        .required = false,
        .num_values = -1,
        .short_key = 'i',
    },
    {
        .id = (u32)PackagerCliArgId::OutputPackageFolder,
        .key = "output-dir",
        .description = "Directory to write the package into. The filename is auto-generated.",
        .value_type = "path",
        .required = false,
        .num_values = 1,
        .short_key = 'o',
    },
    {
        .id = (u32)PackagerCliArgId::PackageName,
        .key = "package-name",
        .description = "Override the auto-generated package filename. Any file extension is stripped.",
        .value_type = "name",
        .required = false,
        .num_values = 1,
        .short_key = 'n',
    },
    {
        .id = (u32)PackagerCliArgId::OutputPackageInfoJsonFile,
        .key = "info-json",
        .description = "Write a JSON file describing the package's instruments, presets, tags, etc. "
                       "Use '-' to write to stdout.",
        .value_type = "path",
        .required = false,
        .num_values = 1,
        .short_key = 'j',
    },
    {
        .id = (u32)PackagerCliArgId::Encrypt,
        .key = "encrypt",
        .description = "Encrypt the output package with a random content key. The key is printed to stdout; "
                       "the output filename uses the .floe-pkg-enc extension.",
        .value_type = {},
        .required = false,
        .num_values = 0,
        .short_key = 'e',
    },
    {
        .id = (u32)PackagerCliArgId::OmitUnreferenced,
        .key = "prune",
        .description = "Silently drop files that aren't used: for libraries, files not referenced from Lua "
                       "(samples, images, IRs) or the .lua/license files; for preset folders, files that "
                       "aren't presets or preset-bank info files. Without this, such files are warned about "
                       "but still included.",
        .value_type = {},
        .required = false,
        .num_values = 0,
    },
});

constexpr String k_packager_description =
    "Packages libraries and presets into a Floe package file (.floe-pkg).\n"
    "Existing packages can be merged into the output. Multiple libraries and preset folders\n"
    "are supported. Additionally:\n"
    "- Validates any Lua files.\n"
    "- Ensures libraries have a License file.\n"
    "- Adds an 'About' document for each library.\n"
    "- Adds an 'Installation' document for the package.\n"
    "- Embeds a checksum file into the package for better change detection if the package\n"
    "  is installed manually.";
