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

pub fn addWindowsCodesign(
    b: *std.Build,
    sign_info: CodesignInfo,
    install_path: []const u8,
    install_step: *std.Build.Step,
) ?*std.Build.Step {
    std.debug.assert(!std.fs.path.isAbsolute(install_path));
    const cert_pfx = b.graph.env_map.get("WINDOWS_CODESIGN_CERT_PFX") orelse {
        std.log.warn("missing WINDOWS_CODESIGN_CERT_PFX environment variable; binaries will be unsigned", .{});
        return null;
    };

    if (cert_pfx.len == 0) {
        std.log.warn("WINDOWS_CODESIGN_CERT_PFX environment variable is empty; binaries will be unsigned", .{});
        return null;
    }

    const cert_password = b.graph.env_map.get("WINDOWS_CODESIGN_CERT_PFX_PASSWORD") orelse {
        std.log.warn("missing WINDOWS_CODESIGN_CERT_PFX_PASSWORD environment variable; binaries will be unsigned", .{});
        return null;
    };

    const write = b.addWriteFiles();

    const cert_lazy_path = write.add("cert.pfx", blk: {
        // The env var is assumed to be base64-encoded PFX data. We decode it here.
        const size = std.base64.standard.Decoder.calcSizeForSlice(cert_pfx) catch {
            @panic("Invalid base64 in WINDOWS_CODESIGN_CERT_PFX");
        };
        const decoded = b.allocator.alloc(u8, size) catch @panic("OOM");

        std.base64.standard.Decoder.decode(decoded, cert_pfx) catch {
            @panic("Invalid base64 in WINDOWS_CODESIGN_CERT_PFX");
        };

        break :blk decoded;
    });

    const cmd = b.addSystemCommand(&.{ "osslsigncode", "sign" });

    cmd.addArg("-pkcs12");
    cmd.addFileArg(cert_lazy_path);

    cmd.addArg("-pass");
    cmd.addArg(cert_password);

    cmd.addArgs(&.{ "-n", sign_info.description });
    cmd.addArgs(&.{ "-i", constants.floe_homepage_url });
    cmd.addArgs(&.{ "-t", "http://timestamp.sectigo.com" });

    cmd.addArgs(&.{ "-in", b.getInstallPath(.prefix, install_path) });

    cmd.addArg("-out");
    const signed_output_lazy_path = cmd.addOutputFileArg("signed_plugin");
    cmd.step.dependOn(install_step);

    // osslsigncode doesn't edit in-place, it creates a separate output binary. We now need to copy that
    // output binary back to the original location.
    const reinstate_step = b.addInstallFile(signed_output_lazy_path, install_path);

    return &reinstate_step.step;
}

fn inNixShell(b: *std.Build) bool {
    return b.graph.env_map.get("IN_NIX_SHELL") != null;
}

fn hostIsNixOS() bool {
    std.fs.accessAbsolute("/etc/NIXOS", .{}) catch return false;
    return true;
}

var binary_patch_needed: ?bool = null;

pub fn patchElfNeeded(b: *std.Build) bool {
    if (binary_patch_needed == null)
        binary_patch_needed = builtin.os.tag == .linux and inNixShell(b) and !hostIsNixOS();

    return binary_patch_needed.?;
}

pub fn maybePatchElfExecutable(compile_step: *std.Build.Step.Compile) ?*std.Build.Step {
    const b = compile_step.step.owner;

    if (!patchElfNeeded(b)) return null;

    const dyn_linker = b.graph.env_map.get("FLOE_DYNAMIC_LINKER") orelse return null;

    const patch_step = b.addSystemCommand(&.{
        "patchelf",
        "--set-interpreter",
        dyn_linker,
    });
    patch_step.addFileArg(compile_step.getEmittedBin());

    return &patch_step.step;
}

pub const PluginInstallResult = struct {
    // The install path relative the prefix.
    plugin_path: []const u8,

    step: *std.Build.Step,

    pub fn fullPath(self: PluginInstallResult, b: *std.Build) []const u8 {
        return b.getInstallPath(.prefix, self.plugin_path);
    }
};

pub fn addInstallSteps(
    b: *std.Build,
    plugin_type: PluginType,
    compile_step: *std.Build.Step.Compile,
    codesign: ?CodesignInfo,
) PluginInstallResult {
    switch (compile_step.rootModuleTarget().os.tag) {
        .linux => {
            const plugin_path = switch (plugin_type) {
                .vst3 => ".vst3/Floe.vst3",
                .clap => ".clap/Floe.clap",
                .au => @panic("AU not supported on Linux"),
            };

            const binary_path = b.fmt("{s}{s}", .{
                plugin_path, switch (plugin_type) {
                    .vst3 => blk: {
                        // For VST3, we need to build a bundle folder structure for the plugin rather than just a
                        // simple binary.
                        std.debug.assert(compile_step.root_module.resolved_target.?.result.cpu.arch == .x86_64);
                        break :blk "/Contents/x86_64-linux/Floe.so";
                    },
                    .clap => "",
                    .au => @panic("AU not supported on Linux"),
                },
            });

            const install = b.addInstallFile(compile_step.getEmittedBin(), binary_path);
            var final_install_step = &install.step;

            if (patchElfNeeded(b)) {
                if (b.graph.env_map.get("FLOE_RPATH")) |rpath| {
                    const patch_step = b.addSystemCommand(&.{
                        "patchelf",
                        "--set-rpath",
                        rpath,
                        b.getInstallPath(.prefix, binary_path),
                    });
                    patch_step.step.dependOn(&install.step);
                    final_install_step = &patch_step.step;
                }
            }

            return .{ .plugin_path = plugin_path, .step = final_install_step };
        },
        .windows => {
            const install_path = switch (plugin_type) {
                .vst3 => "VST3/Floe.vst3",
                .clap => "CLAP/Floe.clap",
                .au => @panic("AU not supported on Windows"),
            };
            const install = b.addInstallFile(compile_step.getEmittedBin(), install_path);
            var final_install_step = &install.step;

            if (codesign) |sign_info| {
                if (addWindowsCodesign(b, sign_info, install_path, &install.step)) |sign_step| {
                    sign_step.dependOn(&install.step);
                    final_install_step = sign_step;
                }
            }

            return .{ .plugin_path = install_path, .step = final_install_step };
        },
        .macos => {
            const install_path = switch (plugin_type) {
                .vst3 => "VST3/Floe.vst3",
                .clap => "CLAP/Floe.clap",
                .au => "Components/Floe.component",
            };
            const bundle_extension = switch (plugin_type) {
                .vst3 => "vst3",
                .clap => "clap",
                .au => "component",
            };

            // A single top-level step so that other steps can depend on the entire bundle being made.
            const make_bundle_step = b.allocator.create(std.Build.Step) catch @panic("OOM");
            make_bundle_step.* = std.Build.Step.init(.{
                .id = .custom,
                .name = b.fmt("make {s} bundle", .{bundle_extension}),
                .owner = b,
            });
            var final_install_step = make_bundle_step;

            // Binary
            {
                const binary_path = b.fmt("{s}/Contents/MacOS/Floe", .{install_path});
                const install = b.addInstallFile(compile_step.getEmittedBin(), binary_path);
                make_bundle_step.dependOn(&install.step);

                // Generate dSYM for debugging.
                const cmd = b.addSystemCommand(&.{"dsymutil"});
                cmd.addArg(b.getInstallPath(.prefix, binary_path));
                cmd.step.dependOn(&install.step);
                make_bundle_step.dependOn(&cmd.step);
            }

            // PkgInfo
            {
                const write = b.addWriteFiles();
                const lazy_path = write.add("PkgInfo", "BNDL????");
                const install = b.addInstallFile(
                    lazy_path,
                    b.fmt("{s}/Contents/PkgInfo", .{install_path}),
                );
                make_bundle_step.dependOn(&install.step);
            }

            // Info.plist
            {
                const version = compile_step.version.?;

                const write = b.addWriteFiles();
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
                const lazy_path = write.add("Info.plist", file_content);
                const install = b.addInstallFile(
                    lazy_path,
                    b.fmt("{s}/Contents/Info.plist", .{install_path}),
                );
                make_bundle_step.dependOn(&install.step);
            }

            // We need to make sure that the audio component service is aware of the new AU. Unfortunately, it
            // doesn't do this automatically sometimes and if we were to run auval right now it might say it's
            // uninstalled. We need to kill the service so that auval will rescan for installed AUs. The command
            // on the terminal to do this is: killall -9 AudioComponentRegistrar. That is, send SIGKILL to the
            // process named AudioComponentRegistrar.
            if (builtin.os.tag == .macos and
                plugin_type == .au and
                std.mem.endsWith(u8, std.mem.trimRight(u8, b.install_path, "/"), "Library/Audio/Plug-Ins") and
                !pathExists(b.getInstallPath(.prefix, install_path)))
            {
                const cmd = b.addSystemCommand(&.{ "killall", "-9", "AudioComponentRegistrar" });
                cmd.step.dependOn(make_bundle_step);
                final_install_step = &cmd.step;
            }

            return .{ .plugin_path = install_path, .step = final_install_step };
        },
        else => @panic("unsupported OS"),
    }
}

fn pathExists(path: []const u8) bool {
    std.fs.accessAbsolute(path, .{}) catch |err| switch (err) {
        error.FileNotFound => return false,
        else => return true, // Other error - let's just say it exists.
    };
    return true;
}
