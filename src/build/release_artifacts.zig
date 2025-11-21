// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");
const builtin = @import("builtin");

const constants = @import("constants.zig");
const std_extras = @import("std_extras.zig");
const configure_binaries = @import("configure_binaries.zig");

pub fn makeRelease(args: struct {
    b: *std.Build,
    target: std.Target,
    windows_installer: ?std.Build.LazyPath,
    au: ?configure_binaries.ConfiguredPlugin,
    vst3: ?configure_binaries.ConfiguredPlugin,
    clap: ?configure_binaries.ConfiguredPlugin,
    packager: ?std.Build.LazyPath,
    version: []const u8,
}) *std.Build.Step {
    const b = args.b;

    const target_triple = std_extras.archAndOsPair(args.target);

    const step = b.allocator.create(std.Build.Step) catch @panic("OOM");
    step.* = std.Build.Step.init(.{
        .id = .custom,
        .name = b.fmt("make {s} release artifacts", .{target_triple.constSlice()}),
        .owner = b,
    });

    const release_dir = b.fmt("release-{s}", .{target_triple.constSlice()});

    switch (args.target.os.tag) {
        .windows => {
            // Installer
            {
                const installer = args.windows_installer orelse
                    @panic("windows installer path must be provided for windows releases");

                const generated_zip = createZipCommand(b, &.{installer});

                const install = b.addInstallFile(generated_zip, b.fmt("{s}{c}Floe-Installer-v{s}-Windows.zip", .{
                    release_dir,
                    std.fs.path.sep,
                    args.version,
                }));
                step.dependOn(&install.step);
            }

            // Manual-install plugins
            {
                const vst3 = args.vst3 orelse @panic("VST3 plugin must be provided for windows releases");
                const clap = args.clap orelse @panic("CLAP plugin must be provided for windows releases");

                const write = b.addWriteFiles();
                const readme = write.add(
                    "readme.txt",
                    manualInstallReadme(b.allocator, .{ .os_name = "Windows", .version = args.version }),
                );

                const generated_zip = createZipCommand(b, &.{
                    vst3.plugin_path,
                    clap.plugin_path,
                    readme,
                });

                const install = b.addInstallFile(generated_zip, b.fmt("{s}{c}Floe-Manual-Install-v{s}-Windows.zip", .{
                    release_dir,
                    std.fs.path.sep,
                    args.version,
                }));
                step.dependOn(&install.step);
            }

            // Packager
            {
                const packager = args.packager orelse
                    @panic("packager binary must be provided for windows releases");
                const generated_zip = createZipCommand(b, &.{packager});
                const install = b.addInstallFile(generated_zip, b.fmt("{s}{c}Floe-Packager-v{s}-Windows.zip", .{
                    release_dir,
                    std.fs.path.sep,
                    args.version,
                }));
                step.dependOn(&install.step);
            }
        },
        .macos => {},
        .linux => {
            // CLAP
            {
                const clap = args.clap orelse @panic("CLAP plugin must be provided for linux releases");

                const tar_cmd = b.addSystemCommand(&.{ "tar", "-czf" });
                const generated_tar = tar_cmd.addOutputFileArg("clap.tar.gz");
                clap.addToRunStepArgs(tar_cmd);

                const install = b.addInstallFile(generated_tar, b.fmt("{s}{c}Floe-CLAP-v{s}-Linux.tar.gz", .{
                    release_dir,
                    std.fs.path.sep,
                    args.version,
                }));
                step.dependOn(&install.step);
            }

            // VST3
            {
                const vst3 = args.vst3 orelse @panic("VST3 plugin must be provided for linux releases");

                const tar_cmd = b.addSystemCommand(&.{ "tar", "-czf" });
                const generated_tar = tar_cmd.addOutputFileArg("vst3.tar.gz");
                vst3.addToRunStepArgs(tar_cmd);

                const install = b.addInstallFile(generated_tar, b.fmt("{s}{c}Floe-VST3-v{s}-Linux.tar.gz", .{
                    release_dir,
                    std.fs.path.sep,
                    args.version,
                }));
                step.dependOn(&install.step);
            }

            // Packager
            {
                const packager = args.packager orelse @panic("packager binary must be provided for linux releases");
                const tar_cmd = b.addSystemCommand(&.{ "tar", "-czf" });
                const generated_tar = tar_cmd.addOutputFileArg("packager.tar.gz");
                tar_cmd.addFileArg(packager);

                const install = b.addInstallFile(generated_tar, b.fmt("{s}{c}Floe-Packager-v{s}-Linux.tar.gz", .{
                    release_dir,
                    std.fs.path.sep,
                    args.version,
                }));
                step.dependOn(&install.step);
            }
        },
        else => @panic("unsupported target OS"),
    }

    return step;
}

fn createZipCommand(b: *std.Build, input_files: []const std.Build.LazyPath) std.Build.LazyPath {
    if (builtin.os.tag != .windows) {
        const zip_cmd = b.addSystemCommand(&.{ "zip", "-r" });
        const out_zip = zip_cmd.addOutputFileArg("installer.zip");
        for (input_files) |file|
            zip_cmd.addFileArg(file);
        return out_zip;
    } else {
        // powershell.exe is most ubiquitous, but its format for the -Path argument doesn't work with
        // LazyPaths since it requires quoted paths separated with commas. We have no way of telling the Run step
        // about this quotes+commas format and it know how to properly depend and resolve LazyPaths.
        // Instead, we use 7z which is often installed (e.g. Github Actions Windows runners have it).
        const zip_cmd = b.addSystemCommand(&.{ "7z", "a", "-tzip" });

        const out_zip = zip_cmd.addOutputFileArg("installer.zip");

        for (input_files) |file|
            zip_cmd.addFileArg(file);

        return out_zip;
    }
}

fn manualInstallReadme(allocator: std.mem.Allocator, args: struct {
    os_name: []const u8,
    version: []const u8,
}) []const u8 {
    return std.fmt.allocPrint(allocator,
        \\These are the manual-install {[os_name]s} plugin files for Floe version {[version]s}.
        \\
        \\It's normally easier to use the installer instead of these manual-install files.
        \\The installer is a separate download to this.
        \\
        \\Installation instructions: https://floe.audio/
    , args) catch @panic("OOM");
}
