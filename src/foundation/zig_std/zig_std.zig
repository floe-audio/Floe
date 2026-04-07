// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");

export fn RapidHash64(seed: u64, data: ?[*]const u8, size: usize) callconv(.c) u64 {
    const slice = if (data) |d| d[0..size] else &[_]u8{};
    return std.hash.RapidHash.hash(seed, slice);
}

// =================================================================================================

export fn Ed25519Verify(
    signature_ptr: *const [64]u8,
    message: ?[*]const u8,
    message_len: usize,
    public_key_ptr: *const [32]u8,
) callconv(.c) bool {
    const Ed25519 = std.crypto.sign.Ed25519;
    const sig = Ed25519.Signature.fromBytes(signature_ptr.*);
    const key = Ed25519.PublicKey.fromBytes(public_key_ptr.*) catch return false;
    const msg = if (message) |m| m[0..message_len] else &[_]u8{};
    sig.verify(msg, key) catch return false;
    return true;
}

export fn Ed25519Sign(
    signature_out: *[64]u8,
    message: ?[*]const u8,
    message_len: usize,
    secret_key_ptr: *const [64]u8,
) callconv(.c) void {
    const Ed25519 = std.crypto.sign.Ed25519;
    const msg = if (message) |m| m[0..message_len] else &[_]u8{};
    const key_pair = Ed25519.KeyPair{
        .secret_key = Ed25519.SecretKey.fromBytes(secret_key_ptr.*) catch unreachable,
        .public_key = Ed25519.PublicKey.fromBytes(secret_key_ptr.*[32..64].*) catch unreachable,
    };
    const sig = key_pair.sign(msg, null) catch unreachable;
    signature_out.* = sig.toBytes();
}

export fn Ed25519KeypairCreate(
    public_key_out: *[32]u8,
    secret_key_out: *[64]u8,
) callconv(.c) void {
    const Ed25519 = std.crypto.sign.Ed25519;
    const key_pair = Ed25519.KeyPair.generate();
    public_key_out.* = key_pair.public_key.toBytes();
    secret_key_out.* = key_pair.secret_key.toBytes();
}

// =================================================================================================

const XChaCha20Poly1305 = std.crypto.aead.chacha_poly.XChaCha20Poly1305;

export fn XChaCha20Poly1305Decrypt(
    plaintext_out: ?[*]u8,
    ciphertext: ?[*]const u8,
    ciphertext_len: usize,
    tag_ptr: *const [16]u8,
    nonce_ptr: *const [24]u8,
    key_ptr: *const [32]u8,
) callconv(.c) bool {
    const ct = if (ciphertext) |c| c[0..ciphertext_len] else &[_]u8{};
    const pt = if (plaintext_out) |p| p[0..ciphertext_len] else return false;
    XChaCha20Poly1305.decrypt(pt, ct, tag_ptr.*, &[_]u8{}, nonce_ptr.*, key_ptr.*) catch return false;
    return true;
}

export fn XChaCha20Poly1305Encrypt(
    ciphertext_out: ?[*]u8,
    tag_out: *[16]u8,
    plaintext: ?[*]const u8,
    plaintext_len: usize,
    nonce_ptr: *const [24]u8,
    key_ptr: *const [32]u8,
) callconv(.c) void {
    const pt = if (plaintext) |p| p[0..plaintext_len] else &[_]u8{};
    const ct = if (ciphertext_out) |c| c[0..plaintext_len] else unreachable;
    XChaCha20Poly1305.encrypt(ct, tag_out, pt, &[_]u8{}, nonce_ptr.*, key_ptr.*);
}

// =================================================================================================

export fn Base64Decode(
    out: [*]u8,
    out_capacity: usize,
    encoded: ?[*]const u8,
    encoded_len: usize,
) callconv(.c) usize {
    const decoder = std.base64.standard.Decoder;
    const src = if (encoded) |e| e[0..encoded_len] else return 0;
    const decoded_len = decoder.calcSizeForSlice(src) catch return 0;
    if (decoded_len > out_capacity) return 0;
    decoder.decode(out[0..decoded_len], src) catch return 0;
    return decoded_len;
}

export fn Base64Encode(
    out: [*]u8,
    out_capacity: usize,
    data: ?[*]const u8,
    data_len: usize,
) callconv(.c) usize {
    const encoder = std.base64.standard.Encoder;
    const src = if (data) |d| d[0..data_len] else return 0;
    const encoded_len = encoder.calcSize(data_len);
    if (encoded_len > out_capacity) return 0;
    _ = encoder.encode(out[0..encoded_len], src);
    return encoded_len;
}

// =================================================================================================

export fn CryptoRandomBytes(out: [*]u8, len: usize) callconv(.c) void {
    std.crypto.random.bytes(out[0..len]);
}
