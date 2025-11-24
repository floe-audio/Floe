// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");
const builtin = @import("builtin");

pub fn findSourceFiles(allocator: std.mem.Allocator, options: struct {
    dir_path: []const u8,
    extensions: []const []const u8,
    exclude_folders: []const []const u8,
    exclude_hidden_folders: bool = true,
    respect_gitignore: bool = true,
}) ![][]const u8 {
    var files = std.ArrayList([]const u8).init(allocator);
    var dir = try std.fs.cwd().openDir(options.dir_path, .{ .iterate = true });
    defer dir.close();

    var walker = try dir.walk(allocator);
    defer walker.deinit();

    while (try walker.next()) |entry| {
        var should_exclude = false;

        if (options.exclude_hidden_folders) {
            // Check if any part of the path contains a hidden folder (starts with '.')
            var path_iter = std.mem.splitScalar(u8, entry.path, std.fs.path.sep);
            while (path_iter.next()) |path_component| {
                if (path_component.len > 0 and path_component[0] == '.') {
                    should_exclude = true;
                    break;
                }
            }
        }

        for (options.exclude_folders) |exclude_folder| {
            if (std.mem.indexOf(u8, entry.path, exclude_folder) != null) {
                should_exclude = true;
                break;
            }
        }
        if (should_exclude) continue;

        if (entry.kind == .file) {
            const ext = std.fs.path.extension(entry.path);
            for (options.extensions) |target_ext| {
                if (std.mem.eql(u8, ext, target_ext)) {
                    try files.append(try allocator.dupe(u8, entry.path));
                    break;
                }
            }
        }
    }

    const all_files = try files.toOwnedSlice();

    if (options.respect_gitignore) {
        return filterGitIgnored(allocator, all_files) catch |err| switch (err) {
            // If git operations fail, just return all files
            error.FileNotFound, error.GitCheckIgnoreFailed => all_files,
            else => return err,
        };
    }

    return all_files;
}

// Rather than parse .gitignore files ourselves, we outsource the work to 'git check-ignore'.
fn filterGitIgnored(allocator: std.mem.Allocator, files: [][]const u8) ![][]const u8 {
    if (files.len == 0) return files;

    var input = std.ArrayList(u8).init(allocator);
    defer input.deinit();

    for (files) |file| {
        try input.appendSlice(file);
        try input.append('\n');
    }

    var child = std.process.Child.init(&.{ "git", "check-ignore", "--stdin", "--no-index" }, allocator);
    child.stdin_behavior = .Pipe;
    child.stdout_behavior = .Pipe;
    child.stderr_behavior = .Pipe;

    try child.spawn();

    try child.stdin.?.writeAll(input.items);
    child.stdin.?.close();
    child.stdin = null;

    var stdout_buf: std.ArrayListUnmanaged(u8) = .empty;
    var stderr_buf: std.ArrayListUnmanaged(u8) = .empty;
    defer stderr_buf.deinit(allocator);
    defer stdout_buf.deinit(allocator);

    try child.collectOutput(allocator, &stdout_buf, &stderr_buf, 1024 * 1024);
    const term = try child.wait();

    // Exit code 1 means no files are ignored (normal case)
    // Exit code 0 means some files are ignored
    switch (term) {
        .Exited => |code| {
            if (code != 0 and code != 1) {
                return error.GitCheckIgnoreFailed;
            }
        },
        else => {
            return error.GitCheckIgnoreFailed;
        },
    }

    var ignored_set = std.StringHashMap(void).init(allocator);
    defer ignored_set.deinit();

    var lines = std.mem.splitScalar(u8, stdout_buf.items, '\n');
    while (lines.next()) |line| {
        const trimmed = std.mem.trim(u8, line, " \t\r\n");
        if (trimmed.len > 0) {
            try ignored_set.put(trimmed, {});
        }
    }

    // Return files that are NOT ignored
    var filtered: std.ArrayListUnmanaged([]const u8) = .empty;
    try filtered.ensureTotalCapacity(allocator, files.len);
    for (files) |file| {
        if (!ignored_set.contains(file)) {
            filtered.appendAssumeCapacity(file);
        }
    }

    return filtered.toOwnedSlice(allocator);
}

/// Recursively copies a directory and all its contents from source to destination. If files in the
/// source and dest are identical, they are not copied again.
pub fn copyDirRecursive(src_path: []const u8, dest_path: []const u8, allocator: std.mem.Allocator) !void {
    var src_dir = std.fs.cwd().openDir(src_path, .{ .iterate = true }) catch |err| switch (err) {
        error.FileNotFound => return,
        else => return err,
    };
    defer src_dir.close();

    std.fs.cwd().makePath(dest_path) catch |err| switch (err) {
        error.PathAlreadyExists => {},
        else => return err,
    };

    var dest_dir = try std.fs.cwd().openDir(dest_path, .{});
    defer dest_dir.close();

    var walker = try src_dir.walk(allocator);
    defer walker.deinit();

    while (try walker.next()) |entry| {
        switch (entry.kind) {
            .file => {
                _ = try entry.dir.updateFile(entry.basename, dest_dir, entry.path, .{});
            },
            .directory => {
                try dest_dir.makePath(entry.path);
            },
            else => continue, // Skip other types like symlinks
        }
    }
}

pub fn archAndOsPair(target: std.Target) std.BoundedArray(u8, 32) {
    var result = std.BoundedArray(u8, 32).init(0) catch @panic("OOM");
    std.fmt.format(
        result.writer(),
        "{s}-{s}",
        .{ @tagName(target.cpu.arch), @tagName(target.os.tag) },
    ) catch @panic("OOM");
    return result;
}

// When using Step.Run, Zig never never prints stdout to the console, only stderr. If the program doesn't put
// debug information in stderr, you don't see anything other than the exit code. This function wraps a command
// such that its stdout is redirected to stderr, so you see all output allowing you to debug why a program fails.
pub fn createCommandWithStdoutToStderr(
    b: *std.Build,
    target: ?std.Target,
    name: []const u8,
) *std.Build.Step.Run {
    const run = std.Build.Step.Run.create(b, name);

    // IMPROVE: it would probably be more robust to create a small Zig program (like fetch()) that does this
    // redirection so we can guarantee cross-platform compatibility and don't have to do the bash vs batch stuff.

    if (builtin.os.tag == .windows) {
        const batch_wrapper = b.addWriteFiles().add("stdout-to-stderr-wrapper.bat",
            \\@echo off
            \\%* 1>&2
            \\exit /b %errorlevel%
        );
        run.addFileArg(batch_wrapper);
    } else {
        const bash_wrapper = b.addWriteFiles().add("stdout-to-stderr-wrapper.sh",
            \\#!/usr/bin/env bash
            \\exec "$@" >&2
        );
        if (target != null and target.?.os.tag == .windows) {
            if (builtin.os.tag == .linux and b.enable_wine) {
                run.addArg("bash");
                run.addFileArg(bash_wrapper);
                run.addArg("wine64");
            } else {
                // It's probably not going to work, but we can try natively. At any rate, we are deferring the
                // error to later when the actual command is run (which might never happen).
                run.addArg("bash");
                run.addFileArg(bash_wrapper);
            }
        } else {
            // macOS or Linux - use system pluginval
            run.addArg("bash");
            run.addFileArg(bash_wrapper);
        }
    }

    return run;
}

// This function is from from TigerBeetle. Modified slightly to fit our needs.
// Copyright TigerBeetle
// SPDX-License-Identifier: Apache-2.0
// Use 'zig fetch' to download and unpack the specified URL, optionally verifying the checksum.
pub fn fetch(b: *std.Build, options: struct {
    url: []const u8,
    file_name: []const u8,
    hash: ?[]const u8,
    executable: bool = false,
}) std.Build.LazyPath {
    const copy_from_cache = b.addRunArtifact(b.addExecutable(.{
        .name = "copy-from-cache",
        .root_module = b.createModule(.{
            .root_source_file = b.addWriteFiles().add("main.zig",
                \\const builtin = @import("builtin");
                \\const std = @import("std");
                \\const assert = std.debug.assert;
                \\
                \\pub fn main() !void {
                \\    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
                \\    const allocator = arena.allocator();
                \\    const args = try std.process.argsAlloc(allocator);
                \\    assert(args.len == 7);
                \\
                \\    const hash_and_newline = try std.fs.cwd().readFileAlloc(allocator, args[2], 128);
                \\    assert(hash_and_newline[hash_and_newline.len - 1] == '\n');
                \\    const hash = hash_and_newline[0 .. hash_and_newline.len - 1];
                \\    if (!std.mem.eql(u8, args[5], "null") and !std.mem.eql(u8, args[5], hash)) {
                \\        std.debug.panic(
                \\            \\bad hash
                \\            \\specified:  {s}
                \\            \\downloaded: {s}
                \\            \\
                \\        , .{ args[5], hash });
                \\    }
                \\
                \\    const source_path = try std.fs.path.join(allocator, &.{ args[1], hash, args[3] });
                \\    try std.fs.cwd().copyFile(
                \\        source_path,
                \\        std.fs.cwd(),
                \\        args[4],
                \\        .{},
                \\    );
                \\    
                \\    if (std.mem.eql(u8, args[6], "executable")) {
                \\        if (builtin.os.tag != .windows) {
                \\            const file = try std.fs.cwd().openFile(args[4], .{});
                \\            defer file.close();
                \\            const permissions = std.fs.File.Permissions{
                \\                .inner = std.fs.File.PermissionsUnix.unixNew(0o755),
                \\            };
                \\            try file.setPermissions(permissions);
                \\        }
                \\    }
                \\}
            ),
            .target = b.graph.host,
        }),
    }));
    copy_from_cache.addArg(
        b.graph.global_cache_root.join(b.allocator, &.{"p"}) catch @panic("OOM"),
    );
    copy_from_cache.addFileArg(
        b.addSystemCommand(&.{ b.graph.zig_exe, "fetch", options.url }).captureStdOut(),
    );
    copy_from_cache.addArg(options.file_name);
    const result = copy_from_cache.addOutputFileArg(options.file_name);
    copy_from_cache.addArg(options.hash orelse "null");
    copy_from_cache.addArg(if (options.executable) "executable" else "null");
    return result;
}

// Loads environment variables from a .env file into the build graph's env_map.
// Based on https://github.com/zigster64/dotenv.zig
// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Scribe of the Ziggurat
pub fn loadEnvFile(dir: std.fs.Dir, env_map: *std.process.EnvMap) !void {
    var file = dir.openFile(".env", .{}) catch {
        return;
    };
    defer file.close();

    var buf_reader = std.io.bufferedReader(file.reader());
    var in_stream = buf_reader.reader();
    var buf: [1024 * 16]u8 = undefined;

    while (try in_stream.readUntilDelimiterOrEof(&buf, '\n')) |line| {
        // ignore commented out lines
        if (line.len > 0 and line[0] == '#') {
            continue;
        }
        // split into KEY and Value
        if (std.mem.indexOf(u8, line, "=")) |index| {
            const key = line[0..index];
            var value = line[index + 1 ..];

            // If the value starts and ends with quotes, remove them
            if (value.len >= 2 and ((value[0] == '"' and value[value.len - 1] == '"') or
                (value[0] == '\'' and value[value.len - 1] == '\'')))
            {
                value = value[1 .. value.len - 1];
            }

            try env_map.put(key, value);
        }
    }
}

pub fn pathExists(path: []const u8) bool {
    std.fs.accessAbsolute(path, .{}) catch |err| switch (err) {
        error.FileNotFound => return false,
        else => return true, // Other error - let's just say it exists.
    };
    return true;
}

pub fn decodeBase64(allocator: std.mem.Allocator, name: []const u8, base64: []const u8) []const u8 {
    const size = std.base64.standard.Decoder.calcSizeForSlice(base64) catch {
        std.debug.panic("Invalid base64 in {s}", .{name});
    };

    const decoded = allocator.alloc(u8, size) catch @panic("OOM");

    std.base64.standard.Decoder.decode(decoded, base64) catch {
        std.debug.panic("Invalid base64 in {s}", .{name});
    };

    return decoded;
}

pub fn validEnvVar(b: *std.Build, name: []const u8, skip_description: ?[]const u8, decode_base64: bool) ?[]const u8 {
    const desc = if (skip_description) |desc| desc else "build may be incomplete";
    const val = b.graph.env_map.get(name) orelse {
        std.log.warn("{s} not set, {s}", .{ name, desc });
        return null;
    };
    if (val.len == 0) {
        std.log.warn("{s} is empty, {s}", .{ name, desc });
        return null;
    }

    return if (decode_base64)
        return decodeBase64(b.allocator, name, val)
    else
        val;
}
