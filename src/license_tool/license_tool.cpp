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

static void PrintCByteArray(Span<u8 const> data) {
    StdPrintF(StdStream::Out, "{{");
    for (auto const i : Range(data.size)) {
        if (i > 0) StdPrintF(StdStream::Out, ", ");
        StdPrintF(StdStream::Out, "0x{02x}", data[i]);
    }
    StdPrintF(StdStream::Out, "}}");
}

static ErrorCodeOr<int> DoGenerateKeypair() {
    Array<u8, k_ed25519_public_key_size> public_key;
    Array<u8, k_ed25519_secret_key_size> secret_key;
    Ed25519KeypairCreate(public_key.data, secret_key.data);

    StdPrintF(StdStream::Out, "Public key (hex):  ");
    PrintHex(public_key);
    StdPrintF(StdStream::Out, "\n");

    StdPrintF(StdStream::Out, "Secret key (hex):  ");
    PrintHex(secret_key);
    StdPrintF(StdStream::Out, "\n\n");

    StdPrintF(StdStream::Out, "Add to k_trusted_signing_keys_storage in license.cpp with a fresh id:\n");
    StdPrintF(StdStream::Out, "    .public_key = ");
    PrintCByteArray(public_key);
    StdPrintF(StdStream::Out, ",\n");

    return 0;
}

static ErrorCodeOr<int> DoSignLicense(Span<CommandLineArg const> cli_args, ArenaAllocator& arena) {
    auto const key_arg = cli_args[ToInt(SignLicenseArgId::SecretKeyHex)];
    auto const package_key_arg = cli_args[ToInt(SignLicenseArgId::PackageKeyHex)];
    auto const email_arg = cli_args[ToInt(SignLicenseArgId::Email)];
    auto const key_id_arg = cli_args[ToInt(SignLicenseArgId::KeyId)];

    Array<u8, k_ed25519_secret_key_size> secret_key;
    if (!ParseHexString(key_arg.values[0], secret_key)) {
        StdPrintF(StdStream::Err,
                  "Error: --secret-key must be a {}-character hex string\n",
                  k_ed25519_secret_key_size * 2);
        return 1;
    }

    Array<u8, license::k_package_key_size> package_key;
    if (!ParseHexString(package_key_arg.values[0], package_key)) {
        StdPrintF(StdStream::Err,
                  "Error: --package-key must be a {}-character hex string\n",
                  license::k_package_key_size * 2);
        return 1;
    }

    auto const parsed_key_id = ParseInt(key_id_arg.values[0], ParseIntBase::Decimal, nullptr);
    if (!parsed_key_id || *parsed_key_id < 1 || *parsed_key_id > 255) {
        StdPrintF(StdStream::Err, "Error: --key-id must be an integer between 1 and 255\n");
        return 1;
    }
    auto const key_id = (u8)*parsed_key_id;

    auto const email = email_arg.values[0];

    auto const license_text =
        TRY(license::CreateSignedLicense(key_id, package_key, email, secret_key.data, arena));

    StdPrintF(StdStream::Out, "{}", String(license_text));

    return 0;
}

static ErrorCodeOr<int> Main(ArgsCstr args) {
    GlobalInit({
        .init_error_reporting = true,
        .set_main_thread = true,
        .panic_response = PanicResponse::Abort,
    });
    DEFER { GlobalDeinit({.shutdown_error_reporting = true}); };

    ArenaAllocator arena {PageAllocator::Instance()};

    auto subcommands = LicenseToolSubcommands();
    auto const parsed = TRY(ParseCommandLineSubcommandsStandard(arena,
                                                                args,
                                                                subcommands,
                                                                {
                                                                    .description = k_license_tool_description,
                                                                    .version = FLOE_VERSION_STRING,
                                                                }));

    switch ((LicenseVerb)parsed.id) {
        case LicenseVerb::GenerateKeypair: return DoGenerateKeypair();
        case LicenseVerb::SignLicense: return DoSignLicense(parsed.args, arena);
        case LicenseVerb::Count: PanicIfReached();
    }
    return 0;
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    auto const result = Main({argc, argv});
    if (result.HasError()) return 1;
    return result.Value();
}
