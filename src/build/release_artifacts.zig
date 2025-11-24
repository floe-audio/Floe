// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");
const builtin = @import("builtin");

const constants = @import("constants.zig");
const std_extras = @import("std_extras.zig");
const configure_binaries = @import("configure_binaries.zig");

pub const Artifact = struct {
    path: std.Build.LazyPath,
    out_filename: []const u8,
};

pub const Artifacts = struct {
    windows_installer: ?Artifact,
    au: ?configure_binaries.ConfiguredPlugin,
    vst3: ?configure_binaries.ConfiguredPlugin,
    clap: ?configure_binaries.ConfiguredPlugin,
    packager: ?Artifact,
};

pub fn makeRelease(
    b: *std.Build,
    archiver: *std.Build.Step.Compile,
    version: []const u8,
    target: std.Target,
    args: Artifacts,
) *std.Build.Step {
    const target_triple = std_extras.archAndOsPair(target);

    const step = b.allocator.create(std.Build.Step) catch @panic("OOM");
    step.* = std.Build.Step.init(.{
        .id = .custom,
        .name = b.fmt("make {s} release artifacts", .{target_triple.constSlice()}),
        .owner = b,
    });

    const release_dir = b.fmt("release-{s}", .{target_triple.constSlice()});

    switch (target.os.tag) {
        .windows => {
            // Installer
            {
                const installer = args.windows_installer orelse
                    @panic("windows installer path must be provided for windows releases");

                const generated_zip = createArchiveCommand(b, archiver, .zip, &[_]FileToArchive{
                    .{
                        .path = installer.path,
                        .path_in_archive = installer.out_filename,
                        .is_dir = false,
                    },
                });

                const install = b.addInstallFile(generated_zip, b.fmt("{s}{c}Floe-Installer-v{s}-Windows.zip", .{
                    release_dir,
                    std.fs.path.sep,
                    version,
                }));
                step.dependOn(&install.step);
            }

            // Manual-install plugins
            {
                const vst3 = args.vst3 orelse @panic("VST3 plugin must be provided for windows releases");
                const clap = args.clap orelse @panic("CLAP plugin must be provided for windows releases");

                const readme = b.addWriteFiles().add(
                    "readme.txt",
                    manualInstallReadme(b.allocator, .{ .os_name = "Windows", .version = version }),
                );

                const generated_zip = createArchiveCommand(b, archiver, .zip, &[_]FileToArchive{
                    .{ .path = vst3.plugin_path, .path_in_archive = vst3.file_name, .is_dir = vst3.is_dir },
                    .{ .path = clap.plugin_path, .path_in_archive = clap.file_name, .is_dir = clap.is_dir },
                    .{ .path = readme, .path_in_archive = "readme.txt", .is_dir = false },
                });

                const install = b.addInstallFile(generated_zip, b.fmt("{s}{c}Floe-Manual-Install-v{s}-Windows.zip", .{
                    release_dir,
                    std.fs.path.sep,
                    version,
                }));
                step.dependOn(&install.step);
            }

            // Packager
            {
                const packager = args.packager orelse
                    @panic("packager binary must be provided for windows releases");
                const generated_zip = createArchiveCommand(b, archiver, .zip, &[_]FileToArchive{
                    .{ .path = packager.path, .path_in_archive = packager.out_filename, .is_dir = false },
                });
                const install = b.addInstallFile(generated_zip, b.fmt("{s}{c}Floe-Packager-v{s}-Windows.zip", .{
                    release_dir,
                    std.fs.path.sep,
                    version,
                }));
                step.dependOn(&install.step);
            }
        },
        .macos => {
            const arch_name_for_file = switch (target.cpu.arch) {
                .aarch64 => "Apple-Silicon",
                .x86_64 => "Intel",
                else => @panic("unsupported macOS architecture"),
            };

            // Packager
            {
                const packager = args.packager orelse @panic("packager binary must be provided for macOS releases");
                const codesigned_packager = maybeAddMacosCodesign(b, packager.path, .{
                    .out_filename = packager.out_filename,
                    .is_bundle = false,
                    .entitlements = false,
                    .cert_type = .application,
                });
                const zip_file = createArchiveCommand(b, archiver, .zip, &[_]FileToArchive{
                    .{ .path = codesigned_packager, .path_in_archive = packager.out_filename, .is_dir = false },
                });
                const notarized_zip = maybeMacosNotarise(b, zip_file, .{
                    .out_filename = packager.out_filename,
                    .is_bundle = false,
                    .staple = false,
                });

                const install = b.addInstallFile(notarized_zip, b.fmt("{s}{c}Floe-Packager-v{s}-macOS-{s}.zip", .{
                    release_dir,
                    std.fs.path.sep,
                    version,
                    arch_name_for_file,
                }));

                step.dependOn(&install.step);
            }

            const au_plugin = args.au orelse @panic("AU plugin must be provided for macOS releases");
            const vst3_plugin = args.vst3 orelse @panic("VST3 plugin must be provided for macOS releases");
            const clap_plugin = args.clap orelse @panic("CLAP plugin must be provided for macOS releases");

            const au = macosCodesignAndNotarise(b, au_plugin);
            const vst3 = macosCodesignAndNotarise(b, vst3_plugin);
            const clap = macosCodesignAndNotarise(b, clap_plugin);

            // Manual-install
            {
                const readme = b.addWriteFiles().add(
                    "readme.txt",
                    manualInstallReadme(b.allocator, .{ .os_name = "macOS", .version = version }),
                );

                const zip_file = createArchiveCommand(b, archiver, .zip, &[_]FileToArchive{
                    .{ .path = readme, .path_in_archive = "readme.txt", .is_dir = false },
                    .{ .path = au, .path_in_archive = au_plugin.file_name, .is_dir = au_plugin.is_dir },
                    .{ .path = vst3, .path_in_archive = vst3_plugin.file_name, .is_dir = vst3_plugin.is_dir },
                    .{ .path = clap, .path_in_archive = clap_plugin.file_name, .is_dir = clap_plugin.is_dir },
                });

                const install = b.addInstallFile(zip_file, b.fmt("{s}{c}Floe-Manual-Install-v{s}-macOS-{s}.zip", .{
                    release_dir,
                    std.fs.path.sep,
                    version,
                    arch_name_for_file,
                }));

                step.dependOn(&install.step);
            }

            if (builtin.os.tag == .macos) {
                const PkgConfig = struct {
                    plugin_path: std.Build.LazyPath,
                    install_folder: []const u8,
                    title: []const u8,
                    description: []const u8,
                    identifier: []const u8,
                    pkg_name: []const u8,
                };

                const pkg_configs = [_]PkgConfig{
                    .{
                        .plugin_path = clap,
                        .install_folder = "/Library/Audio/Plug-Ins/CLAP",
                        .title = "Floe CLAP",
                        .description = "CLAP format of the Floe plugin",
                        .identifier = "com.Floe.clap",
                        .pkg_name = "com.Floe.clap.pkg",
                    },
                    .{
                        .plugin_path = au,
                        .install_folder = "/Library/Audio/Plug-Ins/Components",
                        .title = "Floe Audio Unit (AUv2)",
                        .description = "Audio Unit (v2) format of the Floe plugin",
                        .identifier = "com.Floe.component",
                        .pkg_name = "com.Floe.component.pkg",
                    },
                    .{
                        .plugin_path = vst3,
                        .install_folder = "/Library/Audio/Plug-Ins/VST3",
                        .title = "Floe VST3",
                        .description = "VST3 format of the Floe plugin",
                        .identifier = "com.Floe.vst3",
                        .pkg_name = "com.Floe.vst3.pkg",
                    },
                };

                const productbuild_dir = b.addWriteFiles();

                for (pkg_configs) |pkg_config| {
                    // Create the component pkg.
                    const pkg_cmd = b.addSystemCommand(&.{"pkgbuild"});

                    pkg_cmd.addArgs(&.{ "--identifier", pkg_config.identifier });
                    pkg_cmd.addArgs(&.{ "--version", version });

                    pkg_cmd.addArg("--component");
                    pkg_cmd.addDirectoryArg(pkg_config.plugin_path);

                    pkg_cmd.addArgs(&.{ "--install-location", pkg_config.install_folder });

                    const output_pkg = pkg_cmd.addOutputFileArg(pkg_config.pkg_name);

                    // Copy the component pkg into the productbuild dir.
                    _ = productbuild_dir.addCopyFile(output_pkg, pkg_config.pkg_name);
                }

                const welcome_file_name = "welcome.txt";
                const dist_xml_file_name = "distribution.xml";
                const resources_subdir = "resources";

                _ = productbuild_dir.add(b.pathJoin(&.{ resources_subdir, welcome_file_name }), "This application will install Floe on your computer. You will be able to select which types of audio plugin format you would like to install. Please note that sample libraries are separate: this installer just installs the Floe engine.");

                _ = productbuild_dir.add(
                    dist_xml_file_name,
                    b.fmt(
                        \\<?xml version="1.0" encoding="utf-8"?>
                        \\<installer-gui-script minSpecVersion="1">
                        \\    <title>Floe v{[version]s}</title>
                        \\    <welcome file="{[welcome_file_name]s}" mime-type="text/plain"/>
                        \\    <options customize="always" require-scripts="false" hostArchitectures="{[host_arch]s}"/>
                        \\    <os-version min="{[os_min_version]s}" /> 
                        \\    {[choices]s}
                        \\    <choices-outline>
                        \\        {[choices_outline]s}
                        \\    </choices-outline>
                        \\</installer-gui-script>
                    , .{
                        .version = version,
                        .welcome_file_name = welcome_file_name,
                        .host_arch = switch (target.cpu.arch) {
                            .aarch64 => "arm64",
                            .x86_64 => "x86_64",
                            else => @panic("unsupported macOS architecture"),
                        },
                        .os_min_version = constants.min_macos_version,
                        .choices = blk: {
                            var result = std.ArrayList(u8).init(b.allocator);

                            for (pkg_configs) |pkg_config| {
                                std.fmt.format(result.writer(),
                                    \\<choice id="{[id]s}" title="{[title]s}" description="{[description]s}">
                                    \\    <pkg-ref id="{[id]s}" version="{[version]s}">{[pkg_name]s}</pkg-ref>
                                    \\</choice>
                                , .{
                                    .id = pkg_config.identifier,
                                    .title = pkg_config.title,
                                    .description = pkg_config.description,
                                    .version = version,
                                    .pkg_name = pkg_config.pkg_name,
                                }) catch @panic("OOM");
                            }

                            break :blk result.toOwnedSlice() catch @panic("OOM");
                        },
                        .choices_outline = blk: {
                            var result = std.ArrayList(u8).init(b.allocator);
                            for (pkg_configs) |pkg_config| {
                                std.fmt.format(result.writer(),
                                    \\<line choice="{s}"/>
                                , .{pkg_config.identifier}) catch @panic("OOM");
                            }
                            break :blk result.toOwnedSlice() catch @panic("OOM");
                        },
                    }),
                );

                const productbuild_cmd = b.addSystemCommand(&.{"productbuild"});
                productbuild_cmd.setCwd(productbuild_dir.getDirectory());

                productbuild_cmd.addArgs(&.{ "--distribution", dist_xml_file_name });
                productbuild_cmd.addArgs(&.{ "--resources", resources_subdir });
                productbuild_cmd.addArgs(&.{ "--package-path", "." });

                const final_pkg = productbuild_cmd.addOutputFileArg("installer.pkg");

                const signed = maybeAddMacosCodesign(b, final_pkg, .{
                    .out_filename = "signed-installer.pkg",
                    .is_bundle = false,
                    .entitlements = false,
                    .cert_type = .installer,
                });

                const notarised = maybeMacosNotarise(b, signed, .{
                    .out_filename = "notarised-installer.pkg",
                    .is_bundle = false,
                    .staple = true,
                });

                const zip_file = createArchiveCommand(b, archiver, .zip, &[_]FileToArchive{
                    .{
                        .path = notarised,
                        .path_in_archive = b.fmt("Floe-Installer-v{s}.pkg", .{version}),
                        .is_dir = false,
                    },
                });

                const install = b.addInstallFile(zip_file, b.fmt("{s}{c}Floe-Installer-v{s}-macOS-{s}.zip", .{
                    release_dir,
                    std.fs.path.sep,
                    version,
                    arch_name_for_file,
                }));

                step.dependOn(&install.step);
            } else {
                std.log.warn("IMPORTANT: building macOS package installers requires a macOS host, no installer will be created", .{});
            }
        },
        .linux => {
            // CLAP
            {
                const clap = args.clap orelse @panic("CLAP plugin must be provided for linux releases");

                const tar = createArchiveCommand(b, archiver, .tar_gz, &[_]FileToArchive{
                    .{ .path = clap.plugin_path, .path_in_archive = clap.file_name, .is_dir = clap.is_dir },
                });

                const install = b.addInstallFile(tar, b.fmt("{s}{c}Floe-CLAP-v{s}-Linux.tar.gz", .{
                    release_dir,
                    std.fs.path.sep,
                    version,
                }));
                step.dependOn(&install.step);
            }

            // VST3
            {
                const vst3 = args.vst3 orelse @panic("VST3 plugin must be provided for linux releases");

                const tar = createArchiveCommand(b, archiver, .tar_gz, &[_]FileToArchive{
                    .{ .path = vst3.plugin_path, .path_in_archive = vst3.file_name, .is_dir = vst3.is_dir },
                });

                const install = b.addInstallFile(tar, b.fmt("{s}{c}Floe-VST3-v{s}-Linux.tar.gz", .{
                    release_dir,
                    std.fs.path.sep,
                    version,
                }));
                step.dependOn(&install.step);
            }

            // Packager
            {
                const packager = args.packager orelse @panic("packager binary must be provided for linux releases");
                const tar = createArchiveCommand(b, archiver, .tar_gz, &[_]FileToArchive{
                    .{ .path = packager.path, .path_in_archive = packager.out_filename, .is_dir = false },
                });

                const install = b.addInstallFile(tar, b.fmt("{s}{c}Floe-Packager-v{s}-Linux.tar.gz", .{
                    release_dir,
                    std.fs.path.sep,
                    version,
                }));
                step.dependOn(&install.step);
            }
        },
        else => @panic("unsupported target OS"),
    }

    return step;
}

fn maybeMacosNotarise(
    b: *std.Build,
    path: std.Build.LazyPath,
    options: struct {
        out_filename: []const u8,
        is_bundle: bool,
        staple: bool, // Stapling is only possible for bundles and PKGs.
    },
) std.Build.LazyPath {
    const api_key_json = std_extras.validEnvVar(
        b,
        "MACOS_APP_STORE_CONNECT_API_KEY_JSON",
        "skipping notarization",
        true,
    ) orelse return path;

    const run_cmd = b.addSystemCommand(&.{
        "rcodesign",
        "notary-submit",
        "--wait",
    });

    if (options.staple)
        run_cmd.addArg("--staple");

    run_cmd.addArg("--api-key-path");
    run_cmd.addFileArg(b.addWriteFiles().add("api_key.json", api_key_json));

    run_cmd.addFileArg(path);

    // rcodesign doesn't have a way to specify a separate output path. So we need to do some
    // trickery here so we can return a new LazyPath.
    const write = b.addWriteFiles();
    write.step.dependOn(&run_cmd.step);

    return if (options.is_bundle)
        write.addCopyDirectory(path, options.out_filename, .{})
    else
        write.addCopyFile(path, options.out_filename);
}

fn maybeAddMacosCodesign(
    b: *std.Build,
    path: std.Build.LazyPath,
    options: struct {
        out_filename: []const u8,
        is_bundle: bool,
        entitlements: bool,
        cert_type: enum { application, installer },
    },
) std.Build.LazyPath {
    const cert_p12 = std_extras.validEnvVar(
        b,
        switch (options.cert_type) {
            .application => "MACOS_DEV_APP_CERTS_P12",
            .installer => "MACOS_DEV_INSTALLER_CERT_P12",
        },
        "skipping codesigning",
        true,
    ) orelse return path;

    const cert_password = std_extras.validEnvVar(
        b,
        switch (options.cert_type) {
            .application => "MACOS_DEV_APP_CERTS_P12_PASSWORD",
            .installer => "MACOS_DEV_INSTALLER_CERT_P12_PASSWORD",
        },
        "skipping codesigning",
        false,
    ) orelse return path;

    const team_id = std_extras.validEnvVar(
        b,
        "MACOS_TEAM_ID",
        "skipping codesigning",
        false,
    ) orelse return path;

    const write = b.addWriteFiles();
    const cert_lazy_path = write.add("cert.pfx", cert_p12);

    const run_cmd = b.addSystemCommand(&.{ "rcodesign", "sign", "--for-notarization" });

    run_cmd.addArg("--p12-file");
    run_cmd.addFileArg(cert_lazy_path);

    run_cmd.addArgs(&.{ "--p12-password", cert_password });

    run_cmd.addArgs(&.{ "--team-name", team_id });

    if (options.entitlements) {
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

    if (!options.is_bundle) {
        run_cmd.addFileArg(path);
    } else {
        run_cmd.addDirectoryArg(path);
    }

    return run_cmd.addOutputFileArg(options.out_filename);
}

fn macosCodesignAndNotarise(b: *std.Build, plugin: configure_binaries.ConfiguredPlugin) std.Build.LazyPath {
    const codesigned = maybeAddMacosCodesign(b, plugin.plugin_path, .{
        .out_filename = plugin.file_name,
        .is_bundle = plugin.is_dir,
        .entitlements = true,
        .cert_type = .application,
    });
    const notarised = maybeMacosNotarise(b, codesigned, .{
        .out_filename = plugin.file_name,
        .is_bundle = plugin.is_dir,
        .staple = true,
    });
    return notarised;
}

const FileToArchive = struct {
    path: std.Build.LazyPath,
    path_in_archive: []const u8,
    is_dir: bool = false,
};

// Use out archiver.zig utility to create archives.
fn createArchiveCommand(
    b: *std.Build,
    archiver: *std.Build.Step.Compile,
    archive_type: enum { zip, tar_gz },
    input_files: []const FileToArchive,
) std.Build.LazyPath {
    const cmd = b.addRunArtifact(archiver);
    cmd.addArg(@tagName(archive_type));

    const out_archive = cmd.addOutputFileArg(b.fmt("archive.{s}", .{switch (archive_type) {
        .zip => "zip",
        .tar_gz => "tar.gz",
    }}));

    // The archiver now accepts pairs of paths: {source_path, path_in_archive}
    for (input_files) |file| {
        if (file.is_dir) {
            cmd.addDirectoryArg(file.path);
        } else {
            cmd.addFileArg(file.path);
        }
        cmd.addArg(file.path_in_archive);
    }

    return out_archive;
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
