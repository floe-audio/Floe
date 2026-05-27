// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Floe license keys for encrypted packages (.floe-pkg-enc).
//
// A license key is a text block that the user pastes to unlock an encrypted package during installation.
// It looks like:
//   :FLOE PACKAGE LICENSE
//   <base64 lines>
//   :END
//
// The base64 body decodes to a binary payload followed by a 64-byte Ed25519 signature. The payload contains:
// magic, version, a key_id byte selecting which Floe-trusted signing key produced the signature, a
// 32-byte package key (the XChaCha20-Poly1305 symmetric key needed to decrypt the package), and the
// buyer's email for traceability.
//
// The signature is verified against a public key embedded in Floe's source. The key_id lets us support
// multiple concurrently-trusted signers (e.g. delegate signing authority to a partner) and lets us
// retire a key by shipping a new Floe build that drops it from the trusted table - without invalidating
// licenses signed by other still-trusted keys.
//
// The package key inside the license key is only needed during the one-time package installation. After
// extraction, the library lives as plain files on disk and the key is discarded.
//
// License key signing is typically done server-side.

#pragma once

#include "foundation/foundation.hpp"

namespace license {

constexpr u32 k_license_magic = U32FromChars("FLLK");
constexpr u8 k_license_version = 1;
constexpr usize k_package_key_size = 32;
constexpr usize k_max_email_size = 256;
constexpr String k_license_begin_delimiter = ":FLOE PACKAGE LICENSE"_s;
constexpr String k_license_end_delimiter = ":END"_s;

// Identifies a Floe-trusted Ed25519 signing key. New entries get a fresh, never-reused id. To retire a
// key, remove its entry; licenses signed by it stop verifying immediately on the next Floe build.
struct TrustedSigningKey {
    u8 id;
    Array<u8, 32> public_key;
};

extern Span<TrustedSigningKey const> const k_trusted_signing_keys;

struct LicensePayload {
    u8 key_id;
    Array<u8, k_package_key_size> package_key;
    DynamicArrayBounded<char, k_max_email_size> email;
};

enum class LicenseError : u8 {
    InvalidFormat,
    InvalidBase64,
    InvalidPayload,
    InvalidSignature,
    UnknownKeyId,
};

extern ErrorCodeCategory const g_license_error_category;
PUBLIC ErrorCodeCategory const& ErrorCategoryForEnum(LicenseError) { return g_license_error_category; }

// Parse a ":FLOE PACKAGE LICENSE" ... ":END" block, verify its Ed25519 signature, and extract the payload.
// If trusted_keys_override is non-empty, that table is consulted instead of k_trusted_signing_keys (for
// tests).
ErrorCodeOr<LicensePayload> ParseAndVerify(String pasted_text,
                                           Span<TrustedSigningKey const> trusted_keys_override = {});

// For server-side tooling: create a signed license key block. key_id is embedded in the payload so
// Floe can look up which trusted public key to verify against.
ErrorCodeOr<MutableString> CreateSignedLicense(u8 key_id,
                                               Span<u8 const> package_key,
                                               String email,
                                               u8 const* secret_key,
                                               ArenaAllocator& arena);

} // namespace license
