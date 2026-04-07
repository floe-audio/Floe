// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "utils/cli_arg_parse.hpp"

enum class LicenseToolCliArgId : u32 {
    GenerateKeypair,
    SignLicense,
    SecretKeyHex,
    PackageKeyHex,
    Email,
    Count,
};

auto constexpr k_license_tool_command_line_args_defs = MakeCommandLineArgDefs<LicenseToolCliArgId>({
    {
        .id = (u32)LicenseToolCliArgId::GenerateKeypair,
        .key = "generate-keypair",
        .description = "Generate a new Ed25519 keypair. Prints public and secret keys as hex to stdout.",
        .value_type = {},
        .required = false,
        .num_values = 0,
    },
    {
        .id = (u32)LicenseToolCliArgId::SignLicense,
        .key = "sign-license",
        .description = "Sign a license key. Requires --secret-key, --package-key, and --email.",
        .value_type = {},
        .required = false,
        .num_values = 0,
    },
    {
        .id = (u32)LicenseToolCliArgId::SecretKeyHex,
        .key = "secret-key",
        .description = "Ed25519 secret key as a 128-character hex string.",
        .value_type = "hex",
        .required = false,
        .num_values = 1,
    },
    {
        .id = (u32)LicenseToolCliArgId::PackageKeyHex,
        .key = "package-key",
        .description = "The 32-byte package key as a 64-character hex string.",
        .value_type = "hex",
        .required = false,
        .num_values = 1,
    },
    {
        .id = (u32)LicenseToolCliArgId::Email,
        .key = "email",
        .description = "Customer email address to embed in the license key.",
        .value_type = "email",
        .required = false,
        .num_values = 1,
    },
});

constexpr String k_license_tool_description =
    "Server-side utility for the Floe encrypted package license key system.\n"
    "Two modes:\n"
    "  --generate-keypair  Generate Ed25519 signing keys (one-time setup)\n"
    "  --sign-license      Create a signed license key for a customer";
