// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// C++ interface to functions backed by Zig's standard library.

#pragma once

#include "foundation/container/contiguous.hpp"
#include "foundation/universal_defs.hpp"

extern "C" u64 RapidHash64(u64 seed, void const* data, usize size);

inline u64 RapidHash64(void const* data, usize size) { return RapidHash64(0, data, size); }

inline u64 RapidHash64(ContiguousContainer auto const& data) {
    return RapidHash64(0, data.data, data.size * sizeof(data.data[0]));
}

inline u64 RapidHash64Multiple(ContiguousContainerOfContiguousContainers auto const& c_of_c) {
    if (!c_of_c.size) return 0x5a6ef77074ebc84b;

    auto hash = RapidHash64(c_of_c[0]);
    for (auto const i : Range<usize>(1, c_of_c.size)) {
        auto const bytes = ToBytes(c_of_c[i]);
        hash = RapidHash64(hash, bytes.data, bytes.size);
    }
    return hash;
}

// =================================================================================================

constexpr usize k_ed25519_public_key_size = 32;
constexpr usize k_ed25519_secret_key_size = 64;
constexpr usize k_ed25519_signature_size = 64;

extern "C" bool
Ed25519Verify(u8 const* signature, u8 const* message, usize message_len, u8 const* public_key);

extern "C" void Ed25519Sign(u8* signature_out, u8 const* message, usize message_len, u8 const* secret_key);

extern "C" void Ed25519KeypairCreate(u8* public_key_out, u8* secret_key_out);

// =================================================================================================

constexpr usize k_xchacha20_poly1305_key_size = 32;
constexpr usize k_xchacha20_poly1305_nonce_size = 24;
constexpr usize k_xchacha20_poly1305_tag_size = 16;

extern "C" bool XChaCha20Poly1305Decrypt(u8* plaintext_out,
                                         u8 const* ciphertext,
                                         usize ciphertext_len,
                                         u8 const* tag,
                                         u8 const* nonce,
                                         u8 const* key);

extern "C" void XChaCha20Poly1305Encrypt(u8* ciphertext_out,
                                         u8* tag_out,
                                         u8 const* plaintext,
                                         usize plaintext_len,
                                         u8 const* nonce,
                                         u8 const* key);

// =================================================================================================

extern "C" usize Base64Decode(u8* out, usize out_capacity, u8 const* encoded, usize encoded_len);
extern "C" usize Base64Encode(u8* out, usize out_capacity, u8 const* data, usize data_len);

// =================================================================================================

extern "C" void CryptoRandomBytes(u8* out, usize len);
