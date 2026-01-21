// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Little util that combines multiple files into a single output file.
// Usage: combine_files_cmd <output_file> <input_file1> <input_file2> ...

const std = @import("std");

pub fn main() !u8 {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    const allocator = arena.allocator();
    const args = try std.process.argsAlloc(allocator);

    if (args.len < 3) {
        std.debug.print("Usage: {s} <output_file> <input_file1> <input_file2> ...\n", .{args[0]});
        return 1;
    }

    const out_path = args[1];
    const input_files = args[2..];

    const out_file = try std.fs.cwd().createFile(out_path, .{});
    defer out_file.close();

    for (input_files) |in_path| {
        const in_file = std.fs.cwd().openFile(in_path, .{}) catch |err| {
            std.debug.print("Failed to open input file '{s}': {}\n", .{ in_path, err });
            return 1;
        };
        defer in_file.close();

        const contents = in_file.readToEndAlloc(allocator, std.math.maxInt(usize)) catch |err| {
            std.debug.print("Failed to read input file '{s}': {}\n", .{ in_path, err });
            return 1;
        };

        out_file.writeAll(contents) catch |err| {
            std.debug.print("Failed to write to output file '{s}': {}\n", .{ out_path, err });
            return 1;
        };
    }

    return 0;
}
