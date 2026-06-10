// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");

const constants = @import("constants.zig");
const std_extras = @import("std_extras.zig");
const cdb = @import("combine_cdb_fragments.zig");

pub fn addGlobalCheckSteps(b: *std.Build) void {
    {
        const reuse = b.step("check:reuse", "Check compliance with Reuse licensing spec");
        const run = std_extras.createCommandWithStdoutToStderr(b, null, "run reuse");
        run.addArgs(&.{ "reuse", "lint" });
        reuse.dependOn(&run.step);
    }

    {
        const check_format = b.step("check:format", "Check code formatting with clang-format");
        const check_format_step = CheckFormatStep.create(b);
        check_format.dependOn(&check_format_step.step);
    }

    {
        const check_links = b.step("check:links", "Check links with lychee");

        const run = b.addSystemCommand(&.{
            "lychee",

            // Docusaurus checks for internal file links in markdown files. We just use lychee for external links.
            "--scheme",
            "https",
            "--scheme",
            "http",

            "website",
            "readme.md",
        });
        check_links.dependOn(&run.step);
    }
}

const CheckFormatStep = struct {
    step: std.Build.Step,
    builder: *std.Build,

    pub fn create(builder: *std.Build) *CheckFormatStep {
        const self = builder.allocator.create(CheckFormatStep) catch @panic("OOM");
        self.* = CheckFormatStep{
            .step = std.Build.Step.init(.{
                .id = .custom,
                .name = "check-format",
                .owner = builder,
                .makeFn = make,
            }),
            .builder = builder,
        };
        return self;
    }

    fn make(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
        _ = options;
        const self: *CheckFormatStep = @fieldParentPtr("step", step);

        const source_files = try std_extras.findSourceFiles(self.builder.allocator, .{
            .dir_path = "src",
            .extensions = &.{ ".cpp", ".hpp", ".h", ".mm" },
            .exclude_folders = &.{"shaders"},
        });

        var args = std.ArrayList([]const u8).init(self.builder.allocator);

        try args.append("clang-format");
        try args.append("--dry-run");
        try args.append("--Werror");

        for (source_files) |file| {
            const full_path = self.builder.pathJoin(&.{ "src", file });
            try args.append(full_path);
        }

        _ = try step.evalChildProcess(args.items);
    }
};

pub const ClangTidyStep = struct {
    step: std.Build.Step,
    builder: *std.Build,
    target: std.Target,

    pub fn create(builder: *std.Build, target: std.Target) *ClangTidyStep {
        const self = builder.allocator.create(ClangTidyStep) catch @panic("OOM");
        self.* = ClangTidyStep{
            .step = std.Build.Step.init(.{
                .id = .custom,
                .name = "clang-tidy",
                .owner = builder,
                .makeFn = make,
            }),
            .builder = builder,
            .target = target,
        };
        return self;
    }

    fn make(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
        const self: *ClangTidyStep = @fieldParentPtr("step", step);

        var args = std.ArrayList([]const u8).init(self.builder.allocator);

        try args.append("clang-tidy");

        // We specify the config file because we don't want clang-tidy to go automatically looking for it and
        // sometimes find .clang-tidy files in third-party libraries that are incompatible with our version
        // of clang-tidy.
        try args.append("--config-file=.clang-tidy");

        // We specify the build root so that we get the correct cdb for the target.
        try args.append("-p");
        try args.append(cdb.cdbDirPath(self.builder, self.target));

        // We get all the source files that we compiled by reading the cdb ourselves and selecting our files.
        // This ensures we only check files that were actually compiled.

        // Read the entire compile_commands.json file
        const cdb_contents = try step.owner.build_root.handle.readFileAlloc(
            self.builder.allocator,
            cdb.cdbFilePath(self.builder, self.target),
            1024 * 1024 * 10,
        ); // 10MB max
        defer self.builder.allocator.free(cdb_contents);

        // Parse JSON to extract file paths
        const parsed = try std.json.parseFromSlice(
            []cdb.CompileFragment,
            self.builder.allocator,
            cdb_contents,
            .{},
        );
        defer parsed.deinit();

        // Get absolute path to our src directory
        const src_dir = self.builder.pathFromRoot("src");

        // Extract source files that are in our src/ directory and add them to clang-tidy args
        for (parsed.value) |compile_cmd| {
            const rel_path = std.fs.path.relative(self.builder.allocator, src_dir, compile_cmd.file) catch continue;
            defer self.builder.allocator.free(rel_path);

            // If relative path doesn't start with "..", the file is within src/
            if (!std.mem.startsWith(u8, rel_path, "..")) {
                try args.append(compile_cmd.file);
            }
        }

        const run_result = std.process.Child.run(.{
            .allocator = self.builder.allocator,
            .argv = args.items,
            .progress_node = options.progress_node,
            .cwd = step.owner.build_root.path, // compile_commands.json paths are relative to build root.
            .max_output_bytes = 10 * 1024 * 1024,
        }) catch |err| {
            return step.fail("failed to run clang-tidy: {s}", .{@errorName(err)});
        };
        defer self.builder.allocator.free(run_result.stdout);
        defer self.builder.allocator.free(run_result.stderr);

        // Print stdout (clang-tidy doesn't typically output to stdout, but just in case)
        if (run_result.stdout.len > 0) {
            std.debug.print("{s}", .{run_result.stdout});
        }

        if (run_result.stderr.len > 0) {
            std.debug.print("{s}", .{run_result.stderr});
        }

        switch (run_result.term) {
            .Exited => |code| {
                if (code != 0) {
                    return step.fail("clang-tidy exited with error code {d}", .{code});
                }
            },
            .Signal, .Stopped, .Unknown => {
                return step.fail("clang-tidy terminated unexpectedly", .{});
            },
        }
    }
};
