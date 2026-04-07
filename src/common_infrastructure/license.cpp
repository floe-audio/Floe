// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "license.hpp"

#include "foundation/zig_std/zig_std.hpp"
#include "tests/framework.hpp"

namespace license {

constexpr Array<u8, k_ed25519_public_key_size> k_ed25519_public_key = {
    0x7d, 0x86, 0xb5, 0x3b, 0x5f, 0x2b, 0x76, 0x20, 0xae, 0xc8, 0x00, 0x77, 0xd9, 0x4c, 0x45, 0x21,
    0xcb, 0x59, 0xde, 0xf8, 0x06, 0xbb, 0x93, 0x22, 0x4d, 0x0f, 0x3d, 0xc6, 0x01, 0xd2, 0x32, 0xda,
};

ErrorCodeCategory const g_license_error_category = {
    .category_id = "LI",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((LicenseError)code.code) {
            case LicenseError::InvalidFormat: str = "invalid license key format"; break;
            case LicenseError::InvalidBase64: str = "invalid license key encoding"; break;
            case LicenseError::InvalidPayload: str = "invalid license key payload"; break;
            case LicenseError::InvalidSignature: str = "invalid license key signature"; break;
        }
        return writer.WriteChars(str);
    },
};

// Binary layout of the license payload (before signature).
// All multi-byte fields are little-endian.
#pragma pack(push, 1)
struct PackedPayloadHeader {
    u32 magic;
    u8 version;
    Array<u8, k_package_key_size> package_key;
    u16 email_len;
    // Followed by email_len bytes of email
};
#pragma pack(pop)

static String StripWhitespace(String input, Span<char> buffer) {
    usize out_pos = 0;
    for (auto const i : Range(input.size)) {
        auto const c = input[i];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            if (out_pos >= buffer.size) return {};
            buffer[out_pos++] = c;
        }
    }
    return {buffer.data, out_pos};
}

ErrorCodeOr<LicensePayload> ParseAndVerify(String pasted_text, u8 const* public_key) {
    if (!public_key) public_key = k_ed25519_public_key.data;
    // Find delimiters
    auto const begin_pos = FindSpan(pasted_text, k_license_begin_delimiter);
    if (!begin_pos) return ErrorCode {LicenseError::InvalidFormat};

    auto const after_begin = *begin_pos + k_license_begin_delimiter.size;
    auto const end_pos = FindSpan(pasted_text, k_license_end_delimiter, after_begin);
    if (!end_pos) return ErrorCode {LicenseError::InvalidFormat};

    // Extract the base64 body between delimiters, strip whitespace
    auto const raw_body = pasted_text.SubSpan(after_begin, *end_pos - after_begin);
    constexpr usize k_max_blob = sizeof(PackedPayloadHeader) + k_max_email_size + k_ed25519_signature_size;
    constexpr usize k_max_base64 = (k_max_blob + 2) / 3 * 4;
    Array<char, k_max_base64> strip_buf;
    auto const stripped = StripWhitespace(raw_body, strip_buf);
    if (!stripped.size) return ErrorCode {LicenseError::InvalidBase64};

    // Base64 decode (max decoded size is 3/4 of the base64 input)
    constexpr usize k_max_decoded_size = k_max_base64 * 3 / 4;
    Array<u8, k_max_decoded_size> decoded_buf;
    auto const decoded_len =
        Base64Decode(decoded_buf.data, decoded_buf.size, (u8 const*)stripped.data, stripped.size);
    if (!decoded_len) return ErrorCode {LicenseError::InvalidBase64};

    constexpr usize k_min_size = sizeof(PackedPayloadHeader) + k_ed25519_signature_size;
    if (decoded_len < k_min_size) return ErrorCode {LicenseError::InvalidPayload};

    auto const signature_offset = decoded_len - k_ed25519_signature_size;
    auto const* signature = decoded_buf.data + signature_offset;
    auto const* payload_bytes = decoded_buf.data;
    auto const payload_len = signature_offset;

    // Verify signature
    if (!Ed25519Verify(signature, payload_bytes, payload_len, public_key))
        return ErrorCode {LicenseError::InvalidSignature};

    // Parse payload
    PackedPayloadHeader header;
    CopyMemory(&header, payload_bytes, sizeof(PackedPayloadHeader));
    if (header.magic != k_license_magic) return ErrorCode {LicenseError::InvalidPayload};
    if (header.version != k_license_version) return ErrorCode {LicenseError::InvalidPayload};
    if (sizeof(PackedPayloadHeader) + header.email_len != payload_len)
        return ErrorCode {LicenseError::InvalidPayload};

    LicensePayload result {};
    result.package_key = header.package_key;

    auto const* email_bytes = payload_bytes + sizeof(PackedPayloadHeader);
    if (header.email_len > result.email.Capacity()) return ErrorCode {LicenseError::InvalidPayload};
    for (auto const i : Range<usize>(header.email_len))
        dyn::Append(result.email, (char)email_bytes[i]);

    return result;
}

ErrorCodeOr<MutableString>
CreateSignedLicense(Span<u8 const> package_key, String email, u8 const* secret_key, ArenaAllocator& arena) {
    ASSERT(package_key.size == k_package_key_size);
    if (email.size > k_max_email_size) return ErrorCode {LicenseError::InvalidPayload};

    // Build payload
    auto const payload_size = sizeof(PackedPayloadHeader) + email.size;
    auto payload_buf = arena.AllocateExactSizeUninitialised<u8>(payload_size);

    PackedPayloadHeader header {};
    header.magic = k_license_magic;
    header.version = k_license_version;
    CopyMemory(header.package_key.data, package_key.data, k_package_key_size);
    header.email_len = (u16)email.size;
    CopyMemory(payload_buf.data, &header, sizeof(PackedPayloadHeader));
    CopyMemory(payload_buf.data + sizeof(PackedPayloadHeader), email.data, email.size);

    // Sign
    Array<u8, k_ed25519_signature_size> signature;
    Ed25519Sign(signature.data, payload_buf.data, payload_size, secret_key);

    // Concatenate payload + signature
    auto const blob_size = payload_size + k_ed25519_signature_size;
    auto blob = arena.AllocateExactSizeUninitialised<u8>(blob_size);
    CopyMemory(blob.data, payload_buf.data, payload_size);
    CopyMemory(blob.data + payload_size, signature.data, k_ed25519_signature_size);

    // Base64 encode
    auto const max_b64_size = ((blob_size + 2) / 3 * 4) + 1;
    auto b64_buf = arena.AllocateExactSizeUninitialised<u8>(max_b64_size);
    auto const b64_len = Base64Encode(b64_buf.data, max_b64_size, blob.data, blob_size);
    ASSERT(b64_len > 0);

    // Build final string with delimiters. Split base64 into lines for readability.
    constexpr usize k_line_len = 48;
    auto const num_lines = (b64_len + k_line_len - 1) / k_line_len;
    auto const result_size =
        k_license_begin_delimiter.size + 1 + b64_len + num_lines + k_license_end_delimiter.size + 1;
    auto result = arena.AllocateExactSizeUninitialised<char>(result_size);

    usize pos = 0;
    CopyMemory(result.data + pos, k_license_begin_delimiter.data, k_license_begin_delimiter.size);
    pos += k_license_begin_delimiter.size;
    result[pos++] = '\n';

    for (usize i = 0; i < b64_len; i += k_line_len) {
        auto const chunk = Min(k_line_len, b64_len - i);
        CopyMemory(result.data + pos, b64_buf.data + i, chunk);
        pos += chunk;
        result[pos++] = '\n';
    }

    CopyMemory(result.data + pos, k_license_end_delimiter.data, k_license_end_delimiter.size);
    pos += k_license_end_delimiter.size;
    result[pos++] = '\n';

    return MutableString {result.data, pos};
}

TEST_CASE(TestLicenseRoundtrip) {
    Array<u8, k_ed25519_public_key_size> public_key;
    Array<u8, k_ed25519_secret_key_size> secret_key;
    Ed25519KeypairCreate(public_key.data, secret_key.data);

    Array<u8, k_package_key_size> package_key;
    CryptoRandomBytes(package_key.data, k_package_key_size);

    ArenaAllocatorWithInlineStorage<4096> arena {PageAllocator::Instance()};
    auto const license_text =
        REQUIRE_UNWRAP(CreateSignedLicense(package_key, "test@example.com"_s, secret_key.data, arena));

    // Round-trip: create then parse+verify
    auto const payload = REQUIRE_UNWRAP(ParseAndVerify(String(license_text), public_key.data));
    CHECK(MemoryIsEqual(payload.package_key.data, package_key.data, k_package_key_size));
    CHECK_EQ(payload.email, "test@example.com"_s);

    // Wrong public key should fail
    Array<u8, k_ed25519_public_key_size> wrong_key {};
    CHECK(ParseAndVerify(String(license_text), wrong_key.data).HasError());

    // Tampered text should fail
    auto tampered = arena.Clone(String(license_text));
    if (tampered.size > 20) tampered[20] = 'X';
    CHECK(ParseAndVerify(tampered, public_key.data).HasError());

    return k_success;
}

} // namespace license

TEST_REGISTRATION(RegisterLicenseTests) { REGISTER_TEST(license::TestLicenseRoundtrip); }
