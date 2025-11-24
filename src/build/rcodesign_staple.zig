const builtin = @import("builtin");
const std = @import("std");
const std_extras = @import("std_extras.zig");

pub fn main() !u8 {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    const allocator = arena.allocator();
    const args = try std.process.argsAlloc(allocator);
    std.debug.assert(args.len == 3);

    const in_path = args[1];
    const out_path = args[2];

    const outcome = std.fs.cwd().updateFile(in_path, std.fs.cwd(), out_path, .{});
    if (outcome == error.IsDir) {
        _ = try std_extras.copyDirRecursive(in_path, out_path, allocator);
    } else {
        _ = try outcome;
    }

    const result = try std.process.Child.run(.{
        .allocator = allocator,
        .argv = &.{ "rcodesign", "staple", out_path },
    });

    if (result.stdout.len > 0) std.debug.print("{s}", .{result.stdout});
    if (result.stderr.len > 0) std.debug.print("{s}", .{result.stderr});

    switch (result.term) {
        .Exited => |code| {
            if (code != 0) return code;
        },
        .Signal, .Stopped, .Unknown => {
            std.debug.print("rcodesign terminated unexpectedly\n", .{});
            return 1;
        },
    }

    return 0;
}
