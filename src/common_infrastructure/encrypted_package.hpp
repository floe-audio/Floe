// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Encrypted package format (.floe-pkg-enc).
//
// An encrypted wrapper around a normal .floe-pkg ZIP file. The entire ZIP is encrypted using
// XChaCha20-Poly1305 in fixed-size chunks - we do this to enable random-access decryption.
//
// Format:
//   [Header: 40 bytes]
//   [Chunk 0: ciphertext(chunk_size) + tag(16)]
//   [Chunk 1: ciphertext(chunk_size) + tag(16)]
//   ...
//   [Chunk N: ciphertext(remaining) + tag(16)]
//
// Decryption is only needed at package installation time. After installation, libraries live as plain
// files on disk.

#pragma once

#include "foundation/foundation.hpp"
#include "utils/reader.hpp"

namespace encrypted_package {

constexpr u32 k_magic = U32FromChars("FLOE");
constexpr u8 k_version = 1;
constexpr u32 k_default_chunk_size = Kb(64);
constexpr usize k_tag_size = 16; // Poly1305 tag
constexpr usize k_nonce_size = 24; // XChaCha20 nonce
constexpr usize k_key_size = 32;
constexpr String k_file_extension = ".floe-pkg-enc"_s;

#pragma pack(push, 1)
struct Header {
    u32 magic;
    u8 version;
    u8 reserved[3];
    u8 nonce_seed[k_nonce_size]; // Random seed for deriving per-chunk nonces
    u32 chunk_size; // Plaintext bytes per chunk (last chunk may be smaller)
    u64 total_plaintext_size; // Original ZIP file size
};
#pragma pack(pop)

static_assert(sizeof(Header) == 44);

inline bool HasEncryptedPackageExtension(String path) {
    return path::Equal(path::Extension(path), k_file_extension);
}

// Derive a unique nonce for a given chunk index from the nonce seed.
// nonce = first 16 bytes of seed || chunk_index as little-endian u64
void DeriveChunkNonce(u8* nonce_out, u8 const* nonce_seed, u64 chunk_index);

enum class EncryptedPackageError {
    InvalidHeader,
    DecryptionFailed,
};

extern ErrorCodeCategory const g_encrypted_package_error_category;
PUBLIC ErrorCodeCategory const& ErrorCategoryForEnum(EncryptedPackageError) {
    return g_encrypted_package_error_category;
}

ErrorCodeOr<Header> ReadHeader(Reader& reader);

// Try decrypting chunk 0 to verify the package key matches this package.
ErrorCodeOr<void> VerifyContentKey(Reader& source, Header const& header, Span<u8 const> package_key);

struct DecryptingReader {
    Reader* source;
    Header header;
    Span<u8 const> package_key; // 32 bytes, must remain valid for lifetime of reader
    Allocator& allocator;

    // Internal: cached decrypted chunk
    u8* chunk_buffer; // allocated, size = header.chunk_size
    u64 cached_chunk_index;
    bool has_cached_chunk;
};

ErrorCodeOr<DecryptingReader> CreateDecryptingReader(Reader& source,
                                                     Header const& header,
                                                     Span<u8 const> package_key,
                                                     Allocator& allocator);
void DestroyDecryptingReader(DecryptingReader& dr);

// Read decrypted bytes at a given offset.
ErrorCodeOr<usize> ReadAt(DecryptingReader& dr, u64 plaintext_offset, void* buffer, usize buffer_size);

// The DecryptingReader must outlive the returned Reader.
inline Reader ReaderFromDecryptingReader(DecryptingReader& dr) {
    return Reader::FromCallback(
        (usize)dr.header.total_plaintext_size,
        [](void* ctx, u64 offset, void* buffer, usize buffer_size) -> ErrorCodeOr<usize> {
            return ReadAt(*(DecryptingReader*)ctx, offset, buffer, buffer_size);
        },
        &dr);
}

// Returns the encrypted data including header.
ErrorCodeOr<Span<u8>> Encrypt(Span<u8 const> plaintext, Span<u8 const> package_key, ArenaAllocator& arena);

} // namespace encrypted_package
