// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "utils/cli_arg_parse.hpp"

enum class PackagerCliArgId : u32 {
    LibraryFolder,
    PresetFolder,
    InputPackages,
    OutputPackageFolder,
    PackageName,
    OutputPackageInfoJsonFile,
    Count,
};

auto constexpr k_packager_command_line_args_defs = MakeCommandLineArgDefs<PackagerCliArgId>({
    {
        .id = (u32)PackagerCliArgId::LibraryFolder,
        .key = "library-folders",
        .description = "One or more library folders",
        .value_type = "path",
        .required = false,
        .num_values = -1,
    },
    {
        .id = (u32)PackagerCliArgId::PresetFolder,
        .key = "presets-folders",
        .description = "One or more presets folders",
        .value_type = "path",
        .required = false,
        .num_values = -1,
    },
    {
        .id = (u32)PackagerCliArgId::InputPackages,
        .key = "input-packages",
        .description = "One or more input package files to include in the output package",
        .value_type = "path",
        .required = false,
        .num_values = -1,
    },
    {
        .id = (u32)PackagerCliArgId::OutputPackageFolder,
        .key = "output-folder",
        .description = "Folder to write the created package to",
        .value_type = "path",
        .required = false,
        .num_values = 1,
    },
    {
        .id = (u32)PackagerCliArgId::PackageName,
        .key = "package-name",
        .description = "Package name - inferred from library name if not provided",
        .value_type = "name",
        .required = false,
        .num_values = 1,
    },
    {
        .id = (u32)PackagerCliArgId::OutputPackageInfoJsonFile,
        .key = "output-info-json",
        .description =
            "If set, writes a JSON file with comprehensive package information: instruments, presets, tags, etc.",
        .value_type = "path",
        .required = false,
        .num_values = 1,
    },
});

constexpr String k_packager_description =
    "Takes libraries and presets and turns them into a Floe package file (ZIP).\n"
    "Also accepts existing packages to merge into the output package.\n"
    "You can specify multiple libraries and preset-folders. Additionally:\n"
    "- Validates any Lua files.\n"
    "- Ensures libraries have a License file.\n"
    "- Adds an 'About' document for each library.\n"
    "- Adds a 'Installation' document for the package.\n"
    "- Embeds a checksum file into the package for better change detection if the package\n"
    "  is installed manually.";
