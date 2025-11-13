// Copyright 2018-2025 Sam Windell
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

const std_extras = @import("src/build/std_extras.zig");
const constants = @import("src/build/constants.zig");
const ConcatCompileCommandsStep = @import("src/build/ConcatCompileCommandsStep.zig");

const PostInstallStep = struct {
    step: std.Build.Step,
    make_macos_bundle: bool,
    compile_step: *std.Build.Step.Compile,
    context: *BuildContext,

    fn create(b: *std.Build, options: struct {
        build_context: *BuildContext,
        compile_step: *std.Build.Step.Compile,
        make_macos_bundle: bool,
    }) *PostInstallStep {
        var post_install_step = b.allocator.create(PostInstallStep) catch @panic("OOM");
        post_install_step.* = PostInstallStep{
            .step = std.Build.Step.init(.{
                .id = std.Build.Step.Id.custom,
                .name = b.fmt("{s} post install config", .{options.compile_step.name}),
                .owner = b,
                .makeFn = make,
            }),
            .make_macos_bundle = options.make_macos_bundle,
            .context = options.build_context,
            .compile_step = options.compile_step,
        };
        post_install_step.step.dependOn(&options.compile_step.step);
        post_install_step.step.dependOn(b.getInstallStep());
        return post_install_step;
    }

    fn handleMacosBinaryConfig(
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

            const is_au = std.mem.count(u8, bundle_name, ".component") == 1;

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
                    .bundle_identifier = b.fmt("{s}.{s}", .{ constants.floe_clap_id, bundle_extension_no_dot }),
                    .bundle_name = bundle_name,
                    .major = if (version != null) version.?.major else 1,
                    .minor = if (version != null) version.?.minor else 0,
                    .patch = if (version != null) version.?.patch else 0,
                    .copyright = constants.floe_copyright,
                    .min_macos_version = constants.min_macos_version,

                    // factoryFunction has 'Factory' appended to it because that's what the AUSDK_COMPONENT_ENTRY macro adds.
                    // name uses the format Author: Name because otherwise Logic shows the developer as the 4-character manufacturer code.
                    .audio_unit_dict = if (is_au)
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
                            .vendor = constants.floe_vendor,
                            .description = constants.floe_description,
                            .factory_function = constants.floe_au_factory_function,
                            .version_packed = if (version != null) (version.?.major << 16) | (version.?.minor << 8) | version.?.patch else 0,
                        })
                    else
                        "",
                });
            }

            if (builtin.os.tag == .macos and is_au) {
                // We need to install the AU so that things like auval can find it.
                const home_dir = b.graph.env_map.get("HOME") orelse {
                    return step.fail("HOME environment variable not found, cannot install AU", .{});
                };

                const au_install_dir = b.pathJoin(&.{ home_dir, "Library", "Audio", "Plug-Ins", "Components" });
                const au_install_path = b.pathJoin(&.{ au_install_dir, bundle_name });
                const au_bundle_path = b.pathJoin(&.{ working_dir, bundle_name });

                // Remove existing installation
                // std.fs.deleteTreeAbsolute(au_install_path) catch |err| switch (err) {
                //     error.FileNotFound => {}, // OK if it doesn't exist
                //     else => {
                //         return step.fail("Failed to remove existing AU installation: {}", .{err});
                //     },
                // };

                // Install new AU bundle
                std_extras.copyDirRecursive(au_bundle_path, au_install_path, b.allocator) catch |err| {
                    return step.fail("Failed to install AU bundle: {}", .{err});
                };

                // We need to make sure that the audio component service is aware of the new AU. Unfortunately, it
                // doesn't do this automatically sometimes and if we were to run auval right now it might say it's
                // uninstalled. We need to kill the service so that auval will rescan for installed AUs. The command
                // on the terminal to do this is: killall -9 AudioComponentRegistrar. That is, send SIGKILL to the
                // process named AudioComponentRegistrar.
                _ = std.process.Child.run(.{
                    .allocator = b.allocator,
                    .argv = &.{ "killall", "-9", "AudioComponentRegistrar" },
                }) catch |err| {
                    std.debug.print("Warning: Failed to restart AudioComponentRegistrar: {any}\n", .{err});
                };
            }
        } else {
            final_binary_path = path;
        }

        if (context.optimise != .ReleaseFast) {
            // Generate dSYM for debugging.
            _ = try step.evalChildProcess(&.{ "dsymutil", final_binary_path.? });
        }
    }

    fn make(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
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
                try handleMacosBinaryConfig(
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

                const needs_patch = blk: {
                    const in_nix_shell = step.owner.graph.env_map.get("IN_NIX_SHELL") != null;
                    const is_nixos = blk2: {
                        std.fs.accessAbsolute("/etc/NIXOS", .{}) catch |err| switch (err) {
                            error.FileNotFound => break :blk2 false,
                            else => break :blk2 true,
                        };
                        break :blk2 true;
                    };
                    break :blk in_nix_shell and !is_nixos;
                };

                if (std.mem.indexOf(u8, filename, "Floe.clap.so") != null) {
                    try dir.rename(filename, "Floe.clap");
                    if (needs_patch) {
                        const full_path = try dir.realpathAlloc(step.owner.allocator, "Floe.clap");
                        _ = try step.evalChildProcess(&.{ "patchrpath", full_path });
                    }
                } else if (std.mem.indexOf(u8, filename, "Floe.vst3.so") != null) {
                    const subdir = "Floe.vst3/Contents/x86_64-linux";
                    try dir.makePath(subdir);
                    const file = subdir ++ "/Floe.so";
                    try dir.rename(filename, file);
                    if (needs_patch) {
                        const full_path = try dir.realpathAlloc(step.owner.allocator, file);
                        _ = try step.evalChildProcess(&.{ "patchrpath", full_path });
                    }
                } else {
                    if (needs_patch) {
                        const full_path = try dir.realpathAlloc(step.owner.allocator, filename);
                        _ = try step.evalChildProcess(&.{ "patchinterpreter", full_path });
                    }
                }
            },
            else => {
                unreachable;
            },
        }
    }
};

fn addWin32EmbedInfo(step: *std.Build.Step.Compile, info: struct {
    name: []const u8,
    description: []const u8,
    icon_path: ?std.Build.LazyPath,
}) !void {
    if (step.rootModuleTarget().os.tag != .windows) return;

    const b = step.step.owner;
    const arena = b.allocator;

    var wf = b.addWriteFiles();

    var data = std.ArrayList(u8).init(arena);

    try std.fmt.format(data.writer(),
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
        .this_year = 1970 + @divTrunc(std.time.timestamp(), 60 * 60 * 24 * 365),
        .copyright = constants.floe_copyright,
        .vendor = constants.floe_vendor,
    });

    var rc_include_paths: std.BoundedArray(std.Build.LazyPath, 1) = .{};

    if (info.icon_path) |p| {
        try std.fmt.format(data.writer(), "\nicon_id ICON \"icon.ico\"\n", .{});
        _ = wf.addCopyFile(p, "icon.ico");
        try rc_include_paths.append(p);
    }

    const rc_path = wf.add(b.fmt("{s}.rc", .{step.name}), data.items);

    step.addWin32ResourceFile(.{
        .file = rc_path,
        .include_paths = rc_include_paths.slice(),
    });
}

pub const WindowsCodeSignStep = struct {
    step: std.Build.Step,
    file_path: []const u8,
    description: []const u8,
    context: *BuildContext,

    pub fn create(
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
                .makeFn = make,
            }),
            .file_path = context.b.dupe(file_path),
            .description = context.b.dupe(description),
            .context = context,
        };

        return &cs_step.step;
    }

    fn make(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
        const self: *WindowsCodeSignStep = @fieldParentPtr("step", step);
        _ = options;

        const b = self.context.b;

        // Get absolute paths
        const abs_file = b.pathFromRoot(self.file_path);
        const cache_path = b.pathFromRoot(constants.floe_cache_relative);

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
                constants.floe_homepage_url,
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
};

const BuildMode = enum {
    development,
    performance_profiling,
    production, // a.k.a.: release, end-user, for-distribution
};

const BuildContext = struct {
    b: *std.Build,
    enable_tracy: bool,
    build_mode: BuildMode,
    compile_all_step: *std.Build.Step,
    optimise: std.builtin.OptimizeMode,
    dep_floe_logos: ?*std.Build.Dependency,
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
    dep_lua: *std.Build.Dependency,
    dep_pugl: *std.Build.Dependency,
    dep_pffft: *std.Build.Dependency,
    dep_valgrind_h: *std.Build.Dependency,
    dep_portmidi: *std.Build.Dependency,
    dep_tracy: *std.Build.Dependency,
    dep_vst3_sdk: *std.Build.Dependency,
};

const FlagsBuilder = struct {
    flags: std.ArrayList([]const u8),

    const Options = struct {
        ubsan: bool = false,
        add_compile_commands: bool = true,
        full_diagnostics: bool = false,
        cpp: bool = false,
        objcpp: bool = false,
    };

    pub fn init(
        context: *BuildContext,
        target: std.Build.ResolvedTarget,
        options: Options,
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
        options: Options,
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
                "-DSTRICT",
                "-DNOMINMAX",
            });
        }

        try self.flags.append("-fchar8_t");
        try self.flags.append("-D_USE_MATH_DEFINES");
        try self.flags.append("-D__USE_FILE_OFFSET64");
        try self.flags.append("-D_FILE_OFFSET_BITS=64");
        try self.flags.append("-ftime-trace"); // ClangBuildAnalyzer

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

        // We want __builtin_FILE(), __FILE__ and debug info to be portable because we use this information in
        // stacktraces and logging so we change the absolute paths of all files to be relative to the build root.
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
        // DWARF is a debugging information format. It is used widely, particularly on Linux and macOS. Zig/libbacktrace,
        // which we use for getting nice stack traces can read DWARF information from the executable on any OS. All
        // we need to do is make sure that the DWARF info is available for Zig/libbacktrace to read.
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
        // outputs a .dSYM folder which contains the aggregated DWARF info. Zig/libbacktrace looks for this dSYM folder
        // adjacent to the executable.

        // Include dwarf debug info, even on windows. This means we can use the Zig/libbacktraceeverywhere to get
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
            try self.flags.append(ConcatCompileCommandsStep.cdbFragmentsDir(context.b, target.result));
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

fn applyUniversalSettings(context: *BuildContext, step: *std.Build.Step.Compile, compile_commands: *ConcatCompileCommandsStep) void {
    var b = context.b;
    // NOTE (May 2025, Zig 0.14): LTO on Windows results in debug_info generation that fails to parse with Zig's
    // Dwarf parser (InvalidDebugInfo). We've previously had issues on macOS too. So we disable it for now.
    step.want_lto = false;
    step.rdynamic = true;
    step.linkLibC();

    step.addIncludePath(context.dep_xxhash.path(""));
    step.addIncludePath(context.dep_stb.path(""));
    step.addIncludePath(context.dep_clap.path("include"));
    step.addIncludePath(context.dep_icon_font_cpp_headers.path(""));
    step.addIncludePath(context.dep_dr_libs.path(""));
    step.addIncludePath(context.dep_flac.path("include"));
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

    compile_commands.step.dependOn(&step.step);
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
                    arch_os_abi = "x86_64-windows." ++ constants.min_windows_version;
                    cpu_features = x86_cpu;
                },
                .x86_64_linux => {
                    arch_os_abi = "x86_64-linux-gnu.2.29";
                    cpu_features = x86_cpu;
                },
                .x86_64_macos => {
                    arch_os_abi = "x86_64-macos." ++ constants.min_macos_version;
                    cpu_features = apple_x86_cpu;
                },
                .aarch64_macos => {
                    arch_os_abi = "aarch64-macos." ++ constants.min_macos_version;
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

// Based on https://github.com/zigster64/dotenv.zig
// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Scribe of the Ziggurat
fn loadEnvFile(b: *std.Build) !void {
    var file = b.build_root.handle.openFile(".env", .{}) catch {
        return;
    };
    defer file.close();

    var buf_reader = std.io.bufferedReader(file.reader());
    var in_stream = buf_reader.reader();
    var buf: [1024]u8 = undefined;

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

            try b.graph.env_map.put(key, value);
        }
    }
}

pub fn build(b: *std.Build) void {
    loadEnvFile(b) catch {};

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

    const sanitize_thread = b.option(
        bool,
        "sanitize-thread",
        "Enable thread sanitiser",
    ) orelse false;

    const fetch_floe_logos = b.option(
        bool,
        "fetch-floe-logos",
        "Fetch Floe logos from online - these may have a different licence to the rest of Floe",
    ) orelse false;

    const clap_val = b.step("test-clap-val", "Test using clap-validator");
    const test_vst3_validator = b.step("test-vst3-val", "Run VST3 Validator on built VST3 plugin");
    const pluginval_au = b.step("test-pluginval-au", "Test AU using pluginval");
    const auval = b.step("test-auval", "Test AU using auval");
    var test_step = b.step("test", "Run unit tests");
    const coverage = b.step("test-coverage", "Generate code coverage report");
    const pluginval = b.step("test-pluginval", "Test using pluginval");
    const valgrind = b.step("test-valgrind", "Test using Valgrind");
    const clang_tidy = b.step("clang-tidy", "Run clang-tidy on source files");

    const all_tests = b.step("test-all", "Test using all tests");
    all_tests.dependOn(clap_val);
    all_tests.dependOn(test_vst3_validator);
    all_tests.dependOn(pluginval_au);
    all_tests.dependOn(auval);
    all_tests.dependOn(test_step);
    all_tests.dependOn(coverage);
    all_tests.dependOn(pluginval);
    all_tests.dependOn(valgrind);
    all_tests.dependOn(clang_tidy);

    var build_context: BuildContext = .{
        .b = b,
        .enable_tracy = enable_tracy,
        .build_mode = build_mode,
        .compile_all_step = b.step("compile", "Compile all"),
        .optimise = switch (build_mode) {
            .development => std.builtin.OptimizeMode.Debug,
            .performance_profiling, .production => std.builtin.OptimizeMode.ReleaseSafe,
        },
        .dep_floe_logos = if (fetch_floe_logos)
            b.dependency("floe_logos", .{})
        else
            null,
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
    b.build_root.handle.makeDir(constants.floe_cache_relative) catch {};

    // const install_dir = b.install_path; // zig-out

    const targets = getTargets(b, user_given_target_presets) catch @panic("OOM");

    // If we're building for multiple targets at the same time, we need to choose one that gets to be the final compile_commands.json. We just say the first one.
    const target_for_compile_commands = targets.items[0];
    // We'll try installing the desired compile_commands.json version here in case any previous build already created it.
    ConcatCompileCommandsStep.trySetCdb(b, target_for_compile_commands.result);

    for (targets.items) |target| {
        var join_compile_commands = ConcatCompileCommandsStep.create(b, target, target.query.eql(target_for_compile_commands.query));
        build_context.compile_all_step.dependOn(&join_compile_commands.step);

        // Separate output directory when thread sanitizer is enabled to avoid overwriting default binaries
        const install_subfolder_string = b.fmt("{s}{s}", .{ std_extras.archAndOsPair(target.result).slice(), if (sanitize_thread) "-tsan" else "" });
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

        const windows_ntddi_version: i64 = @intFromEnum(std.Target.Os.WindowsVersion.parse(constants.min_windows_version) catch @panic("invalid win ver"));

        const build_config_step = b.addConfigHeader(.{
            .style = .blank,
        }, .{
            .PRODUCTION_BUILD = build_context.build_mode == .production,
            .OPTIMISED_BUILD = build_context.optimise != .Debug,
            .RUNTIME_SAFETY_CHECKS_ON = build_context.optimise == .Debug or build_context.optimise == .ReleaseSafe,
            .FLOE_VERSION_STRING = floe_version_string,
            .FLOE_VERSION_HASH = floe_version_hash,
            .FLOE_DESCRIPTION = constants.floe_description,
            .FLOE_HOMEPAGE_URL = constants.floe_homepage_url,
            .FLOE_MANUAL_URL = constants.floe_manual_url,
            .FLOE_DOWNLOAD_URL = constants.floe_download_url,
            .FLOE_CHANGELOG_URL = constants.floe_changelog_url,
            .FLOE_SOURCE_CODE_URL = constants.floe_source_code_url,
            .FLOE_PROJECT_ROOT_PATH = b.build_root.path.?,
            .FLOE_PROJECT_CACHE_PATH = b.pathJoin(&.{ b.build_root.path.?, constants.floe_cache_relative }),
            .FLOE_VENDOR = constants.floe_vendor,
            .FLOE_CLAP_ID = constants.floe_clap_id,
            .IS_WINDOWS = target.result.os.tag == .windows,
            .IS_MACOS = target.result.os.tag == .macos,
            .IS_LINUX = target.result.os.tag == .linux,
            .OS_DISPLAY_NAME = b.fmt("{s}", .{@tagName(target.result.os.tag)}),
            .ARCH_DISPLAY_NAME = b.fmt("{s}", .{@tagName(target.result.cpu.arch)}),
            .MIN_WINDOWS_NTDDI_VERSION = windows_ntddi_version,
            .MIN_MACOS_VERSION = constants.min_macos_version,
            .SENTRY_DSN = b.graph.env_map.get("SENTRY_DSN"),
        });

        if (target.result.os.tag == .windows and sanitize_thread) {
            std.log.err("thread sanitiser is not supported on Windows targets", .{});
            @panic("thread sanitiser is not supported on Windows targets");
        }

        const module_options: std.Build.Module.CreateOptions = .{
            .target = target,
            .optimize = build_context.optimise,
            .strip = false,
            .pic = true,
            .link_libc = true,
            .omit_frame_pointer = false,
            .unwind_tables = .sync,
            .sanitize_thread = sanitize_thread,
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
            applyUniversalSettings(&build_context, tracy, join_compile_commands);
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

            applyUniversalSettings(&build_context, pugl, join_compile_commands);
        }

        var debug_info_module_options = module_options;
        debug_info_module_options.root_source_file = b.path("src/utils/debug_info/debug_info.zig");
        const debug_info_lib = b.addObject(.{
            .name = "debug_info_lib",
            .root_module = b.createModule(debug_info_module_options),
        });
        debug_info_lib.linkLibC(); // Means better debug info on Linux
        debug_info_lib.addIncludePath(b.path("src/utils/debug_info"));

        // IMPROVE: does this need to be a library? is foundation/os/plugin all linked together?
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
                library_path ++ "/utils/no_hash.cpp",
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
            applyUniversalSettings(&build_context, library, join_compile_commands);
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
            applyUniversalSettings(&build_context, fft_convolver, join_compile_commands);
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
                    path ++ "/autosave.cpp",
                    path ++ "/checksum_crc32_file.cpp",
                    path ++ "/common_errors.cpp",
                    path ++ "/descriptors/param_descriptors.cpp",
                    path ++ "/error_reporting.cpp",
                    path ++ "/folder_node.cpp",
                    path ++ "/global.cpp",
                    path ++ "/package_format.cpp",
                    path ++ "/paths.cpp",
                    path ++ "/persistent_store.cpp",
                    path ++ "/preferences.cpp",
                    path ++ "/preset_bank_info.cpp",
                    path ++ "/sample_library/audio_file.cpp",
                    path ++ "/sample_library/sample_library.cpp",
                    path ++ "/sample_library/sample_library_lua.cpp",
                    path ++ "/sample_library/sample_library_mdata.cpp",
                    path ++ "/sentry/sentry.cpp",
                    path ++ "/state/macros.cpp",
                    path ++ "/state/state_coding.cpp",
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
            applyUniversalSettings(&build_context, common_infrastructure, join_compile_commands);
        }

        var embedded_files: ?*std.Build.Step.Compile = null;
        if (!constants.embed_files_workaround) {
            var emdedded_files_module_options = module_options;
            emdedded_files_module_options.root_source_file = b.path("build_resources/embedded_files.zig");
            embedded_files = b.addObject(.{
                .name = "embedded_files",
                .root_module = b.createModule(emdedded_files_module_options),
            });
            {
                var embedded_files_options = b.addOptions();
                if (build_context.dep_floe_logos) |logos| {
                    embedded_files_options.addOptionPath("logo_file", logos.path("rasterized/plugin-gui-logo.png"));
                    embedded_files_options.addOptionPath("icon_file", logos.path("rasterized/icon-background-256px.png"));
                } else {
                    embedded_files_options.addOption(?[]const u8, "logo_file", null);
                    embedded_files_options.addOption(?[]const u8, "icon_file", null);
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
                    plugin_path ++ "/engine/check_for_update.cpp",
                    plugin_path ++ "/engine/engine.cpp",
                    plugin_path ++ "/engine/package_installation.cpp",
                    plugin_path ++ "/engine/shared_engine_systems.cpp",
                    plugin_path ++ "/gui/gui.cpp",
                    plugin_path ++ "/gui/gui2_bot_panel.cpp",
                    plugin_path ++ "/gui/gui2_common_browser.cpp",
                    plugin_path ++ "/gui/gui2_inst_browser.cpp",
                    plugin_path ++ "/gui/gui2_ir_browser.cpp",
                    plugin_path ++ "/gui/gui2_library_dev_panel.cpp",
                    plugin_path ++ "/gui/gui2_macros.cpp",
                    plugin_path ++ "/gui/gui2_parameter_component.cpp",
                    plugin_path ++ "/gui/gui2_preset_browser.cpp",
                    plugin_path ++ "/gui/gui2_save_preset_panel.cpp",
                    plugin_path ++ "/gui/gui2_top_panel.cpp",
                    plugin_path ++ "/gui/gui_button_widgets.cpp",
                    plugin_path ++ "/gui/gui_dragger_widgets.cpp",
                    plugin_path ++ "/gui/gui_draw_knob.cpp",
                    plugin_path ++ "/gui/gui_drawing_helpers.cpp",
                    plugin_path ++ "/gui/gui_editor_widgets.cpp",
                    plugin_path ++ "/gui/gui_effects.cpp",
                    plugin_path ++ "/gui/gui_envelope.cpp",
                    plugin_path ++ "/gui/gui_keyboard.cpp",
                    plugin_path ++ "/gui/gui_knob_widgets.cpp",
                    plugin_path ++ "/gui/gui_label_widgets.cpp",
                    plugin_path ++ "/gui/gui_layer.cpp",
                    plugin_path ++ "/gui/gui_library_images.cpp",
                    plugin_path ++ "/gui/gui_waveform_images.cpp",
                    plugin_path ++ "/gui/gui_mid_panel.cpp",
                    plugin_path ++ "/gui/gui_modal_windows.cpp",
                    plugin_path ++ "/gui/gui_peak_meter_widget.cpp",
                    plugin_path ++ "/gui/gui_prefs.cpp",
                    plugin_path ++ "/gui/gui_waveform.cpp",
                    plugin_path ++ "/gui/gui_widget_compounds.cpp",
                    plugin_path ++ "/gui/gui_widget_helpers.cpp",
                    plugin_path ++ "/gui/gui_window.cpp",
                    plugin_path ++ "/gui_framework/draw_list.cpp",
                    plugin_path ++ "/gui_framework/draw_list_opengl.cpp",
                    plugin_path ++ "/gui_framework/gui_box_system.cpp",
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
            plugin.addConfigHeader(build_config_step);
            plugin.linkLibrary(library);
            plugin.linkLibrary(common_infrastructure);
            plugin.linkLibrary(fft_convolver);
            if (constants.embed_files_workaround) {
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
            applyUniversalSettings(&build_context, plugin, join_compile_commands);
        }

        if (!constants.clap_only and build_context.build_mode != .production) {
            var docs_generator = b.addExecutable(.{
                .name = "docs_generator",
                .root_module = b.createModule(module_options),
            });
            docs_generator.addCSourceFiles(.{
                .files = &.{
                    "src/docs_generator/docs_generator.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                }).flags.items,
            });
            docs_generator.root_module.addCMacro("FINAL_BINARY_TYPE", "DocsGenerator");
            docs_generator.linkLibrary(common_infrastructure);
            docs_generator.addIncludePath(b.path("src"));
            docs_generator.addConfigHeader(build_config_step);
            applyUniversalSettings(&build_context, docs_generator, join_compile_commands);
            b.getInstallStep().dependOn(&b.addInstallArtifact(docs_generator, .{ .dest_dir = install_subfolder }).step);

            const docs_generator_post_install_step = PostInstallStep.create(b, .{
                .build_context = &build_context,
                .compile_step = docs_generator,
                .make_macos_bundle = false,
            });
            build_context.compile_all_step.dependOn(&docs_generator_post_install_step.step);
        }

        if (!constants.clap_only) {
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
            if (constants.embed_files_workaround) {
                packager.addCSourceFile(.{
                    .file = b.dependency("embedded_files_workaround", .{}).path("embedded_files.cpp"),
                });
            } else {
                packager.addObject(embedded_files.?);
            }
            applyUniversalSettings(&build_context, packager, join_compile_commands);
            const packager_install_artifact_step = b.addInstallArtifact(
                packager,
                .{ .dest_dir = install_subfolder },
            );
            b.getInstallStep().dependOn(&packager_install_artifact_step.step);

            const sign_step = WindowsCodeSignStep.create(
                &build_context,
                target,
                b.getInstallPath(install_dir, packager.out_filename),
                "Floe packager",
            );
            if (sign_step) |s| {
                s.dependOn(&packager.step);
                s.dependOn(b.getInstallStep());
                build_context.compile_all_step.dependOn(s);
            }
        }

        if (!constants.clap_only) {
            var preset_editor = b.addExecutable(.{
                .name = "preset-editor",
                .root_module = b.createModule(module_options),
                .version = floe_version,
            });
            preset_editor.addCSourceFiles(.{
                .files = &.{
                    "src/preset_editor_tool/preset_editor.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                }).flags.items,
            });
            preset_editor.root_module.addCMacro("FINAL_BINARY_TYPE", "PresetEditor");
            preset_editor.linkLibrary(common_infrastructure);
            preset_editor.addIncludePath(b.path("src"));
            preset_editor.addConfigHeader(build_config_step);
            preset_editor.linkLibrary(miniz);
            if (constants.embed_files_workaround) {
                preset_editor.addCSourceFile(.{
                    .file = b.dependency("embedded_files_workaround", .{}).path("embedded_files.cpp"),
                });
            } else {
                preset_editor.addObject(embedded_files.?);
            }
            applyUniversalSettings(&build_context, preset_editor, join_compile_commands);
            const preset_editor_install_artifact_step = b.addInstallArtifact(
                preset_editor,
                .{ .dest_dir = install_subfolder },
            );
            b.getInstallStep().dependOn(&preset_editor_install_artifact_step.step);

            // IMPROVE: export preset-editor as a production artifact?
        }

        var clap_final_step: ?*std.Build.Step = null;
        var clap_compile_step: ?*std.Build.Step.Compile = null;
        if (!sanitize_thread) {
            const clap = b.addSharedLibrary(.{
                .name = "Floe.clap",
                .root_module = b.createModule(module_options),
                .version = floe_version,
            });
            clap_compile_step = clap;
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
            applyUniversalSettings(&build_context, clap, join_compile_commands);
            addWin32EmbedInfo(clap, .{
                .name = "Floe CLAP",
                .description = constants.floe_description,
                .icon_path = null,
            }) catch @panic("OOM");

            const clap_post_install_step = PostInstallStep.create(b, .{
                .build_context = &build_context,
                .compile_step = clap,
                .make_macos_bundle = true,
            });

            const sign_step = WindowsCodeSignStep.create(
                &build_context,
                target,
                b.getInstallPath(install_dir, clap.name),
                "Floe CLAP Plugin",
            );
            if (sign_step) |s| {
                s.dependOn(&clap_post_install_step.step);
                build_context.compile_all_step.dependOn(s);
                clap_final_step = s;
            } else {
                build_context.compile_all_step.dependOn(&clap_post_install_step.step);
                clap_final_step = &clap_post_install_step.step;
            }
        }

        // standalone is for development-only at the moment
        if (!constants.clap_only and build_context.build_mode != .production) {
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
                applyUniversalSettings(&build_context, miniaudio, join_compile_commands);
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
                applyUniversalSettings(&build_context, portmidi, join_compile_commands);
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
            applyUniversalSettings(&build_context, floe_standalone, join_compile_commands);

            const post_install_step = PostInstallStep.create(b, .{
                .build_context = &build_context,
                .compile_step = floe_standalone,
                .make_macos_bundle = false,
            });
            build_context.compile_all_step.dependOn(&post_install_step.step);
        }

        const vst3_sdk = b.addStaticLibrary(.{
            .name = "VST3",
            .root_module = b.createModule(module_options),
        });
        const vst3_validator = b.addExecutable(.{
            .name = "VST3-Validator",
            .root_module = b.createModule(module_options),
        });
        if (!constants.clap_only) {
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
                applyUniversalSettings(&build_context, vst3_sdk, join_compile_commands);
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
                applyUniversalSettings(&build_context, vst3_validator, join_compile_commands);
                b.getInstallStep().dependOn(&b.addInstallArtifact(
                    vst3_validator,
                    .{ .dest_dir = install_subfolder },
                ).step);

                const vst3_validator_post_install_step = PostInstallStep.create(b, .{
                    .build_context = &build_context,
                    .compile_step = vst3_validator,
                    .make_macos_bundle = false,
                });
                build_context.compile_all_step.dependOn(&vst3_validator_post_install_step.step);
            }
        }

        var vst3_final_step: ?*std.Build.Step = null;
        if (!constants.clap_only and !sanitize_thread) {
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
            vst3.addIncludePath(build_context.dep_clap_wrapper.path("libs/psl"));
            vst3.addIncludePath(build_context.dep_clap_wrapper.path("src"));
            vst3.addIncludePath(build_context.dep_vst3_sdk.path(""));
            vst3.linkLibCpp();

            vst3.linkLibrary(plugin);
            vst3.linkLibrary(vst3_sdk);

            vst3.addConfigHeader(build_config_step);
            vst3.addIncludePath(b.path("src"));

            const vst3_install_artifact_step = b.addInstallArtifact(vst3, .{ .dest_dir = install_subfolder });
            b.getInstallStep().dependOn(&vst3_install_artifact_step.step);
            applyUniversalSettings(&build_context, vst3, join_compile_commands);
            addWin32EmbedInfo(vst3, .{
                .name = "Floe VST3",
                .description = constants.floe_description,
                .icon_path = null,
            }) catch @panic("OOM");

            const vst3_post_install_step = PostInstallStep.create(b, .{
                .build_context = &build_context,
                .compile_step = vst3,
                .make_macos_bundle = true,
            });

            const sign_step = WindowsCodeSignStep.create(
                &build_context,
                target,
                b.getInstallPath(install_dir, vst3.name),
                "Floe VST3 Plugin",
            );
            if (sign_step) |s| {
                s.dependOn(&vst3_post_install_step.step);
                build_context.compile_all_step.dependOn(s);
                vst3_final_step = s;
            } else {
                build_context.compile_all_step.dependOn(&vst3_post_install_step.step);
                vst3_final_step = &vst3_post_install_step.step;
            }

            // Test VST3
            {
                const run_tests = std_extras.createCommandWithStdoutToStderr(b, target, "run VST3-Validator");
                run_tests.addArtifactArg(vst3_validator);
                run_tests.addArg(b.getInstallPath(install_dir, vst3.name));
                run_tests.step.dependOn(vst3_final_step.?);
                run_tests.expectExitCode(0);

                test_vst3_validator.dependOn(&run_tests.step);
            }
        }

        if (!constants.clap_only and target.result.os.tag == .macos and !sanitize_thread) {
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
                applyUniversalSettings(&build_context, au_sdk, join_compile_commands);
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
                        b.pathJoin(&.{ constants.floe_cache_relative, "generated_entrypoints.hxx" }),
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
                        .factory_function = constants.floe_au_factory_function,
                        .clap_name = "Floe",
                        .clap_id = constants.floe_clap_id,
                    })) catch @panic("could not write to file");
                }

                {
                    const file = b.build_root.handle.createFile(
                        b.pathJoin(&.{ constants.floe_cache_relative, "generated_cocoaclasses.hxx" }),
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
                        \\ #define CLAP_WRAPPER_EDITOR_NAME "Floe"
                        \\ #include "detail/auv2/wrappedview.asinclude.mm"
                        \\ #undef CLAP_WRAPPER_COCOA_CLASS_NSVIEW
                        \\ #undef CLAP_WRAPPER_COCOA_CLASS
                        \\ #undef CLAP_WRAPPER_TIMER_CALLBACK
                        \\ #undef CLAP_WRAPPER_FILL_AUCV
                        \\ #undef CLAP_WRAPPER_EDITOR_NAME
                        \\
                        \\ bool fillAudioUnitCocoaView(AudioUnitCocoaViewInfo* viewInfo, std::shared_ptr<Clap::Plugin> _plugin) {{
                        \\     if (strcmp(_plugin->_plugin->desc->id, "{[clap_id]s}") == 0) {{
                        \\         if (!_plugin->_ext._gui) return false;
                        \\         return fillAUCV_{[name]s}(viewInfo);
                        \\     }}
                        \\ }}
                    , .{
                        .name = b.fmt("Floe{d}", .{floe_version_hash}),
                        .clap_id = constants.floe_clap_id,
                    })) catch @panic("could not write to file");
                }

                au.addIncludePath(b.path("third_party_libs/clap/include"));
                au.addIncludePath(build_context.dep_au_sdk.path("include"));
                au.addIncludePath(build_context.dep_clap_wrapper.path("include"));
                au.addIncludePath(build_context.dep_clap_wrapper.path("libs/fmt"));
                au.addIncludePath(build_context.dep_clap_wrapper.path("src"));
                au.addIncludePath(b.path(constants.floe_cache_relative));
                au.linkLibCpp();

                au.linkLibrary(plugin);
                au.linkLibrary(au_sdk);
                au.linkFramework("AudioToolbox");
                au.linkFramework("CoreMIDI");

                au.addConfigHeader(build_config_step);
                au.addIncludePath(b.path("src"));

                const au_install_artifact_step = b.addInstallArtifact(au, .{ .dest_dir = install_subfolder });
                b.getInstallStep().dependOn(&au_install_artifact_step.step);
                applyUniversalSettings(&build_context, au, join_compile_commands);

                const au_post_install_step = PostInstallStep.create(b, .{
                    .build_context = &build_context,
                    .compile_step = au,
                    .make_macos_bundle = true,
                });
                build_context.compile_all_step.dependOn(&au_post_install_step.step);

                // Pluginval AU
                {
                    // Pluginval puts all of it's output in stdout, not stderr.
                    const run = std_extras.createCommandWithStdoutToStderr(b, target, "run pluginval AU");

                    run.addArgs(&.{
                        "pluginval",
                        "--validate",
                        b.getInstallPath(install_dir, "Floe.component"),
                    });

                    run.step.dependOn(&au_post_install_step.step);
                    run.expectExitCode(0);

                    pluginval_au.dependOn(&run.step);
                }

                // auval
                {
                    const run_auval = std_extras.createCommandWithStdoutToStderr(b, target, "run auval");
                    run_auval.addArgs(&.{ "auval", "-v", "aumu", "FLOE", "floA" });
                    run_auval.step.dependOn(&au_post_install_step.step);
                    run_auval.expectExitCode(0);

                    auval.dependOn(&run_auval.step);
                }
            }
        }

        if (!constants.clap_only and target.result.os.tag == .windows) {
            const installer_path = "src/windows_installer";

            {
                const writeManifest = (struct {
                    fn writeManifest(
                        builder: *std.Build,
                        name: []const u8,
                        require_admin: bool,
                        description: []const u8,
                    ) []const u8 {
                        const manifest_path = builder.pathJoin(&.{
                            constants.floe_cache_relative,
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
                            .id = constants.floe_clap_id,
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
                applyUniversalSettings(&build_context, win_uninstaller, join_compile_commands);

                const uninstall_artifact_step = b.addInstallArtifact(
                    win_uninstaller,
                    .{ .dest_dir = install_subfolder },
                );
                build_context.compile_all_step.dependOn(&uninstall_artifact_step.step);

                const uninstall_sign_step = WindowsCodeSignStep.create(
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

                var rc_include_path: std.BoundedArray(std.Build.LazyPath, 2) = .{};
                rc_include_path.append(b.path("zig-out/x86_64-windows")) catch @panic("OOM");

                if (build_context.dep_floe_logos) |logos| {
                    const sidebar_img = "rasterized/win-installer-sidebar.png";
                    const sidebar_img_lazy_path = logos.path(sidebar_img);
                    rc_include_path.append(sidebar_img_lazy_path.dirname()) catch @panic("OOM");
                    win_installer.root_module.addCMacro(
                        "SIDEBAR_IMAGE_PATH",
                        b.fmt("\"{s}\"", .{std.fs.path.basename(sidebar_img)}),
                    );
                }
                win_installer.root_module.addCMacro(
                    "VST3_PLUGIN_PATH_RELATIVE_BUILD_ROOT",
                    "\"Floe.vst3\"",
                );
                win_installer.root_module.addCMacro(
                    "CLAP_PLUGIN_PATH_RELATIVE_BUILD_ROOT",
                    "\"Floe.clap\"",
                );
                win_installer.root_module.addCMacro(
                    "UNINSTALLER_PATH_RELATIVE_BUILD_ROOT",
                    b.fmt("\"{s}\"", .{win_uninstaller.out_filename}),
                );
                win_installer.addWin32ResourceFile(.{
                    .file = b.path(installer_path ++ "/resources.rc"),
                    .include_paths = rc_include_path.slice(),
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
                    .icon_path = if (build_context.dep_floe_logos) |logos| logos.path("rasterized/icon.ico") else null,
                }) catch @panic("OOM");
                win_installer.addConfigHeader(build_config_step);
                win_installer.addIncludePath(b.path("src"));
                win_installer.addObject(stb_image);
                win_installer.linkLibrary(library);
                win_installer.linkLibrary(miniz);
                win_installer.linkLibrary(common_infrastructure);
                applyUniversalSettings(&build_context, win_installer, join_compile_commands);

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

                const installer_sign_step = WindowsCodeSignStep.create(
                    &build_context,
                    target,
                    b.getInstallPath(install_dir, win_installer.out_filename),
                    "Floe Installer",
                );
                if (installer_sign_step) |s| {
                    s.dependOn(&artifact_step.step);
                    build_context.compile_all_step.dependOn(s);
                } else {
                    build_context.compile_all_step.dependOn(&artifact_step.step);
                }
            }
        }

        var tests_compile_step: ?*std.Build.Step.Compile = null;
        if (!constants.clap_only and build_context.build_mode != .production) {
            const tests = b.addExecutable(.{
                .name = "tests",
                .root_module = b.createModule(module_options),
            });
            tests_compile_step = tests;
            tests.addCSourceFiles(.{
                .files = &.{
                    "src/common_infrastructure/final_binary_type.cpp",
                    "src/foundation/container/bitset.cpp",
                    "src/foundation/container/bounded_list.cpp",
                    "src/foundation/container/circular_buffer.cpp",
                    "src/foundation/container/dynamic_array.cpp",
                    "src/foundation/container/function.cpp",
                    "src/foundation/container/function_queue.cpp",
                    "src/foundation/container/hash_table.cpp",
                    "src/foundation/container/optional.cpp",
                    "src/foundation/container/path_pool.cpp",
                    "src/foundation/container/tagged_union.cpp",
                    "src/foundation/error/assert_f.cpp",
                    "src/foundation/error/error_code.cpp",
                    "src/foundation/memory/allocators.cpp",
                    "src/foundation/utils/algorithm.cpp",
                    "src/foundation/utils/format.cpp",
                    "src/foundation/utils/geometry.cpp",
                    "src/foundation/utils/linked_list.cpp",
                    "src/foundation/utils/maths.cpp",
                    "src/foundation/utils/memory.cpp",
                    "src/foundation/utils/path.cpp",
                    "src/foundation/utils/random.cpp",
                    "src/foundation/utils/version.cpp",
                    "src/foundation/utils/writer.cpp",
                    "src/tests/tests_main.cpp",
                    "src/utils/error_notifications.cpp",
                    "src/utils/json/json_reader.cpp",
                    "src/utils/json/json_writer.cpp",
                    "src/utils/thread_extra/atomic_queue.cpp",
                    "src/utils/thread_extra/atomic_ref_list.cpp",
                    "src/utils/thread_extra/atomic_swap_buffer.cpp",
                    "src/utils/thread_extra/thread_pool.cpp",
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
            applyUniversalSettings(&build_context, tests, join_compile_commands);

            const post_install_step = PostInstallStep.create(b, .{
                .build_context = &build_context,
                .compile_step = tests,
                .make_macos_bundle = false,
            });
            build_context.compile_all_step.dependOn(&post_install_step.step);

            // Run tests
            {
                const run_tests = b.addRunArtifact(tests);
                run_tests.addArgs(&.{
                    "--log-level=debug",
                    b.fmt("--junit-xml-output-path={s}/results{s}.junit.xml", .{
                        b.pathFromRoot(constants.floe_cache_relative),
                        if (sanitize_thread) "-tsan" else "",
                    }),
                });
                run_tests.expectExitCode(0);

                const capture_std_streams = false;

                if (capture_std_streams) {
                    const test_results_dir = "test_results";
                    test_step.dependOn(&b.addInstallFile(
                        run_tests.captureStdErr(),
                        b.fmt(test_results_dir ++ "/tests{s}-stderr.txt", .{if (sanitize_thread) "-tsan" else ""}),
                    ).step);
                    test_step.dependOn(&b.addInstallFile(
                        run_tests.captureStdOut(),
                        b.fmt(test_results_dir ++ "/tests{s}-stdout.txt", .{if (sanitize_thread) "-tsan" else ""}),
                    ).step);
                } else {
                    test_step.dependOn(&run_tests.step);
                }
            }

            // Coverage tests
            if (builtin.os.tag == .linux) {
                const run_coverage = b.addSystemCommand(&.{
                    "kcov",
                    b.fmt("--include-pattern={s}", .{b.pathFromRoot("src")}),
                    b.fmt("{s}/coverage-out", .{constants.floe_cache_relative}),
                });
                run_coverage.addArtifactArg(tests_compile_step.?);
                run_coverage.expectExitCode(0);
                coverage.dependOn(&run_coverage.step);
            }
        }

        // Clap Validator test
        if (clap_compile_step != null) {
            const run = std_extras.createCommandWithStdoutToStderr(b, target, "run clap-validator");
            if (target.result.os.tag == .windows) {
                run.addFileArg(std_extras.fetch(b, .{
                    .url = "https://github.com/free-audio/clap-validator/releases/download/0.3.2/clap-validator-0.3.2-windows.zip",
                    .file_name = "clap-validator.exe",
                    .hash = "N-V-__8AAACYMwAKpkDTKEWrhJhUyBs1LxycLWN8iFpe5p6r",
                }));
            } else {
                run.addArg("clap-validator");
            }
            run.addArgs(&.{
                "validate",
                "--test-filter",
                ".*(process|param|state-reproducibility-flush).*",
                "--invert-filter",
            });
            run.addArtifactArg(clap_compile_step.?);
            run.expectExitCode(0);

            clap_val.dependOn(&run.step);
        }

        // Pluginval test
        if (vst3_final_step != null) {
            // Pluginval puts all of it's output in stdout, not stderr.
            const run = std_extras.createCommandWithStdoutToStderr(b, target, "run pluginval");

            if (target.result.os.tag == .windows) {
                // On Windows, we use a downloaded binary.
                run.addFileArg(std_extras.fetch(b, .{
                    .url = "https://github.com/Tracktion/pluginval/releases/download/v1.0.3/pluginval_Windows.zip",
                    .file_name = "pluginval.exe",
                    .hash = "N-V-__8AAABcNACEKUY1SsEfHGFybDSKUo4JGhYN5bgZ146c",
                }));
            } else {
                // On macOS and Linux, we assume pluginval is available in the path. We typically have a debug build
                // of pluginval available in our Nix shell environment. Having a custom built debug version can
                // help diagnose plugin issues.
                run.addArg("pluginval");
            }

            // In headless environments such as CI, GUI tests always fail on Linux so we skip them.
            if (builtin.os.tag == .linux and b.graph.env_map.get("DISPLAY") == null) {
                run.addArg("--skip-gui-tests");
            }

            run.addArgs(&.{
                "--validate",
                b.getInstallPath(install_dir, "Floe.vst3"),
            });
            run.expectExitCode(0);

            run.step.dependOn(vst3_final_step.?);
            pluginval.dependOn(&run.step);
        }

        // Valgrind test
        if (tests_compile_step != null and !sanitize_thread) {
            const run = b.addSystemCommand(&.{
                "valgrind",
                "--leak-check=full",
                "--fair-sched=yes",
                "--num-callers=25",
                "--gen-suppressions=all",
                b.fmt("--suppressions={s}", .{b.pathFromRoot("valgrind.supp")}),
                "--error-exitcode=1",
                "--exit-on-first-error=no",
            });
            run.addArtifactArg(tests_compile_step.?);
            run.addArgs(&.{
                "--log-level=debug",
                b.fmt("--junit-xml-output-path={s}/results-valgrind.junit.xml", .{
                    b.pathFromRoot(constants.floe_cache_relative),
                }),
            });
            run.expectExitCode(0);

            valgrind.dependOn(&run.step);
        }

        // clang-tidy
        {
            const clang_tidy_step = ClangTidyStep.create(b, target);
            clang_tidy_step.step.dependOn(&join_compile_commands.step);
            clang_tidy.dependOn(&clang_tidy_step.step);
        }
    }

    {
        const reuse = b.step("check-reuse", "Check compliance with Reuse licensing spec");
        const run = std_extras.createCommandWithStdoutToStderr(b, null, "run reuse");
        run.addArgs(&.{ "reuse", "lint" });
        reuse.dependOn(&run.step);
        all_tests.dependOn(reuse);
    }

    {
        const check_format = b.step("check-format", "Check code formatting with clang-format");
        const check_format_step = CheckFormatStep.create(b);
        check_format.dependOn(&check_format_step.step);
        all_tests.dependOn(check_format);
    }

    {
        const check_spelling = b.step("check-spelling", "Check spelling with hunspell");
        const check_spelling_step = CheckSpellingStep.create(b);
        check_spelling.dependOn(&check_spelling_step.step);
        all_tests.dependOn(check_spelling);
    }

    {
        const check_links = b.step("check-links", "Check links with lychee");
        const check_links_step = CheckLinksStep.create(b);
        check_links.dependOn(&check_links_step.step);
        all_tests.dependOn(check_links);
    }

    {
        const format = b.step("format", "Format code with clang-format");
        const format_step = FormatStep.create(b);
        format.dependOn(&format_step.step);
        all_tests.dependOn(format);
    }

    build_context.compile_all_step.dependOn(b.getInstallStep());
    b.default_step = build_context.compile_all_step;
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

const FormatStep = struct {
    step: std.Build.Step,
    builder: *std.Build,

    pub fn create(builder: *std.Build) *FormatStep {
        const self = builder.allocator.create(FormatStep) catch @panic("OOM");
        self.* = FormatStep{
            .step = std.Build.Step.init(.{
                .id = .custom,
                .name = "clang-format",
                .owner = builder,
                .makeFn = make,
            }),
            .builder = builder,
        };
        return self;
    }

    fn make(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
        _ = options;
        const self: *FormatStep = @fieldParentPtr("step", step);

        const source_files = try std_extras.findSourceFiles(self.builder.allocator, .{
            .dir_path = "src",
            .extensions = &.{ ".cpp", ".hpp", ".h", ".mm" },
            .exclude_folders = &.{},
        });

        var args = std.ArrayList([]const u8).init(self.builder.allocator);

        try args.append("clang-format");
        try args.append("-i");

        for (source_files) |file| {
            const full_path = self.builder.pathJoin(&.{ "src", file });
            try args.append(full_path);
        }

        _ = try step.evalChildProcess(args.items);
    }
};

const ClangTidyStep = struct {
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
        const cdb_contents = try std.fs.cwd().readFileAlloc(
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

        // hunspell doesn't do anything fancy at all, it just checks each word for spelling. It means we get lots of
        // false positives, but I think it's still worth it. We can just add words to ignored-spellings.dic.
        // In vim, use :sort u to remove duplicates.
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

const CheckLinksStep = struct {
    step: std.Build.Step,
    builder: *std.Build,

    pub fn create(builder: *std.Build) *CheckLinksStep {
        const self = builder.allocator.create(CheckLinksStep) catch @panic("OOM");
        self.* = CheckLinksStep{
            .step = std.Build.Step.init(.{
                .id = .custom,
                .name = "check-links",
                .owner = builder,
                .makeFn = make,
            }),
            .builder = builder,
        };
        return self;
    }

    fn make(step: *std.Build.Step, options: std.Build.Step.MakeOptions) !void {
        _ = options;
        const self: *CheckLinksStep = @fieldParentPtr("step", step);

        const docusaurus_localhost = "http://localhost:3000";

        var args = std.ArrayList([]const u8).init(self.builder.allocator);
        defer args.deinit();

        try args.append("lychee");

        // For some reason creativecommons links return 403 via lychee, so we exclude them.
        try args.append("--exclude");
        try args.append("https://creativecommons.org/licenses/by/2.0");
        try args.append("--exclude");
        try args.append("https://creativecommons.org/licenses/by/4.0");
        try args.append("--exclude");
        try args.append("https://creativecommons.org/licenses/by-sa/4.0");

        // If our website is being served locally (Docusaurus dev server), we can check links against the
        // local version.
        {
            var client = std.http.Client{ .allocator = self.builder.allocator };
            defer client.deinit();

            var response_body = std.ArrayList(u8).init(self.builder.allocator);
            defer response_body.deinit();

            var localhost_running = false;
            if (client.fetch(.{
                .location = .{ .url = docusaurus_localhost },
                .method = .HEAD,
                .response_storage = .{ .dynamic = &response_body },
            })) |fetch_result| {
                if (fetch_result.status == .ok) {
                    localhost_running = true;
                }
            } else |_| {
                localhost_running = false;
            }

            if (localhost_running) {
                try args.append("--remap");
                try args.append("https://floe.audio " ++ docusaurus_localhost);
                try args.append("--base");
                try args.append(docusaurus_localhost);
            }
        }

        try args.append("website");
        try args.append("readme.md");

        _ = try step.evalChildProcess(args.items);
    }
};
