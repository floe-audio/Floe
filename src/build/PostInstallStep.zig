// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");
const builtin = @import("builtin");
const std_extras = @import("std_extras.zig");

const constants = @import("constants.zig");

// The post-install step is responsible for any platform-specific renaming or bundling; preparations for
// the binary to be accepted by the OS, etc.
const PostInstallStep = @This();

step: std.Build.Step,
make_macos_bundle: bool,
compile_step: *std.Build.Step.Compile,
optimise: std.builtin.OptimizeMode,

pub fn create(b: *std.Build, options: struct {
    optimise: std.builtin.OptimizeMode,
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
        .optimise = options.optimise,
        .compile_step = options.compile_step,
    };
    post_install_step.step.dependOn(&options.compile_step.step);
    post_install_step.step.dependOn(b.getInstallStep());
    return post_install_step;
}

fn makeMacos(
    optimise: std.builtin.OptimizeMode,
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

    if (optimise != .ReleaseFast) {
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
            try makeMacos(
                self.optimise,
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
