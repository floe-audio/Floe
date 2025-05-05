// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Notes on compiling/cross-compiling:
// Windows:
// - Zig has MinGW built-in. So Windows.h and pretty much all win32 API is available by default.
//   Cross-compiling is therefore easy.
// macOS:
// - The macOS SDKs are not included in Zig, we have to ensure they are available ourselves, and tell
//   zig where they are. We use Nix for this. See flake.nix. After that, cross-compiling works
//   well. And as a bonus, we don't have to install Xcode if we're on a mac.
// Linux:
// - We don't support cross-compiling for Linux at the moment.
// - For native compiling, we rely on nix and pkg-config. Use 'pkg-config --list-all | fzf' to find
//   the names of libraries to use when doing linkSystemLibrary2().
//
// NOTE: this whole file needs to be refactored.
// We need to more closely follow Zig's build system patterns: steps, step dependencies, enable
// parallelism of steps where possible, etc.
// Additionally, I think it will pay off to move all tests and scripts to zig too, so we can have
// a single point of truth for all configuration, building and testing

const std = @import("std");
const builtin = @import("builtin");

const floe_description = "Sample library engine";
const floe_copyright = "Sam Windell";
const floe_vendor = "Floe Audio";
const floe_clap_id = "com.floe-audio.floe";
const floe_homepage_url = "https://floe.audio";
const floe_manual_url = "https://floe.audio";
const floe_download_url = "https://floe.audio/installation/download-and-install-floe.html";
const floe_source_code_url = "https://github.com/floe-audio/Floe";
const floe_au_factory_function = "FloeAuV2";
const min_macos_version = "11.0.0"; // use 3-part version for plist
const min_windows_version = "win10";

const floe_cache_relative = ".floe-cache";

const embed_files_workaround = true;
const clap_only = false;

const ConcatCompileCommandsStep = struct {
    step: std.Build.Step,
    target: std.Build.ResolvedTarget,
    use_as_default: bool,
};

fn archAndOsPair(target: std.Target) std.BoundedArray(u8, 32) {
    var result = std.BoundedArray(u8, 32).init(0) catch @panic("OOM");
    std.fmt.format(
        result.writer(),
        "{s}-{s}",
        .{ @tagName(target.cpu.arch), @tagName(target.os.tag) },
    ) catch @panic("OOM");
    return result;
}

fn compileCommandsDirForTarget(b: *std.Build, target: std.Target) ![]u8 {
    return b.pathJoin(&.{
        b.build_root.path.?,
        floe_cache_relative,
        b.fmt("compile_commands_{s}", .{archAndOsPair(target).slice()}),
    });
}

fn compileCommandsFileForTarget(b: *std.Build, target: std.Target) ![]u8 {
    return b.fmt(
        "{s}.json",
        .{compileCommandsDirForTarget(b, target) catch @panic("OOM")},
    );
}

fn tryCopyCompileCommandsForTargetFileToDefault(b: *std.Build, target: std.Target) void {
    const generic_out_path = b.pathJoin(&.{
        b.build_root.path.?,
        floe_cache_relative,
        "compile_commands.json",
    });
    const out_path = compileCommandsFileForTarget(b, target) catch @panic("OOM");
    std.fs.copyFileAbsolute(out_path, generic_out_path, .{ .override_mode = null }) catch {};
}

fn tryConcatCompileCommands(step: *std.Build.Step) !void {
    const self: *ConcatCompileCommandsStep = @fieldParentPtr("step", step);
    const b = step.owner;

    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();

    const CompileFragment = struct {
        directory: []u8,
        file: []u8,
        output: []u8,
        arguments: [][]u8,
    };

    var compile_commands = std.ArrayList(CompileFragment).init(arena.allocator());
    const compile_commands_dir = try compileCommandsDirForTarget(b, self.target.result);

    {
        const maybe_dir = std.fs.openDirAbsolute(compile_commands_dir, .{ .iterate = true });
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

                var parsed_data = try std.json.parseFromSlice(
                    CompileFragment,
                    arena.allocator(),
                    trimmed_json,
                    .{},
                );

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

                    // clang-tidy doesn't like this when cross-compiling macos, it's a sequence we need to look for and remove, it's no good just removing the '+pan' by itself
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
        const out_path = compileCommandsFileForTarget(b, self.target.result) catch @panic("OOM");

        const maybe_file = std.fs.openFileAbsolute(out_path, .{});
        if (maybe_file != std.fs.File.OpenError.FileNotFound) {
            const f = try maybe_file;
            defer f.close();

            const file_contents = try f.readToEndAlloc(arena.allocator(), 1024 * 1024 * 1024);
            defer arena.allocator().free(file_contents);

            const existing_compile_commands = try std.json.parseFromSlice(
                []CompileFragment,
                arena.allocator(),
                file_contents,
                .{},
            );

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

        var out_f = try std.fs.createFileAbsolute(out_path, .{});
        defer out_f.close();
        var buffered_writer: std.io.BufferedWriter(
            20 * 1024,
            @TypeOf(out_f.writer()),
        ) = .{ .unbuffered_writer = out_f.writer() };

        try std.json.stringify(compile_commands.items, .{}, buffered_writer.writer());
        try buffered_writer.flush();

        try std.fs.deleteTreeAbsolute(compile_commands_dir);

        if (self.use_as_default) {
            tryCopyCompileCommandsForTargetFileToDefault(b, self.target.result);
        }
    }
}

fn concatCompileCommands(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
    _ = options;

    tryConcatCompileCommands(step) catch |err| {
        std.debug.print("failed to concatenate compile commands: {any}\n", .{err});
    };
}

const PostInstallStep = struct {
    step: std.Build.Step,
    make_macos_bundle: bool,
    compile_step: *std.Build.Step.Compile,
    context: *BuildContext,
};

fn postInstallMacosBinary(
    context: *BuildContext,
    step: *std.Build.Step,
    make_macos_bundle: bool,
    path: []const u8,
    bundle_name: []const u8,
    version: ?std.SemanticVersion,
) !void {
    var b = step.owner;

    var final_binary_path: ?[]const u8 = null;
    if (make_macos_bundle) {
        const working_dir = std.fs.path.dirname(path).?;
        var dir = try std.fs.openDirAbsolute(working_dir, .{});
        defer dir.close();

        var bundle_extension_no_dot = std.fs.path.extension(bundle_name);
        std.debug.assert(bundle_extension_no_dot.len != 0);
        bundle_extension_no_dot = bundle_extension_no_dot[1..bundle_extension_no_dot.len];

        try dir.deleteTree(bundle_name);
        try dir.makePath(b.pathJoin(&.{ bundle_name, "Contents", "MacOS" }));

        const exe_name = std.fs.path.stem(bundle_name);
        final_binary_path = b.pathJoin(&.{ working_dir, bundle_name, "Contents", "MacOS", exe_name });
        {
            var dest_dir = try dir.openDir(b.pathJoin(&.{ bundle_name, "Contents", "MacOS" }), .{});
            defer dest_dir.close();
            try dir.copyFile(std.fs.path.basename(path), dest_dir, exe_name, .{});
        }

        {
            const pkg_info_file = try dir.createFile(
                b.pathJoin(&.{ bundle_name, "Contents", "PkgInfo" }),
                .{},
            );
            defer pkg_info_file.close();
            try pkg_info_file.writeAll("BNDL????");
        }

        {
            const pkg_info_file = try dir.createFile(
                b.pathJoin(&.{ bundle_name, "Contents", "Info.plist" }),
                .{},
            );
            defer pkg_info_file.close();

            try std.fmt.format(pkg_info_file.writer(),
                \\<?xml version="1.0" encoding="UTF-8"?>
                \\<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
                \\<plist version="1.0">
                \\    <dict>
                \\        <key>CFBundleDevelopmentRegion</key>
                \\        <string>English</string>
                \\        <key>CFBundleExecutable</key>
                \\        <string>{[executable_name]s}</string>
                \\        <key>CFBundleIdentifier</key>
                \\        <string>{[bundle_identifier]s}</string>
                \\        <key>CFBundleInfoDictionaryVersion</key>
                \\        <string>6.0</string>
                \\        <key>CFBundleName</key>
                \\        <string>{[bundle_name]s}</string>
                \\        <key>CFBundleSignature</key>
                \\        <string>????</string>
                \\        <key>CFBundlePackageType</key>
                \\        <string>BNDL</string>
                \\        <key>CFBundleShortVersionString</key>
                \\        <string>{[major]d}.{[minor]d}.{[patch]d}</string>
                \\        <key>CFBundleVersion</key>
                \\        <string>{[major]d}.{[minor]d}.{[patch]d}</string>
                \\        <key>NSPrincipalClass</key>
                \\        <string/>
                \\        <key>CFBundleSupportedPlatforms</key>
                \\        <array>
                \\            <string>MacOSX</string>
                \\        </array>
                \\        <key>NSHighResolutionCapable</key>
                \\        <true />
                \\        <key>NSHumanReadableCopyright</key>
                \\        <string>Copyright {[copyright]s}</string>
                \\        <key>LSMinimumSystemVersion</key>
                \\        <string>{[min_macos_version]s}</string>
                \\{[audio_unit_dict]s}
                \\    </dict>
                \\</plist>
            , .{
                .executable_name = exe_name,
                .bundle_identifier = b.fmt("{s}.{s}", .{ floe_clap_id, bundle_extension_no_dot }),
                .bundle_name = bundle_name,
                .major = if (version != null) version.?.major else 1,
                .minor = if (version != null) version.?.minor else 0,
                .patch = if (version != null) version.?.patch else 0,
                .copyright = floe_copyright,
                .min_macos_version = min_macos_version,

                // factoryFunction has 'Factory' appended to it because that's what the AUSDK_COMPONENT_ENTRY macro adds.
                // name uses the format Author: Name because otherwise Logic shows the developer as the 4-character manufacturer code.
                .audio_unit_dict = if (std.mem.count(u8, bundle_name, ".component") == 1)
                    b.fmt(
                        \\        <key>AudioComponents</key>
                        \\        <array>
                        \\            <dict>
                        \\                <key>name</key>
                        \\                <string>{[vendor]s}: Floe</string>
                        \\                <key>description</key>
                        \\                <string>{[description]s}</string>
                        \\                <key>factoryFunction</key>
                        \\                <string>{[factory_function]s}Factory</string>
                        \\                <key>manufacturer</key>
                        \\                <string>floA</string>
                        \\                <key>subtype</key>
                        \\                <string>FLOE</string>
                        \\                <key>type</key>
                        \\                <string>aumu</string>
                        \\                <key>version</key>
                        \\                <integer>{[version_packed]d}</integer>
                        \\                <key>sandboxSafe</key>
                        \\                <true/>
                        \\                <key>resourceUsage</key>
                        \\                <dict>
                        \\                    <key>network.client</key>
                        \\                    <true/>
                        \\                    <key>temporary-exception.files.all.read-write</key>
                        \\                    <true/>
                        \\                </dict>
                        \\                <key>tags</key>
                        \\                <array>
                        \\                  <string>Instrument</string>
                        \\                  <string>Synthesizer</string>
                        \\                  <string>Stereo</string>
                        \\                </array>
                        \\            </dict>
                        \\        </array>
                    , .{
                        .vendor = floe_vendor,
                        .description = floe_description,
                        .factory_function = floe_au_factory_function,
                        .version_packed = if (version != null) (version.?.major << 16) | (version.?.minor << 8) | version.?.patch else 0,
                    })
                else
                    "",
            });
        }
    } else {
        final_binary_path = path;
    }

    if (context.optimise != .ReleaseFast) {
        _ = try step.evalChildProcess(&.{ "dsymutil", final_binary_path.? });
    }
}

const LipoStep = struct {
    step: std.Build.Step,
    make_macos_bundle: bool,
    step_x86: ?*std.Build.Step.Compile,
    step_arm: ?*std.Build.Step.Compile,
    context: *BuildContext,
};

fn addToLipoSteps(context: *BuildContext, step: *std.Build.Step.Compile, make_macos_bundle: bool) !void {
    if (step.rootModuleTarget().os.tag != .macos) return;

    const name = step.name;
    if (!context.lipo_steps.contains(name)) {
        var lipo_step = context.b.allocator.create(LipoStep) catch @panic("OOM");
        lipo_step.* = LipoStep{
            .step = std.Build.Step.init(.{
                .id = std.Build.Step.Id.custom,
                .name = "Lipo step",
                .owner = context.b,
                .makeFn = doLipoStep,
            }),
            .make_macos_bundle = make_macos_bundle,
            .step_x86 = null,
            .step_arm = null,
            .context = context,
        };
        context.master_step.dependOn(&lipo_step.step);
        try context.lipo_steps.put(name, lipo_step);
    }

    const lipo_step = context.lipo_steps.get(name);

    var add_step: ?*?*std.Build.Step.Compile = null;
    if (step.rootModuleTarget().cpu.arch == .x86_64) {
        add_step = &lipo_step.?.step_x86;
    } else if (step.rootModuleTarget().cpu.arch == .aarch64) {
        add_step = &lipo_step.?.step_arm;
    }

    if (add_step) |s| {
        s.* = step;
        lipo_step.?.step.dependOn(&step.step);
        lipo_step.?.step.dependOn(context.b.getInstallStep());
    }
}

fn doLipoStep(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
    const self: *LipoStep = @fieldParentPtr("step", step);
    _ = options;

    if (self.step_x86 == null) return;
    if (self.step_arm == null) return;
    const x86 = self.step_x86.?;
    const arm = self.step_arm.?;

    const path1 = x86.installed_path.?;
    const path2 = arm.installed_path.?;

    const working_dir = std.fs.path.dirname(std.fs.path.dirname(path1).?).?;
    var dir = try std.fs.openDirAbsolute(working_dir, .{});
    defer dir.close();

    const universal_dir = "universal-macos";
    try dir.makePath(universal_dir);
    const out_path = step.owner.pathJoin(&.{ working_dir, universal_dir, std.fs.path.basename(path1) });

    _ = try step.evalChildProcess(&.{ "llvm-lipo", "-create", "-output", out_path, path1, path2 });

    try postInstallMacosBinary(self.context, step, self.make_macos_bundle, out_path, x86.name, x86.version);
}

const Win32EmbedInfo = struct {
    name: []const u8,
    description: []const u8,
    icon_path: ?[]const u8,
};

fn addWin32EmbedInfo(step: *std.Build.Step.Compile, info: Win32EmbedInfo) !void {
    if (step.rootModuleTarget().os.tag != .windows) return;

    const b = step.step.owner;
    const arena = b.allocator;

    const path = try std.fmt.allocPrint(arena, "{s}/{s}.rc", .{ floe_cache_relative, step.name });
    const file = try b.build_root.handle.createFile(path, .{});
    defer file.close();

    const this_year = 1970 + @divTrunc(std.time.timestamp(), 60 * 60 * 24 * 365);

    try std.fmt.format(file.writer(),
        \\ #include <windows.h>
        \\ 
        \\ VS_VERSION_INFO VERSIONINFO
        \\  FILEVERSION {[major]d},{[minor]d},{[patch]d},0
        \\  PRODUCTVERSION {[major]d},{[minor]d},{[patch]d},0
        \\ BEGIN
        \\     BLOCK "StringFileInfo"
        \\     BEGIN
        \\         BLOCK "040904b0"
        \\         BEGIN
        \\             VALUE "CompanyName", "{[vendor]s}\0"
        \\             VALUE "FileDescription", "{[description]s}\0"
        \\             VALUE "FileVersion", "{[major]d}.{[minor]d}.{[patch]d}\0"
        \\             VALUE "LegalCopyright", "{[copyright]s} © {[this_year]d}\0"
        \\             VALUE "ProductName", "{[name]s}\0"
        \\             VALUE "ProductVersion", "{[major]d}.{[minor]d}.{[patch]d}\0"
        \\         END
        \\     END
        \\     BLOCK "VarFileInfo"
        \\     BEGIN
        \\         VALUE "Translation", 0x409, 1200
        \\     END
        \\ END
    , .{
        .major = if (step.version != null) step.version.?.major else 0,
        .minor = if (step.version != null) step.version.?.minor else 0,
        .patch = if (step.version != null) step.version.?.patch else 0,
        .description = info.description,
        .name = info.name,
        .this_year = this_year,
        .copyright = floe_copyright,
        .vendor = floe_vendor,
    });

    if (info.icon_path) |p|
        try std.fmt.format(file.writer(), "icon_id ICON \"{s}\"\n", .{p});

    step.addWin32ResourceFile(.{ .file = b.path(path) });
}

fn performPostInstallConfig(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();

    const self: *PostInstallStep = @fieldParentPtr("step", step);
    _ = options;
    var path = self.compile_step.installed_path.?;

    switch (self.compile_step.rootModuleTarget().os.tag) {
        .windows => {
            var out_filename = self.compile_step.out_filename;

            const dll_types_to_remove = [_][]const u8{ "clap", "vst3" };
            for (dll_types_to_remove) |t| {
                const suffix = try std.fmt.allocPrint(arena.allocator(), ".{s}.dll", .{t});
                if (std.mem.endsWith(u8, path, suffix)) {
                    const new_path = try std.fmt.allocPrint(
                        arena.allocator(),
                        "{s}.{s}",
                        .{ path[0 .. path.len - suffix.len], t },
                    );
                    try std.fs.renameAbsolute(path, new_path);
                    path = new_path;

                    std.debug.assert(std.mem.endsWith(u8, out_filename, suffix));
                    out_filename = try std.fmt.allocPrint(
                        arena.allocator(),
                        "{s}.{s}",
                        .{ out_filename[0 .. out_filename.len - suffix.len], t },
                    );
                }
            }
        },
        .macos => {
            try postInstallMacosBinary(
                self.context,
                step,
                self.make_macos_bundle,
                path,
                self.compile_step.name,
                self.compile_step.version,
            );
        },
        .linux => {
            const working_dir = std.fs.path.dirname(path).?;
            var dir = try std.fs.openDirAbsolute(working_dir, .{});
            defer dir.close();
            const filename = std.fs.path.basename(path);

            if (std.mem.indexOf(u8, filename, "Floe.clap.so") != null) {
                try dir.rename(filename, "Floe.clap");
            } else if (std.mem.indexOf(u8, filename, "Floe.vst3.so") != null) {
                const subdir = "Floe.vst3/Contents/x86_64-linux";
                try dir.makePath(subdir);
                try dir.rename(filename, subdir ++ "/Floe.so");
            }
        },
        else => {
            unreachable;
        },
    }
}

pub const WindowsCodeSignStep = struct {
    step: std.Build.Step,
    file_path: []const u8,
    description: []const u8,
    context: *BuildContext,
};

pub fn addWindowsCodeSignStep(
    context: *BuildContext,
    target: std.Build.ResolvedTarget,
    file_path: []const u8,
    description: []const u8,
) ?*std.Build.Step {
    if (context.build_mode != .production) return null;
    if (target.result.os.tag != .windows) return null;

    const cs_step = context.b.allocator.create(WindowsCodeSignStep) catch @panic("OOM");
    cs_step.* = WindowsCodeSignStep{
        .step = std.Build.Step.init(.{
            .id = std.Build.Step.Id.custom,
            .name = "Windows code signing",
            .owner = context.b,
            .makeFn = performWindowsCodeSign,
        }),
        .file_path = context.b.dupe(file_path),
        .description = context.b.dupe(description),
        .context = context,
    };

    return &cs_step.step;
}

fn performWindowsCodeSign(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
    const self: *WindowsCodeSignStep = @fieldParentPtr("step", step);
    _ = options;

    const b = self.context.b;

    // Get absolute paths
    const abs_file = b.pathFromRoot(self.file_path);
    const cache_path = b.pathFromRoot(floe_cache_relative);

    // Create the cert file directory if needed
    const cert_dir = std.fs.path.dirname(cache_path);
    if (cert_dir) |dir| {
        try std.fs.cwd().makePath(dir);
    }

    // Get environment variables
    const cert_pfx = b.graph.env_map.get("WINDOWS_CODESIGN_CERT_PFX") orelse {
        std.log.err("missing WINDOWS_CODESIGN_CERT_PFX environment variable", .{});
        return error.MissingEnvironmentVariable;
    };

    if (cert_pfx.len == 0) {
        std.log.err("WINDOWS_CODESIGN_CERT_PFX environment variable is empty", .{});
        return error.MissingEnvironmentVariable;
    }

    const cert_password = b.graph.env_map.get("WINDOWS_CODESIGN_CERT_PFX_PASSWORD") orelse {
        std.log.err("missing WINDOWS_CODESIGN_CERT_PFX_PASSWORD environment variable", .{});
        return error.MissingEnvironmentVariable;
    };

    // Create cert file if it doesn't exist
    const cert_file_path = try std.fs.path.join(
        b.allocator,
        &[_][]const u8{ cache_path, "windows-codesign-cert.pfx" },
    );
    defer b.allocator.free(cert_file_path);

    var found_cert = true;
    std.fs.accessAbsolute(cert_file_path, .{}) catch {
        found_cert = false;
    };
    if (!found_cert) {
        // Decode base64 encoded certificate and write to file
        const size = try std.base64.standard.Decoder.calcSizeForSlice(cert_pfx);
        const decoded = try b.allocator.alloc(u8, size);
        defer b.allocator.free(decoded);

        try std.base64.standard.Decoder.decode(decoded, cert_pfx);

        var file = try std.fs.createFileAbsolute(cert_file_path, .{});
        defer file.close();

        try file.writeAll(decoded);
    }

    // Create the signed output path
    const signed_path = try std.fmt.allocPrint(b.allocator, "{s}.signed", .{abs_file});
    defer b.allocator.free(signed_path);

    // Execute osslsigncode
    const result = try std.process.Child.run(.{
        .allocator = b.allocator,
        .argv = &[_][]const u8{
            "osslsigncode",
            "sign",
            "-pkcs12",
            cert_file_path,
            "-pass",
            cert_password,
            "-n",
            self.description,
            "-i",
            floe_homepage_url,
            "-t",
            "http://timestamp.sectigo.com",
            "-in",
            abs_file,
            "-out",
            signed_path,
        },
    });
    defer {
        b.allocator.free(result.stdout);
        b.allocator.free(result.stderr);
    }

    if (result.term.Exited != 0) {
        std.log.err("osslsigncode failed: {s}", .{result.stderr});
        return error.SigningFailed;
    }

    // Move the signed file to the original location
    try std.fs.renameAbsolute(signed_path, abs_file);

    std.log.info("Successfully code signed {s}", .{abs_file});
}

const BuildMode = enum {
    development,
    performance_profiling,
    production, // a.k.a.: release, end-user, for-distribution
};

const BuildContext = struct {
    b: *std.Build,
    enable_tracy: bool,
    build_mode: BuildMode,
    lipo_steps: std.StringHashMap(*LipoStep),
    master_step: *std.Build.Step,
    test_step: *std.Build.Step,
    optimise: std.builtin.OptimizeMode,
    external_resources_subdir: ?[]const u8,
    dep_xxhash: *std.Build.Dependency,
    dep_stb: *std.Build.Dependency,
    dep_au_sdk: *std.Build.Dependency,
    dep_miniaudio: *std.Build.Dependency,
    dep_clap: *std.Build.Dependency,
    dep_clap_wrapper: *std.Build.Dependency,
    dep_dr_libs: *std.Build.Dependency,
    dep_flac: *std.Build.Dependency,
    dep_icon_font_cpp_headers: *std.Build.Dependency,
    dep_miniz: *std.Build.Dependency,
    dep_libbacktrace: *std.Build.Dependency,
    dep_lua: *std.Build.Dependency,
    dep_pugl: *std.Build.Dependency,
    dep_pffft: *std.Build.Dependency,
    dep_valgrind_h: *std.Build.Dependency,
    dep_portmidi: *std.Build.Dependency,
    dep_tracy: *std.Build.Dependency,
    dep_vst3_sdk: *std.Build.Dependency,
};

const FlagsBuilderOptions = struct {
    ubsan: bool = false,
    add_compile_commands: bool = true,
    full_diagnostics: bool = false,
    cpp: bool = false,
    objcpp: bool = false,
};

const FlagsBuilder = struct {
    flags: std.ArrayList([]const u8),

    pub fn init(
        context: *BuildContext,
        target: std.Build.ResolvedTarget,
        options: FlagsBuilderOptions,
    ) FlagsBuilder {
        var result = FlagsBuilder{
            .flags = std.ArrayList([]const u8).init(context.b.allocator),
        };
        result.addCoreFlags(context, target, options) catch @panic("OOM");
        return result;
    }

    fn addFlag(self: *FlagsBuilder, flag: []const u8) void {
        self.flags.append(flag) catch @panic("OOM");
    }

    fn addCoreFlags(
        self: *FlagsBuilder,
        context: *BuildContext,
        target: std.Build.ResolvedTarget,
        options: FlagsBuilderOptions,
    ) !void {
        if (options.full_diagnostics) {
            try self.flags.appendSlice(&.{
                "-Werror",
                "-Wconversion",
                "-Wexit-time-destructors",
                "-Wglobal-constructors",
                "-Wall",
                "-Wextra",
                "-Wextra-semi",
                "-Wshadow",
                "-Wimplicit-fallthrough",
                "-Wunused-member-function",
                "-Wunused-template",
                "-Wcast-align",
                "-Wdouble-promotion",
                "-Woverloaded-virtual",
                "-Wno-missing-field-initializers",
                "-DFLAC__NO_DLL",
                "-DPUGL_DISABLE_DEPRECATED",
                "-DPUGL_STATIC",

                // Minimise windows.h size for faster compile times:
                // "Define one or more of the NOapi symbols to exclude the API. For example, NOCOMM excludes the serial
                // communication API. For a list of support NOapi symbols, see Windows.h."
                "-DWIN32_LEAN_AND_MEAN",
                "-DNOKANJI",
                "-DNOHELP",
                "-DNOMCX",
                "-DNOCLIPBOARD",
                "-DNOMEMMGR",
                "-DNOMETAFILE",
                "-DNOMINMAX",
                "-DNOOPENFILE",
                "-DNOSERVICE",
                "-DNOSOUND",
                "-DNOTEXTMETRIC",
                "-DSTRICT",
                "-DNOMINMAX",
            });
        }

        try self.flags.append("-fchar8_t");
        try self.flags.append("-D_USE_MATH_DEFINES");
        try self.flags.append("-D__USE_FILE_OFFSET64");
        try self.flags.append("-D_FILE_OFFSET_BITS=64");
        try self.flags.append("-ftime-trace");

        try self.flags.append("-DMINIZ_USE_UNALIGNED_LOADS_AND_STORES=0");
        try self.flags.append("-DMINIZ_NO_STDIO");
        try self.flags.append("-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES");
        try self.flags.append(context.b.fmt("-DMINIZ_LITTLE_ENDIAN={d}", .{@intFromBool(target.result.cpu.arch.endian() == .little)}));
        try self.flags.append("-DMINIZ_HAS_64BIT_REGISTERS=1");

        try self.flags.append("-DSTBI_NO_STDIO");
        try self.flags.append("-DSTBI_MAX_DIMENSIONS=65535"); // we use u16 for dimensions

        if (target.result.os.tag == .linux) {
            // NOTE(Sam, June 2024): workaround for a bug in Zig (most likely) where our shared library always causes a
            // crash after dlclose(), as described here: https://github.com/ziglang/zig/issues/17908
            // The workaround involves adding this flag and also adding a custom bit of code using
            // __attribute__((destructor)) to manually call __cxa_finalize():
            // https://stackoverflow.com/questions/34308720/where-is-dso-handle-defined/48256026#48256026
            try self.flags.append("-fno-use-cxa-atexit");
        }

        // We want __builtin_FILE() and __FILE__ to be portable because we use this information in stacktraces and
        // logging so we change the absolute paths of all files to be relative to the build root.
        // __FILE__, __builtin_FILE(), and DWARF info should be made relative.
        // -ffile-prefix-map=OLD=NEW is an alias for both -fdebug-prefix-map and -fmacro-prefix-map
        try self.flags.append(context.b.fmt("-ffile-prefix-map={s}{s}=", .{
            context.b.pathFromRoot(""),
            std.fs.path.sep_str,
        }));
        try self.flags.append("-fvisibility=hidden");

        if (context.build_mode != .production and context.enable_tracy) {
            try self.flags.append("-DTRACY_ENABLE");
            try self.flags.append("-DTRACY_MANUAL_LIFETIME");
            try self.flags.append("-DTRACY_DELAYED_INIT");
            try self.flags.append("-DTRACY_ONLY_LOCALHOST");
            if (target.result.os.tag == .linux) {
                // Couldn't get these working well so just disabling them
                try self.flags.append("-DTRACY_NO_CALLSTACK");
                try self.flags.append("-DTRACY_NO_SYSTEM_TRACING");
            }
        }

        // A bit of information about debug symbols:
        //
        // DWARF is a debugging information format. It is used widely, particularly on Linux and macOS. libbacktrace,
        // which we use for getting nice stack traces can read DWARF information from the executable on any OS. All
        // we need to do is make sure that the DWARF info is available for libbacktrace to read.
        //
        // On Windows, there is the PDB format, this is a separate file that contains the debug information. Zig
        // generates this too, but we can tell it to also embed DWARF debug info into the executable, that's what the
        // -gdwarf flag does.
        //
        // On Linux, it's easy, just use the same flag.
        //
        // On macOS, there is a slightly different approach. DWARF info is embedded in the compiled .o flags. But it's
        // not aggregated into the final executable. Instead, the final executable contains a 'debug map' which points
        // to all of the object files and shows where the DWARF info is. You can see this map by running
        // 'dsymutil --dump-debug-map my-exe'.
        //
        // In order to aggregate the DWARF info into the final executable, we need to run 'dsymutil my-exe'. This then
        // outputs a .dSYM folder which contains the aggregated DWARF info. libbacktrace looks for this dSYM folder
        // adjacent to the executable.

        // Include dwarf debug info, even on windows. This means we can use the libbacktrace/Zig everywhere to get
        // really good stack traces.
        //
        // We use DWARF 4 because Zig has a problem with version 5: https://github.com/ziglang/zig/issues/23732
        try self.flags.append("-gdwarf-4");

        if (options.ubsan) {
            if (context.optimise != .ReleaseFast) {
                // By default, zig enables UBSan (unless ReleaseFast mode) in trap mode. Meaning it will catch undefined
                // behaviour and trigger a trap which can be caught by signal handlers. UBSan also has a mode where
                // undefined behaviour will instead call various functions. This is called the UBSan runtime. It's
                // really easy to implement the 'minimal' version of this runtime: we just have to declare a bunch of
                // functions like __ubsan_handle_x. So that's what we do rather than trying to link with the system's
                // version. https://github.com/ziglang/zig/issues/5163#issuecomment-811606110
                try self.flags.append("-fno-sanitize-trap=undefined"); // undo zig's default behaviour (trap mode)
                try self.flags.append("-fno-sanitize=function");
                const minimal_runtime_mode = false; // I think it's better performance. Certainly less information.
                if (minimal_runtime_mode) {
                    try self.flags.append("-fsanitize-runtime"); // set it to 'minimal' mode
                }
            }
        } else {
            try self.flags.append("-fno-sanitize=all");
        }

        if (options.cpp) {
            try self.flags.append("-std=c++2c");
        }
        if (options.objcpp) {
            try self.flags.append("-std=c++2b");
            try self.flags.append("-ObjC++");
            try self.flags.append("-fobjc-arc");
        }

        if (options.add_compile_commands) {
            try self.flags.append("-gen-cdb-fragment-path");
            // IMPROVE: will this error if the path contains a space?
            try self.flags.append(try compileCommandsDirForTarget(context.b, target.result));
        }

        if (target.result.os.tag == .windows) {
            // On Windows, fix compile errors related to deprecated usage of string in mingw
            try self.flags.append("-DSTRSAFE_NO_DEPRECATE");
            try self.flags.append("-DUNICODE");
            try self.flags.append("-D_UNICODE");
        } else if (target.result.os.tag == .macos) {
            try self.flags.append("-DGL_SILENCE_DEPRECATION"); // disable opengl warnings on macos

            // don't fail when compiling macOS obj-c SDK headers
            try self.flags.appendSlice(&.{
                "-Wno-elaborated-enum-base",
                "-Wno-missing-method-return-type",
                "-Wno-deprecated-declarations",
                "-Wno-deprecated-anon-enum-enum-conversion",
                "-D__kernel_ptr_semantics=",
                "-Wno-c99-extensions",
            });
        }
    }
};

fn applyUniversalSettings(context: *BuildContext, step: *std.Build.Step.Compile) void {
    var b = context.b;
    // NOTE (May 2025, Zig 0.14): LTO on Windows results in debug_info generation that fails to parse with Zig's
    // Dwarf parser (InvalidDebugInfo). We skip LTO for now.
    if (step.rootModuleTarget().os.tag != .macos and step.rootModuleTarget().os.tag != .windows) {
        // TODO: try LTO on mac again
        // LTO doesn't seem like it's supported on mac
        step.want_lto = context.build_mode == .production;
    }
    step.rdynamic = true;
    step.linkLibC();

    step.addIncludePath(context.dep_xxhash.path(""));
    step.addIncludePath(context.dep_stb.path(""));
    step.addIncludePath(context.dep_clap.path("include"));
    step.addIncludePath(context.dep_dr_libs.path(""));
    step.addIncludePath(context.dep_flac.path("include"));
    step.addIncludePath(context.dep_libbacktrace.path(""));
    step.addIncludePath(context.dep_lua.path(""));
    step.addIncludePath(context.dep_pugl.path("include"));
    step.addIncludePath(context.dep_clap_wrapper.path("include"));
    step.addIncludePath(context.dep_tracy.path("public"));
    step.addIncludePath(context.dep_valgrind_h.path(""));
    step.addIncludePath(context.dep_portmidi.path("pm_common"));
    step.addIncludePath(context.dep_miniz.path(""));
    step.addIncludePath(b.path("third_party_libs/miniz"));

    step.addIncludePath(b.path("."));
    step.addIncludePath(b.path("src"));
    step.addIncludePath(b.path("third_party_libs"));

    if (step.rootModuleTarget().os.tag == .macos) {
        const sdk_root = b.graph.env_map.get("MACOSX_SDK_SYSROOT");
        if (sdk_root == null) {
            // This environment variable should be set and should be a path containing the macOS SDKS.
            // Nix is a great way to provide this. See flake.nix.
            //
            // An alternative option would be to download the macOS SDKs manually. For example:
            // https://github.com/joseluisq/macosx-sdks. And then set the env-var to that.
            @panic("env var $MACOSX_SDK_SYSROOT must be set");
        }
        b.sysroot = sdk_root;

        step.addSystemIncludePath(.{ .cwd_relative = b.pathJoin(&.{ sdk_root.?, "/usr/include" }) });
        step.addLibraryPath(.{ .cwd_relative = b.pathJoin(&.{ sdk_root.?, "/usr/lib" }) });
        step.addFrameworkPath(.{ .cwd_relative = b.pathJoin(&.{ sdk_root.?, "/System/Library/Frameworks" }) });
    }
}

fn getTargets(b: *std.Build, user_given_target_presets: ?[]const u8) !std.ArrayList(std.Build.ResolvedTarget) {
    var preset_strings: []const u8 = "native";
    if (user_given_target_presets != null) {
        preset_strings = user_given_target_presets.?;
    }

    const SupportedTargetId = enum {
        native,
        x86_64_windows,
        x86_64_linux,
        x86_64_macos,
        aarch64_macos,
    };

    // declare a hash map of target presets to SupportedTargetId
    var target_map = std.StringHashMap([]const SupportedTargetId).init(b.allocator);

    // the actual targets
    try target_map.put("native", &.{.native});
    try target_map.put("x86_64-windows", &.{.x86_64_windows});
    try target_map.put("x86_64-linux", &.{.x86_64_linux});
    try target_map.put("x86_64-macos", &.{.x86_64_macos});
    try target_map.put("aarch64-macos", &.{.aarch64_macos});

    // aliases/shortcuts
    try target_map.put("windows", &.{.x86_64_windows});
    try target_map.put("linux", &.{.x86_64_linux});
    try target_map.put("mac_x86", &.{.x86_64_macos});
    try target_map.put("mac_arm", &.{.aarch64_macos});
    try target_map.put("mac_ub", &.{ .x86_64_macos, .aarch64_macos });
    if (builtin.os.tag == .linux) {
        try target_map.put("dev", &.{ .native, .x86_64_windows, .aarch64_macos });
    } else if (builtin.os.tag == .macos) {
        try target_map.put("dev", &.{ .native, .x86_64_windows });
    }

    var targets = std.ArrayList(std.Build.ResolvedTarget).init(b.allocator);

    // I think Win10+ and macOS 11+ would allow us to target x86_64_v2 (which includes SSE3 and SSE4), but I can't
    // find definitive information on this. It's not a big deal for now; the baseline x86_64 target includes SSE2
    // which is the important feature for our performance-critical code.
    const x86_cpu = "x86_64";
    const apple_x86_cpu = "x86_64_v2";
    const apple_arm_cpu = "apple_m1";

    var it = std.mem.splitSequence(u8, preset_strings, ",");
    while (it.next()) |preset_string| {
        const target_ids = target_map.get(preset_string) orelse {
            std.debug.print("unknown target preset: {s}\n", .{preset_string});
            @panic("unknown target preset");
        };

        for (target_ids) |t| {
            var arch_os_abi: []const u8 = undefined;
            var cpu_features: []const u8 = undefined;
            switch (t) {
                .native => {
                    arch_os_abi = "native";
                    // valgrind doesn't like some AVX instructions so we'll target the baseline x86_64 for now
                    cpu_features = if (builtin.cpu.arch == .x86_64) x86_cpu else "native";
                },
                .x86_64_windows => {
                    arch_os_abi = "x86_64-windows." ++ min_windows_version;
                    cpu_features = x86_cpu;
                },
                .x86_64_linux => {
                    arch_os_abi = "x86_64-linux-gnu.2.29";
                    cpu_features = x86_cpu;
                },
                .x86_64_macos => {
                    arch_os_abi = "x86_64-macos." ++ min_macos_version;
                    cpu_features = apple_x86_cpu;
                },
                .aarch64_macos => {
                    arch_os_abi = "aarch64-macos." ++ min_macos_version;
                    cpu_features = apple_arm_cpu;
                },
            }

            try targets.append(b.resolveTargetQuery(try std.Target.Query.parse(.{
                .arch_os_abi = arch_os_abi,
                .cpu_features = cpu_features,
            })));
        }
    }

    return targets;
}

fn getLicenceText(b: *std.Build, filename: []const u8) ![]const u8 {
    const file = try b.build_root.handle.openFile(
        b.pathJoin(&.{ "LICENSES", filename }),
        .{ .mode = std.fs.File.OpenMode.read_only },
    );
    defer file.close();

    return try file.readToEndAlloc(b.allocator, 1024 * 1024 * 1024);
}

const ExternalResource = struct {
    relative_path: []const u8,
    absolute_path: []const u8,
};

fn getExternalResource(context: *BuildContext, name: []const u8) ?ExternalResource {
    if (context.external_resources_subdir == null) {
        std.debug.print(
            "WARNING: external resource folder is not set. Some aspects of the final build might be empty\n",
            .{},
        );
        return null;
    }

    const relative_path = context.b.pathJoin(&.{ context.external_resources_subdir.?, name });
    const absolute_path = context.b.pathJoin(&.{ context.b.build_root.path.?, relative_path });

    var found = true;
    std.fs.accessAbsolute(absolute_path, .{}) catch {
        found = false;
    };
    if (found) {
        return .{ .relative_path = relative_path, .absolute_path = absolute_path };
    } else {
        std.debug.print(
            "WARNING: external resource \"{s}\" not found in {s}. Some aspects of the final build might be empty\n",
            .{ name, context.external_resources_subdir.? },
        );
        return null;
    }
}

pub fn build(b: *std.Build) void {
    const build_mode = b.option(
        BuildMode,
        "build-mode",
        "The preset for building the project, affects optimisation, debug settings, etc.",
    ) orelse .development;

    const use_pkg_config = std.Build.Module.SystemLib.UsePkgConfig.yes;

    // Installing plugins to global plugin folders requires admin rights but it's often easier to debug
    // things without requiring admin. For production builds it's always enabled.
    var windows_installer_require_admin = b.option(
        bool,
        "win-installer-elevated",
        "Whether the installer should be set to administrator-required mode",
    ) orelse (build_mode == .production);
    if (build_mode == .production) windows_installer_require_admin = true;

    const enable_tracy = b.option(bool, "tracy", "Enable Tracy profiler") orelse false;

    var build_context: BuildContext = .{
        .b = b,
        .enable_tracy = enable_tracy,
        .build_mode = build_mode,
        .lipo_steps = std.StringHashMap(*LipoStep).init(b.allocator),
        .master_step = b.step("compile", "Compile all"),
        .test_step = b.step("test", "Run tests"),
        .optimise = switch (build_mode) {
            .development => std.builtin.OptimizeMode.Debug,
            .performance_profiling, .production => std.builtin.OptimizeMode.ReleaseSafe,
        },
        .external_resources_subdir = b.option(
            []const u8,
            "external-resources",
            "Path relative to build.zig that contains external build resources",
        ),
        .dep_xxhash = b.dependency("xxhash", .{}),
        .dep_stb = b.dependency("stb", .{}),
        .dep_au_sdk = b.dependency("audio_unit_sdk", .{}),
        .dep_miniaudio = b.dependency("miniaudio", .{}),
        .dep_clap = b.dependency("clap", .{}),
        .dep_clap_wrapper = b.dependency("clap_wrapper", .{}),
        .dep_dr_libs = b.dependency("dr_libs", .{}),
        .dep_flac = b.dependency("flac", .{}),
        .dep_icon_font_cpp_headers = b.dependency("icon_font_cpp_headers", .{}),
        .dep_miniz = b.dependency("miniz", .{}),
        .dep_libbacktrace = b.dependency("libbacktrace", .{}),
        .dep_lua = b.dependency("lua", .{}),
        .dep_pugl = b.dependency("pugl", .{}),
        .dep_pffft = b.dependency("pffft", .{}),
        .dep_valgrind_h = b.dependency("valgrind_h", .{}),
        .dep_portmidi = b.dependency("portmidi", .{}),
        .dep_tracy = b.dependency("tracy", .{}),
        .dep_vst3_sdk = b.dependency("vst3_sdk", .{}),
    };

    const user_given_target_presets = b.option([]const u8, "targets", "Target operating system");

    // ignore any error
    b.build_root.handle.makeDir(floe_cache_relative) catch {};

    // const install_dir = b.install_path; // zig-out

    const targets = getTargets(b, user_given_target_presets) catch @panic("OOM");

    // If we're building for multiple targets at the same time, we need to choose one that gets to be the final compile_commands.json.
    const target_for_compile_commands = targets.items[0];
    // We'll try installing the desired compile_commands.json version here in case any previous build already created it.
    tryCopyCompileCommandsForTargetFileToDefault(b, target_for_compile_commands.result);

    for (targets.items) |target| {
        var join_compile_commands = b.allocator.create(ConcatCompileCommandsStep) catch @panic("OOM");
        join_compile_commands.* = ConcatCompileCommandsStep{
            .step = std.Build.Step.init(.{
                .id = std.Build.Step.Id.custom,
                .name = "Concatenate compile_commands JSON",
                .owner = b,
                .makeFn = concatCompileCommands,
            }),
            .target = target,
            .use_as_default = target.query.eql(target_for_compile_commands.query),
        };

        const install_subfolder_string = b.dupe(archAndOsPair(target.result).slice());
        const install_dir = std.Build.InstallDir{ .custom = install_subfolder_string };
        const install_subfolder = std.Build.Step.InstallArtifact.Options.Dir{
            .override = install_dir,
        };

        const git_commit = std.mem.trim(u8, b.run(&.{ "git", "rev-parse", "--short", "HEAD" }), " \r\n\t");

        var floe_version_string: ?[]const u8 = null;
        {
            var file = b.build_root.handle.openFile(
                "version.txt",
                .{ .mode = .read_only },
            ) catch @panic("version.txt not found");
            defer file.close();
            floe_version_string = file.readToEndAlloc(b.allocator, 256) catch @panic("version.txt error");
            floe_version_string = std.mem.trim(u8, floe_version_string.?, " \r\n\t");

            if (build_context.build_mode != .production) {
                floe_version_string = b.fmt("{s}+{s}", .{ floe_version_string.?, git_commit });
            }
        }

        const floe_version = std.SemanticVersion.parse(floe_version_string.?) catch @panic("invalid version");
        const floe_version_hash = std.hash.Fnv1a_32.hash(floe_version_string.?);

        const windows_ntddi_version: i64 = @intFromEnum(std.Target.Os.WindowsVersion.parse(min_windows_version) catch @panic("invalid win ver"));

        const build_config_step = b.addConfigHeader(.{
            .style = .blank,
        }, .{
            .PRODUCTION_BUILD = build_context.build_mode == .production,
            .RUNTIME_SAFETY_CHECKS_ON = build_context.optimise == .Debug or build_context.optimise == .ReleaseSafe,
            .FLOE_VERSION_STRING = floe_version_string,
            .FLOE_VERSION_HASH = floe_version_hash,
            .FLOE_DESCRIPTION = floe_description,
            .FLOE_HOMEPAGE_URL = floe_homepage_url,
            .FLOE_MANUAL_URL = floe_manual_url,
            .FLOE_DOWNLOAD_URL = floe_download_url,
            .FLOE_SOURCE_CODE_URL = floe_source_code_url,
            .FLOE_PROJECT_ROOT_PATH = b.build_root.path.?,
            .FLOE_PROJECT_CACHE_PATH = b.pathJoin(&.{ b.build_root.path.?, floe_cache_relative }),
            .FLOE_GLOBAL_ZIG_CACHE_PATH = b.graph.global_cache_root.path,
            .FLOE_VENDOR = floe_vendor,
            .FLOE_CLAP_ID = floe_clap_id,
            .ZIG_BACKTRACE = target.result.os.tag != .macos,
            .IS_WINDOWS = target.result.os.tag == .windows,
            .IS_MACOS = target.result.os.tag == .macos,
            .IS_LINUX = target.result.os.tag == .linux,
            .OS_DISPLAY_NAME = b.fmt("{s}", .{@tagName(target.result.os.tag)}),
            .ARCH_DISPLAY_NAME = b.fmt("{s}", .{@tagName(target.result.cpu.arch)}),
            .MIN_WINDOWS_NTDDI_VERSION = windows_ntddi_version,
            .MIN_MACOS_VERSION = min_macos_version,
            .SENTRY_DSN = b.graph.env_map.get("SENTRY_DSN"),
        });

        const module_options: std.Build.Module.CreateOptions = .{
            .target = target,
            .optimize = build_context.optimise,
            .strip = false,
            .pic = true,
            .link_libc = true,
            .omit_frame_pointer = false,
        };

        var stb_sprintf = b.addObject(.{
            .name = "stb_sprintf",
            .root_module = b.createModule(module_options),
        });
        stb_sprintf.addCSourceFile(.{
            .file = b.path("third_party_libs/stb_sprintf.c"),
            .flags = FlagsBuilder.init(&build_context, target, .{}).flags.items,
        });
        stb_sprintf.addIncludePath(build_context.dep_stb.path(""));

        var xxhash = b.addObject(.{
            .name = "xxhash",
            .root_module = b.createModule(module_options),
        });
        xxhash.addCSourceFile(.{
            .file = build_context.dep_xxhash.path("xxhash.c"),
            .flags = FlagsBuilder.init(&build_context, target, .{}).flags.items,
        });
        xxhash.linkLibC();

        const tracy = b.addStaticLibrary(.{
            .name = "tracy",
            .root_module = b.createModule(module_options),
        });
        {
            tracy.addCSourceFile(.{
                .file = build_context.dep_tracy.path("public/TracyClient.cpp"),
                .flags = FlagsBuilder.init(&build_context, target, .{}).flags.items,
            });

            switch (target.result.os.tag) {
                .windows => {
                    tracy.linkSystemLibrary("ws2_32");
                },
                .macos => {},
                .linux => {},
                else => {
                    unreachable;
                },
            }
            tracy.linkLibCpp();
            applyUniversalSettings(&build_context, tracy);
        }

        const vitfx = b.addStaticLibrary(.{
            .name = "vitfx",
            .root_module = b.createModule(module_options),
        });
        {
            const vitfx_path = "third_party_libs/vitfx";
            vitfx.addCSourceFiles(.{
                .files = &.{
                    vitfx_path ++ "/src/synthesis/effects/reverb.cpp",
                    vitfx_path ++ "/src/synthesis/effects/phaser.cpp",
                    vitfx_path ++ "/src/synthesis/effects/delay.cpp",
                    vitfx_path ++ "/src/synthesis/framework/processor.cpp",
                    vitfx_path ++ "/src/synthesis/framework/processor_router.cpp",
                    vitfx_path ++ "/src/synthesis/framework/value.cpp",
                    vitfx_path ++ "/src/synthesis/framework/feedback.cpp",
                    vitfx_path ++ "/src/synthesis/framework/operators.cpp",
                    vitfx_path ++ "/src/synthesis/filters/phaser_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/synth_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/sallen_key_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/comb_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/digital_svf.cpp",
                    vitfx_path ++ "/src/synthesis/filters/dirty_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/ladder_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/diode_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/formant_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/formant_manager.cpp",
                    vitfx_path ++ "/wrapper.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{}).flags.items,
            });
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/synthesis"));
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/synthesis/framework"));
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/synthesis/filters"));
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/synthesis/lookups"));
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/common"));
            vitfx.linkLibCpp();

            vitfx.addIncludePath(build_context.dep_tracy.path("public"));

            b.getInstallStep().dependOn(&b.addInstallArtifact(vitfx, .{ .dest_dir = install_subfolder }).step);
        }

        const libbacktrace = b.addStaticLibrary(.{
            .name = "backtrace",
            .root_module = b.createModule(module_options),
        });
        if (true) {
            const libbacktrace_root_path = build_context.dep_libbacktrace.path("");
            const posix_sources = .{
                "mmap.c",
                "mmapio.c",
            };

            const backtrace_flags = FlagsBuilder.init(&build_context, target, .{}).flags.items;

            libbacktrace.addCSourceFiles(.{
                .root = libbacktrace_root_path,
                .files = &.{
                    "backtrace.c",
                    "dwarf.c",
                    "fileline.c",
                    "print.c",
                    "read.c",
                    "simple.c",
                    "sort.c",
                    "state.c",
                    "posix.c",
                },
                .flags = backtrace_flags,
            });

            const backtrace_supported_header = b.addConfigHeader(
                .{
                    .include_path = "backtrace-supported.h",
                },
                .{
                    .BACKTRACE_SUPPORTED = 1,
                    .BACKTRACE_USES_MALLOC = @intFromBool(target.result.os.tag == .windows),
                    .BACKTRACE_SUPPORTS_THREADS = 1,
                    .BACKTRACE_SUPPORTS_DATA = 1,
                },
            );

            const config_header = b.addConfigHeader(
                .{
                    .include_path = "config.h",
                },
                .{
                    .BACKTRACE_ELF_SIZE = target.result.ptrBitWidth(),
                    .BACKTRACE_XCOFF_SIZE = target.result.ptrBitWidth(),
                    .HAVE_ATOMIC_FUNCTIONS = 1,
                    .HAVE_SYNC_FUNCTIONS = 1,
                    .HAVE_DECL_STRNLEN = 1,
                    .HAVE_DLFCN_H = 1,
                    .HAVE_DL_ITERATE_PHDR = 1,
                    .HAVE_INTTYPES_H = 1,
                    .HAVE_LINK_H = 1,
                    .HAVE_LSTAT = 1,
                    .HAVE_MEMORY_H = 1,
                    .HAVE_READLINK = 1,
                    .HAVE_STDINT_H = 1,
                    .HAVE_STDLIB_H = 1,
                    .HAVE_STRINGS_H = 1,
                    .HAVE_STRING_H = 1,
                    .HAVE_SYS_LDR_H = 1,
                    .HAVE_SYS_MMAN_H = 1,
                    .HAVE_SYS_STAT_H = 1,
                    .HAVE_SYS_TYPES_H = 1,
                    .HAVE_UNISTD_H = 1,
                    .STDC_HEADERS = 1,
                    ._FILE_OFFSET_BITS = 64,
                    ._LARGE_FILES = 1,
                },
            );

            switch (target.result.os.tag) {
                .windows => {
                    config_header.addValues(.{
                        .HAVE_DECL__PGMPTR = target.result.os.tag == .windows,
                        .HAVE_WINDOWS_H = 1,
                    });

                    libbacktrace.addCSourceFiles(.{
                        .root = libbacktrace_root_path,
                        .files = &.{
                            "pecoff.c",
                            "alloc.c",
                        },
                        .flags = backtrace_flags,
                    });
                },
                .macos => {
                    config_header.addValues(.{
                        .HAVE_MACH_O_DYLD_H = 1,
                        .HAVE_FCNTL = 1,
                    });

                    libbacktrace.addCSourceFiles(.{
                        .root = libbacktrace_root_path,
                        .files = &posix_sources,
                        .flags = backtrace_flags,
                    });
                    libbacktrace.addCSourceFiles(.{
                        .root = libbacktrace_root_path,
                        .files = &.{"macho.c"},
                        .flags = backtrace_flags,
                    });
                },
                .linux => {
                    config_header.addValues(.{
                        ._POSIX_SOURCE = 1,
                        ._GNU_SOURCE = 1,
                        .HAVE_CLOCK_GETTIME = target.result.os.tag == .linux,
                        .HAVE_DECL_GETPAGESIZE = target.result.os.tag == .linux,
                        .HAVE_FCNTL = 1,
                    });

                    libbacktrace.addCSourceFiles(.{
                        .root = libbacktrace_root_path,
                        .files = &.{"elf.c"},
                        .flags = backtrace_flags,
                    });
                    libbacktrace.addCSourceFiles(.{
                        .root = libbacktrace_root_path,
                        .files = &posix_sources,
                        .flags = backtrace_flags,
                    });
                },
                else => {
                    unreachable;
                },
            }

            libbacktrace.linkLibC();
            libbacktrace.addConfigHeader(config_header);
            libbacktrace.addConfigHeader(backtrace_supported_header);
            applyUniversalSettings(&build_context, libbacktrace);
        }

        const pugl = b.addStaticLibrary(.{
            .name = "pugl",
            .root_module = b.createModule(module_options),
        });
        {
            const pugl_path = build_context.dep_pugl.path("src");
            const pugl_version = std.hash.Fnv1a_32.hash(build_context.dep_pugl.builder.pkg_hash);

            const pugl_flags = FlagsBuilder.init(&build_context, target, .{}).flags.items;

            pugl.addCSourceFiles(.{
                .root = pugl_path,
                .files = &.{
                    "common.c",
                    "internal.c",
                    "internal.c",
                },
                .flags = pugl_flags,
            });

            switch (target.result.os.tag) {
                .windows => {
                    pugl.addCSourceFiles(.{
                        .root = pugl_path,
                        .files = &.{
                            "win.c",
                            "win_gl.c",
                            "win_stub.c",
                        },
                        .flags = pugl_flags,
                    });
                    pugl.linkSystemLibrary("opengl32");
                    pugl.linkSystemLibrary("gdi32");
                    pugl.linkSystemLibrary("dwmapi");
                },
                .macos => {
                    pugl.addCSourceFiles(.{
                        .root = pugl_path,
                        .files = &.{
                            "mac.m",
                            "mac_gl.m",
                            "mac_stub.m",
                        },
                        .flags = pugl_flags,
                    });
                    pugl.root_module.addCMacro("PuglWindow", b.fmt("PuglWindow{d}", .{pugl_version}));
                    pugl.root_module.addCMacro("PuglWindowDelegate", b.fmt("PuglWindowDelegate{d}", .{pugl_version}));
                    pugl.root_module.addCMacro("PuglWrapperView", b.fmt("PuglWrapperView{d}", .{pugl_version}));
                    pugl.root_module.addCMacro("PuglOpenGLView", b.fmt("PuglOpenGLView{d}", .{pugl_version}));

                    pugl.linkFramework("OpenGL");
                    pugl.linkFramework("CoreVideo");
                },
                else => {
                    pugl.addCSourceFiles(.{
                        .root = pugl_path,
                        .files = &.{
                            "x11.c",
                            "x11_gl.c",
                            "x11_stub.c",
                        },
                        .flags = pugl_flags,
                    });
                    pugl.root_module.addCMacro("USE_XRANDR", "0");
                    pugl.root_module.addCMacro("USE_XSYNC", "1");
                    pugl.root_module.addCMacro("USE_XCURSOR", "1");

                    pugl.linkSystemLibrary2("gl", .{ .use_pkg_config = use_pkg_config });
                    pugl.linkSystemLibrary2("glx", .{ .use_pkg_config = use_pkg_config });
                    pugl.linkSystemLibrary2("x11", .{ .use_pkg_config = use_pkg_config });
                    pugl.linkSystemLibrary2("xcursor", .{ .use_pkg_config = use_pkg_config });
                    pugl.linkSystemLibrary2("xext", .{ .use_pkg_config = use_pkg_config });
                },
            }

            pugl.root_module.addCMacro("PUGL_DISABLE_DEPRECATED", "1");
            pugl.root_module.addCMacro("PUGL_STATIC", "1");

            applyUniversalSettings(&build_context, pugl);
        }

        var debug_info_module_options = module_options;
        debug_info_module_options.root_source_file = b.path("src/utils/debug_info/debug_info.zig");
        const debug_info_lib = b.addObject(.{
            .name = "debug_info_lib",
            .root_module = b.createModule(debug_info_module_options),
        });
        debug_info_lib.linkLibC(); // Means better debug info on Linux
        debug_info_lib.addIncludePath(b.path("src/utils/debug_info"));

        // TODO: does this need to be a library? is foundation/os/plugin all linked together?
        const library = b.addStaticLibrary(.{
            .name = "library",
            .root_module = b.createModule(module_options),
        });
        {
            const library_path = "src";

            const common_source_files = .{
                library_path ++ "/utils/debug/debug.cpp",
                library_path ++ "/utils/cli_arg_parse.cpp",
                library_path ++ "/utils/leak_detecting_allocator.cpp",
                library_path ++ "/tests/framework.cpp",
                library_path ++ "/utils/logger/logger.cpp",
                library_path ++ "/foundation/utils/string.cpp",
                library_path ++ "/os/filesystem.cpp",
                library_path ++ "/os/misc.cpp",
                library_path ++ "/os/web.cpp",
                library_path ++ "/os/threading.cpp",
            };

            const unix_source_files = .{
                library_path ++ "/os/filesystem_unix.cpp",
                library_path ++ "/os/misc_unix.cpp",
                library_path ++ "/os/threading_pthread.cpp",
            };

            const windows_source_files = .{
                library_path ++ "/os/filesystem_windows.cpp",
                library_path ++ "/os/misc_windows.cpp",
                library_path ++ "/os/threading_windows.cpp",
                library_path ++ "/os/web_windows.cpp",
            };

            const macos_source_files = .{
                library_path ++ "/os/filesystem_mac.mm",
                library_path ++ "/os/misc_mac.mm",
                library_path ++ "/os/threading_mac.cpp",
                library_path ++ "/os/web_mac.mm",
            };

            const linux_source_files = .{
                library_path ++ "/os/filesystem_linux.cpp",
                library_path ++ "/os/misc_linux.cpp",
                library_path ++ "/os/threading_linux.cpp",
                library_path ++ "/os/web_linux.cpp",
            };

            const library_flags = FlagsBuilder.init(&build_context, target, .{
                .full_diagnostics = true,
                .ubsan = true,
                .cpp = true,
            }).flags.items;

            switch (target.result.os.tag) {
                .windows => {
                    library.addCSourceFiles(.{
                        .files = &windows_source_files,
                        .flags = library_flags,
                    });
                    library.linkSystemLibrary("dbghelp");
                    library.linkSystemLibrary("shlwapi");
                    library.linkSystemLibrary("ole32");
                    library.linkSystemLibrary("crypt32");
                    library.linkSystemLibrary("uuid");
                    library.linkSystemLibrary("winhttp");

                    // synchronization.lib (https://github.com/ziglang/zig/issues/14919)
                    library.linkSystemLibrary("api-ms-win-core-synch-l1-2-0");
                },
                .macos => {
                    library.addCSourceFiles(.{
                        .files = &unix_source_files,
                        .flags = library_flags,
                    });
                    library.addCSourceFiles(.{
                        .files = &macos_source_files,
                        .flags = FlagsBuilder.init(&build_context, target, .{
                            .full_diagnostics = true,
                            .ubsan = true,
                            .objcpp = true,
                        }).flags.items,
                    });
                    library.linkFramework("Cocoa");
                    library.linkFramework("CoreFoundation");
                    library.linkFramework("AppKit");
                },
                .linux => {
                    library.addCSourceFiles(.{ .files = &unix_source_files, .flags = library_flags });
                    library.addCSourceFiles(.{ .files = &linux_source_files, .flags = library_flags });
                    library.linkSystemLibrary2("libcurl", .{ .use_pkg_config = use_pkg_config });
                },
                else => {
                    unreachable;
                },
            }

            library.addCSourceFiles(.{ .files = &common_source_files, .flags = library_flags });
            library.addConfigHeader(build_config_step);
            library.linkLibC();
            library.linkLibrary(tracy);
            library.addObject(debug_info_lib);
            library.addObject(stb_sprintf);
            // library.linkLibCpp(); // needed for __cxa_demangle
            library.linkLibrary(libbacktrace);
            join_compile_commands.step.dependOn(&library.step);
            applyUniversalSettings(&build_context, library);
        }

        var stb_image = b.addObject(.{
            .name = "stb_image",
            .root_module = b.createModule(module_options),
        });
        stb_image.addCSourceFile(.{
            .file = b.path("third_party_libs/stb_image_impls.c"),
            .flags = FlagsBuilder.init(&build_context, target, .{}).flags.items,
        });
        stb_image.addIncludePath(build_context.dep_stb.path(""));
        stb_image.linkLibC();

        var dr_wav = b.addObject(.{
            .name = "dr_wav",
            .root_module = b.createModule(module_options),
        });
        dr_wav.addCSourceFile(
            .{
                .file = b.path("third_party_libs/dr_wav_implementation.c"),
                .flags = FlagsBuilder.init(&build_context, target, .{}).flags.items,
            },
        );
        dr_wav.addIncludePath(build_context.dep_dr_libs.path(""));
        dr_wav.linkLibC();

        var miniz = b.addStaticLibrary(.{
            .name = "miniz",
            .root_module = b.createModule(module_options),
        });
        {
            miniz.addCSourceFiles(.{
                .root = build_context.dep_miniz.path(""),
                .files = &.{
                    "miniz.c",
                    "miniz_tdef.c",
                    "miniz_tinfl.c",
                    "miniz_zip.c",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{}).flags.items,
            });
            miniz.addIncludePath(build_context.dep_miniz.path(""));
            miniz.linkLibC();
            miniz.addIncludePath(b.path("third_party_libs/miniz"));
        }

        const flac = b.addStaticLibrary(.{
            .name = "flac",
            .root_module = b.createModule(module_options),
        });
        {
            const flac_flags = FlagsBuilder.init(&build_context, target, .{}).flags.items;

            flac.addCSourceFiles(.{
                .root = build_context.dep_flac.path("src/libFLAC"),
                .files = &.{
                    "bitmath.c",
                    "bitreader.c",
                    "bitwriter.c",
                    "cpu.c",
                    "crc.c",
                    "fixed.c",
                    "fixed_intrin_sse2.c",
                    "fixed_intrin_ssse3.c",
                    "fixed_intrin_sse42.c",
                    "fixed_intrin_avx2.c",
                    "float.c",
                    "format.c",
                    "lpc.c",
                    "lpc_intrin_neon.c",
                    "lpc_intrin_sse2.c",
                    "lpc_intrin_sse41.c",
                    "lpc_intrin_avx2.c",
                    "lpc_intrin_fma.c",
                    "md5.c",
                    "memory.c",
                    "metadata_iterators.c",
                    "metadata_object.c",
                    "stream_decoder.c",
                    "stream_encoder.c",
                    "stream_encoder_intrin_sse2.c",
                    "stream_encoder_intrin_ssse3.c",
                    "stream_encoder_intrin_avx2.c",
                    "stream_encoder_framing.c",
                    "window.c",
                },
                .flags = flac_flags,
            });

            const config_header = b.addConfigHeader(
                .{
                    .style = .{ .cmake = build_context.dep_flac.path("config.cmake.h.in") },
                    .include_path = "config.h",
                },
                .{
                    .CPU_IS_BIG_ENDIAN = target.result.cpu.arch.endian() == .big,
                    .ENABLE_64_BIT_WORDS = target.result.ptrBitWidth() == 64,
                    .FLAC__ALIGN_MALLOC_DATA = target.result.cpu.arch.isX86(),
                    .FLAC__CPU_ARM64 = target.result.cpu.arch.isAARCH64(),
                    .FLAC__SYS_DARWIN = target.result.os.tag == .macos,
                    .FLAC__SYS_LINUX = target.result.os.tag == .linux,
                    .HAVE_BYTESWAP_H = target.result.os.tag == .linux,
                    .HAVE_CPUID_H = target.result.cpu.arch.isX86(),
                    .HAVE_FSEEKO = true,
                    .HAVE_ICONV = target.result.os.tag != .windows,
                    .HAVE_INTTYPES_H = true,
                    .HAVE_MEMORY_H = true,
                    .HAVE_STDINT_H = true,
                    .HAVE_STRING_H = true,
                    .HAVE_STDLIB_H = true,
                    .HAVE_TYPEOF = true,
                    .HAVE_UNISTD_H = true,
                    .GIT_COMMIT_DATE = "",
                    .GIT_COMMIT_HASH = "",
                    .GIT_COMMIT_TAG = "",
                    .PROJECT_VERSION = "1.0.0",
                },
            );

            flac.linkLibC();
            flac.root_module.addCMacro("HAVE_CONFIG_H", "");
            flac.addConfigHeader(config_header);
            flac.addIncludePath(build_context.dep_flac.path("include"));
            flac.addIncludePath(build_context.dep_flac.path("src/libFLAC/include"));
            if (target.result.os.tag == .windows) {
                flac.root_module.addCMacro("FLAC__NO_DLL", "");
                flac.addCSourceFile(.{
                    .file = build_context.dep_flac.path("src/share/win_utf8_io/win_utf8_io.c"),
                    .flags = flac_flags,
                });
            }
        }

        const fft_convolver = b.addStaticLibrary(.{
            .name = "fftconvolver",
            .root_module = b.createModule(module_options),
        });
        {
            var fft_flags: FlagsBuilder = FlagsBuilder.init(&build_context, target, .{});
            if (target.result.os.tag == .macos) {
                fft_convolver.linkFramework("Accelerate");
                fft_flags.addFlag("-DAUDIOFFT_APPLE_ACCELERATE");
                fft_flags.addFlag("-ObjC++");
            } else {
                fft_convolver.addCSourceFile(.{
                    .file = build_context.dep_pffft.path("pffft.c"),
                    .flags = FlagsBuilder.init(&build_context, target, .{}).flags.items,
                });
                fft_flags.addFlag("-DAUDIOFFT_PFFFT");
            }

            fft_convolver.addCSourceFiles(.{
                .files = &.{
                    "third_party_libs/FFTConvolver/AudioFFT.cpp",
                    "third_party_libs/FFTConvolver/FFTConvolver.cpp",
                    "third_party_libs/FFTConvolver/TwoStageFFTConvolver.cpp",
                    "third_party_libs/FFTConvolver/Utilities.cpp",
                    "third_party_libs/FFTConvolver/wrapper.cpp",
                },
                .flags = fft_flags.flags.items,
            });
            fft_convolver.linkLibCpp();
            fft_convolver.addIncludePath(build_context.dep_pffft.path(""));
            applyUniversalSettings(&build_context, fft_convolver);
        }

        const common_infrastructure = b.addStaticLibrary(.{
            .name = "common_infrastructure",
            .root_module = b.createModule(module_options),
        });
        {
            const lua = b.addStaticLibrary(.{
                .name = "lua",
                .target = target,
                .optimize = build_context.optimise,
            });
            {
                const flags = [_][]const u8{
                    switch (target.result.os.tag) {
                        .linux => "-DLUA_USE_LINUX",
                        .macos => "-DLUA_USE_MACOSX",
                        .windows => "-DLUA_USE_WINDOWS",
                        else => "-DLUA_USE_POSIX",
                    },
                    if (build_context.optimise == .Debug) "-DLUA_USE_APICHECK" else "",
                };

                // compile as C++ so it uses exceptions instead of setjmp/longjmp. we use try/catch when handling lua
                lua.addCSourceFile(.{
                    .file = b.path("third_party_libs/lua.cpp"),
                    .flags = &flags,
                });
                lua.addIncludePath(build_context.dep_lua.path(""));
                lua.linkLibC();
            }

            const path = "src/common_infrastructure";
            common_infrastructure.addCSourceFiles(.{
                .files = &.{
                    path ++ "/audio_utils.cpp",
                    path ++ "/checksum_crc32_file.cpp",
                    path ++ "/common_errors.cpp",
                    path ++ "/descriptors/param_descriptors.cpp",
                    path ++ "/error_reporting.cpp",
                    path ++ "/global.cpp",
                    path ++ "/package_format.cpp",
                    path ++ "/paths.cpp",
                    path ++ "/preferences.cpp",
                    path ++ "/sample_library/audio_file.cpp",
                    path ++ "/sample_library/sample_library.cpp",
                    path ++ "/sample_library/sample_library_lua.cpp",
                    path ++ "/sample_library/sample_library_mdata.cpp",
                    path ++ "/sentry/sentry.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                }).flags.items,
            });

            common_infrastructure.linkLibrary(lua);
            common_infrastructure.addObject(dr_wav);
            common_infrastructure.linkLibrary(flac);
            common_infrastructure.addObject(xxhash);
            common_infrastructure.addConfigHeader(build_config_step);
            common_infrastructure.addIncludePath(b.path(path));
            common_infrastructure.linkLibrary(library);
            common_infrastructure.linkLibrary(miniz);
            applyUniversalSettings(&build_context, common_infrastructure);
            join_compile_commands.step.dependOn(&common_infrastructure.step);
        }

        var embedded_files: ?*std.Build.Step.Compile = null;
        if (!embed_files_workaround) {
            var emdedded_files_module_options = module_options;
            emdedded_files_module_options.root_source_file = b.path("build_resources/embedded_files.zig");
            embedded_files = b.addObject(.{
                .name = "embedded_files",
                .root_module = b.createModule(emdedded_files_module_options),
            });
            {
                var embedded_files_options = b.addOptions();
                {
                    const logo_resource = getExternalResource(&build_context, "Logos/rasterized/plugin-gui-logo.png");
                    const logo_path = if (logo_resource) |r| r.absolute_path else null;
                    embedded_files_options.addOption(?[]const u8, "logo_file", logo_path);
                }
                {
                    const icon_resource = getExternalResource(&build_context, "Logos/rasterized/icon-background-256px.png");
                    const icon_path = if (icon_resource) |r| r.absolute_path else null;
                    embedded_files_options.addOption(?[]const u8, "icon_file", icon_path);
                }
                embedded_files.?.root_module.addOptions("build_options", embedded_files_options);
            }
            embedded_files.?.linkLibC();
            embedded_files.?.addIncludePath(b.path("build_resources"));
        }

        const plugin = b.addStaticLibrary(.{
            .name = "plugin",
            .root_module = b.createModule(module_options),
        });
        {
            const plugin_path = "src/plugin";

            const plugin_flags = FlagsBuilder.init(&build_context, target, .{
                .full_diagnostics = true,
                .ubsan = true,
                .cpp = true,
            }).flags.items;

            plugin.addCSourceFiles(.{
                .files = &(.{
                    plugin_path ++ "/engine/autosave.cpp",
                    plugin_path ++ "/engine/autosave.cpp",
                    plugin_path ++ "/engine/engine.cpp",
                    plugin_path ++ "/engine/package_installation.cpp",
                    plugin_path ++ "/engine/shared_engine_systems.cpp",
                    plugin_path ++ "/gui/gui.cpp",
                    plugin_path ++ "/gui/gui2_inst_picker.cpp",
                    plugin_path ++ "/gui/gui2_ir_picker.cpp",
                    plugin_path ++ "/gui/gui2_preset_picker.cpp",
                    plugin_path ++ "/gui/gui2_save_preset_panel.cpp",
                    plugin_path ++ "/gui/gui_bot_panel.cpp",
                    plugin_path ++ "/gui/gui_button_widgets.cpp",
                    plugin_path ++ "/gui/gui_dragger_widgets.cpp",
                    plugin_path ++ "/gui/gui_drawing_helpers.cpp",
                    plugin_path ++ "/gui/gui_editor_widgets.cpp",
                    plugin_path ++ "/gui/gui_effects.cpp",
                    plugin_path ++ "/gui/gui_envelope.cpp",
                    plugin_path ++ "/gui/gui_keyboard.cpp",
                    plugin_path ++ "/gui/gui_knob_widgets.cpp",
                    plugin_path ++ "/gui/gui_label_widgets.cpp",
                    plugin_path ++ "/gui/gui_layer.cpp",
                    plugin_path ++ "/gui/gui_library_images.cpp",
                    plugin_path ++ "/gui/gui_mid_panel.cpp",
                    plugin_path ++ "/gui/gui_modal_windows.cpp",
                    plugin_path ++ "/gui/gui_peak_meter_widget.cpp",
                    plugin_path ++ "/gui/gui_prefs.cpp",
                    plugin_path ++ "/gui/gui_top_panel.cpp",
                    plugin_path ++ "/gui/gui_velocity_buttons.cpp",
                    plugin_path ++ "/gui/gui_waveform.cpp",
                    plugin_path ++ "/gui/gui_widget_compounds.cpp",
                    plugin_path ++ "/gui/gui_widget_helpers.cpp",
                    plugin_path ++ "/gui/gui_window.cpp",
                    plugin_path ++ "/gui_framework/draw_list.cpp",
                    plugin_path ++ "/gui_framework/draw_list_opengl.cpp",
                    plugin_path ++ "/gui_framework/gui_imgui.cpp",
                    plugin_path ++ "/gui_framework/gui_platform.cpp",
                    plugin_path ++ "/gui_framework/layout.cpp",
                    plugin_path ++ "/plugin/hosting_tests.cpp",
                    plugin_path ++ "/plugin/plugin.cpp",
                    plugin_path ++ "/preset_server/preset_server.cpp",
                    plugin_path ++ "/processing_utils/midi.cpp",
                    plugin_path ++ "/processing_utils/volume_fade.cpp",
                    plugin_path ++ "/processor/layer_processor.cpp",
                    plugin_path ++ "/processor/processor.cpp",
                    plugin_path ++ "/processor/voices.cpp",
                    plugin_path ++ "/sample_lib_server/sample_library_server.cpp",
                    plugin_path ++ "/state/state_coding.cpp",
                }),
                .flags = plugin_flags,
            });

            switch (target.result.os.tag) {
                .windows => {
                    plugin.addCSourceFiles(.{
                        .files = &.{
                            plugin_path ++ "/gui_framework/gui_platform_windows.cpp",
                        },
                        .flags = plugin_flags,
                    });
                },
                .linux => {
                    plugin.addCSourceFiles(.{
                        .files = &.{
                            plugin_path ++ "/gui_framework/gui_platform_linux.cpp",
                        },
                        .flags = plugin_flags,
                    });
                },
                .macos => {
                    plugin.addCSourceFiles(.{
                        .files = &.{
                            plugin_path ++ "/gui_framework/gui_platform_mac.mm",
                        },
                        .flags = FlagsBuilder.init(&build_context, target, .{
                            .full_diagnostics = true,
                            .ubsan = true,
                            .objcpp = true,
                        }).flags.items,
                    });
                },
                else => {
                    unreachable;
                },
            }

            const licences_header = b.addConfigHeader(.{
                .include_path = "licence_texts.h",
                .style = .blank,
            }, .{
                .GPL_3_LICENSE = getLicenceText(b, "GPL-3.0-or-later.txt") catch @panic("missing license text"),
                .APACHE_2_0_LICENSE = getLicenceText(b, "Apache-2.0.txt") catch @panic("missing license text"),
                .FFTPACK_LICENSE = getLicenceText(b, "LicenseRef-FFTPACK.txt") catch @panic("missing license text"),
                .OFL_1_1_LICENSE = getLicenceText(b, "OFL-1.1.txt") catch @panic("missing license text"),
                .BSD_3_CLAUSE_LICENSE = getLicenceText(b, "BSD-3-Clause.txt") catch @panic("missing license text"),
                .BSD_2_CLAUSE_LICENSE = getLicenceText(b, "BSD-2-Clause.txt") catch @panic("missing license text"),
                .ISC_LICENSE = getLicenceText(b, "ISC.txt") catch @panic("missing license text"),
                .MIT_LICENSE = getLicenceText(b, "MIT.txt") catch @panic("missing license text"),
            });
            plugin.addConfigHeader(licences_header);

            plugin.addIncludePath(b.path("src/plugin"));
            plugin.addIncludePath(b.path("src"));
            plugin.addIncludePath(build_context.dep_icon_font_cpp_headers.path(""));
            plugin.addConfigHeader(build_config_step);
            plugin.linkLibrary(library);
            plugin.linkLibrary(common_infrastructure);
            plugin.linkLibrary(fft_convolver);
            if (embed_files_workaround) {
                plugin.addCSourceFile(.{
                    .file = b.dependency("embedded_files_workaround", .{}).path("embedded_files.cpp"),
                });
            } else {
                plugin.addObject(embedded_files.?);
            }
            plugin.linkLibrary(tracy);
            plugin.linkLibrary(pugl);
            plugin.addObject(stb_image);
            plugin.addIncludePath(b.path("src/plugin/gui/live_edit_defs"));
            plugin.linkLibrary(vitfx);
            plugin.linkLibrary(miniz);
            applyUniversalSettings(&build_context, plugin);
            join_compile_commands.step.dependOn(&plugin.step);
        }

        if (!clap_only and build_context.build_mode != .production) {
            var docs_preprocessor = b.addExecutable(.{
                .name = "docs_preprocessor",
                .root_module = b.createModule(module_options),
            });
            docs_preprocessor.addCSourceFiles(.{
                .files = &.{
                    "src/docs_preprocessor/docs_preprocessor.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                }).flags.items,
            });
            docs_preprocessor.root_module.addCMacro("FINAL_BINARY_TYPE", "DocsPreprocessor");
            docs_preprocessor.linkLibrary(common_infrastructure);
            docs_preprocessor.addIncludePath(b.path("src"));
            docs_preprocessor.addConfigHeader(build_config_step);
            join_compile_commands.step.dependOn(&docs_preprocessor.step);
            applyUniversalSettings(&build_context, docs_preprocessor);
            b.getInstallStep().dependOn(&b.addInstallArtifact(docs_preprocessor, .{ .dest_dir = install_subfolder }).step);
        }

        if (!clap_only) {
            var packager = b.addExecutable(.{
                .name = "floe-packager",
                .root_module = b.createModule(module_options),
                .version = floe_version,
            });
            packager.addCSourceFiles(.{
                .files = &.{
                    "src/packager_tool/packager.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                }).flags.items,
            });
            packager.root_module.addCMacro("FINAL_BINARY_TYPE", "Packager");
            packager.linkLibrary(common_infrastructure);
            packager.addIncludePath(b.path("src"));
            packager.addConfigHeader(build_config_step);
            packager.linkLibrary(miniz);
            if (embed_files_workaround) {
                packager.addCSourceFile(.{
                    .file = b.dependency("embedded_files_workaround", .{}).path("embedded_files.cpp"),
                });
            } else {
                packager.addObject(embedded_files.?);
            }
            join_compile_commands.step.dependOn(&packager.step);
            addToLipoSteps(&build_context, packager, false) catch @panic("OOM");
            applyUniversalSettings(&build_context, packager);
            const packager_install_artifact_step = b.addInstallArtifact(
                packager,
                .{ .dest_dir = install_subfolder },
            );
            b.getInstallStep().dependOn(&packager_install_artifact_step.step);

            const sign_step = addWindowsCodeSignStep(
                &build_context,
                target,
                b.getInstallPath(install_dir, packager.out_filename),
                "Floe library packager",
            );
            if (sign_step) |s| {
                s.dependOn(&packager.step);
                s.dependOn(b.getInstallStep());
                build_context.master_step.dependOn(s);
            }
        }

        var clap_final_step: ?*std.Build.Step = null;
        {
            const clap = b.addSharedLibrary(.{
                .name = "Floe.clap",
                .root_module = b.createModule(module_options),
                .version = floe_version,
            });
            clap.addCSourceFiles(.{
                .files = &.{
                    "src/plugin/plugin/plugin_entry.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                }).flags.items,
            });
            clap.root_module.addCMacro("FINAL_BINARY_TYPE", "Clap");
            clap.addConfigHeader(build_config_step);
            clap.addIncludePath(b.path("src"));
            clap.linkLibrary(plugin);
            const clap_install_artifact_step = b.addInstallArtifact(clap, .{ .dest_dir = install_subfolder });
            b.getInstallStep().dependOn(&clap_install_artifact_step.step);
            applyUniversalSettings(&build_context, clap);
            addWin32EmbedInfo(clap, .{
                .name = "Floe CLAP",
                .description = floe_description,
                .icon_path = null,
            }) catch @panic("OOM");
            join_compile_commands.step.dependOn(&clap.step);
            addToLipoSteps(&build_context, clap, true) catch @panic("OOM");

            var clap_post_install_step = b.allocator.create(PostInstallStep) catch @panic("OOM");
            clap_post_install_step.* = PostInstallStep{
                .step = std.Build.Step.init(.{
                    .id = std.Build.Step.Id.custom,
                    .name = "Post install config",
                    .owner = b,
                    .makeFn = performPostInstallConfig,
                }),
                .make_macos_bundle = true,
                .context = &build_context,
                .compile_step = clap,
            };
            clap_post_install_step.step.dependOn(&clap.step);
            clap_post_install_step.step.dependOn(b.getInstallStep());

            const sign_step = addWindowsCodeSignStep(
                &build_context,
                target,
                b.getInstallPath(install_dir, clap.name),
                "Floe CLAP Plugin",
            );
            if (sign_step) |s| {
                s.dependOn(&clap_post_install_step.step);
                build_context.master_step.dependOn(s);
                clap_final_step = s;
            } else {
                build_context.master_step.dependOn(&clap_post_install_step.step);
                clap_final_step = &clap_post_install_step.step;
            }
        }

        // standalone is for development-only at the moment
        if (!clap_only and build_context.build_mode != .production) {
            const miniaudio = b.addStaticLibrary(.{
                .name = "miniaudio",
                .root_module = b.createModule(module_options),
            });
            {
                miniaudio.addCSourceFile(.{
                    .file = b.path("third_party_libs/miniaudio.c"),
                    .flags = FlagsBuilder.init(&build_context, target, .{}).flags.items,
                });
                // Disabling pulse audio because it was causing lots of stutters on my machine.
                miniaudio.root_module.addCMacro("MA_NO_PULSEAUDIO", "1");
                miniaudio.linkLibC();
                miniaudio.addIncludePath(build_context.dep_miniaudio.path(""));
                switch (target.result.os.tag) {
                    .macos => {
                        applyUniversalSettings(&build_context, miniaudio);
                        miniaudio.linkFramework("CoreAudio");
                    },
                    .windows => {
                        miniaudio.linkSystemLibrary("dsound");
                    },
                    .linux => {
                        miniaudio.linkSystemLibrary2("alsa", .{ .use_pkg_config = use_pkg_config });
                    },
                    else => {
                        unreachable;
                    },
                }
            }

            const portmidi = b.addStaticLibrary(.{
                .name = "portmidi",
                .root_module = b.createModule(module_options),
            });
            {
                const pm_root = build_context.dep_portmidi.path("");
                const pm_flags = FlagsBuilder.init(&build_context, target, .{}).flags.items;
                portmidi.addCSourceFiles(.{
                    .root = pm_root,
                    .files = &.{
                        "pm_common/portmidi.c",
                        "pm_common/pmutil.c",
                        "porttime/porttime.c",
                    },
                    .flags = pm_flags,
                });
                switch (target.result.os.tag) {
                    .macos => {
                        portmidi.addCSourceFiles(.{
                            .root = pm_root,
                            .files = &.{
                                "pm_mac/pmmacosxcm.c",
                                "pm_mac/pmmac.c",
                                "porttime/ptmacosx_cf.c",
                                "porttime/ptmacosx_mach.c",
                            },
                            .flags = pm_flags,
                        });
                        portmidi.linkFramework("CoreAudio");
                        portmidi.linkFramework("CoreMIDI");
                        applyUniversalSettings(&build_context, portmidi);
                    },
                    .windows => {
                        portmidi.addCSourceFiles(.{
                            .root = pm_root,
                            .files = &.{
                                "pm_win/pmwin.c",
                                "pm_win/pmwinmm.c",
                                "porttime/ptwinmm.c",
                            },
                            .flags = pm_flags,
                        });
                        portmidi.linkSystemLibrary("winmm");
                    },
                    .linux => {
                        portmidi.addCSourceFiles(.{
                            .root = pm_root,
                            .files = &.{
                                "pm_linux/pmlinux.c",
                                "pm_linux/pmlinuxalsa.c",
                                "porttime/ptlinux.c",
                            },
                            .flags = pm_flags,
                        });
                        portmidi.root_module.addCMacro("PMALSA", "1");
                        portmidi.linkSystemLibrary2("alsa", .{ .use_pkg_config = use_pkg_config });
                    },
                    else => {
                        unreachable;
                    },
                }

                portmidi.linkLibC();
                portmidi.addIncludePath(build_context.dep_portmidi.path("porttime"));
                portmidi.addIncludePath(build_context.dep_portmidi.path("pm_common"));
            }

            const floe_standalone = b.addExecutable(.{
                .name = "floe_standalone",
                .root_module = b.createModule(module_options),
            });

            floe_standalone.addCSourceFiles(.{
                .files = &.{
                    "src/standalone_wrapper/standalone_wrapper.cpp",
                    "src/plugin/plugin/plugin_entry.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                }).flags.items,
            });

            floe_standalone.root_module.addCMacro("FINAL_BINARY_TYPE", "Standalone");
            floe_standalone.addConfigHeader(build_config_step);
            floe_standalone.addIncludePath(b.path("src"));
            floe_standalone.linkLibrary(portmidi);
            floe_standalone.linkLibrary(miniaudio);
            floe_standalone.addIncludePath(build_context.dep_miniaudio.path(""));
            floe_standalone.linkLibrary(plugin);
            b.getInstallStep().dependOn(&b.addInstallArtifact(
                floe_standalone,
                .{ .dest_dir = install_subfolder },
            ).step);
            join_compile_commands.step.dependOn(&floe_standalone.step);
            applyUniversalSettings(&build_context, floe_standalone);

            var post_install_step = b.allocator.create(PostInstallStep) catch @panic("OOM");
            post_install_step.* = PostInstallStep{
                .step = std.Build.Step.init(.{
                    .id = std.Build.Step.Id.custom,
                    .name = "Post install config",
                    .owner = b,
                    .makeFn = performPostInstallConfig,
                }),
                .make_macos_bundle = false,
                .context = &build_context,
                .compile_step = floe_standalone,
            };
            post_install_step.step.dependOn(&floe_standalone.step);
            post_install_step.step.dependOn(b.getInstallStep());
            build_context.master_step.dependOn(&post_install_step.step);
        }

        const vst3_sdk = b.addStaticLibrary(.{
            .name = "VST3",
            .root_module = b.createModule(module_options),
        });
        const vst3_validator = b.addExecutable(.{
            .name = "VST3-Validator",
            .root_module = b.createModule(module_options),
        });
        if (!clap_only) {
            var flags = FlagsBuilder.init(&build_context, target, .{
                .ubsan = false,
            });
            if (build_context.optimise == .Debug) {
                flags.addFlag("-DDEVELOPMENT=1");
            } else {
                flags.addFlag("-DRELEASE=1");
            }
            // Ignore warning about non-reproducible __DATE__ usage.
            flags.addFlag("-Wno-date-time");

            {
                vst3_sdk.addCSourceFiles(.{
                    .root = build_context.dep_vst3_sdk.path(""),
                    .files = &.{
                        "base/source/baseiids.cpp",
                        "base/source/fbuffer.cpp",
                        "base/source/fdebug.cpp",
                        "base/source/fdynlib.cpp",
                        "base/source/fobject.cpp",
                        "base/source/fstreamer.cpp",
                        "base/source/fstring.cpp",
                        "base/source/timer.cpp",
                        "base/source/updatehandler.cpp",

                        "base/thread/source/fcondition.cpp",
                        "base/thread/source/flock.cpp",

                        "public.sdk/source/common/commoniids.cpp",
                        "public.sdk/source/common/memorystream.cpp",
                        "public.sdk/source/common/openurl.cpp",
                        "public.sdk/source/common/pluginview.cpp",
                        "public.sdk/source/common/readfile.cpp",
                        "public.sdk/source/common/systemclipboard_linux.cpp",
                        "public.sdk/source/common/systemclipboard_mac.mm",
                        "public.sdk/source/common/systemclipboard_win32.cpp",
                        "public.sdk/source/common/threadchecker_linux.cpp",
                        "public.sdk/source/common/threadchecker_mac.mm",
                        "public.sdk/source/common/threadchecker_win32.cpp",

                        "pluginterfaces/base/conststringtable.cpp",
                        "pluginterfaces/base/coreiids.cpp",
                        "pluginterfaces/base/funknown.cpp",
                        "pluginterfaces/base/ustring.cpp",

                        "public.sdk/source/main/pluginfactory.cpp",
                        "public.sdk/source/main/moduleinit.cpp",
                        "public.sdk/source/vst/vstinitiids.cpp",
                        "public.sdk/source/vst/vstnoteexpressiontypes.cpp",
                        "public.sdk/source/vst/vstsinglecomponenteffect.cpp",
                        "public.sdk/source/vst/vstaudioeffect.cpp",
                        "public.sdk/source/vst/vstcomponent.cpp",
                        "public.sdk/source/vst/vstsinglecomponenteffect.cpp",
                        "public.sdk/source/vst/vstcomponentbase.cpp",
                        "public.sdk/source/vst/vstbus.cpp",
                        "public.sdk/source/vst/vstparameters.cpp",
                        "public.sdk/source/vst/utility/stringconvert.cpp",
                    },
                    .flags = flags.flags.items,
                });

                switch (target.result.os.tag) {
                    .windows => {},
                    .linux => {},
                    .macos => {
                        vst3_sdk.linkFramework("CoreFoundation");
                        vst3_sdk.linkFramework("Foundation");
                    },
                    else => {},
                }

                vst3_sdk.addIncludePath(build_context.dep_vst3_sdk.path(""));
                vst3_sdk.linkLibCpp();
                applyUniversalSettings(&build_context, vst3_sdk);
            }

            {
                vst3_validator.addCSourceFiles(.{
                    .root = build_context.dep_vst3_sdk.path(""),
                    .files = &.{
                        "public.sdk/source/common/memorystream.cpp",
                        "public.sdk/source/main/moduleinit.cpp",
                        "public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp",
                        "public.sdk/source/vst/hosting/test/connectionproxytest.cpp",
                        "public.sdk/source/vst/hosting/test/eventlisttest.cpp",
                        "public.sdk/source/vst/hosting/test/hostclassestest.cpp",
                        "public.sdk/source/vst/hosting/test/parameterchangestest.cpp",
                        "public.sdk/source/vst/hosting/test/pluginterfacesupporttest.cpp",
                        "public.sdk/source/vst/hosting/test/processdatatest.cpp",
                        "public.sdk/source/vst/hosting/plugprovider.cpp",
                        "public.sdk/source/vst/testsuite/bus/busactivation.cpp",
                        "public.sdk/source/vst/testsuite/bus/busconsistency.cpp",
                        "public.sdk/source/vst/testsuite/bus/businvalidindex.cpp",
                        "public.sdk/source/vst/testsuite/bus/checkaudiobusarrangement.cpp",
                        "public.sdk/source/vst/testsuite/bus/scanbusses.cpp",
                        "public.sdk/source/vst/testsuite/bus/sidechainarrangement.cpp",
                        "public.sdk/source/vst/testsuite/general/editorclasses.cpp",
                        "public.sdk/source/vst/testsuite/general/midilearn.cpp",
                        "public.sdk/source/vst/testsuite/general/midimapping.cpp",
                        "public.sdk/source/vst/testsuite/general/plugcompat.cpp",
                        "public.sdk/source/vst/testsuite/general/scanparameters.cpp",
                        "public.sdk/source/vst/testsuite/general/suspendresume.cpp",
                        "public.sdk/source/vst/testsuite/general/terminit.cpp",
                        "public.sdk/source/vst/testsuite/noteexpression/keyswitch.cpp",
                        "public.sdk/source/vst/testsuite/noteexpression/noteexpression.cpp",
                        "public.sdk/source/vst/testsuite/processing/automation.cpp",
                        "public.sdk/source/vst/testsuite/processing/process.cpp",
                        "public.sdk/source/vst/testsuite/processing/processcontextrequirements.cpp",
                        "public.sdk/source/vst/testsuite/processing/processformat.cpp",
                        "public.sdk/source/vst/testsuite/processing/processinputoverwriting.cpp",
                        "public.sdk/source/vst/testsuite/processing/processtail.cpp",
                        "public.sdk/source/vst/testsuite/processing/processthreaded.cpp",
                        "public.sdk/source/vst/testsuite/processing/silenceflags.cpp",
                        "public.sdk/source/vst/testsuite/processing/silenceprocessing.cpp",
                        "public.sdk/source/vst/testsuite/processing/speakerarrangement.cpp",
                        "public.sdk/source/vst/testsuite/processing/variableblocksize.cpp",
                        "public.sdk/source/vst/testsuite/state/bypasspersistence.cpp",
                        "public.sdk/source/vst/testsuite/state/invalidstatetransition.cpp",
                        "public.sdk/source/vst/testsuite/state/repeatidenticalstatetransition.cpp",
                        "public.sdk/source/vst/testsuite/state/validstatetransition.cpp",
                        "public.sdk/source/vst/testsuite/testbase.cpp",
                        "public.sdk/source/vst/testsuite/unit/checkunitstructure.cpp",
                        "public.sdk/source/vst/testsuite/unit/scanprograms.cpp",
                        "public.sdk/source/vst/testsuite/unit/scanunits.cpp",
                        "public.sdk/source/vst/testsuite/vsttestsuite.cpp",
                        "public.sdk/source/vst/utility/testing.cpp",
                        "public.sdk/samples/vst-hosting/validator/source/main.cpp",
                        "public.sdk/samples/vst-hosting/validator/source/usediids.cpp",
                        "public.sdk/samples/vst-hosting/validator/source/validator.cpp",

                        "public.sdk/source/vst/hosting/connectionproxy.cpp",
                        "public.sdk/source/vst/hosting/eventlist.cpp",
                        "public.sdk/source/vst/hosting/hostclasses.cpp",
                        "public.sdk/source/vst/hosting/module.cpp",
                        "public.sdk/source/vst/hosting/parameterchanges.cpp",
                        "public.sdk/source/vst/hosting/pluginterfacesupport.cpp",
                        "public.sdk/source/vst/hosting/processdata.cpp",
                        "public.sdk/source/vst/vstpresetfile.cpp",
                    },
                    .flags = flags.flags.items,
                });

                switch (target.result.os.tag) {
                    .windows => {
                        vst3_validator.addCSourceFiles(.{
                            .root = build_context.dep_vst3_sdk.path(""),
                            .files = &.{"public.sdk/source/vst/hosting/module_win32.cpp"},
                            .flags = flags.flags.items,
                        });
                        vst3_validator.linkSystemLibrary("ole32");
                    },
                    .linux => {
                        vst3_validator.addCSourceFiles(.{
                            .root = build_context.dep_vst3_sdk.path(""),
                            .files = &.{"public.sdk/source/vst/hosting/module_linux.cpp"},
                            .flags = flags.flags.items,
                        });
                    },
                    .macos => {
                        vst3_validator.addCSourceFiles(.{
                            .root = build_context.dep_vst3_sdk.path(""),
                            .files = &.{"public.sdk/source/vst/hosting/module_mac.mm"},
                            .flags = FlagsBuilder.init(&build_context, target, .{
                                .objcpp = true,
                            }).flags.items,
                        });
                    },
                    else => {},
                }

                vst3_validator.addIncludePath(build_context.dep_vst3_sdk.path(""));
                vst3_validator.linkLibCpp();
                vst3_validator.linkLibrary(vst3_sdk);
                vst3_validator.linkLibrary(library); // for ubsan runtime
                applyUniversalSettings(&build_context, vst3_validator);
                b.getInstallStep().dependOn(&b.addInstallArtifact(
                    vst3_validator,
                    .{ .dest_dir = install_subfolder },
                ).step);
                addToLipoSteps(&build_context, vst3_validator, false) catch @panic("OOM");

                // const run_tests = b.addRunArtifact(vst3_validator);
                // run_tests.addArg(b.pathJoin(&.{ install_dir, install_subfolder_string, "Floe.vst3" }));
                // build_context.test_step.dependOn(&run_tests.step);
            }
        }

        var vst3_final_step: ?*std.Build.Step = null;
        if (!clap_only) {
            const vst3 = b.addSharedLibrary(.{
                .name = "Floe.vst3",
                .version = floe_version,
                .root_module = b.createModule(module_options),
            });
            switch (target.result.os.tag) {
                .windows => {
                    vst3.root_module.addCMacro("WIN", "1");
                },
                .linux => {
                    vst3.root_module.addCMacro("LIN", "1");
                },
                .macos => {
                    vst3.root_module.addCMacro("MAC", "1");
                },
                else => {},
            }
            if (build_context.optimise == .Debug) {
                vst3.root_module.addCMacro("DEVELOPMENT", "1");
            } else {
                vst3.root_module.addCMacro("RELEASE", "1");
            }
            vst3.root_module.addCMacro("MACOS_USE_STD_FILESYSTEM", "1");
            vst3.root_module.addCMacro("CLAP_WRAPPER_VERSION", "\"0.11.0\"");
            vst3.root_module.addCMacro("STATICALLY_LINKED_CLAP_ENTRY", "1");

            var flags = FlagsBuilder.init(&build_context, target, .{
                .ubsan = false,
            });
            flags.addFlag("-fno-char8_t");

            vst3.addCSourceFiles(.{
                .files = &.{
                    "src/plugin/plugin/plugin_entry.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                }).flags.items,
            });
            vst3.root_module.addCMacro("FINAL_BINARY_TYPE", "Vst3");

            const wrapper_src_path = build_context.dep_clap_wrapper.path("src");
            vst3.addCSourceFiles(.{
                .root = wrapper_src_path,
                .files = &.{
                    "wrapasvst3.cpp",
                    "wrapasvst3_entry.cpp",
                    "wrapasvst3_export_entry.cpp",
                    "detail/vst3/parameter.cpp",
                    "detail/vst3/plugview.cpp",
                    "detail/vst3/process.cpp",
                    "detail/vst3/categories.cpp",
                    "clap_proxy.cpp",
                    "detail/shared/sha1.cpp",
                    "detail/clap/fsutil.cpp",
                },
                .flags = flags.flags.items,
            });

            switch (target.result.os.tag) {
                .windows => {
                    vst3.addCSourceFile(.{
                        .file = build_context.dep_clap_wrapper.path("src/detail/os/windows.cpp"),
                        .flags = flags.flags.items,
                    });
                    vst3.addCSourceFiles(.{
                        .root = build_context.dep_vst3_sdk.path(""),
                        .files = &.{"public.sdk/source/main/dllmain.cpp"},
                        .flags = flags.flags.items,
                    });
                },
                .linux => {
                    vst3.addCSourceFile(.{
                        .file = build_context.dep_clap_wrapper.path("src/detail/os/linux.cpp"),
                        .flags = flags.flags.items,
                    });
                    vst3.addCSourceFiles(.{
                        .root = build_context.dep_vst3_sdk.path(""),
                        .files = &.{"public.sdk/source/main/linuxmain.cpp"},
                        .flags = flags.flags.items,
                    });
                },
                .macos => {
                    vst3.addCSourceFiles(.{
                        .root = wrapper_src_path,
                        .files = &.{
                            "detail/os/macos.mm",
                            "detail/clap/mac_helpers.mm",
                        },
                        .flags = flags.flags.items,
                    });
                    vst3.addCSourceFiles(.{
                        .root = build_context.dep_vst3_sdk.path(""),
                        .files = &.{"public.sdk/source/main/macmain.cpp"},
                        .flags = flags.flags.items,
                    });
                },
                else => {},
            }

            vst3.addIncludePath(build_context.dep_clap_wrapper.path("include"));
            vst3.addIncludePath(build_context.dep_clap_wrapper.path("libs/fmt"));
            vst3.addIncludePath(build_context.dep_clap_wrapper.path("src"));
            vst3.addIncludePath(build_context.dep_vst3_sdk.path(""));
            vst3.linkLibCpp();

            vst3.linkLibrary(plugin);
            vst3.linkLibrary(vst3_sdk);

            vst3.addConfigHeader(build_config_step);
            vst3.addIncludePath(b.path("src"));

            const vst3_install_artifact_step = b.addInstallArtifact(vst3, .{ .dest_dir = install_subfolder });
            b.getInstallStep().dependOn(&vst3_install_artifact_step.step);
            applyUniversalSettings(&build_context, vst3);
            addWin32EmbedInfo(vst3, .{
                .name = "Floe VST3",
                .description = floe_description,
                .icon_path = null,
            }) catch @panic("OOM");
            addToLipoSteps(&build_context, vst3, true) catch @panic("OOM");

            var vst3_post_install_step = b.allocator.create(PostInstallStep) catch @panic("OOM");
            vst3_post_install_step.* = PostInstallStep{
                .step = std.Build.Step.init(.{
                    .id = std.Build.Step.Id.custom,
                    .name = "Post install config",
                    .owner = b,
                    .makeFn = performPostInstallConfig,
                }),
                .make_macos_bundle = true,
                .context = &build_context,
                .compile_step = vst3,
            };
            vst3_post_install_step.step.dependOn(b.getInstallStep());

            const sign_step = addWindowsCodeSignStep(
                &build_context,
                target,
                b.getInstallPath(install_dir, vst3.name),
                "Floe VST3 Plugin",
            );
            if (sign_step) |s| {
                s.dependOn(&vst3_post_install_step.step);
                build_context.master_step.dependOn(s);
                vst3_final_step = s;
            } else {
                build_context.master_step.dependOn(&vst3_post_install_step.step);
                vst3_final_step = &vst3_post_install_step.step;
            }
        }

        if (!clap_only and target.result.os.tag == .macos) {
            const au_sdk = b.addStaticLibrary(.{
                .name = "AU",
                .root_module = b.createModule(module_options),
            });
            {
                au_sdk.addCSourceFiles(.{
                    .root = build_context.dep_au_sdk.path("src/AudioUnitSDK"),
                    .files = &.{
                        "AUBuffer.cpp",
                        "AUBufferAllocator.cpp",
                        "AUEffectBase.cpp",
                        "AUInputElement.cpp",
                        "AUMIDIBase.cpp",
                        "AUBase.cpp",
                        "AUMIDIEffectBase.cpp",
                        "AUOutputElement.cpp",
                        "AUPlugInDispatch.cpp",
                        "AUScopeElement.cpp",
                        "ComponentBase.cpp",
                        "MusicDeviceBase.cpp",
                    },
                    .flags = FlagsBuilder.init(&build_context, target, .{
                        .cpp = true,
                    }).flags.items,
                });
                au_sdk.addIncludePath(build_context.dep_au_sdk.path("include"));
                au_sdk.linkLibCpp();
                applyUniversalSettings(&build_context, au_sdk);
            }

            {
                const wrapper_src_path = build_context.dep_clap_wrapper.path("src");

                var flags = FlagsBuilder.init(&build_context, target, .{});
                switch (target.result.os.tag) {
                    .windows => {
                        flags.addFlag("-DWIN=1");
                    },
                    .linux => {
                        flags.addFlag("-DLIN=1");
                    },
                    .macos => {
                        flags.addFlag("-DMAC=1");
                    },
                    else => {},
                }
                if (build_context.optimise == .Debug) {
                    flags.addFlag("-DDEVELOPMENT=1");
                } else {
                    flags.addFlag("-DRELEASE=1");
                }
                flags.addFlag("-fno-char8_t");
                flags.addFlag("-DMACOS_USE_STD_FILESYSTEM=1");
                flags.addFlag("-DCLAP_WRAPPER_VERSION=\"0.11.0\"");
                flags.addFlag("-DSTATICALLY_LINKED_CLAP_ENTRY=1");

                const au = b.addSharedLibrary(.{
                    .name = "Floe.component",
                    .root_module = b.createModule(module_options),
                    .version = floe_version,
                });
                au.addCSourceFiles(.{
                    .files = &.{
                        "src/plugin/plugin/plugin_entry.cpp",
                        "src/common_infrastructure/final_binary_type.cpp",
                    },
                    .flags = FlagsBuilder.init(&build_context, target, .{
                        .full_diagnostics = true,
                        .ubsan = true,
                        .objcpp = true,
                    }).flags.items,
                });
                au.root_module.addCMacro("FINAL_BINARY_TYPE", "AuV2");

                au.addCSourceFiles(.{
                    .root = wrapper_src_path,
                    .files = &.{
                        "clap_proxy.cpp",
                        "detail/shared/sha1.cpp",
                        "detail/clap/fsutil.cpp",
                        "detail/os/macos.mm",
                        "detail/clap/mac_helpers.mm",
                        "wrapasauv2.cpp",
                        "detail/auv2/process.cpp",
                        "detail/auv2/wrappedview.mm",
                        "detail/auv2/parameter.cpp",
                        "detail/auv2/auv2_shared.mm",
                    },
                    .flags = flags.flags.items,
                });

                {
                    const file = b.build_root.handle.createFile(
                        b.pathJoin(&.{ floe_cache_relative, "generated_entrypoints.hxx" }),
                        .{ .truncate = true },
                    ) catch @panic("could not create file");
                    defer file.close();
                    file.writeAll(b.fmt(
                        \\ #pragma once
                        \\ #include "detail/auv2/auv2_base_classes.h"
                        \\
                        \\ struct {[factory_function]s} : free_audio::auv2_wrapper::WrapAsAUV2 {{
                        \\     {[factory_function]s}(AudioComponentInstance ci) : 
                        \\         free_audio::auv2_wrapper::WrapAsAUV2(AUV2_Type::aumu_musicdevice, 
                        \\                                              "{[clap_name]s}",
                        \\                                              "{[clap_id]s}",
                        \\                                              0,
                        \\                                              ci) {{
                        \\     }}
                        \\ }};
                        \\ AUSDK_COMPONENT_ENTRY(ausdk::AUMusicDeviceFactory, {[factory_function]s});
                    , .{
                        .factory_function = floe_au_factory_function,
                        .clap_name = "Floe",
                        .clap_id = floe_clap_id,
                    })) catch @panic("could not write to file");
                }

                {
                    const file = b.build_root.handle.createFile(
                        b.pathJoin(&.{ floe_cache_relative, "generated_cocoaclasses.hxx" }),
                        .{ .truncate = true },
                    ) catch @panic("could not create file");
                    defer file.close();
                    file.writeAll(b.fmt(
                        \\ #pragma once
                        \\
                        \\ #define CLAP_WRAPPER_COCOA_CLASS_NSVIEW {[name]s}_nsview
                        \\ #define CLAP_WRAPPER_COCOA_CLASS {[name]s}
                        \\ #define CLAP_WRAPPER_TIMER_CALLBACK timerCallback_{[name]s}
                        \\ #define CLAP_WRAPPER_FILL_AUCV fillAUCV_{[name]s}
                        \\ #include "detail/auv2/wrappedview.asinclude.mm"
                        \\ #undef CLAP_WRAPPER_COCOA_CLASS_NSVIEW
                        \\ #undef CLAP_WRAPPER_COCOA_CLASS
                        \\ #undef CLAP_WRAPPER_TIMER_CALLBACK
                        \\ #undef CLAP_WRAPPER_FILL_AUCV
                        \\
                        \\ bool fillAudioUnitCocoaView(AudioUnitCocoaViewInfo* viewInfo, std::shared_ptr<Clap::Plugin> _plugin) {{
                        \\     if (strcmp(_plugin->_plugin->desc->id, "{[clap_id]s}") == 0) {{
                        \\         if (!_plugin->_ext._gui) return false;
                        \\         return fillAUCV_{[name]s}(viewInfo);
                        \\     }}
                        \\ }}
                    , .{
                        .name = b.fmt("Floe{d}", .{floe_version_hash}),
                        .clap_id = floe_clap_id,
                    })) catch @panic("could not write to file");
                }

                au.addIncludePath(b.path("third_party_libs/clap/include"));
                au.addIncludePath(build_context.dep_au_sdk.path("include"));
                au.addIncludePath(build_context.dep_clap_wrapper.path("include"));
                au.addIncludePath(build_context.dep_clap_wrapper.path("libs/fmt"));
                au.addIncludePath(build_context.dep_clap_wrapper.path("src"));
                au.addIncludePath(b.path(floe_cache_relative));
                au.linkLibCpp();

                au.linkLibrary(plugin);
                au.linkLibrary(au_sdk);
                au.linkFramework("AudioToolbox");
                au.linkFramework("CoreMIDI");

                au.addConfigHeader(build_config_step);
                au.addIncludePath(b.path("src"));

                const au_install_artifact_step = b.addInstallArtifact(au, .{ .dest_dir = install_subfolder });
                b.getInstallStep().dependOn(&au_install_artifact_step.step);
                applyUniversalSettings(&build_context, au);
                addToLipoSteps(&build_context, au, true) catch @panic("OOM");

                var au_post_install_step = b.allocator.create(PostInstallStep) catch @panic("OOM");
                au_post_install_step.* = PostInstallStep{
                    .step = std.Build.Step.init(.{
                        .id = std.Build.Step.Id.custom,
                        .name = "Post install config",
                        .owner = b,
                        .makeFn = performPostInstallConfig,
                    }),
                    .make_macos_bundle = true,
                    .context = &build_context,
                    .compile_step = au,
                };
                au_post_install_step.step.dependOn(b.getInstallStep());
                build_context.master_step.dependOn(&au_post_install_step.step);
            }
        }

        if (!clap_only and target.result.os.tag == .windows) {
            const installer_path = "src/windows_installer";

            // the logos probably have a different license to the rest of the codebase, so we keep them separate and optional
            const logo_image = getExternalResource(&build_context, "Logos/rasterized/icon.ico");
            const sidebar_image = getExternalResource(&build_context, "Logos/rasterized/win-installer-sidebar.png");

            {
                const writeManifest = (struct {
                    fn writeManifest(
                        builder: *std.Build,
                        name: []const u8,
                        require_admin: bool,
                        description: []const u8,
                    ) []const u8 {
                        const manifest_path = builder.pathJoin(&.{
                            floe_cache_relative,
                            builder.fmt(
                                "{s}.manifest",
                                .{name},
                            ),
                        });
                        const file = builder.build_root.handle.createFile(
                            manifest_path,
                            .{ .truncate = true },
                        ) catch @panic("could not create file");
                        defer file.close();
                        file.writeAll(builder.fmt(
                            \\ <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
                            \\ <assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
                            \\ <assemblyIdentity
                            \\     version="1.0.0.0"
                            \\     processorArchitecture="amd64"
                            \\     name="{[id]s}.{[name]s}"
                            \\     type="win32"
                            \\ />
                            \\ <description>{[description]s}</description>
                            \\ <dependency>
                            \\     <dependentAssembly>
                            \\         <assemblyIdentity
                            \\             type="win32"
                            \\             name="Microsoft.Windows.Common-Controls"
                            \\             version="6.0.0.0"
                            \\             processorArchitecture="amd64"
                            \\             publicKeyToken="6595b64144ccf1df"
                            \\             language="*"
                            \\         />
                            \\     </dependentAssembly>
                            \\ </dependency>
                            \\ <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
                            \\     <application>
                            \\         <!-- Windows 10, 11 -->
                            \\         <supportedOS Id="{{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}}"/>
                            \\         <!-- Windows 8.1 -->
                            \\         <supportedOS Id="{{1f676c76-80e1-4239-95bb-83d0f6d0da78}}"/>
                            \\         <!-- Windows 8 -->
                            \\         <supportedOS Id="{{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}}"/>
                            \\     </application>
                            \\ </compatibility>
                            \\ <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
                            \\     <security>
                            \\         <requestedPrivileges>
                            \\         <requestedExecutionLevel level="{[execution_level]s}" uiAccess="false" />
                            \\         </requestedPrivileges>
                            \\     </security>
                            \\ </trustInfo>
                            \\ <asmv3:application>
                            \\     <asmv3:windowsSettings xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">
                            \\         <dpiAwareness>PerMonitorV2</dpiAwareness>
                            \\         <longPathAware>true</longPathAware>
                            \\     </asmv3:windowsSettings>
                            \\     <asmv3:windowsSettings xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">
                            \\         <activeCodePage>UTF-8</activeCodePage>
                            \\     </asmv3:windowsSettings>
                            \\ </asmv3:application>
                            \\ </assembly>
                        , .{
                            .id = floe_clap_id,
                            .name = name,
                            .description = description,
                            .execution_level = if (require_admin) "requireAdministrator" else "asInvoker",
                        })) catch @panic("could not write to file");
                        return manifest_path;
                    }
                }).writeManifest;

                const flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                });

                const uninstaller_name = "Floe-Uninstaller";
                const win_uninstaller = b.addExecutable(.{
                    .name = uninstaller_name,
                    .root_module = b.createModule(module_options),
                    .version = floe_version,
                    .win32_manifest = b.path(writeManifest(
                        b,
                        "Uninstaller",
                        windows_installer_require_admin,
                        "Uninstaller for Floe plugins",
                    )),
                });
                win_uninstaller.subsystem = .Windows;

                win_uninstaller.root_module.addCMacro(
                    "UNINSTALLER_PATH_RELATIVE_BUILD_ROOT",
                    b.fmt("\"zig-out/x86_64-windows/{s}\"", .{win_uninstaller.out_filename}),
                );

                win_uninstaller.addCSourceFiles(.{
                    .files = &.{
                        installer_path ++ "/uninstaller.cpp",
                        installer_path ++ "/gui.cpp",
                        "src/common_infrastructure/final_binary_type.cpp",
                    },
                    .flags = flags.flags.items,
                });
                win_uninstaller.root_module.addCMacro("FINAL_BINARY_TYPE", "WindowsUninstaller");
                win_uninstaller.linkSystemLibrary("gdi32");
                win_uninstaller.linkSystemLibrary("version");
                win_uninstaller.linkSystemLibrary("comctl32");
                win_uninstaller.addConfigHeader(build_config_step);
                win_uninstaller.addIncludePath(b.path("src"));
                win_uninstaller.addObject(stb_image);
                win_uninstaller.linkLibrary(library);
                win_uninstaller.linkLibrary(miniz);
                win_uninstaller.linkLibrary(common_infrastructure);
                applyUniversalSettings(&build_context, win_uninstaller);
                join_compile_commands.step.dependOn(&win_uninstaller.step);

                const uninstall_artifact_step = b.addInstallArtifact(
                    win_uninstaller,
                    .{ .dest_dir = install_subfolder },
                );
                build_context.master_step.dependOn(&uninstall_artifact_step.step);

                const uninstall_sign_step = addWindowsCodeSignStep(
                    &build_context,
                    target,
                    b.getInstallPath(install_dir, win_uninstaller.out_filename),
                    "Floe Uninstaller",
                );
                if (uninstall_sign_step) |s| {
                    s.dependOn(&uninstall_artifact_step.step);
                }

                const win_installer_description = "Installer for Floe plugins";
                const win_installer = b.addExecutable(.{
                    .name = b.fmt("Floe-Installer-v{s}", .{ .version = floe_version_string.? }),
                    .root_module = b.createModule(module_options),
                    .version = floe_version,
                    .win32_manifest = b.path(writeManifest(
                        b,
                        "Installer",
                        windows_installer_require_admin,
                        win_installer_description,
                    )),
                });
                win_installer.subsystem = .Windows;

                if (sidebar_image != null) {
                    win_installer.root_module.addCMacro(
                        "SIDEBAR_IMAGE_PATH",
                        b.fmt("\"{s}\"", .{sidebar_image.?.relative_path}),
                    );
                }
                win_installer.root_module.addCMacro(
                    "VST3_PLUGIN_PATH_RELATIVE_BUILD_ROOT",
                    "\"zig-out/x86_64-windows/Floe.vst3\"",
                );
                win_installer.root_module.addCMacro(
                    "CLAP_PLUGIN_PATH_RELATIVE_BUILD_ROOT",
                    "\"zig-out/x86_64-windows/Floe.clap\"",
                );
                win_installer.root_module.addCMacro(
                    "UNINSTALLER_PATH_RELATIVE_BUILD_ROOT",
                    b.fmt("\"zig-out/x86_64-windows/{s}\"", .{win_uninstaller.out_filename}),
                );
                win_installer.addWin32ResourceFile(.{
                    .file = b.path(installer_path ++ "/resources.rc"),
                    .flags = win_installer.root_module.c_macros.items,
                });

                win_installer.addCSourceFiles(.{
                    .files = &.{
                        installer_path ++ "/installer.cpp",
                        installer_path ++ "/gui.cpp",
                        "src/common_infrastructure/final_binary_type.cpp",
                    },
                    .flags = flags.flags.items,
                });

                win_installer.root_module.addCMacro("FINAL_BINARY_TYPE", "WindowsInstaller");
                win_installer.linkSystemLibrary("gdi32");
                win_installer.linkSystemLibrary("version");
                win_installer.linkSystemLibrary("comctl32");

                addWin32EmbedInfo(win_installer, .{
                    .name = "Floe Installer",
                    .description = win_installer_description,
                    .icon_path = if (logo_image != null) logo_image.?.relative_path else null,
                }) catch @panic("OOM");
                win_installer.addConfigHeader(build_config_step);
                win_installer.addIncludePath(b.path("src"));
                win_installer.addObject(stb_image);
                win_installer.linkLibrary(library);
                win_installer.linkLibrary(miniz);
                win_installer.linkLibrary(common_infrastructure);
                applyUniversalSettings(&build_context, win_installer);

                // everything needs to be installed before we compile the installer because it needs to embed the
                // plugins
                win_installer.step.dependOn(vst3_final_step.?);
                win_installer.step.dependOn(clap_final_step.?);
                if (uninstall_sign_step) |s| {
                    win_installer.step.dependOn(s);
                } else {
                    win_installer.step.dependOn(&uninstall_artifact_step.step);
                }

                const artifact_step = b.addInstallArtifact(win_installer, .{ .dest_dir = install_subfolder });
                join_compile_commands.step.dependOn(&win_installer.step);

                const installer_sign_step = addWindowsCodeSignStep(
                    &build_context,
                    target,
                    b.getInstallPath(install_dir, win_installer.out_filename),
                    "Floe Installer",
                );
                if (installer_sign_step) |s| {
                    s.dependOn(&artifact_step.step);
                    build_context.master_step.dependOn(s);
                } else {
                    build_context.master_step.dependOn(&artifact_step.step);
                }
            }
        }

        if (!clap_only and build_context.build_mode != .production) {
            const tests = b.addExecutable(.{
                .name = "tests",
                .root_module = b.createModule(module_options),
            });
            tests.addCSourceFiles(.{
                .files = &.{
                    "src/tests/tests_main.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                }).flags.items,
            });
            tests.root_module.addCMacro("FINAL_BINARY_TYPE", "Tests");
            tests.addConfigHeader(build_config_step);
            tests.linkLibrary(plugin);
            b.getInstallStep().dependOn(&b.addInstallArtifact(tests, .{ .dest_dir = install_subfolder }).step);
            applyUniversalSettings(&build_context, tests);
            join_compile_commands.step.dependOn(&tests.step);
            addToLipoSteps(&build_context, tests, false) catch @panic("OOM");

            var post_install_step = b.allocator.create(PostInstallStep) catch @panic("OOM");
            post_install_step.* = PostInstallStep{
                .step = std.Build.Step.init(.{
                    .id = std.Build.Step.Id.custom,
                    .name = "Post install config",
                    .owner = b,
                    .makeFn = performPostInstallConfig,
                }),
                .make_macos_bundle = false,
                .context = &build_context,
                .compile_step = tests,
            };
            post_install_step.step.dependOn(&tests.step);
            post_install_step.step.dependOn(b.getInstallStep());
            build_context.master_step.dependOn(&post_install_step.step);

            const run_tests = b.addRunArtifact(tests);
            build_context.test_step.dependOn(&run_tests.step);
        }

        build_context.master_step.dependOn(&join_compile_commands.step);
    }

    build_context.master_step.dependOn(b.getInstallStep());
    b.default_step = build_context.master_step;
}
