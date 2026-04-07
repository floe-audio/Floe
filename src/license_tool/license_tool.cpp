// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "license_tool.hpp"

#include "foundation/foundation.hpp"
#include "foundation/zig_std/zig_std.hpp"
#include "os/misc.hpp"
#include "utils/cli_arg_parse.hpp"

#include "common_infrastructure/global.hpp"
#include "common_infrastructure/license.hpp"

static Optional<u8> HexCharToNibble(char c) {
    if (c >= '0' && c <= '9') return (u8)(c - '0');
    if (c >= 'a' && c <= 'f') return (u8)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (u8)(c - 'A' + 10);
    return k_nullopt;
}

static bool ParseHexString(String hex, Span<u8> out) {
    if (hex.size != out.size * 2) return false;
    for (usize i = 0; i < out.size; i++) {
        auto const hi = HexCharToNibble(hex[i * 2]);
        auto const lo = HexCharToNibble(hex[(i * 2) + 1]);
        if (!hi || !lo) return false;
        out[i] = (u8)(*hi << 4 | *lo);
    }
    return true;
}

static void PrintHex(Span<u8 const> data) {
    for (auto const byte : data)
        StdPrintF(StdStream::Out, "{02x}", byte);
}

static ErrorCodeOr<int> DoGenerateKeypair() {
    Array<u8, k_ed25519_public_key_size> public_key;
    Array<u8, k_ed25519_secret_key_size> secret_key;
    Ed25519KeypairCreate(public_key.data, secret_key.data);

    StdPrintF(StdStream::Out, "Public key:  ");
    PrintHex(public_key);
    StdPrintF(StdStream::Out, "\n");

    StdPrintF(StdStream::Out, "Secret key:  ");
    PrintHex(secret_key);
    StdPrintF(StdStream::Out, "\n");

    return 0;
}

static ErrorCodeOr<int> DoSignLicense(Span<CommandLineArg const> cli_args, ArenaAllocator& arena) {
    auto const key_arg = cli_args[ToInt(LicenseToolCliArgId::SecretKeyHex)];
    auto const package_key_arg = cli_args[ToInt(LicenseToolCliArgId::PackageKeyHex)];
    auto const email_arg = cli_args[ToInt(LicenseToolCliArgId::Email)];

    if (!key_arg.was_provided || !package_key_arg.was_provided || !email_arg.was_provided) {
        StdPrintF(StdStream::Err,
                  "Error: --sign-license requires --secret-key, --package-key, and --email\n");
        return 1;
    }

    // Parse secret key
    Array<u8, k_ed25519_secret_key_size> secret_key;
    if (!ParseHexString(key_arg.values[0], secret_key)) {
        StdPrintF(StdStream::Err,
                  "Error: --secret-key must be a {}-character hex string\n",
                  k_ed25519_secret_key_size * 2);
        return 1;
    }

    // Parse content key
    Array<u8, license::k_package_key_size> package_key;
    if (!ParseHexString(package_key_arg.values[0], package_key)) {
        StdPrintF(StdStream::Err,
                  "Error: --package-key must be a {}-character hex string\n",
                  license::k_package_key_size * 2);
        return 1;
    }

    auto const email = email_arg.values[0];

    auto const license_text =
        TRY(license::CreateSignedLicense(package_key, email, secret_key.data, arena));

    StdPrintF(StdStream::Out, "{}", String(license_text));

    return 0;
}

static ErrorCodeOr<int> Main(ArgsCstr args) {
    GlobalInit({.init_error_reporting = true, .set_main_thread = true});
    DEFER { GlobalDeinit({.shutdown_error_reporting = true}); };

    ArenaAllocator arena {PageAllocator::Instance()};

    auto const cli_args = TRY(ParseCommandLineArgsStandard(arena,
                                                           args,
                                                           k_license_tool_command_line_args_defs,
                                                           {
                                                               .handle_help_option = true,
                                                               .print_usage_on_error = true,
                                                               .description = k_license_tool_description,
                                                               .version = FLOE_VERSION_STRING,
                                                           }));

    auto const generate_keypair = cli_args[ToInt(LicenseToolCliArgId::GenerateKeypair)].was_provided;
    auto const sign_license = cli_args[ToInt(LicenseToolCliArgId::SignLicense)].was_provided;

    if (!generate_keypair && !sign_license) {
        StdPrintF(StdStream::Err, "Error: specify --generate-keypair or --sign-license\n");
        return 1;
    }

    if (generate_keypair && sign_license) {
        StdPrintF(StdStream::Err, "Error: specify only one of --generate-keypair or --sign-license\n");
        return 1;
    }

    if (generate_keypair) return DoGenerateKeypair();
    if (sign_license) return DoSignLicense(cli_args, arena);

    return 0;
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    auto const result = Main({argc, argv});
    if (result.HasError()) return 1;
    return result.Value();
}
