// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");
const constants = @import("constants.zig");

pub const BuildMode = enum {
    development,
    performance_profiling,
    production, // a.k.a.: release, end-user, for-distribution
};

pub const BgfxApi = enum { vulkan, direct3d11, metal };

pub const Options = struct {
    build_mode: BuildMode,
    windows_installer_require_admin: bool,
    enable_tracy: bool,
    sanitize_thread: bool,
    fetch_floe_logos: bool,
    targets: ?[]const u8,
};

pub const BuildContext = struct {
    b: *std.Build,
    floe_version_string: []const u8,
    floe_version: std.SemanticVersion,
    floe_version_hash: u32,
    native_archiver: ?*std.Build.Step.Compile,
    native_docs_generator: ?*std.Build.Step.Compile,
    enable_tracy: bool,
    build_mode: BuildMode,
    optimise: std.builtin.OptimizeMode,
    windows_installer_require_admin: bool,

    // Dependencies.
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
    dep_bx: *std.Build.Dependency,
    dep_bimg: *std.Build.Dependency,
    dep_bgfx: *std.Build.Dependency,

    // The compile-all top-level step is special in that it lives on the context.
    compile_all: *std.Build.Step,
};

pub const TopLevelSteps = struct {
    // Installs.
    build_release: *std.Build.Step,
    install_plugins: *std.Build.Step, // only plugins
    install_all: *std.Build.Step, // everything, dev tools as well

    // Tests.
    test_step: *std.Build.Step,
    coverage: *std.Build.Step,
    clap_val: *std.Build.Step,
    test_vst3_validator: *std.Build.Step,
    pluginval_au: *std.Build.Step,
    auval: *std.Build.Step,
    pluginval: *std.Build.Step,
    valgrind: *std.Build.Step,
    test_windows_install: *std.Build.Step,
    ci: *std.Build.Step,
    ci_basic: *std.Build.Step,

    // Scripts.
    clang_tidy: *std.Build.Step,
    format_step: *std.Build.Step,
    create_gh_release: *std.Build.Step,
    upload_errors: *std.Build.Step,
    shaderc: *std.Build.Step,
    website_gen: *std.Build.Step,
    website_build: *std.Build.Step,
    website_dev: *std.Build.Step,
    website_promote: *std.Build.Step,
    remove_unused_gui_defs: *std.Build.Step,
};

pub const TargetConfig = struct {
    pub fn create(
        ctx: *const BuildContext,
        resolved_target: std.Build.ResolvedTarget,
        options: *const Options,
    ) TargetConfig {
        const target = resolved_target.result;
        // Create a unique hash for this configuration. We use when we need to unique generate folders even when
        // multiple zig builds processes are running simultaneously. Ideally we would use hashUserInputOptionsMap
        // from std.Build, but it's private and quite complicated to copy here. This manual approach is simple but
        // not as robust.
        const config_hash = blk: {
            var hasher = std.hash.Wyhash.init(0);
            hasher.update(target.zigTriple(ctx.b.allocator) catch "");
            hasher.update(@tagName(options.build_mode));
            hasher.update(std.mem.asBytes(&options.enable_tracy));
            hasher.update(std.mem.asBytes(&options.sanitize_thread));
            hasher.update(std.mem.asBytes(&options.windows_installer_require_admin));
            break :blk hasher.final();
        };

        const cdb_dir = blk: {
            // To better avoid collisions when multiple 'zig build' processes are running simultaneously we use a
            // unique hash based on the config.
            const path = ctx.b.fmt("tmp{s}cdb{x}", .{ std.fs.path.sep_str, config_hash });
            ctx.b.cache_root.handle.makePath(path) catch |err| {
                std.debug.print("failed to make fragments path: {any}\n", .{err});
            };
            break :blk path;
        };

        const bgfx_api: BgfxApi = switch (resolved_target.result.os.tag) {
            .windows => .direct3d11,
            .macos => .metal,
            .linux => .vulkan,
            else => .vulkan,
        };
        const result: TargetConfig = .{
            .floe_config_h = ctx.b.addConfigHeader(.{
                .style = .blank,
            }, .{
                .PRODUCTION_BUILD = ctx.build_mode == .production,
                .OPTIMISED_BUILD = ctx.optimise != .Debug,
                .RUNTIME_SAFETY_CHECKS_ON = ctx.optimise == .Debug or ctx.optimise == .ReleaseSafe,
                .FLOE_VERSION_STRING = ctx.floe_version_string,
                .FLOE_VERSION_HASH = ctx.floe_version_hash,
                .FLOE_DESCRIPTION = constants.floe_description,
                .FLOE_HOMEPAGE_URL = constants.floe_homepage_url,
                .FLOE_MANUAL_URL = constants.floe_manual_url,
                .FLOE_DOWNLOAD_URL = constants.floe_download_url,
                .FLOE_CHANGELOG_URL = constants.floe_changelog_url,
                .FLOE_SOURCE_CODE_URL = constants.floe_source_code_url,
                .FLOE_PROJECT_ROOT_PATH = ctx.b.build_root.path.?,
                .FLOE_PROJECT_CACHE_PATH = ctx.b.pathJoin(&.{
                    ctx.b.build_root.path.?,
                    constants.floe_cache_relative,
                }),
                .FLOE_VENDOR = constants.floe_vendor,
                .FLOE_CLAP_ID = constants.floe_clap_id,
                .FLOE_BGFX_API_METAL = bgfx_api == .metal,
                .FLOE_BGFX_API_VULKAN = bgfx_api == .vulkan,
                .FLOE_BGFX_API_DIRECT3D11 = bgfx_api == .direct3d11,
                .IS_WINDOWS = target.os.tag == .windows,
                .IS_MACOS = target.os.tag == .macos,
                .IS_LINUX = target.os.tag == .linux,
                .OS_DISPLAY_NAME = ctx.b.fmt("{s}", .{@tagName(target.os.tag)}),
                .ARCH_DISPLAY_NAME = ctx.b.fmt("{s}", .{@tagName(target.cpu.arch)}),
                .MIN_WINDOWS_NTDDI_VERSION = @intFromEnum(std.Target.Os.WindowsVersion.parse(
                    constants.min_windows_version,
                ) catch @panic("invalid win ver")),
                .MIN_MACOS_VERSION = constants.min_macos_version,
                .SENTRY_DSN = ctx.b.graph.env_map.get("SENTRY_DSN"),
            }),
            .module_options = .{
                .target = resolved_target,
                .optimize = ctx.optimise,
                .strip = false,
                .pic = true,
                .link_libc = true,
                .omit_frame_pointer = false,
                .unwind_tables = .sync,
                .sanitize_thread = options.sanitize_thread,
            },
            .resolved_target = resolved_target,
            .target = resolved_target.result,
            .bgfx_api = bgfx_api,
            .cdb_fragments_dir = cdb_dir,
            .cdb_fragments_dir_real = ctx.b.cache_root.handle.realpathAlloc(ctx.b.allocator, cdb_dir) catch unreachable,
        };

        return result;
    }

    floe_config_h: *std.Build.Step.ConfigHeader,
    module_options: std.Build.Module.CreateOptions,
    resolved_target: std.Build.ResolvedTarget,
    target: std.Target,
    bgfx_api: BgfxApi,
    cdb_fragments_dir: []const u8,
    cdb_fragments_dir_real: []const u8,
};
