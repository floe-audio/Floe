// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");

const constants = @import("constants.zig");
const std_extras = @import("std_extras.zig");
const ConcatCompileCommandsStep = @import("ConcatCompileCommandsStep.zig");

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
        // We use hunspell for the spell-check. It doesn't do anything fancy at all, it just checks each word for spelling.
        // It means we get lots of false positives, but I think it's still worth it. We can just add words to
        // ignored-spellings.dic. In vim, use :sort u to remove duplicates.
        const check_spelling = b.step("check:spelling", "Check spelling with hunspell");
        const check_spelling_step = CheckSpellingStep.create(b);
        check_spelling.dependOn(&check_spelling_step.step);
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
            .exclude_folders = &.{},
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
    target: std.Build.ResolvedTarget,

    pub fn create(builder: *std.Build, target: std.Build.ResolvedTarget) *ClangTidyStep {
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
        try args.append(ConcatCompileCommandsStep.cdbDirPath(self.builder, self.target.result));

        // We get all the source files that we compiled by reading the cdb ourselves and selecting our files.
        // This ensures we only check files that were actually compiled.

        // Read the entire compile_commands.json file
        const cdb_contents = try step.owner.build_root.handle.readFileAlloc(
            self.builder.allocator,
            ConcatCompileCommandsStep.cdbFilePath(self.builder, self.target.result),
            1024 * 1024 * 10,
        ); // 10MB max
        defer self.builder.allocator.free(cdb_contents);

        // Parse JSON to extract file paths
        const parsed = try std.json.parseFromSlice(
            []ConcatCompileCommandsStep.CompileFragment,
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

const CheckSpellingStep = struct {
    step: std.Build.Step,
    builder: *std.Build,

    pub fn create(builder: *std.Build) *CheckSpellingStep {
        const self = builder.allocator.create(CheckSpellingStep) catch @panic("OOM");
        self.* = CheckSpellingStep{
            .step = std.Build.Step.init(.{
                .id = .custom,
                .name = "check-spelling",
                .owner = builder,
                .makeFn = make,
            }),
            .builder = builder,
        };
        return self;
    }

    fn make(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
        _ = options;
        const self: *CheckSpellingStep = @fieldParentPtr("step", step);

        const markdown_files = try std_extras.findSourceFiles(self.builder.allocator, .{
            .dir_path = ".",
            .extensions = &.{ ".md", ".mdx" },
            .exclude_folders = &.{"third_party_libs"},
            .respect_gitignore = true,
        });

        if (markdown_files.len == 0) {
            return;
        }

        var args = std.ArrayList([]const u8).init(self.builder.allocator);
        try args.append("hunspell");
        try args.append("-l");
        try args.append("-d");
        try args.append("en_GB");
        try args.append("-p");
        try args.append("ignored-spellings.dic");

        for (markdown_files) |file| {
            try args.append(file);
        }

        const result = try step.evalChildProcess(args.items);

        if (result.len > 0) {
            var unique_words = std.StringHashMap(void).init(self.builder.allocator);
            defer unique_words.deinit();

            var line_iter = std.mem.splitScalar(u8, result, '\n');
            while (line_iter.next()) |line| {
                const trimmed = std.mem.trim(u8, line, " \t\r\n");
                if (trimmed.len > 0) {
                    const owned_line = try self.builder.allocator.dupe(u8, trimmed);
                    try unique_words.put(owned_line, {});
                }
            }

            var sorted_lines = std.ArrayList([]const u8).init(self.builder.allocator);
            var word_iterator = unique_words.keyIterator();
            while (word_iterator.next()) |word| {
                try sorted_lines.append(word.*);
            }

            std.sort.pdq([]const u8, sorted_lines.items, {}, struct {
                fn lessThan(_: void, a: []const u8, b: []const u8) bool {
                    return std.mem.order(u8, a, b) == .lt;
                }
            }.lessThan);

            for (sorted_lines.items) |line| {
                std.debug.print("Spelling error in {s}\n", .{line});
            }

            return error.SpellingErrors;
        }
    }
};
