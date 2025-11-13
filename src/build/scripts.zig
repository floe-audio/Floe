// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Rather than bash scripts, we use Zig for our scripting needs because:
// - It's robustly cross-platform
// - Much easier to avoid painful debugging issues and complexity when doing something non-trivial
// - Removes our dependencies on various shell tools like parallel, fd, jq, etc.

const std = @import("std");
const builtin = @import("builtin");
const std_extras = @import("std_extras.zig");

pub fn main() !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer _ = arena.deinit();
    const allocator = arena.allocator();

    const args = try std.process.argsAlloc(allocator);

    if (args.len < 2) {
        std.debug.print("Usage: {s} <command>\n", .{args[0]});
        std.debug.print("Commands:\n", .{});
        std.debug.print("  format\n", .{});
        std.debug.print("  echo-latest-changes\n", .{});
        return;
    }

    const command = args[1];

    if (std.mem.eql(u8, command, "format")) {
        try runFormat(allocator);
    } else if (std.mem.eql(u8, command, "echo-latest-changes")) {
        try runEchoLatestChanges(allocator);
    } else if (std.mem.eql(u8, command, "ci")) {
        try runCi(allocator);
    } else {
        std.debug.print("Unknown command: {s}\n", .{command});
        std.process.exit(1);
    }
}

fn runFormat(allocator: std.mem.Allocator) !void {
    const source_files = try std_extras.findSourceFiles(allocator, .{
        .dir_path = "src",
        .extensions = &.{ ".cpp", ".hpp", ".h", ".mm" },
        .exclude_folders = &.{},
    });

    var args = std.ArrayList([]const u8).init(allocator);
    defer args.deinit();

    try args.append("clang-format");
    try args.append("-i");

    for (source_files) |file| {
        const full_path = try std.fs.path.join(allocator, &.{ "src", file });
        try args.append(try allocator.dupe(u8, full_path));
    }

    var child = std.process.Child.init(args.items, allocator);
    const result = try child.spawnAndWait();

    switch (result) {
        .Exited => |code| if (code != 0) std.process.exit(code),
        else => std.process.exit(1),
    }
}

fn runEchoLatestChanges(allocator: std.mem.Allocator) !void {
    const stdout = std.io.getStdOut().writer();

    // Read version file
    const version_content = std.fs.cwd().readFileAlloc(allocator, "version.txt", 1024) catch |err| {
        std.debug.print("Error reading version.txt: {}\n", .{err});
        std.process.exit(1);
    };

    const version = std.mem.trim(u8, version_content, " \n\r\t");

    // Read changelog
    const changelog_content = std.fs.cwd().readFileAlloc(allocator, "website/docs/changelog.md", 1024 * 1024) catch |err| {
        std.debug.print("Error reading changelog.md: {}\n", .{err});
        std.process.exit(1);
    };

    // Find version section
    const version_header = try std.fmt.allocPrint(allocator, "## {s}", .{version});

    var lines = std.mem.splitScalar(u8, changelog_content, '\n');
    var found_version = false;
    var output_lines = std.ArrayList([]const u8).init(allocator);
    defer output_lines.deinit();

    while (lines.next()) |line| {
        if (found_version) {
            // Check if we hit the next section (## followed by non-#)
            if (std.mem.startsWith(u8, line, "## ") and !std.mem.startsWith(u8, line, "## #")) {
                break;
            }
            try output_lines.append(line);
        } else if (std.mem.eql(u8, line, version_header)) {
            found_version = true;
        }
    }

    if (!found_version) {
        std.debug.print("Version {s} not found in changelog\n", .{version});
        std.process.exit(1);
    }

    // Print the changes without trailing newline
    for (output_lines.items, 0..) |line, i| {
        if (i == output_lines.items.len - 1 and output_lines.items.len > 0) {
            // Don't print newline for last line, and handle empty case
            if (line.len > 0) {
                try stdout.print("{s}", .{line});
            }
        } else {
            try stdout.print("{s}\n", .{line});
        }
    }
}

var std_stream_mutex: std.Thread.Mutex = .{};

fn runProcess(args: []const []const u8) void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer _ = arena.deinit();
    const allocator = arena.allocator();

    var child = std.process.Child.init(args, allocator);

    child.stdin_behavior = .Ignore;
    child.stdout_behavior = .Pipe;
    child.stderr_behavior = .Pipe;

    var stdout: std.ArrayListUnmanaged(u8) = .empty;
    errdefer stdout.deinit(allocator);
    var stderr: std.ArrayListUnmanaged(u8) = .empty;
    errdefer stderr.deinit(allocator);

    child.spawn() catch |err| {
        std.debug.print("Failed to spawn process: {any}\n", .{err});
        return;
    };
    errdefer {
        _ = child.kill() catch {};
    }
    child.collectOutput(allocator, &stdout, &stderr, 200 * 1024) catch |err| {
        std.debug.print("Failed to collect output: {any}\n", .{err});
        return;
    };
    const result = child.wait() catch |err| {
        std.debug.print("Failed to wait for process: {any}\n", .{err});
        return;
    };

    // TODO: probably put these into files and at the end of the CI, print out
    // the files if there were errors.
    // TODO: build GITHUB_STEP_SUMMARY
    if (stdout.items.len > 0) {
        std_stream_mutex.lock();
        defer std_stream_mutex.unlock();
        std.io.getStdErr().writer().print("{s}", .{stdout.items}) catch {};
    }
    if (stderr.items.len > 0) {
        std_stream_mutex.lock();
        defer std_stream_mutex.unlock();
        std.io.getStdErr().writer().print("{s}", .{stderr.items}) catch {};
    }

    // print result
    std.debug.print("return code: {d}\n", .{result.Exited});
}

fn runCi(allocator: std.mem.Allocator) !void {
    const zig_exe = try std.process.getEnvVarOwned(allocator, "ZIG_EXE");

    // Parallel for speed.
    var wg: std.Thread.WaitGroup = .{};

    wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "test" }});
    wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "test-clap-val" }});
    wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "test-pluginval" }});
    wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "test-vst3-val" }});

    wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "-Dbuild-mode=performance_profiling", "test" }});
    wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "-Dbuild-mode=performance_profiling", "test-clap-val" }});
    wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "-Dbuild-mode=performance_profiling", "test-pluginval" }});
    wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "-Dbuild-mode=performance_profiling", "test-vst3-val" }});

    switch (builtin.os.tag) {
        .linux => {
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "coverage" }});
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "valgrind" }});
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "-Dbuild-mode=performance_profiling", "valgrind" }});

            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "-Dsanitize-thread", "test" }});
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "-Dsanitize-thread", "-Dbuild-mode=performance_profiling", "test" }});

            // We choose Linux to do OS-agnostic checks.
            wg.spawnManager(runProcess, .{&.{
                zig_exe,
                "build",
                "-Dtargets=x86_64-linux,x86_64-windows,aarch64-macos",
                "clang-tidy",
            }});
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "check-reuse" }});
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "check-format" }});
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "check-spelling" }});
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "check-links" }});
        },
        .windows => {},
        .macos => {
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "-Dsanitize-thread", "test" }});
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "-Dsanitize-thread", "-Dbuild-mode=performance_profiling", "test" }});

            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "test-pluginval-au" }});
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "test-auval" }});
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "-Dbuild-mode=performance_profiling", "test-pluginval-au" }});
            wg.spawnManager(runProcess, .{&.{ zig_exe, "build", "-Dbuild-mode=performance_profiling", "test-auval" }});
        },
        else => {},
    }

    wg.wait();
}
