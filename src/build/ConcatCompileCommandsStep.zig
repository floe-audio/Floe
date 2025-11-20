// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");

const constants = @import("constants.zig");
const std_extras = @import("std_extras.zig");

// Clang's compile_commands.json (also called compilation database or CDB) is used by tools like clangd and
// clang-tidy. Clang can generate all the fragments of this JSON when given the -gen-cdb-fragment-path flag.
// All we need to do is concatenate the fragments into a single JSON array.

const ConcatCompileCommandsStep = @This();

step: std.Build.Step,
target: std.Build.ResolvedTarget,
use_as_default: bool,
fragments_dir_cache_subpath: []const u8,

pub const CompileFragment = struct {
    directory: []u8,
    file: []u8,
    output: []u8,
    arguments: [][]u8,
};

pub fn create(b: *std.Build, target: std.Build.ResolvedTarget, use_as_default: bool) *ConcatCompileCommandsStep {
    const join_compile_commands = b.allocator.create(ConcatCompileCommandsStep) catch @panic("OOM");
    join_compile_commands.* = ConcatCompileCommandsStep{
        .step = std.Build.Step.init(.{
            .id = std.Build.Step.Id.custom,
            .name = "Concatenate compile_commands JSON",
            .owner = b,
            .makeFn = make,
        }),
        .target = target,
        .use_as_default = use_as_default,
        .fragments_dir_cache_subpath = fragmentsDirInCache(b, target.result),
    };

    return join_compile_commands;
}

fn fragmentsDirInCache(b: *std.Build, target: std.Target) []u8 {
    // To better avoid collisions when multiple 'zig build' processes are running simultaneously, we include a hash of the
    // install prefix in the path since it's likely to be different for different build processes.
    var hasher = std.hash.Fnv1a_64.init();
    hasher.update(b.install_prefix);
    const hash = hasher.final();

    const path = b.fmt("tmp{s}{s}-{x}", .{ std.fs.path.sep_str, std_extras.archAndOsPair(target).slice(), hash });
    b.cache_root.handle.makePath(path) catch |err| {
        std.debug.print("failed to make fragments path: {any}\n", .{err});
    };
    return path;
}

pub fn addClangArgument(cdb_step: *ConcatCompileCommandsStep, flags: *std.ArrayList([]const u8)) !void {
    const b = cdb_step.step.owner;
    try flags.appendSlice(&.{
        "-gen-cdb-fragment-path",
        try b.cache_root.handle.realpathAlloc(b.allocator, cdb_step.fragments_dir_cache_subpath),
    });
}

// Relative the build root.
pub fn cdbDirPath(b: *std.Build, target: std.Target) []u8 {
    return b.pathJoin(&.{
        constants.floe_cache_relative,
        "compile_commands",
        std_extras.archAndOsPair(target).slice(),
    });
}

// Relative the build root.
pub fn cdbFilePath(b: *std.Build, target: std.Target) []u8 {
    return b.pathJoin(&.{ cdbDirPath(b, target), "compile_commands.json" });
}

// We have different compile_commands.json per architecture/OS pair in separate folders. However,
// for tools like clangd, it's easier to have a single 'currently active' cdb. This function attempts to
// copy the cdb for the given target to the default location.
pub fn trySetCdb(b: *std.Build, target: std.Target) void {
    const default_cdb = b.pathJoin(&.{
        constants.floe_cache_relative,
        "compile_commands.json",
    });
    const cdb = cdbFilePath(b, target);

    // Ignore errors.
    _ = b.build_root.handle.updateFile(cdb, b.build_root.handle, default_cdb, .{ .override_mode = null }) catch {};
}

fn makeCdbFromFragments(step: *std.Build.Step) !void {
    const self: *ConcatCompileCommandsStep = @fieldParentPtr("step", step);
    const b = step.owner;

    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();

    var compile_commands = std.ArrayList(CompileFragment).init(arena.allocator());

    {
        const maybe_dir = b.cache_root.handle.openDir(self.fragments_dir_cache_subpath, .{ .iterate = true });
        if (maybe_dir != std.fs.Dir.OpenError.FileNotFound) {
            var dir = try maybe_dir;
            defer dir.close();
            var dir_it = dir.iterate();
            while (try dir_it.next()) |entry| {
                const read_file = try dir.openFile(entry.name, .{ .mode = std.fs.File.OpenMode.read_only });
                defer read_file.close();

                const file_contents = try read_file.readToEndAlloc(arena.allocator(), 1024 * 1024 * 1024);
                defer arena.allocator().free(file_contents);

                var trimmed_json = std.mem.trimRight(u8, file_contents, "\n\r \t");
                if (std.mem.endsWith(u8, trimmed_json, ",")) {
                    trimmed_json = trimmed_json[0 .. trimmed_json.len - 1];
                }

                var parsed_data = std.json.parseFromSlice(
                    CompileFragment,
                    arena.allocator(),
                    trimmed_json,
                    .{},
                ) catch |err| {
                    // Print debug info and return error
                    std.debug.print("Failed to parse compile command fragment '{s}': {any}\n", .{ entry.name, err });
                    return err;
                };

                var already_present = false;
                for (compile_commands.items) |command| {
                    if (std.mem.eql(u8, command.file, parsed_data.value.file)) {
                        already_present = true;
                        break;
                    }
                }
                if (!already_present) {
                    var args = std.ArrayList([]u8).fromOwnedSlice(arena.allocator(), parsed_data.value.arguments);

                    var to_remove = std.ArrayList(u32).init(arena.allocator());
                    var index: u32 = 0;
                    for (args.items) |arg| {
                        // clangd doesn't like this flag
                        if (std.mem.eql(u8, arg, "--no-default-config"))
                            try to_remove.append(index);

                        // clang-tidy doesn't like this flag being there
                        if (std.mem.eql(u8, arg, "-ftime-trace"))
                            try to_remove.append(index);

                        // windows WSL clangd doesn't like this flag being there
                        if (std.mem.eql(u8, arg, "-fsanitize=thread"))
                            try to_remove.append(index);

                        // clang-tidy doesn't like this
                        if (std.mem.eql(u8, arg, "-ObjC++"))
                            try to_remove.append(index);

                        index = index + 1;
                    }

                    // clang-tidy doesn't like this when cross-compiling macos, it's a sequence we need to look
                    // for and remove, it's no good just removing the '+pan' by itself
                    index = 0;
                    for (args.items) |arg| {
                        if (std.mem.eql(u8, arg, "-Xclang")) {
                            if (index + 3 < args.items.len) {
                                if (std.mem.eql(u8, args.items[index + 1], "-target-feature") and
                                    std.mem.eql(u8, args.items[index + 2], "-Xclang") and
                                    std.mem.eql(u8, args.items[index + 3], "+pan"))
                                {
                                    try to_remove.append(index);
                                    try to_remove.append(index + 1);
                                    try to_remove.append(index + 2);
                                    try to_remove.append(index + 3);
                                }
                            }
                        }
                        index = index + 1;
                    }

                    var num_removed: u32 = 0;
                    for (to_remove.items) |i| {
                        _ = args.orderedRemove(i - num_removed);
                        num_removed = num_removed + 1;
                    }

                    parsed_data.value.arguments = try args.toOwnedSlice();

                    try compile_commands.append(parsed_data.value);
                }
            }
        }
    }

    if (compile_commands.items.len != 0) {
        b.build_root.handle.makePath(cdbDirPath(b, self.target.result)) catch {};
        const out_path = cdbFilePath(b, self.target.result);

        const maybe_file = b.build_root.handle.openFile(out_path, .{});
        if (maybe_file != std.fs.File.OpenError.FileNotFound) {
            const f = try maybe_file;
            defer f.close();

            const file_contents = try f.readToEndAlloc(arena.allocator(), 1024 * 1024 * 1024);
            defer arena.allocator().free(file_contents);

            const existing_compile_commands = std.json.parseFromSlice(
                []CompileFragment,
                arena.allocator(),
                file_contents,
                .{},
            ) catch |err| {
                // Print debug info and return error
                std.debug.print("Failed to parse existing compile commands from '{s}': {any}\n", .{ out_path, err });
                return err;
            };

            for (existing_compile_commands.value) |existing_c| {
                var is_replaced_by_newer = false;
                for (compile_commands.items) |new_c| {
                    if (std.mem.eql(u8, new_c.file, existing_c.file)) {
                        is_replaced_by_newer = true;
                        break;
                    }
                }

                if (!is_replaced_by_newer) {
                    try compile_commands.append(existing_c);
                }
            }
        }

        {
            var out_f = try b.build_root.handle.createFile(out_path, .{});
            defer out_f.close();
            var buffered_writer = std.io.bufferedWriter(out_f.writer());

            try std.json.stringify(compile_commands.items, .{}, buffered_writer.writer());
            try buffered_writer.flush();
        }

        try b.build_root.handle.deleteTree(self.fragments_dir_cache_subpath);

        if (self.use_as_default) {
            trySetCdb(b, self.target.result);
        }
    }
}

fn make(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
    _ = options;

    makeCdbFromFragments(step) catch |err| {
        std.debug.print("failed to concatenate compile commands: {any}\n", .{err});

        if (@errorReturnTrace()) |trace|
            std.debug.dumpStackTrace(trace.*);
    };
}
