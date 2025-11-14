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

const CiTask = struct {
    args: []const []const u8,
    term: std.process.Child.Term,
    stdout: []u8,
    stderr: []u8,
};

const CiReport = struct {
    mutex: std.Thread.Mutex,
    arena: std.heap.ArenaAllocator,
    tasks: std.ArrayListUnmanaged(CiTask),

    pub fn print(report: *CiReport) void {
        for (report.tasks.items) |task| {
            switch (task.term) {
                .Exited => |code| {
                    if (code != 0) {
                        std.debug.print("[stdout]\n{s}\n", .{task.stdout});
                        std.debug.print("[stderr]\n{s}\n", .{task.stderr});
                    }
                },
                .Signal, .Stopped, .Unknown => {
                    std.debug.print("[stdout]\n{s}\n", .{task.stdout});
                    std.debug.print("[stderr]\n{s}\n", .{task.stderr});
                },
            }
        }

        // Summary: args and exit code
        std.debug.print("\n=== CI Summary ===\n", .{});
        for (report.tasks.items) |task| {
            switch (task.term) {
                .Exited => |code| {
                    std.debug.print("Command: ", .{});
                    for (task.args, 0..) |arg, i| {
                        if (i != 0) std.debug.print(" ", .{});
                        std.debug.print("{s}", .{arg});
                    }
                    std.debug.print(" => Exit code: {d}\n", .{code});
                },
                .Signal, .Stopped, .Unknown => {
                    std.debug.print("Command: ", .{});
                    for (task.args, 0..) |arg, i| {
                        if (i != 0) std.debug.print(" ", .{});
                        std.debug.print("{s}", .{arg});
                    }
                    std.debug.print(" => Terminated by signal, stopped or unknown\n", .{});
                },
            }
        }
    }
};

// Args are assumed to be static data.
fn runZigBuild(ci_report: *CiReport, args: []const []const u8) void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer _ = arena.deinit();
    const allocator = arena.allocator();

    const zig_exe = std.process.getEnvVarOwned(allocator, "ZIG_EXE") catch |err| {
        std.debug.print("Environment variable ZIG_EXE not set: {any}\n", .{err});
        return;
    };

    var child = std.process.Child.init(blk: {
        const full_args = allocator.alloc([]const u8, args.len + 2) catch @panic("OOM");
        full_args[0] = zig_exe;
        full_args[1] = "build";
        for (args, 0..) |arg, i| {
            full_args[i + 2] = arg;
        }
        break :blk full_args;
    }, allocator);

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
    child.collectOutput(allocator, &stdout, &stderr, 20 * 1024 * 1024) catch |err| {
        std.debug.print("Failed to collect output: {any}\n", .{err});
        return;
    };

    const result = child.wait() catch |err| {
        std.debug.print("Failed to wait for process: {any}\n", .{err});
        return;
    };

    {
        ci_report.mutex.lock();
        defer ci_report.mutex.unlock();
        const ci_allocator = ci_report.arena.allocator();
        const task = CiTask{
            .args = args,
            .term = result,
            .stdout = ci_allocator.dupe(u8, stdout.items) catch @panic("OOM"),
            .stderr = ci_allocator.dupe(u8, stderr.items) catch @panic("OOM"),
        };
        ci_report.tasks.append(ci_allocator, task) catch @panic("OOM");
    }
}

fn runCi(allocator: std.mem.Allocator) !void {
    var ci_report: CiReport = .{
        .mutex = .{},
        .arena = std.heap.ArenaAllocator.init(allocator),
        .tasks = std.ArrayListUnmanaged(CiTask).empty,
    };

    // Parallel for speed.
    var wg: std.Thread.WaitGroup = .{};

    // TODO: we need to specify --prefix for these various commands so their installations don't interfere with each other.

    wg.spawnManager(runZigBuild, .{ &ci_report, &.{"test"} });
    wg.spawnManager(runZigBuild, .{ &ci_report, &.{"test-clap-val"} });
    wg.spawnManager(runZigBuild, .{ &ci_report, &.{"test-pluginval"} });
    wg.spawnManager(runZigBuild, .{ &ci_report, &.{"test-vst3-val"} });

    wg.spawnManager(runZigBuild, .{ &ci_report, &.{ "-Dbuild-mode=performance_profiling", "test" } });
    wg.spawnManager(runZigBuild, .{ &ci_report, &.{ "-Dbuild-mode=performance_profiling", "test-clap-val" } });
    wg.spawnManager(runZigBuild, .{ &ci_report, &.{ "-Dbuild-mode=performance_profiling", "test-pluginval" } });
    wg.spawnManager(runZigBuild, .{ &ci_report, &.{ "-Dbuild-mode=performance_profiling", "test-vst3-val" } });

    if (false) {
        switch (builtin.os.tag) {
            .linux => {
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{"coverage"} });
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{"valgrind"} });
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{ "-Dbuild-mode=performance_profiling", "valgrind" } });

                wg.spawnManager(runZigBuild, .{ &ci_report, &.{ "-Dsanitize-thread", "test" } });
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{ "-Dsanitize-thread", "-Dbuild-mode=performance_profiling", "test" } });

                // We choose Linux to do OS-agnostic checks.
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{
                    "-Dtargets=x86_64-linux,x86_64-windows,aarch64-macos",
                    "clang-tidy",
                } });
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{"check-reuse"} });
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{"check-format"} });
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{"check-spelling"} });
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{"check-links"} });
            },
            .windows => {},
            .macos => {
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{ "-Dsanitize-thread", "test" } });
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{ "-Dsanitize-thread", "-Dbuild-mode=performance_profiling", "test" } });

                wg.spawnManager(runZigBuild, .{ &ci_report, &.{"test-pluginval-au"} });
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{"test-auval"} });
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{ "-Dbuild-mode=performance_profiling", "test-pluginval-au" } });
                wg.spawnManager(runZigBuild, .{ &ci_report, &.{ "-Dbuild-mode=performance_profiling", "test-auval" } });
            },
            else => {},
        }
    }

    wg.wait();

    ci_report.print();
}
