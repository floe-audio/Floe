// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Little util that wraps a command and redirects its stdout to stderr.

// Why this exists:
// In the Zig build system when using std.Build.Step.Run, Zig never prints stdout to the console, only stderr.
// If the program doesn't put debug information in stderr, you don't see anything other than the exit code.
// This wrapper redirects stdout to stderr so you see all output, allowing you to debug why a program fails.

const builtin = @import("builtin");
const std = @import("std");

pub fn main() !u8 {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    const allocator = arena.allocator();
    var args = try std.process.argsAlloc(allocator);

    std.debug.assert(args.len >= 2);

    const result = try std.process.Child.run(.{
        .allocator = allocator,
        .argv = args[1..],
    });

    // Redirect stdout to stderr
    if (result.stdout.len > 0) std.io.getStdErr().writer().writeAll(result.stdout) catch {};
    if (result.stderr.len > 0) std.io.getStdErr().writer().writeAll(result.stderr) catch {};

    switch (result.term) {
        .Exited => |code| {
            return code;
        },
        .Signal, .Stopped, .Unknown => {
            std.io.getStdErr().writer().writeAll("terminated unexpectedly\n") catch {};
            return 1;
        },
    }
}

