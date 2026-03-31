// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");

export fn RapidHash64(seed: u64, data: ?[*]const u8, size: usize) callconv(.c) u64 {
    const slice = if (data) |d| d[0..size] else &[_]u8{};
    return std.hash.RapidHash.hash(seed, slice);
}
