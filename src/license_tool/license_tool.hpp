// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "utils/cli_arg_parse.hpp"

enum class LicenseVerb : u8 { GenerateKeypair, SignLicense, Count };

enum class SignLicenseArgId : u8 {
    SecretKeyHex,
    PackageKeyHex,
    Email,
    KeyId,
    Count,
};

auto constexpr k_sign_license_arg_defs = MakeCommandLineArgDefs<SignLicenseArgId>({
    {
        .id = (u32)SignLicenseArgId::SecretKeyHex,
        .key = "secret-key",
        .description = "Ed25519 secret key as a 128-character hex string.",
        .value_type = "hex",
        .required = true,
        .num_values = 1,
    },
    {
        .id = (u32)SignLicenseArgId::PackageKeyHex,
        .key = "package-key",
        .description = "The 32-byte package key as a 64-character hex string.",
        .value_type = "hex",
        .required = true,
        .num_values = 1,
    },
    {
        .id = (u32)SignLicenseArgId::Email,
        .key = "email",
        .description = "Customer email address to embed in the license key.",
        .value_type = "email",
        .required = true,
        .num_values = 1,
    },
    {
        .id = (u32)SignLicenseArgId::KeyId,
        .key = "key-id",
        .description = "The id (1-255) of the Floe-trusted signing key this signature corresponds to.",
        .value_type = "u8",
        .required = true,
        .num_values = 1,
    },
});

constexpr String k_license_tool_description =
    "Server-side utility for the Floe encrypted package license key system.";

PUBLIC Array<CommandLineSubcommand, ToInt(LicenseVerb::Count)> LicenseToolSubcommands() {
    return {
        CommandLineSubcommand {
            .id = (u32)LicenseVerb::GenerateKeypair,
            .name = "generate-keypair"_s,
            .description =
                "Generate a new Ed25519 keypair. Prints public and secret keys as hex to stdout."_s,
            .args = {},
        },
        CommandLineSubcommand {
            .id = (u32)LicenseVerb::SignLicense,
            .name = "sign-license"_s,
            .description = "Create a signed license key for a customer."_s,
            .args = k_sign_license_arg_defs,
        },
    };
}
