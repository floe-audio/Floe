// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Rather than bash scripts, we use Zig for our scripting needs because:
// - It's robustly cross-platform
// - Much easier to avoid painful debugging issues and complexity when doing something non-trivial
// - Removes our dependencies on various shell tools like parallel, fd, jq, etc.

const std = @import("std");
const builtin = @import("builtin");
const std_extras = @import("std_extras.zig");
const TestHttpServer = @import("TestHttpServer.zig");

const Context = struct {
    allocator: std.mem.Allocator,
    env_map: std.process.EnvMap,
};

pub fn main() !u8 {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer _ = arena.deinit();

    var context = Context{
        .allocator = arena.allocator(),
        .env_map = try std.process.getEnvMap(arena.allocator()),
    };

    const args = try std.process.argsAlloc(context.allocator);

    if (args.len < 2) {
        std.debug.print("Usage: {s} <command>\n", .{args[0]});
        std.debug.print("Commands:\n", .{});
        std.debug.print("  format\n", .{});
        std.debug.print("  echo-latest-changes\n", .{});
        std.debug.print("  upload-errors\n", .{});
        return 1;
    }

    const command = args[1];

    if (std.mem.eql(u8, command, "format")) {
        return runFormat(&context);
    } else if (std.mem.eql(u8, command, "echo-latest-changes")) {
        return runEchoLatestChanges(&context);
    } else if (std.mem.eql(u8, command, "upload-errors")) {
        return runUploadErrors(&context);
    } else if (std.mem.eql(u8, command, "ci")) {
        return runCi(&context);
    } else {
        std.debug.print("Unknown command: {s}\n", .{command});
        return 1;
    }
}

fn runFormat(context: *Context) !u8 {
    const source_files = try std_extras.findSourceFiles(context.allocator, .{
        .dir_path = "src",
        .extensions = &.{ ".cpp", ".hpp", ".h", ".mm" },
        .exclude_folders = &.{},
    });

    var args = std.ArrayList([]const u8).init(context.allocator);
    defer args.deinit();

    try args.append("clang-format");
    try args.append("-i");

    for (source_files) |file| {
        const full_path = try std.fs.path.join(context.allocator, &.{ "src", file });
        try args.append(try context.allocator.dupe(u8, full_path));
    }

    var child = std.process.Child.init(args.items, context.allocator);
    const result = try child.spawnAndWait();

    return switch (result) {
        .Exited => |code| code,
        else => 1,
    };
}

fn runEchoLatestChanges(context: *Context) !u8 {
    const stdout = std.io.getStdOut().writer();

    // Read version file
    const version_content = std.fs.cwd().readFileAlloc(context.allocator, "version.txt", 1024) catch |err| {
        std.debug.print("Error reading version.txt: {}\n", .{err});
        return 1;
    };

    const version = std.mem.trim(u8, version_content, " \n\r\t");

    // Read changelog
    const changelog_content = std.fs.cwd().readFileAlloc(context.allocator, "website/docs/changelog.md", 1024 * 1024) catch |err| {
        std.debug.print("Error reading changelog.md: {}\n", .{err});
        return 1;
    };

    // Find version section
    const version_header = try std.fmt.allocPrint(context.allocator, "## {s}", .{version});

    var lines = std.mem.splitScalar(u8, changelog_content, '\n');
    var found_version = false;
    var output_lines = std.ArrayList([]const u8).init(context.allocator);
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
        return 1;
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
    return 0;
}

fn runUploadErrors(context: *Context) !u8 {
    // Determine the logs directory based on the OS
    const logs_dir = switch (builtin.os.tag) {
        .linux => blk: {
            const home = context.env_map.get("HOME") orelse {
                std.debug.print("HOME environment variable not set\n", .{});
                return 1;
            };
            break :blk try std.fs.path.join(context.allocator, &.{ home, ".local", "state", "Floe", "Logs" });
        },
        .macos => blk: {
            const home = context.env_map.get("HOME") orelse {
                std.debug.print("HOME environment variable not set\n", .{});
                return 1;
            };
            break :blk try std.fs.path.join(context.allocator, &.{ home, "Library", "Logs", "Floe" });
        },
        .windows => blk: {
            const localappdata = context.env_map.get("LOCALAPPDATA") orelse {
                std.debug.print("LOCALAPPDATA environment variable not set\n", .{});
                return 1;
            };
            break :blk try std.fs.path.join(context.allocator, &.{ localappdata, "Floe", "Logs" });
        },
        else => {
            std.debug.print("Unsupported OS\n", .{});
            return 1;
        },
    };

    // Check if directory exists
    var dir = std.fs.cwd().openDir(logs_dir, .{ .iterate = true }) catch |err| switch (err) {
        error.FileNotFound => {
            // Directory doesn't exist, nothing to do
            return 0;
        },
        else => {
            std.debug.print("Error opening logs directory: {}\n", .{err});
            return 1;
        },
    };
    defer dir.close();

    // Iterate through files in the directory
    var iterator = dir.iterate();
    while (try iterator.next()) |entry| {
        if (entry.kind != .file) continue;

        // Check if file has .floe-report extension
        if (!std.mem.endsWith(u8, entry.name, ".floe-report")) continue;

        // Upload the report using sentry-cli
        const full_path = try std.fs.path.join(context.allocator, &.{ logs_dir, entry.name });

        const result = std.process.Child.run(.{
            .allocator = context.allocator,
            .argv = &.{ "sentry-cli", "send-envelope", "--raw", full_path },
        }) catch |err| {
            std.debug.print("Failed to run sentry-cli for {s}: {}\n", .{ entry.name, err });
            continue;
        };
        defer context.allocator.free(result.stdout);
        defer context.allocator.free(result.stderr);

        // Print captured outputs to stderr
        if (result.stdout.len > 0) {
            std.io.getStdErr().writer().writeAll(result.stdout) catch {};
        }
        if (result.stderr.len > 0) {
            std.io.getStdErr().writer().writeAll(result.stderr) catch {};
        }

        switch (result.term) {
            .Exited => |code| {
                if (code == 0) {
                    // Successfully uploaded, remove the file
                    dir.deleteFile(entry.name) catch |err| {
                        std.debug.print("Warning: Failed to delete {s}: {}\n", .{ entry.name, err });
                    };
                } else {
                    std.debug.print("sentry-cli failed for {s} with exit code {d}\n", .{ entry.name, code });
                }
            },
            else => {
                std.debug.print("sentry-cli terminated unexpectedly for {s}: {any}\n", .{ entry.name, result.term });
            },
        }
    }

    return 0;
}

const CiTask = struct {
    args: []const []const u8,
    term: std.process.Child.Term,
    stdout: []u8,
    stderr: []u8,
    time_taken_seconds: f64,

    pub fn writeArgs(self: *const CiTask, writer: anytype) !void {
        try writer.writeAll("zig build");
        for (self.args) |arg| {
            try writer.print(" {s}", .{arg});
        }
    }
};

const CiReport = struct {
    context: *Context,
    mutex: std.Thread.Mutex,
    arena: std.heap.ArenaAllocator,
    tasks: std.ArrayListUnmanaged(CiTask),

    pub fn returnCode(self: *const CiReport) u8 {
        for (self.tasks.items) |task| {
            if (task.term != .Exited or task.term.Exited != 0) {
                return 1;
            }
        }
        return 0;
    }

    fn setColorWithFlush(buffered_writer: anytype, console: std.io.tty.Config, color: std.io.tty.Color) !void {
        try buffered_writer.flush();
        try console.setColor(buffered_writer.writer(), color);
        try buffered_writer.flush();
    }

    fn sortTasksAlphabetic(ctx: void, a: CiTask, b: CiTask) bool {
        _ = ctx;
        var a_buf = std.BoundedArray(u8, 4096).init(0) catch unreachable;
        var b_buf = std.BoundedArray(u8, 4096).init(0) catch unreachable;

        a.writeArgs(a_buf.writer()) catch @panic("args too long");
        b.writeArgs(b_buf.writer()) catch @panic("args too long");

        return std.ascii.lessThanIgnoreCase(a_buf.slice(), b_buf.slice());
    }

    pub fn print(report: *CiReport) !void {
        std.sort.pdq(CiTask, report.tasks.items, {}, sortTasksAlphabetic);

        // Print diagnostics for failed tasks.
        {
            const stream = std.io.getStdErr();
            var stderr_buffered = std.io.bufferedWriter(stream.writer());
            const writer = stderr_buffered.writer();

            var console = std.io.tty.detectConfig(stream);

            // GitHub Actions logs seem to always show ANSI escape codes correctly, even if the terminal config isn't
            // detected properly.
            if (report.context.env_map.get("GITHUB_ACTIONS") != null) console = std.io.tty.Config.escape_codes;

            for (report.tasks.items) |task| {
                if (task.term == .Exited and task.term.Exited == 0) {
                    continue;
                }

                // Heading.
                {
                    try setColorWithFlush(&stderr_buffered, console, .magenta);
                    try writer.writeAll("[");

                    try writer.writeAll("\"");
                    try task.writeArgs(writer);
                    try writer.writeAll("\"");

                    switch (task.term) {
                        .Exited => |code| {
                            try std.fmt.format(writer, " failed with exit code {d}", .{code});
                        },
                        .Signal, .Stopped, .Unknown => {
                            try std.fmt.format(writer, " terminated unexpectedly by {any}", .{task.term});
                        },
                    }

                    try writer.writeAll("]\n");
                    try setColorWithFlush(&stderr_buffered, console, .reset);
                }

                // Stdout and stderr.
                const Output = struct {
                    name: []const u8,
                    data: []u8,
                };
                for ([_]Output{
                    .{ .name = "stdout", .data = task.stdout },
                    .{ .name = "stderr", .data = task.stderr },
                }) |output| {
                    try setColorWithFlush(&stderr_buffered, console, .blue);
                    try std.fmt.format(writer, "[{s}]\n", .{output.name});
                    try setColorWithFlush(&stderr_buffered, console, .reset);

                    try writer.writeAll(output.data);
                    try writer.writeAll("\n");
                }
            }
            try stderr_buffered.flush();
        }

        // Summary: markdown table to stdout
        {
            var stdout_buffered = std.io.bufferedWriter(std.io.getStdOut().writer());
            const writer = stdout_buffered.writer();

            try writer.writeAll("| Command | Exit Code | Time Taken |\n");
            try writer.writeAll("|---|---|---|\n");

            for (report.tasks.items) |task| {
                try writer.writeAll("| ");

                // Command column
                try task.writeArgs(writer);

                try writer.writeAll(" | ");

                // Exit code/termination info column
                switch (task.term) {
                    .Exited => |code| {
                        try writer.print("{s} {d}", .{ if (code == 0) "✅" else "❌", code });
                    },
                    .Signal, .Stopped, .Unknown => {
                        try writer.print("{s} Terminated: {any}", .{ "❌", task.term });
                    },
                }

                try writer.writeAll(" | ");

                // Time taken column
                try writer.print("{d:.2}s", .{task.time_taken_seconds});

                try writer.writeAll(" |\n");
            }

            try stdout_buffered.flush();
        }
    }
};

// Args are assumed to live for the remaining duration of the program.
fn runZigBuild(ci_report: *CiReport, args: []const []const u8) void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer _ = arena.deinit();
    const allocator = arena.allocator();

    const zig_exe = ci_report.context.env_map.get("ZIG_EXE") orelse {
        std.debug.print("Environment variable ZIG_EXE not set\n", .{});
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

    var timer = std.time.Timer.start() catch @panic("Timer unavailable");

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
            .time_taken_seconds = @as(f64, @floatFromInt(timer.read())) / std.time.ns_per_s,
        };
        ci_report.tasks.append(ci_allocator, task) catch @panic("OOM");
    }
}

fn spawnZigBuild(wg: *std.Thread.WaitGroup, ci_report: *CiReport, args: []const []const u8) void {
    const args_copy = ci_report.arena.allocator().dupe([]const u8, args) catch @panic("OOM");
    wg.spawnManager(runZigBuild, .{ ci_report, args_copy });
}

// Outputs markdown table to stdout.
fn runCi(context: *Context) !u8 {
    var tsa: std.heap.ThreadSafeAllocator = .{ .child_allocator = context.allocator };

    var ci_report: CiReport = .{
        .context = context,
        .mutex = .{},
        .arena = std.heap.ArenaAllocator.init(tsa.allocator()),
        .tasks = std.ArrayListUnmanaged(CiTask).empty,
    };

    // Start a simple HTTP server so that tests can use it.
    var http_server = TestHttpServer.init(tsa.allocator(), 8081) catch |err| {
        std.debug.print("Failed to initialize HTTP server: {}\n", .{err});
        return 1;
    };
    defer http_server.stop();
    http_server.start() catch |err| {
        std.debug.print("Failed to start HTTP server: {}\n", .{err});
        return 1;
    };

    // Parallel for speed.
    var wg: std.Thread.WaitGroup = .{};

    // Just a standard zig build.
    const empty_args: []const []const u8 = &.{};
    spawnZigBuild(&wg, &ci_report, empty_args);

    const core_tests = [_][]const u8{ "test", "test:clap-val", "test:pluginval", "test:vst3-val" };

    // Tests in debug mode.
    for (core_tests) |test_cmd| {
        spawnZigBuild(&wg, &ci_report, &.{
            test_cmd,
            "--prefix",
            "zig-out/debug",
        });
    }

    // Tests in optimised mode.
    for (core_tests) |test_cmd| {
        spawnZigBuild(&wg, &ci_report, &.{
            test_cmd,
            "-Dbuild-mode=performance_profiling",
            "--prefix",
            "zig-out/optimised",
        });
    }

    switch (builtin.os.tag) {
        .linux => {
            spawnZigBuild(&wg, &ci_report, &.{
                "test-coverage",
            });

            // Valgrind.
            // IMPROVE: run validators (in single-process mode) through valgrind
            spawnZigBuild(&wg, &ci_report, &.{
                "test:valgrind",
            });
            spawnZigBuild(&wg, &ci_report, &.{
                "test:valgrind",
                "-Dbuild-mode=performance_profiling",
            });

            // Unit tests with thread sanitizer.
            spawnZigBuild(&wg, &ci_report, &.{
                "test",
                "-Dsanitize-thread",
            });
            spawnZigBuild(&wg, &ci_report, &.{
                "test",
                "-Dsanitize-thread",
                "-Dbuild-mode=performance_profiling",
            });

            spawnZigBuild(&wg, &ci_report, &.{
                "test",
                "-Dsanitize-thread",
                "--prefix",
                "zig-out/debug-sanitized",
            });
            spawnZigBuild(&wg, &ci_report, &.{
                "test",
                "-Dsanitize-thread",
                "-Dbuild-mode=performance_profiling",
                "--prefix",
                "zig-out/optimised-sanitized",
            });

            // We choose Linux to do OS-agnostic checks.
            spawnZigBuild(&wg, &ci_report, &.{
                "check:clang-tidy",
                "-Dtargets=x86_64-linux,x86_64-windows,aarch64-macos",
            });

            spawnZigBuild(&wg, &ci_report, &.{"check:reuse"});
            spawnZigBuild(&wg, &ci_report, &.{"check:format"});
            spawnZigBuild(&wg, &ci_report, &.{"check:spelling"});

            spawnZigBuild(&wg, &ci_report, &.{"script:website-build"});
        },
        .windows => {
            spawnZigBuild(&wg, &ci_report, &.{"test:windows-install"});
        },
        .macos => {
            spawnZigBuild(&wg, &ci_report, &.{ "-Dsanitize-thread", "test" });
            spawnZigBuild(&wg, &ci_report, &.{ "-Dsanitize-thread", "-Dbuild-mode=performance_profiling", "test" });

            const au_install_location = try auInstallLocation(context);
            spawnZigBuild(&wg, &ci_report, &.{ "test:pluginval-au", "--prefix", au_install_location });
            spawnZigBuild(&wg, &ci_report, &.{ "test:auval", "--prefix", au_install_location });
        },
        else => {},
    }

    wg.wait();

    switch (builtin.os.tag) {
        .macos => {
            // AU tests require the plugin to be installed to the system location. We therefore cannot run
            // multiple parallel AU testing with different binaries (such as debug binary and optimised binary).
            // So we run another set of AU tests here.

            wg.reset();

            const au_install_location = try auInstallLocation(context);

            spawnZigBuild(&wg, &ci_report, &.{
                "test:pluginval-au",
                "-Dbuild-mode=performance_profiling",
                "--prefix",
                au_install_location,
            });
            spawnZigBuild(&wg, &ci_report, &.{
                "test:auval",
                "-Dbuild-mode=performance_profiling",
                "--prefix",
                au_install_location,
            });

            wg.wait();
        },
        else => {},
    }

    ci_report.print() catch |err| {
        std.debug.print("Failed to print CI report: {any}\n", .{err});
        return 1;
    };

    // Upload logs
    _ = try runUploadErrors(context);

    return ci_report.returnCode();
}

fn auInstallLocation(context: *Context) ![]const u8 {
    // Get HOME
    const home = context.env_map.get("HOME") orelse {
        return error.EnvVarNotSet;
    };
    const result = try std.fs.path.join(context.allocator, &.{ home, "Library", "Audio", "Plug-Ins" });
    return result;
}
