// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "encrypted_package.hpp"

#include "foundation/zig_std/zig_std.hpp"
#include "tests/framework.hpp"

namespace encrypted_package {

ErrorCodeCategory const g_encrypted_package_error_category = {
    .category_id = "EP",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((EncryptedPackageError)code.code) {
            case EncryptedPackageError::InvalidHeader: str = "invalid encrypted package header"; break;
            case EncryptedPackageError::DecryptionFailed: str = "decryption failed (wrong key?)"; break;
        }
        return writer.WriteChars(str);
    },
};

void DeriveChunkNonce(u8* nonce_out, u8 const* nonce_seed, u64 chunk_index) {
    CopyMemory(nonce_out, nonce_seed, 16);
    static_assert(k_endianness == Endianness::Little);
    CopyMemory(nonce_out + 16, &chunk_index, sizeof(u64));
}

ErrorCodeOr<Header> ReadHeader(Reader& reader) {
    reader.pos = 0;
    Header header;
    auto const bytes_read = TRY(reader.Read(&header, sizeof(Header)));
    if (bytes_read < sizeof(Header)) return ErrorCode {EncryptedPackageError::InvalidHeader};
    if (header.magic != k_magic) return ErrorCode {EncryptedPackageError::InvalidHeader};
    if (header.version != k_version) return ErrorCode {EncryptedPackageError::InvalidHeader};
    if (header.chunk_size == 0) return ErrorCode {EncryptedPackageError::InvalidHeader};
    return header;
}

// Decrypt a single chunk from the source reader.
// source_offset is the file offset where this chunk's ciphertext begins.
// plaintext_size is the number of plaintext bytes in this chunk (may be less than chunk_size for last chunk).
static ErrorCodeOr<void> DecryptChunk(Reader& source,
                                      u64 source_offset,
                                      usize plaintext_size,
                                      u8 const* nonce_seed,
                                      u64 chunk_index,
                                      u8 const* package_key,
                                      u8* plaintext_out,
                                      u8* ciphertext_buf) {
    // Read ciphertext + tag
    auto const ct_plus_tag_size = plaintext_size + k_tag_size;
    source.pos = (usize)source_offset;
    auto const bytes_read = TRY(source.Read(ciphertext_buf, ct_plus_tag_size));
    if (bytes_read < ct_plus_tag_size) return ErrorCode {EncryptedPackageError::DecryptionFailed};

    // Derive nonce
    u8 nonce[k_nonce_size];
    DeriveChunkNonce(nonce, nonce_seed, chunk_index);

    // Decrypt
    auto const* tag = ciphertext_buf + plaintext_size;
    if (!XChaCha20Poly1305Decrypt(plaintext_out, ciphertext_buf, plaintext_size, tag, nonce, package_key))
        return ErrorCode {EncryptedPackageError::DecryptionFailed};

    return k_success;
}

ErrorCodeOr<void> VerifyContentKey(Reader& source, Header const& header, Span<u8 const> package_key) {
    ASSERT(package_key.size == k_key_size);
    auto const first_chunk_plaintext_size = Min((u64)header.chunk_size, header.total_plaintext_size);

    // Temporary buffers for chunk 0
    u8 ct_buf[k_default_chunk_size + k_tag_size];
    u8 pt_buf[k_default_chunk_size];
    ASSERT(first_chunk_plaintext_size <= k_default_chunk_size);

    return DecryptChunk(source,
                        sizeof(Header),
                        (usize)first_chunk_plaintext_size,
                        header.nonce_seed,
                        0,
                        package_key.data,
                        pt_buf,
                        ct_buf);
}

ErrorCodeOr<DecryptingReader> CreateDecryptingReader(Reader& source,
                                                     Header const& header,
                                                     Span<u8 const> package_key,
                                                     Allocator& allocator) {
    ASSERT(package_key.size == k_key_size);
    auto buf = allocator.Allocate({.size = header.chunk_size, .alignment = 1});

    return DecryptingReader {
        .source = &source,
        .header = header,
        .package_key = package_key,
        .allocator = allocator,
        .chunk_buffer = (u8*)buf.data,
        .cached_chunk_index = 0,
        .has_cached_chunk = false,
    };
}

void DestroyDecryptingReader(DecryptingReader& dr) {
    if (dr.chunk_buffer) {
        dr.allocator.Free({dr.chunk_buffer, dr.header.chunk_size});
        dr.chunk_buffer = nullptr;
    }
}

static usize ChunkPlaintextSize(Header const& header, u64 chunk_index) {
    auto const num_full_chunks = header.total_plaintext_size / header.chunk_size;
    auto const remainder = header.total_plaintext_size % header.chunk_size;
    if (chunk_index < num_full_chunks) return header.chunk_size;
    if (chunk_index == num_full_chunks && remainder > 0) return (usize)remainder;
    return 0; // past the end
}

static u64 ChunkFileOffset(Header const& header, u64 chunk_index) {
    // Each chunk on disk is: plaintext_size + k_tag_size bytes
    // But all chunks except possibly the last have plaintext_size == chunk_size
    u64 offset = sizeof(Header);
    auto const num_full_chunks = header.total_plaintext_size / header.chunk_size;
    if (chunk_index <= num_full_chunks) {
        auto const full_chunks_before = Min(chunk_index, num_full_chunks);
        offset += full_chunks_before * ((u64)header.chunk_size + k_tag_size);
        if (chunk_index > num_full_chunks) {
            auto const remainder = header.total_plaintext_size % header.chunk_size;
            if (remainder > 0) offset += remainder + k_tag_size;
        }
    }
    return offset;
}

ErrorCodeOr<usize> ReadAt(DecryptingReader& dr, u64 plaintext_offset, void* buffer, usize buffer_size) {
    auto* out = (u8*)buffer;
    usize bytes_written = 0;

    while (bytes_written < buffer_size) {
        if (plaintext_offset >= dr.header.total_plaintext_size) break;

        auto const chunk_index = plaintext_offset / dr.header.chunk_size;
        auto const offset_in_chunk = (usize)(plaintext_offset % dr.header.chunk_size);
        auto const chunk_pt_size = ChunkPlaintextSize(dr.header, chunk_index);
        if (chunk_pt_size == 0) break;

        // Decrypt chunk if not cached
        if (!dr.has_cached_chunk || dr.cached_chunk_index != chunk_index) {
            // Need a temporary buffer for ciphertext + tag
            // Use stack allocation for reasonable chunk sizes
            ASSERT(chunk_pt_size <= k_default_chunk_size);
            u8 ct_buf[k_default_chunk_size + k_tag_size];

            auto const file_offset = ChunkFileOffset(dr.header, chunk_index);
            TRY(DecryptChunk(*dr.source,
                             file_offset,
                             chunk_pt_size,
                             dr.header.nonce_seed,
                             chunk_index,
                             dr.package_key.data,
                             dr.chunk_buffer,
                             ct_buf));
            dr.cached_chunk_index = chunk_index;
            dr.has_cached_chunk = true;
        }

        auto const available = chunk_pt_size - offset_in_chunk;
        auto const to_copy = Min(available, buffer_size - bytes_written);
        CopyMemory(out + bytes_written, dr.chunk_buffer + offset_in_chunk, to_copy);
        bytes_written += to_copy;
        plaintext_offset += to_copy;
    }

    return bytes_written;
}

ErrorCodeOr<Span<u8>> Encrypt(Span<u8 const> plaintext, Span<u8 const> package_key, ArenaAllocator& arena) {
    ASSERT(package_key.size == k_key_size);

    auto const chunk_size = k_default_chunk_size;
    auto const num_full_chunks = plaintext.size / chunk_size;
    auto const remainder = plaintext.size % chunk_size;
    auto const num_chunks = num_full_chunks + (remainder > 0 ? 1 : 0);

    // Calculate total output size
    auto const output_size = sizeof(Header) + plaintext.size + (num_chunks * k_tag_size);
    auto output = arena.AllocateExactSizeUninitialised<u8>(output_size);

    // Write header
    Header header {};
    header.magic = k_magic;
    header.version = k_version;
    CryptoRandomBytes(header.nonce_seed, k_nonce_size);
    header.chunk_size = chunk_size;
    header.total_plaintext_size = plaintext.size;
    CopyMemory(output.data, &header, sizeof(Header));

    // Encrypt each chunk
    usize out_pos = sizeof(Header);
    for (u64 i = 0; i < num_chunks; i++) {
        auto const chunk_offset = (usize)(i * chunk_size);
        auto const this_chunk_size = (i == num_chunks - 1 && remainder > 0) ? remainder : chunk_size;

        u8 nonce[k_nonce_size];
        DeriveChunkNonce(nonce, header.nonce_seed, i);

        auto* ct_out = output.data + out_pos;
        auto* tag_out = ct_out + this_chunk_size;
        XChaCha20Poly1305Encrypt(ct_out,
                                 tag_out,
                                 plaintext.data + chunk_offset,
                                 this_chunk_size,
                                 nonce,
                                 package_key.data);
        out_pos += this_chunk_size + k_tag_size;
    }

    ASSERT(out_pos == output_size);
    return output;
}

TEST_CASE(TestEncryptedPackageRoundtrip) {
    constexpr usize k_test_size = 150000;
    Array<u8, k_test_size> plaintext;
    for (usize i = 0; i < k_test_size; i++)
        plaintext[i] = (u8)((i * 37) + 13);

    // Generate a random content key
    Array<u8, k_key_size> package_key;
    CryptoRandomBytes(package_key.data, k_key_size);

    // Encrypt
    ArenaAllocatorWithInlineStorage<512000> arena {PageAllocator::Instance()};
    auto const encrypted = REQUIRE_UNWRAP(Encrypt(plaintext, package_key, arena));

    // Verify header
    Header header;
    CopyMemory(&header, encrypted.data, sizeof(Header));
    CHECK_EQ(header.magic, k_magic);
    CHECK_EQ(header.version, k_version);
    CHECK_EQ(header.chunk_size, k_default_chunk_size);
    CHECK_EQ(header.total_plaintext_size, (u64)k_test_size);

    // Create a reader from the encrypted data
    auto source = Reader::FromMemory(encrypted);

    // Verify content key check works
    auto const read_header = REQUIRE_UNWRAP(ReadHeader(source));
    REQUIRE_UNWRAP(VerifyContentKey(source, read_header, package_key));

    // Verify wrong key fails
    Array<u8, k_key_size> wrong_key;
    CryptoRandomBytes(wrong_key.data, k_key_size);
    CHECK(VerifyContentKey(source, read_header, wrong_key).HasError());

    // Create decrypting reader and read back all plaintext
    auto dr = REQUIRE_UNWRAP(CreateDecryptingReader(source, read_header, package_key, arena));

    Array<u8, k_test_size> decrypted;
    auto const bytes_read = REQUIRE_UNWRAP(ReadAt(dr, 0, decrypted.data, k_test_size));
    CHECK_EQ(bytes_read, k_test_size);
    CHECK(MemoryIsEqual(decrypted.data, plaintext.data, k_test_size));

    // Test partial reads at various offsets
    SUBCASE("read middle of chunk") {
        Array<u8, 100> buf;
        auto const n = REQUIRE_UNWRAP(ReadAt(dr, 1000, buf.data, 100));
        CHECK_EQ(n, (usize)100);
        CHECK(MemoryIsEqual(buf.data, plaintext.data + 1000, 100));
    }

    SUBCASE("read across chunk boundary") {
        auto const offset = k_default_chunk_size - 50;
        Array<u8, 200> buf;
        auto const n = REQUIRE_UNWRAP(ReadAt(dr, offset, buf.data, 200));
        CHECK_EQ(n, (usize)200);
        CHECK(MemoryIsEqual(buf.data, plaintext.data + offset, 200));
    }

    SUBCASE("read past end") {
        Array<u8, 200> buf;
        auto const n = REQUIRE_UNWRAP(ReadAt(dr, k_test_size - 50, buf.data, 200));
        CHECK_EQ(n, (usize)50);
        CHECK(MemoryIsEqual(buf.data, plaintext.data + k_test_size - 50, 50));
    }

    DestroyDecryptingReader(dr);

    return k_success;
}

} // namespace encrypted_package

TEST_REGISTRATION(RegisterEncryptedPackageTests) {
    REGISTER_TEST(encrypted_package::TestEncryptedPackageRoundtrip);
}
