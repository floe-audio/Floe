// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");

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

    // Top level steps.
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
