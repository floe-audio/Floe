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
        return 1;
    }

    const command = args[1];

    if (std.mem.eql(u8, command, "format")) {
        return runFormat(&context);
    } else if (std.mem.eql(u8, command, "create-gh-release")) {
        return runCreateGithubRelease(&context);
    } else if (std.mem.eql(u8, command, "upload-errors")) {
        return runUploadErrors(&context);
    } else if (std.mem.eql(u8, command, "ci")) {
        return runCi(&context, .full);
    } else if (std.mem.eql(u8, command, "ci-basic")) {
        return runCi(&context, .basic);
    } else if (std.mem.eql(u8, command, "website-promote-beta-to-stable")) {
        return runWebsitePromoteBetaToStable(&context);
    } else {
        std.log.err("Unknown command: {s}\n", .{command});
        return 1;
    }
}

// `docusaurus docs:version` does not allow overwriting an existing version.
// We workaround this by deleting the existing stable version. But if we
// were to now run `docs:version stable`, it errors because as part of its
// process it partly builds the site (to get the sidebars, etc.) and finds
// references to the versions that we just deleted. So we temporarily
// remove all references.
fn runWebsitePromoteBetaToStable(context: *Context) !u8 {
    const website_path = "website";

    var dir = try std.fs.cwd().openDir(website_path, .{});
    defer dir.close();

    dir.deleteFile("versions.json") catch |err| switch (err) {
        error.FileNotFound => {},
        else => return err,
    };

    // These already handle the case where the directories don't exist.
    try dir.deleteTree("versioned_sidebars");
    try dir.deleteTree("versioned_docs");

    // Workaround errors due to missing version references.
    const config_name = "versions-config.js";
    const backup_config_name = "versions-config.js.backup";
    try dir.copyFile(config_name, dir, backup_config_name, .{});
    defer {
        // Restore. We can't really do anything about errors here.
        dir.rename(backup_config_name, config_name) catch {
            std.log.warn("Failed to restore versions-config.js from backup\n", .{});
        };
    }

    try dir.writeFile(.{ .sub_path = config_name, .data = "export default {};\n" });

    var child = std.process.Child.init(&.{ "npm", "run", "docusaurus", "docs:version", "stable" }, context.allocator);
    child.cwd = website_path;
    child.stdin_behavior = .Ignore;

    const result = try child.spawnAndWait();

    switch (result) {
        .Exited => |code| {
            if (code != 0) {
                std.log.err("npm command failed with exit code {d}\n", .{code});
                return code;
            }
        },
        else => {
            std.debug.print("npm command terminated unexpectedly: {any}\n", .{result});
            return 1;
        },
    }

    return 0;
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

fn runCreateGithubRelease(context: *Context) !u8 {
    const version = std.mem.trim(
        u8,
        try std.fs.cwd().readFileAlloc(context.allocator, "version.txt", 1024),
        " \n\r\t",
    );
    const version_sem = std.SemanticVersion.parse(version) catch |err| {
        std.log.err("version.txt is not valid '{s}': {}\n", .{ version, err });
        return 1;
    };

    var changelog_lines = std.ArrayList([]const u8).init(context.allocator);

    // Read changelog and find version section
    {
        const version_header = try std.fmt.allocPrint(context.allocator, "## {s}", .{version});
        const changelog_content = try std.fs.cwd().readFileAlloc(
            context.allocator,
            "website/docs/changelog.md",
            1024 * 1024 * 4,
        );

        var lines = std.mem.splitScalar(u8, changelog_content, '\n');
        var found_version = false;

        while (lines.next()) |line| {
            if (found_version) {
                // Check if we hit the next section (## followed by non-#)
                if (std.mem.startsWith(u8, line, "## ") and !std.mem.startsWith(u8, line, "## #")) {
                    break;
                }
                try changelog_lines.append(line);
            } else if (std.mem.eql(u8, line, version_header)) {
                found_version = true;
            }
        }

        if (!found_version) {
            std.log.err("Version {s} not found in changelog\n", .{version});
            return 1;
        }
    }

    const changes_filepath = try tempFilePath(context.allocator, &context.env_map);
    defer std.fs.deleteFileAbsolute(changes_filepath) catch {};
    {
        var file = try std.fs.createFileAbsolute(changes_filepath, .{});
        defer file.close();
        var buffered_writer = std.io.bufferedWriter(file.writer());
        const writer = buffered_writer.writer();

        for (changelog_lines.items, 0..) |line, i| {
            if (i == changelog_lines.items.len - 1 and changelog_lines.items.len > 0) {
                // Don't print newline for last line, and handle empty case
                if (line.len > 0) {
                    try writer.print("{s}", .{line});
                }
            } else {
                try writer.print("{s}\n", .{line});
            }
        }

        try buffered_writer.flush();
    }

    // Create draft release
    {
        var gh_args = std.ArrayList([]const u8).init(context.allocator);

        try gh_args.append("gh");
        try gh_args.append("release");
        try gh_args.append("create");
        try gh_args.append(try std.fmt.allocPrint(context.allocator, "v{s}", .{version}));

        try gh_args.append("--draft");

        try gh_args.append("--title");
        try gh_args.append(try std.fmt.allocPrint(context.allocator, "Release v{s}", .{version}));

        try gh_args.append("--notes-file");
        try gh_args.append(changes_filepath);

        if (version_sem.pre != null)
            try gh_args.append("--prerelease");

        const result = try std.process.Child.run(.{
            .allocator = context.allocator,
            .argv = gh_args.items,
        });

        if (result.stdout.len > 0)
            try std.io.getStdErr().writeAll(result.stdout);
        if (result.stderr.len > 0)
            try std.io.getStdOut().writeAll(result.stderr);

        if (result.term != .Exited or result.term.Exited != 0) {
            std.log.err("gh release command failed: {any}\n", .{result.term});
            return 1;
        }
    }

    return 0;
}

fn runUploadErrors(context: *Context) !u8 {
    // Determine the logs directory based on the OS
    const logs_dir = switch (builtin.os.tag) {
        .linux => blk: {
            const home = context.env_map.get("HOME") orelse {
                std.log.err("HOME environment variable not set\n", .{});
                return 1;
            };
            break :blk try std.fs.path.join(context.allocator, &.{ home, ".local", "state", "Floe", "Logs" });
        },
        .macos => blk: {
            const home = context.env_map.get("HOME") orelse {
                std.log.err("HOME environment variable not set\n", .{});
                return 1;
            };
            break :blk try std.fs.path.join(context.allocator, &.{ home, "Library", "Logs", "Floe" });
        },
        .windows => blk: {
            const localappdata = context.env_map.get("LOCALAPPDATA") orelse {
                std.log.err("LOCALAPPDATA environment variable not set\n", .{});
                return 1;
            };
            break :blk try std.fs.path.join(context.allocator, &.{ localappdata, "Floe", "Logs" });
        },
        else => {
            std.log.err("Unsupported OS\n", .{});
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
            std.log.err("Error opening logs directory: {}\n", .{err});
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
            std.log.err("Failed to run sentry-cli for {s}: {}\n", .{ entry.name, err });
            continue;
        };
        defer context.allocator.free(result.stdout);
        defer context.allocator.free(result.stderr);

        var print_streams = false;

        switch (result.term) {
            .Exited => |code| {
                if (code == 0) {
                    // Successfully uploaded, remove the file
                    dir.deleteFile(entry.name) catch |err| {
                        std.log.warn("Failed to delete {s}: {}\n", .{ entry.name, err });
                    };
                } else {
                    std.log.err("sentry-cli failed for {s} with exit code {d}\n", .{ entry.name, code });
                    print_streams = true;
                }
            },
            else => {
                std.log.err("sentry-cli terminated unexpectedly for {s}: {any}\n", .{ entry.name, result.term });
                print_streams = true;
            },
        }

        if (print_streams) {
            if (result.stdout.len > 0) {
                std.io.getStdErr().writer().writeAll(result.stdout) catch {};
            }
            if (result.stderr.len > 0) {
                std.io.getStdErr().writer().writeAll(result.stderr) catch {};
            }
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
    timed_out: bool,

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

    pub fn print(report: *CiReport, summary_writer: std.io.AnyWriter) !void {
        std.sort.pdq(CiTask, report.tasks.items, {}, sortTasksAlphabetic);

        // Print diagnostics for failed tasks.
        {
            const stream = std.io.getStdOut();
            var buffered_writer = std.io.bufferedWriter(stream.writer());
            const writer = buffered_writer.writer();

            var console = std.io.tty.detectConfig(stream);

            // GitHub Actions logs seem to always show ANSI escape codes correctly, even if the terminal config isn't
            // detected properly.
            const is_github_actions = report.context.env_map.get("GITHUB_ACTIONS") != null;
            if (is_github_actions) console = std.io.tty.Config.escape_codes;

            for (report.tasks.items) |task| {
                if (task.term == .Exited and task.term.Exited == 0) {
                    continue;
                }

                // Heading.
                {
                    try setColorWithFlush(&buffered_writer, console, .magenta);
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

                    try writer.writeAll("]");
                    try setColorWithFlush(&buffered_writer, console, .reset);
                    try writer.writeAll("\n");
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
                    try setColorWithFlush(&buffered_writer, console, .blue);
                    try writer.print("[{s}]", .{output.name});
                    try setColorWithFlush(&buffered_writer, console, .reset);
                    try writer.writeAll("\n");

                    if (output.data.len == 0) {
                        try writer.writeAll("(no output)\n");
                        continue;
                    }

                    if (is_github_actions)
                        try writer.print("::group::{s}\n", .{output.name});

                    try writer.writeAll(output.data);
                    try writer.writeAll("\n");

                    if (is_github_actions)
                        try writer.writeAll("::endgroup::\n");
                }
            }
            try buffered_writer.flush();
        }

        // Summary markdown table.
        {
            try summary_writer.writeAll("| Command | Exit Code | Time Taken |\n");
            try summary_writer.writeAll("|---|---|---|\n");

            for (report.tasks.items) |task| {
                try summary_writer.writeAll("| ");

                // Command column
                try task.writeArgs(summary_writer);

                try summary_writer.writeAll(" | ");

                // Exit code/termination info column
                switch (task.term) {
                    .Exited => |code| {
                        try summary_writer.print("{s} {d}", .{ if (code == 0) "✅" else "❌", code });
                    },
                    .Signal, .Stopped, .Unknown => {
                        try summary_writer.print("{s} Terminated: {any}", .{ "❌", task.term });
                    },
                }
                if (task.timed_out)
                    try summary_writer.writeAll(" (timeout)");

                try summary_writer.writeAll(" | ");

                // Time taken column
                try summary_writer.print("{d:.2}s", .{task.time_taken_seconds});

                try summary_writer.writeAll(" |\n");
            }
        }
    }
};

fn killProcess(child: *std.process.Child) !void {
    if (builtin.os.tag == .windows) {
        std.os.windows.TerminateProcess(child.id, 1) catch |err| switch (err) {
            error.PermissionDenied => {
                // The Zig std does another wait here, so we copy that.
                std.os.windows.WaitForSingleObjectEx(child.id, 0, false) catch return err;

                // Otherwise, consider it already terminated - not an error.
                return;
            },
            else => return err,
        };
    } else {
        std.posix.kill(child.id, std.posix.SIG.TERM) catch |err| switch (err) {
            error.ProcessNotFound => return, // Already terminated.
            else => return err,
        };
    }
}

fn tryRunZigBuild(ci_report: *CiReport, args: []const []const u8) !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer _ = arena.deinit();
    const allocator = arena.allocator();

    const zig_exe = ci_report.context.env_map.get("ZIG_EXE") orelse {
        return error.EnvVarNotSet;
    };

    const gha_out_file: ?[]const u8 = if (args.len != 0 and std.mem.eql(u8, args[0], "test") and ci_report.context.env_map.get("GITHUB_ACTIONS") != null)
        try tempFilePath(allocator, &ci_report.context.env_map)
    else
        null;
    defer if (gha_out_file) |path| std.fs.deleteFileAbsolute(path) catch {};

    var child = std.process.Child.init(blk: {
        var num_args = args.len + 2;
        if (gha_out_file != null) num_args += 2;
        const full_args = try allocator.alloc([]const u8, num_args);
        var pos: usize = 0;
        full_args[pos] = zig_exe;
        pos += 1;
        full_args[pos] = "build";
        pos += 1;
        for (args) |arg| {
            full_args[pos] = arg;
            pos += 1;
        }
        if (gha_out_file) |path| {
            full_args[pos] = "--";
            pos += 1;
            full_args[pos] = try std.fmt.allocPrint(allocator, "--gha-annotations-output-path={s}", .{
                path,
            });
            pos += 1;
        }
        break :blk full_args;
    }, allocator);

    child.stdin_behavior = .Ignore;
    child.stdout_behavior = .Pipe;
    child.stderr_behavior = .Pipe;

    var timer = try std.time.Timer.start();

    try child.spawn();
    errdefer {
        _ = child.kill() catch {};
    }

    const timeout_seconds = 60 * 60;

    var stdout: []const u8 = &.{};
    var stderr: []const u8 = &.{};

    // Collect outputs
    {
        var poller = std.io.poll(allocator, enum { stdout, stderr }, .{
            .stdout = child.stdout.?,
            .stderr = child.stderr.?,
        });
        defer poller.deinit();

        const max_bytes = 20 * 1024 * 1024;
        while (try poller.pollTimeout(std.time.ns_per_s)) {
            if (poller.fifo(.stdout).count > max_bytes or poller.fifo(.stderr).count > max_bytes) {
                return error.OutputTooLarge;
            }

            if (timer.read() >= timeout_seconds * std.time.ns_per_s) {
                break;
            }
        }

        stdout = try poller.fifo(.stdout).toOwnedSlice();
        stderr = try poller.fifo(.stderr).toOwnedSlice();
    }

    // Wait
    const result = blk: {
        var done = std.atomic.Value(bool).init(false);

        const thread = try std.Thread.spawn(
            .{
                .allocator = allocator,
            },
            struct {
                fn wait(process: *std.process.Child, done_inner: *std.atomic.Value(bool)) void {
                    _ = process.wait() catch |err| {
                        std.log.err("Failed to wait for process: {any}\n", .{err});
                    };

                    done_inner.store(true, .release);
                }
            }.wait,
            .{ &child, &done },
        );

        var timed_out = false;
        while (!done.load(.acquire)) {
            std.time.sleep(100 * std.time.ns_per_ms);
            if (timer.read() >= timeout_seconds * std.time.ns_per_s) {
                std.log.warn("Process timed out after {d} seconds, killing...\n", .{timeout_seconds});
                try killProcess(&child);
                timed_out = true;
                break;
            }
        }

        thread.join();

        break :blk .{ .result = try child.wait(), .timed_out = timed_out };
    };

    {
        var stdout_parts = std.BoundedArray([]const u8, 2).init(0) catch unreachable;
        try stdout_parts.append(stdout);
        if (gha_out_file) |path| {
            const annotations = std.fs.cwd().readFileAlloc(allocator, path, 1024 * 1024 * 4) catch |err|
                if (err == error.FileNotFound) "" else return err;

            if (annotations.len != 0)
                try stdout_parts.append(annotations);
        }

        ci_report.mutex.lock();
        defer ci_report.mutex.unlock();
        const ci_allocator = ci_report.arena.allocator();
        const task = CiTask{
            .args = args,
            .term = result.result,
            .stdout = try std.mem.join(ci_allocator, "\n", stdout_parts.constSlice()),
            .stderr = try ci_allocator.dupe(u8, stderr),
            .time_taken_seconds = @as(f64, @floatFromInt(timer.read())) / std.time.ns_per_s,
            .timed_out = result.timed_out,
        };
        try ci_report.tasks.append(ci_allocator, task);
    }
}

fn runZigBuild(ci_report: *CiReport, args: []const []const u8) void {
    _ = tryRunZigBuild(ci_report, args) catch |err| {
        std.log.err("runZigBuild failed: {any}\n", .{err});
        if (@errorReturnTrace()) |st|
            std.debug.dumpStackTrace(st.*);
    };
}

fn spawnZigBuild(pool: *std.Thread.Pool, wg: *std.Thread.WaitGroup, ci_report: *CiReport, args: []const []const u8) void {
    const args_copy = ci_report.arena.allocator().dupe([]const u8, args) catch @panic("OOM");
    pool.spawnWg(wg, runZigBuild, .{ ci_report, args_copy });
}

// Outputs markdown table to stdout.
fn runCi(context: *Context, test_level: enum { basic, full }) !u8 {
    var tsa: std.heap.ThreadSafeAllocator = .{ .child_allocator = context.allocator };

    var ci_report: CiReport = .{
        .context = context,
        .mutex = .{},
        .arena = std.heap.ArenaAllocator.init(tsa.allocator()),
        .tasks = std.ArrayListUnmanaged(CiTask).empty,
    };

    // Start a simple HTTP server so that tests can use it.
    var http_server = TestHttpServer.start(tsa.allocator()) catch |err| {
        std.log.err("Failed to start HTTP server: {}\n", .{err});
        return 1;
    };
    defer http_server.stop(tsa.allocator());

    // Initialize thread pool for parallel execution.
    var pool: std.Thread.Pool = undefined;
    try pool.init(.{
        .allocator = tsa.allocator(),
        // 50% of CPU cores to account that the child processes probably multi-thread too.
        .n_jobs = @max(1, (std.Thread.getCpuCount() catch 1) / 2),
    });
    defer pool.deinit();

    var wg: std.Thread.WaitGroup = .{};

    // Just a standard zig build.
    const empty_args: []const []const u8 = &.{};
    spawnZigBuild(&pool, &wg, &ci_report, empty_args);

    const core_tests = [_][]const u8{
        "test",
        "test:clap-val",
        "test:pluginval",
        "test:vst3-val",
    };

    // Tests in debug mode.
    for (core_tests) |test_cmd| {
        spawnZigBuild(&pool, &wg, &ci_report, &.{
            test_cmd,
            "--prefix",
            "zig-out/debug",
        });
    }

    // Tests in optimised mode.
    if (test_level == .full) {
        for (core_tests) |test_cmd| {
            spawnZigBuild(&pool, &wg, &ci_report, &.{
                test_cmd,
                "-Dbuild-mode=performance_profiling",
                "--prefix",
                "zig-out/optimised",
            });
        }
    }

    switch (builtin.os.tag) {
        .linux => {
            if (test_level == .full) {
                spawnZigBuild(&pool, &wg, &ci_report, &.{
                    "test-coverage",
                });

                // Valgrind.
                // IMPROVE: run validators (in single-process mode) through valgrind
                spawnZigBuild(&pool, &wg, &ci_report, &.{
                    "test:valgrind",
                });
                spawnZigBuild(&pool, &wg, &ci_report, &.{
                    "test:valgrind",
                    "-Dbuild-mode=performance_profiling",
                });

                // Unit tests with thread sanitizer.
                spawnZigBuild(&pool, &wg, &ci_report, &.{
                    "test",
                    "-Dsanitize-thread",
                });
                spawnZigBuild(&pool, &wg, &ci_report, &.{
                    "test",
                    "-Dsanitize-thread",
                    "-Dbuild-mode=performance_profiling",
                });

                spawnZigBuild(&pool, &wg, &ci_report, &.{
                    "test",
                    "-Dsanitize-thread",
                    "--prefix",
                    "zig-out/debug-sanitized",
                });
                spawnZigBuild(&pool, &wg, &ci_report, &.{
                    "test",
                    "-Dsanitize-thread",
                    "-Dbuild-mode=performance_profiling",
                    "--prefix",
                    "zig-out/optimised-sanitized",
                });

                // We choose Linux to do OS-agnostic checks.
                spawnZigBuild(&pool, &wg, &ci_report, &.{
                    "check:clang-tidy",
                    "-Dtargets=x86_64-linux,x86_64-windows,aarch64-macos",
                });
                spawnZigBuild(&pool, &wg, &ci_report, &.{"script:website-build"});
            }

            spawnZigBuild(&pool, &wg, &ci_report, &.{"check:reuse"});
            spawnZigBuild(&pool, &wg, &ci_report, &.{"check:format"});
            spawnZigBuild(&pool, &wg, &ci_report, &.{"check:spelling"});
        },
        .windows => {
            spawnZigBuild(&pool, &wg, &ci_report, &.{"test:windows-install"});
        },
        .macos => {
            if (test_level == .full) {
                spawnZigBuild(&pool, &wg, &ci_report, &.{
                    "test",
                    "-Dsanitize-thread",
                });
                spawnZigBuild(&pool, &wg, &ci_report, &.{
                    "test",
                    "-Dsanitize-thread",
                    "-Dbuild-mode=performance_profiling",
                });
            }

            const au_install_location = try auInstallLocation(context);
            spawnZigBuild(&pool, &wg, &ci_report, &.{ "test:pluginval-au", "--prefix", au_install_location });
            spawnZigBuild(&pool, &wg, &ci_report, &.{ "test:auval", "--prefix", au_install_location });
        },
        else => {},
    }

    pool.waitAndWork(&wg);

    if (test_level == .full) {
        switch (builtin.os.tag) {
            .macos => {
                // AU tests require the plugin to be installed to the AU system location. We therefore cannot run
                // multiple parallel AU testing with different binaries (such as debug binary and optimised binary).
                // So we run another set of AU tests here.

                wg.reset();

                const au_install_location = try auInstallLocation(context);

                spawnZigBuild(&pool, &wg, &ci_report, &.{
                    "test:pluginval-au",
                    "-Dbuild-mode=performance_profiling",
                    "--prefix",
                    au_install_location,
                });
                spawnZigBuild(&pool, &wg, &ci_report, &.{
                    "test:auval",
                    "-Dbuild-mode=performance_profiling",
                    "--prefix",
                    au_install_location,
                });

                pool.waitAndWork(&wg);
            },
            else => {},
        }
    }

    {
        var file: ?std.fs.File = null;
        defer if (file) |f| f.close();

        var writer: std.io.AnyWriter = undefined;

        var stdout_buffered = std.io.bufferedWriter(std.io.getStdOut().writer());
        writer = stdout_buffered.writer().any();

        if (context.env_map.get("GITHUB_STEP_SUMMARY")) |summary_path| {
            file = try std.fs.cwd().createFile(summary_path, .{
                .read = true,
                .truncate = false,
            });
            try file.?.seekFromEnd(0);

            writer = file.?.writer().any();
        }

        ci_report.print(writer) catch |err| {
            std.log.err("Failed to print CI report: {any}\n", .{err});
            return 1;
        };

        try stdout_buffered.flush();
    }

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

// This code is from https://github.com/liyu1981/tmpfile.zig
// Licensed under the MIT License. Copyright liyu1981.
const WindowsTempDir = struct {
    const DWORD = std.os.windows.DWORD;
    const LPWSTR = std.os.windows.LPWSTR;
    const MAX_PATH = std.os.windows.MAX_PATH;
    const WCHAR = std.os.windows.WCHAR;

    pub extern "C" fn GetTempPath2W(BufferLength: DWORD, Buffer: LPWSTR) DWORD;

    pub fn get(allocator: std.mem.Allocator) ![]const u8 {
        // use GetTempPathW2, https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-gettemppathw
        var wchar_buf: [MAX_PATH + 2]WCHAR = undefined;
        wchar_buf[MAX_PATH + 1] = 0;
        const ret = GetTempPath2W(MAX_PATH + 1, wchar_buf[0 .. MAX_PATH + 1 :0].ptr);
        if (ret != 0) {
            const path = wchar_buf[0..ret];
            return std.unicode.utf16LeToUtf8Alloc(allocator, path);
        } else {
            return error.GetTempPath2WFailed;
        }
    }
};

fn tempFilePath(allocator: std.mem.Allocator, env_map: *std.process.EnvMap) ![]const u8 {
    var dir = switch (builtin.os.tag) {
        .linux, .macos => env_map.get("TMPDIR") orelse "/tmp",
        .windows => try WindowsTempDir.get(allocator),
        else => return error.UnsupportedOS,
    };

    std.fs.cwd().makePath(dir) catch |err| {
        std.log.err("Failed to create temp directory {s}: {any}\n", .{ dir, err });
        return err;
    };

    dir = try std.fs.realpathAlloc(allocator, dir);

    var bytes: [8]u8 = undefined;
    try std.posix.getrandom(&bytes);

    const codec = std.base64.url_safe_no_pad;

    const size = codec.Encoder.calcSize(bytes.len);
    const b64_buf = try allocator.alloc(u8, size);
    defer allocator.free(b64_buf);
    const filename = codec.Encoder.encode(b64_buf, &bytes);

    return try std.fs.path.join(allocator, &.{ dir, filename });
}
