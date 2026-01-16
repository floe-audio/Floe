// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");
const ConcatCompileCommandsStep = @import("ConcatCompileCommandsStep.zig");
const constants = @import("constants.zig");

pub const BuildMode = enum {
    development,
    performance_profiling,
    production, // a.k.a.: release, end-user, for-distribution
};

pub const RendererType = enum { custom_opengl2, custom_direct3d9, bgfx };
pub const GraphicsApi = enum { opengl, vulkan, metal, direct3d };

pub const Options = struct {
    build_mode: BuildMode,
    renderer_type: ?RendererType,
    graphics_api: ?GraphicsApi,
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

    // Top-level steps.
    compile_all_step: *std.Build.Step,
    release: *std.Build.Step,
    test_step: *std.Build.Step,
    coverage: *std.Build.Step,
    clap_val: *std.Build.Step,
    test_vst3_validator: *std.Build.Step,
    pluginval_au: *std.Build.Step,
    auval: *std.Build.Step,
    pluginval: *std.Build.Step,
    valgrind: *std.Build.Step,
    test_windows_install: *std.Build.Step,
    clang_tidy: *std.Build.Step,
    format_step: *std.Build.Step,
    gh_release_step: *std.Build.Step,
    ci_step: *std.Build.Step,
    ci_basic_step: *std.Build.Step,
    upload_errors_step: *std.Build.Step,
    shaderc: *std.Build.Step,
    website_gen_step: *std.Build.Step,
    website_build_step: *std.Build.Step,
    website_dev_step: *std.Build.Step,
    website_promote_step: *std.Build.Step,
    install_all_step: *std.Build.Step,
};

pub const TargetConfig = struct {
    pub fn create(
        ctx: *BuildContext,
        resolved_target: std.Build.ResolvedTarget,
        options: Options,
        set_as_cdb: bool,
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
            hasher.update(std.mem.asBytes(&options.renderer_type));
            hasher.update(std.mem.asBytes(&options.graphics_api));
            break :blk hasher.final();
        };
        const renderer_type: RendererType = options.renderer_type orelse switch (target.os.tag) {
            .windows => .custom_direct3d9,
            .linux => .custom_opengl2,
            .macos => .custom_opengl2,
            else => @panic("unsupported OS"),
        };
        const graphics_api: GraphicsApi = options.graphics_api orelse switch (target.os.tag) {
            .windows => .direct3d,
            .linux => .opengl,
            .macos => .opengl,
            else => @panic("unsupported OS"),
        };
        const result: TargetConfig = .{
            .renderer_type = renderer_type,
            .graphics_api = graphics_api,
            .concat_cdb = ConcatCompileCommandsStep.create(ctx.b, target, set_as_cdb, config_hash),
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
                .FLOE_RENDERER_CUSTOM_OPENGL2 = renderer_type == .custom_opengl2,
                .FLOE_RENDERER_CUSTOM_DIRECT9 = renderer_type == .custom_direct3d9,
                .FLOE_RENDERER_BGFX = renderer_type == .bgfx,
                .FLOE_GRAPHICS_API_OPENGL = graphics_api == .opengl,
                .FLOE_GRAPHICS_API_VULKAN = graphics_api == .vulkan,
                .FLOE_GRAPHICS_API_METAL = graphics_api == .metal,
                .FLOE_GRAPHICS_API_DIRECT3D = graphics_api == .direct3d,
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
        };

        return result;
    }

    renderer_type: RendererType,
    graphics_api: GraphicsApi,
    concat_cdb: *ConcatCompileCommandsStep,
    floe_config_h: *std.Build.Step.ConfigHeader,
    module_options: std.Build.Module.CreateOptions,
    resolved_target: std.Build.ResolvedTarget,
    target: std.Target,
};
