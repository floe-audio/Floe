// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");
const builtin = @import("builtin");

const constants = @import("constants.zig");
const std_extras = @import("std_extras.zig");

pub const PluginType = enum { clap, vst3, au };

pub const CodesignInfo = struct {
    description: []const u8,
};

pub fn maybeAddWindowsCodesign(
    compile_step: *std.Build.Step.Compile,
    sign_info: CodesignInfo,
) std.Build.LazyPath {
    const binary_path = compile_step.getEmittedBin();

    if (compile_step.rootModuleTarget().os.tag != .windows) {
        return binary_path;
    }

    const b = compile_step.step.owner;

    const cert_pfx = std_extras.validEnvVar(
        b,
        "WINDOWS_CODESIGN_CERT_PFX",
        "skipping codesigning",
        true,
    ) orelse return binary_path;

    const cert_password = std_extras.validEnvVar(
        b,
        "WINDOWS_CODESIGN_CERT_PFX_PASSWORD",
        "skipping codesigning",
        false,
    ) orelse return binary_path;

    const cert_lazy_path = b.addWriteFiles().add("cert.pfx", cert_pfx);

    // We use osslsigncode instead of signtool.exe because it allows us to do this process cross-platform.
    const cmd = b.addSystemCommand(&.{ "osslsigncode", "sign" });

    cmd.addArg("-pkcs12");
    cmd.addFileArg(cert_lazy_path);

    cmd.addArg("-pass");
    cmd.addArg(cert_password);

    cmd.addArgs(&.{ "-n", sign_info.description });
    cmd.addArgs(&.{ "-i", constants.floe_homepage_url });
    cmd.addArgs(&.{ "-t", "http://timestamp.sectigo.com" });

    cmd.addArg("-in");
    cmd.addFileArg(binary_path);

    cmd.addArg("-out");
    const signed_output_lazy_path = cmd.addOutputFileArg(compile_step.out_filename);

    return signed_output_lazy_path;
}

// These helpers are for dealing with Nix-related issues when we are building inside a Nix devshell, but the host is
// not NixOS (such as how we use Github Actions Ubuntu runners).
const NixHelper = struct {
    binary_patch_needed: ?bool = null,

    // Executables (as opposed to shared libraries) will default to being interpreted by the system's dynamic
    // linker (often /lib64/ld-linux-x86-64.so.2). This can cause problems relating to using different versions
    // of glibc at the same time. So we use patchelf to force using the same ld.
    pub fn maybePatchElfExecutable(self: *NixHelper, compile_step: *std.Build.Step.Compile) std.Build.LazyPath {
        const b = compile_step.step.owner;

        if (!self.patchElfNeeded(b)) return compile_step.getEmittedBin();

        const dyn_linker = b.graph.env_map.get("FLOE_DYNAMIC_LINKER") orelse return compile_step.getEmittedBin();

        const patch_step = b.addSystemCommand(&.{
            "patchelf",
            "--set-interpreter",
            dyn_linker,
            "--output",
        });
        const result = patch_step.addOutputFileArg(compile_step.name);
        patch_step.addFileArg(compile_step.getEmittedBin());

        return result;
    }

    // The dynamic linker can normally find the libraries inside the nix devshell except when we are running
    // an external program that hosts our audio plugin. For example clap-validator fails to load our clap with
    // the error 'libGL.so.1 cannot be found'. Possible this is due to LD_LIBRARY_PATH not being available to
    // the external program.
    // As well as LD_LIBRARY_PATH, dynamic linkers also look at the rpath of the binary (which is embedded in
    // the binary itself) to find the libraries. So that's what we use patchelf for here.
    pub fn maybePatchElfSharedLibrary(
        self: *NixHelper,
        compile_step: *std.Build.Step.Compile,
    ) std.Build.LazyPath {
        const b = compile_step.step.owner;

        if (!self.patchElfNeeded(b)) return compile_step.getEmittedBin();

        const rpath = b.graph.env_map.get("FLOE_RPATH") orelse return compile_step.getEmittedBin();

        const patch_step = b.addSystemCommand(&.{
            "patchelf",
            "--set-rpath",
            rpath,
            "--output",
        });
        const result = patch_step.addOutputFileArg("patched_plugin");
        patch_step.addFileArg(compile_step.getEmittedBin());

        return result;
    }

    fn patchElfNeeded(self: *NixHelper, b: *std.Build) bool {
        if (self.binary_patch_needed == null)
            self.binary_patch_needed = builtin.os.tag == .linux and inNixShell(b) and !hostIsNixOS();

        return self.binary_patch_needed.?;
    }

    fn inNixShell(b: *std.Build) bool {
        return b.graph.env_map.get("IN_NIX_SHELL") != null;
    }

    fn hostIsNixOS() bool {
        std.fs.accessAbsolute("/etc/NIXOS", .{}) catch return false;
        return true;
    }
};

pub var nix_helper: NixHelper = .{};

pub const ConfiguredPlugin = struct {
    plugin_path: std.Build.LazyPath, // Path to the plugin (DSO or bundle folder).
    file_name: []const u8,
    is_dir: bool, // Whether the plugin_path is a directory.
    install_step: *std.Build.Step, // Step that installs the plugin to the install prefix.

    pub fn addToRunStepArgsWithPrefix(
        self: *const ConfiguredPlugin,
        run_step: *std.Build.Step.Run,
        prefix: []const u8,
    ) void {
        if (self.is_dir) {
            run_step.addPrefixedDirectoryArg(prefix, self.plugin_path);
        } else {
            run_step.addPrefixedFileArg(prefix, self.plugin_path);
        }
    }

    pub fn addToRunStepArgs(self: *const ConfiguredPlugin, run_step: *std.Build.Step.Run) void {
        self.addToRunStepArgsWithPrefix(run_step, "");
    }
};

// The generated binary may not have the right name, extension or folder structure to be a valid
// audio plugin. This function performs the necessary setup.
pub fn addConfiguredPlugin(
    b: *std.Build,
    plugin_type: PluginType,
    compile_step: *std.Build.Step.Compile,
    codesign: ?CodesignInfo,
) ConfiguredPlugin {
    switch (compile_step.rootModuleTarget().os.tag) {
        .linux => {
            const path = nix_helper.maybePatchElfSharedLibrary(compile_step);

            // Path to the actual plugin (whether a binary or a bundle folder).
            const plugin_path = switch (plugin_type) {
                .vst3 => "Floe.vst3",
                .clap => "Floe.clap",
                .au => @panic("AU not supported on Linux"),
            };

            var is_dir: bool = false;

            // Path to the binary shared library.
            const binary_path = b.fmt("{s}{s}", .{
                plugin_path, switch (plugin_type) {
                    .vst3 => blk: {
                        // For VST3, we need to build a bundle folder structure for the plugin rather than just a
                        // simple binary.
                        is_dir = true;
                        std.debug.assert(compile_step.root_module.resolved_target.?.result.cpu.arch == .x86_64);
                        break :blk "/Contents/x86_64-linux/Floe.so";
                    },
                    .clap => "",
                    .au => @panic("AU not supported on Linux"),
                },
            });

            var result_path = b.addWriteFiles().addCopyFile(path, binary_path);

            // The 'result' LazyPath points to the file inside the generated directory. We need it to point to the
            // plugin (which is a folder in the case of VST3).
            result_path.generated.sub_path = plugin_path;

            const install_step = blk: {
                // Subdirectory inside the prefix where the plugin will be installed. These subdirs allow for HOME to
                // be used and the plugins will be correctly found by hosts.
                const install_subdir = switch (plugin_type) {
                    .vst3 => ".vst3",
                    .clap => ".clap",
                    .au => @panic("AU not supported on Linux"),
                };

                if (is_dir) {
                    const install = b.addInstallDirectory(.{
                        .source_dir = result_path,
                        .install_dir = .prefix,
                        .install_subdir = b.pathJoin(&.{ install_subdir, plugin_path }),
                    });
                    b.getInstallStep().dependOn(&install.step);
                    break :blk &install.step;
                } else {
                    const install = b.addInstallFile(result_path, b.pathJoin(&.{ install_subdir, plugin_path }));
                    b.getInstallStep().dependOn(&install.step);
                    break :blk &install.step;
                }
            };

            return .{
                .plugin_path = result_path,
                .file_name = plugin_path,
                .is_dir = is_dir,
                .install_step = install_step,
            };
        },
        .windows => {
            std.debug.assert(codesign != null);
            const path = maybeAddWindowsCodesign(compile_step, codesign.?);

            // On Windows, plugins are just single files. So we don't need to worry about bundle structures.
            const plugin_path = switch (plugin_type) {
                .vst3 => "Floe.vst3",
                .clap => "Floe.clap",
                .au => @panic("AU not supported on Windows"),
            };

            const result_path = b.addWriteFiles().addCopyFile(path, plugin_path);

            // Install.
            const install = blk: {
                // This subfolder allows for the plugin to be installed to either %COMMONPROGRAMFILES% or
                // %LOCALAPPDATA%\Programs\Common.
                const install_subdir = switch (plugin_type) {
                    .vst3 => "VST3",
                    .clap => "CLAP",
                    .au => @panic("AU not supported on Windows"),
                };

                const install = b.addInstallFile(result_path, b.pathJoin(&.{ install_subdir, plugin_path }));
                b.getInstallStep().dependOn(&install.step);
                break :blk &install.step;
            };

            return .{
                .plugin_path = result_path,
                .file_name = plugin_path,
                .is_dir = false,
                .install_step = install,
            };
        },
        .macos => {
            const install_path = switch (plugin_type) {
                .vst3 => "Floe.vst3",
                .clap => "Floe.clap",
                .au => "Floe.component",
            };
            const bundle_extension = switch (plugin_type) {
                .vst3 => "vst3",
                .clap => "clap",
                .au => "component",
            };

            const write = b.addWriteFiles();

            var result_path: std.Build.LazyPath = undefined;

            // Binary
            {
                result_path = write.addCopyFile(
                    compile_step.getEmittedBin(),
                    b.fmt("{s}/Contents/MacOS/Floe", .{install_path}),
                );

                // Generate dSYM for debugging.
                // NOTE(Sam): does this need a dependsOn?
                const cmd = b.addSystemCommand(&.{"dsymutil"});
                cmd.addFileArg(result_path);

                // The result points to the binary, for plugins, we want it to point to the bundle since that's
                // what hosts/validators use.
                result_path.generated.sub_path = install_path;
            }

            // PkgInfo
            _ = write.add(b.fmt("{s}/Contents/PkgInfo", .{install_path}), "BNDL????");

            // Info.plist
            {
                const version = compile_step.version.?;

                const file_content = std.fmt.allocPrint(b.allocator,
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
                    .executable_name = "Floe",
                    .bundle_identifier = b.fmt("{s}.{s}", .{
                        constants.floe_clap_id,
                        bundle_extension,
                    }),
                    .bundle_name = b.fmt("Floe.{s}", .{bundle_extension}),
                    .major = version.major,
                    .minor = version.minor,
                    .patch = version.patch,
                    .copyright = constants.floe_copyright,
                    .min_macos_version = constants.min_macos_version,

                    // factoryFunction has 'Factory' appended to it because that's what the AUSDK_COMPONENT_ENTRY
                    // macro adds. name uses the format Author: Name because otherwise Logic shows the developer as
                    // the 4-character manufacturer code.
                    .audio_unit_dict = if (plugin_type == .au)
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
                            \\                <string>{[manufacturer]s}</string>
                            \\                <key>subtype</key>
                            \\                <string>{[subtype]s}</string>
                            \\                <key>type</key>
                            \\                <string>{[type]s}</string>
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
                            .version_packed = (version.major << 16) | (version.minor << 8) | version.patch,
                            .manufacturer = constants.floe_au_manufacturer_code,
                            .subtype = constants.floe_au_subtype,
                            .type = constants.floe_au_type,
                        })
                    else
                        "",
                }) catch @panic("OOM");

                _ = write.add(b.fmt("{s}/Contents/Info.plist", .{install_path}), file_content);
            }

            // Install.
            const install = blk: {
                // Subpath ready for a --prefix of /Library/Audio/Plug-Ins, or the HOME equivalent.
                const install = b.addInstallDirectory(.{
                    .source_dir = result_path,
                    .install_dir = .prefix,
                    .install_subdir = b.pathJoin(&.{ switch (plugin_type) {
                        .vst3 => "VST3",
                        .clap => "CLAP",
                        .au => "Components",
                    }, install_path }),
                });
                b.getInstallStep().dependOn(&install.step);
                break :blk &install.step;
            };

            return .{
                .plugin_path = result_path,
                .file_name = install_path,
                .is_dir = true,
                .install_step = install,
            };
        },
        else => @panic("unsupported OS"),
    }
}
