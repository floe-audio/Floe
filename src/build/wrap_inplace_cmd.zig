// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Little util that wraps an in-place command (that is, a command that modifies its input file/directory) and makes it
// an out-of-place command so you can specify an input and output path separately.

// Why this exists:
// In the Zig build system when using std.Build.Step.Run, we have a nice API for specifying input and output files. The build system
// uses this information to determine re-runs and caching because it understands the command is a pure function from input to output.
// However, some tools that we want to use, such as rcodesign, only support in-place modification of files.
//
// Using this wrapper fixed an issue we had with where dependent notarization steps were coming back as invalid - presumably due to
// some copying/clearing (or something of that nature) that happened as a result of using in-place modification in the zig cache.

const builtin = @import("builtin");
const std = @import("std");
const std_extras = @import("std_extras.zig");

pub fn main() !u8 {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    const allocator = arena.allocator();
    var args = try std.process.argsAlloc(allocator);

    // args[1] is the index to the arg that is the input path for this command
    const in_path_arg_index = std.fmt.parseInt(u32, args[1], 10) catch @panic("arg[2] must be an int index for input path arg");

    // args[2] is the output path
    const out_path = args[2];

    // Copy the input path to the output path.
    {
        const outcome = std.fs.cwd().updateFile(args[in_path_arg_index], std.fs.cwd(), out_path, .{});
        if (outcome == error.IsDir) {
            _ = try std_extras.copyDirRecursive(args[in_path_arg_index], out_path, allocator);
        } else {
            _ = try outcome;
        }
    }

    // Now run the in-place command but with the input path replaced with the output path.
    {
        args[in_path_arg_index] = out_path;

        const result = try std.process.Child.run(.{
            .allocator = allocator,
            .argv = args[3..],
        });

        if (result.stdout.len > 0) std.debug.print("{s}", .{result.stdout});
        if (result.stderr.len > 0) std.debug.print("{s}", .{result.stderr});

        switch (result.term) {
            .Exited => |code| {
                if (code != 0) return code;
            },
            .Signal, .Stopped, .Unknown => {
                std.debug.print("terminated unexpectedly\n", .{});
                return 1;
            },
        }
    }

    return 0;
}
