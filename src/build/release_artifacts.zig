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

                const generated_zip = createZipCommand(b, &.{installer}, &.{});

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

                const readme = b.addWriteFiles().add(
                    "readme.txt",
                    manualInstallReadme(b.allocator, .{ .os_name = "Windows", .version = args.version }),
                );

                const generated_zip = createZipCommand(b, &.{
                    vst3.plugin_path,
                    clap.plugin_path,
                    readme,
                }, &.{});

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
                const generated_zip = createZipCommand(b, &.{packager}, &.{});
                const install = b.addInstallFile(generated_zip, b.fmt("{s}{c}Floe-Packager-v{s}-Windows.zip", .{
                    release_dir,
                    std.fs.path.sep,
                    args.version,
                }));
                step.dependOn(&install.step);
            }
        },
        .macos => {
            const arch_name_for_file = switch (args.target.cpu.arch) {
                .aarch64 => "Apple-Silicon",
                .x86_64 => "Intel",
                else => @panic("unsupported macOS architecture"),
            };

            // Packager
            {
                const packager = args.packager orelse @panic("packager binary must be provided for macOS releases");
                const codesigned_packager = maybeAddMacosCodesign(b, packager, false, false);
                const zip_file = createZipCommand(b, &.{codesigned_packager}, &.{});

                const install = b.addInstallFile(zip_file, b.fmt("{s}{c}Floe-Packager-v{s}-macOS-{s}.zip", .{
                    release_dir,
                    std.fs.path.sep,
                    args.version,
                    arch_name_for_file,
                }));

                // If notarization step exists, make the install step depend on it.
                if (maybeMacosNotarise(b, zip_file)) |s| {
                    install.step.dependOn(s);
                }

                step.dependOn(&install.step);
            }

            const au = args.au orelse @panic("AU plugin must be provided for macOS releases");
            const vst3 = args.vst3 orelse @panic("VST3 plugin must be provided for macOS releases");
            const clap = args.clap orelse @panic("CLAP plugin must be provided for macOS releases");

            const signed_au = maybeAddMacosCodesign(b, au.plugin_path, true, true);
            const signed_vst3 = maybeAddMacosCodesign(b, vst3.plugin_path, true, true);
            const signed_clap = maybeAddMacosCodesign(b, clap.plugin_path, true, true);

            const maybe_notarise_au = maybeMacosNotarise(b, signed_au);
            const maybe_notarise_vst3 = maybeMacosNotarise(b, signed_vst3);
            const maybe_notarise_clap = maybeMacosNotarise(b, signed_clap);

            // Manual-install
            {
                const readme = b.addWriteFiles().add(
                    "readme.txt",
                    manualInstallReadme(b.allocator, .{ .os_name = "macOS", .version = args.version }),
                );

                const zip_file = createZipCommand(b, &.{readme}, &.{ signed_au, signed_vst3, signed_clap });

                const install = b.addInstallFile(zip_file, b.fmt("{s}{c}Floe-Manual-Install-v{s}-macOS-{s}.zip", .{
                    release_dir,
                    std.fs.path.sep,
                    args.version,
                    arch_name_for_file,
                }));

                if (maybe_notarise_au) |s| install.step.dependOn(s);
                if (maybe_notarise_vst3) |s| install.step.dependOn(s);
                if (maybe_notarise_clap) |s| install.step.dependOn(s);

                step.dependOn(&install.step);
            }

            if (builtin.os.tag == .macos) {
                const write = b.addWriteFiles();
                // TODO: create pkgs
            } else {
                std.log.warn("IMPORTANT: building macOS package installers requires a macOS machine", .{});
            }
        },
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

// Notarizating doesn't actually change files, it just uploads them and waits for approval.
fn maybeMacosNotarise(
    b: *std.Build,
    path: std.Build.LazyPath,
) ?*std.Build.Step {
    const api_key_json = std_extras.validEnvVar(
        b,
        "MACOS_APP_STORE_CONNECT_API_KEY_JSON",
        "skipping notarization",
        true,
    ) orelse return null;

    const run_cmd = b.addSystemCommand(&.{
        "rcodesign",
        "notary-submit",
        "--wait",
    });

    run_cmd.addArg("--api-key-path");
    run_cmd.addFileArg(b.addWriteFiles().add("api_key.json", api_key_json));

    run_cmd.addFileArg(path);

    return &run_cmd.step;
}

fn maybeAddMacosCodesign(
    b: *std.Build,
    binary_path: std.Build.LazyPath,
    is_dir: bool,
    entitlements: bool,
) std.Build.LazyPath {
    const cert_p12 = std_extras.validEnvVar(
        b,
        "MACOS_DEV_CERTS_P12",
        "skipping codesigning",
        true,
    ) orelse return binary_path;

    const cert_password = std_extras.validEnvVar(
        b,
        "MACOS_DEV_CERTS_P12_PASSWORD",
        "skipping codesigning",
        false,
    ) orelse return binary_path;

    const team_id = std_extras.validEnvVar(
        b,
        "MACOS_TEAM_ID",
        "skipping codesigning",
        false,
    ) orelse return binary_path;

    const write = b.addWriteFiles();
    const cert_lazy_path = write.add("cert.pfx", cert_p12);

    const run_cmd = b.addSystemCommand(&.{ "rcodesign", "sign", "--for-notarization" });

    run_cmd.addArg("--p12-file");
    run_cmd.addFileArg(cert_lazy_path);

    run_cmd.addArgs(&.{ "--p12-password", cert_password });

    run_cmd.addArgs(&.{ "--team-name", team_id });

    if (entitlements) {
        run_cmd.addArg("-e");
        run_cmd.addFileArg(write.add("entitlements.plist",
            \\<?xml version="1.0" encoding="UTF-8"?>
            \\<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
            \\<plist version="1.0">
            \\<dict>
            \\    <key>com.apple.security.app-sandbox</key>
            \\    <true/>
            \\    <key>com.apple.security.files.user-selected.read-write</key>
            \\    <true/>
            \\    <key>com.apple.security.assets.music.read-write</key>
            \\    <true/>
            \\    <key>com.apple.security.files.bookmarks.app-scope</key>
            \\    <true/>
            \\</dict>
            \\</plist>
        ));
    }

    if (!is_dir) {
        run_cmd.addFileArg(binary_path);
    } else {
        run_cmd.addDirectoryArg(binary_path);
    }

    return run_cmd.addOutputFileArg("codesigned_binary");
}

fn createZipCommand(
    b: *std.Build,
    input_files: []const std.Build.LazyPath,
    input_dirs: []const std.Build.LazyPath,
) std.Build.LazyPath {
    if (builtin.os.tag != .windows) {
        const zip_cmd = b.addSystemCommand(&.{ "zip", "-r" });
        const out_zip = zip_cmd.addOutputFileArg("installer.zip");
        for (input_files) |file|
            zip_cmd.addFileArg(file);
        for (input_dirs) |dir|
            zip_cmd.addDirectoryArg(dir);
        return out_zip;
    } else {
        // powershell.exe is most ubiquitous, but its format for the -Path argument doesn't work with
        // LazyPaths since it requires quoted paths separated with commas. We have no way of telling the Run step
        // about this quotes+commas format and so it can't properly depend-on and resolve LazyPaths.
        // Instead, we use 7z which is often installed (e.g. Github Actions Windows runners have it).
        const zip_cmd = b.addSystemCommand(&.{ "7z", "a", "-tzip" });

        const out_zip = zip_cmd.addOutputFileArg("installer.zip");

        for (input_files) |file|
            zip_cmd.addFileArg(file);
        for (input_dirs) |dir|
            zip_cmd.addDirectoryArg(dir);

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
