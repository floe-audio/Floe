// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");

const constants = @import("constants.zig");
const std_extras = @import("std_extras.zig");

// Clang's compile_commands.json (also called compilation database or CDB) is used by tools like clangd and
// clang-tidy.
//
// Clang can generate all the fragments of this JSON when given the -gen-cdb-fragment-path flag.
// All we need to do is combine the fragments into a single JSON array.

pub const CdbTargetInfo = struct {
    pub fn dupe(self: *const CdbTargetInfo, a: std.mem.Allocator) !CdbTargetInfo {
        var result = self.*;
        result.fragments_dir = try a.dupe(u8, self.fragments_dir);
        return result;
    }

    target: std.Target,
    fragments_dir: []const u8,
    set_as_active: bool,
};

pub const CombineCdbFragmentsStep = struct {
    step: std.Build.Step,
    target_infos: std.ArrayList(CdbTargetInfo),

    pub fn create(b: *std.Build) *CombineCdbFragmentsStep {
        const join_compile_commands = b.allocator.create(CombineCdbFragmentsStep) catch @panic("OOM");
        join_compile_commands.* = CombineCdbFragmentsStep{
            .step = std.Build.Step.init(.{
                .id = .custom,
                .name = "Combine compile commands fragments",
                .owner = b,
                .makeFn = make,
            }),
            .target_infos = std.ArrayList(CdbTargetInfo).init(b.allocator),
        };

        return join_compile_commands;
    }

    pub fn addTarget(self: *CombineCdbFragmentsStep, config: CdbTargetInfo) !void {
        try self.target_infos.append(try config.dupe(self.step.owner.allocator));
    }

    fn combineCdbFragments(b: *std.Build, target_info: CdbTargetInfo) !void {
        var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
        defer arena.deinit();

        var compile_commands = std.ArrayList(CompileFragment).init(arena.allocator());

        {
            const maybe_dir = b.cache_root.handle.openDir(target_info.fragments_dir, .{ .iterate = true });
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
            b.build_root.handle.makePath(cdbDirPath(b, target_info.target)) catch {};
            const out_path = cdbFilePath(b, target_info.target);

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

            try b.build_root.handle.deleteTree(target_info.fragments_dir);

            if (target_info.set_as_active) {
                trySetCdb(b, target_info.target);
            }
        }
    }

    fn make(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
        _ = options;
        const self: *CombineCdbFragmentsStep = @fieldParentPtr("step", step);

        for (self.target_infos.items) |target_info| {
            combineCdbFragments(self.step.owner, target_info) catch |err| {
                std.debug.print("failed to combine compile command fragments: {any}\n", .{err});

                if (@errorReturnTrace()) |trace|
                    std.debug.dumpStackTrace(trace.*);
            };
        }
    }
};

pub const CompileFragment = struct {
    directory: []u8,
    file: []u8,
    output: []u8,
    arguments: [][]u8,
};

pub fn insertHiddenCombineCdbStep(
    b: *std.Build,
    step: **std.Build.Step,
    cdb_steps: *std.ArrayList(*CombineCdbFragmentsStep),
) void {
    var combine_cdb = CombineCdbFragmentsStep.create(b);
    cdb_steps.append(combine_cdb) catch @panic("OOM");

    // Make the true step depend on the combine-CDB step.
    step.*.dependOn(&combine_cdb.step);

    // We now hide the true step so the rest of the code can continue as if they are using
    // the top level step.
    step.* = &combine_cdb.step;
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

pub fn addGenerateCdbFragmentFlags(flags: *std.ArrayList([]const u8), dir: []const u8) !void {
    try flags.appendSlice(&.{ "-gen-cdb-fragment-path", dir });
}
