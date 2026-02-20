// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");
const builtin = @import("builtin");

const std_extras = @import("src/build/std_extras.zig");
const constants = @import("src/build/constants.zig");
const cdb = @import("src/build/combine_cdb_fragments.zig");
const check_steps = @import("src/build/check_steps.zig");
const configure_binaries = @import("src/build/configure_binaries.zig");
const release_artifacts = @import("src/build/release_artifacts.zig");
const bgfx_shaderc = @import("src/build/bgfx_shaderc.zig");
const build_context = @import("src/build/context.zig");

const BuildContext = build_context.BuildContext;
const Options = build_context.Options;
const TopLevelSteps = build_context.TopLevelSteps;
const TargetConfig = build_context.TargetConfig;

comptime {
    const current_zig = builtin.zig_version;
    const required_zig = std.SemanticVersion.parse("0.14.0") catch unreachable;
    if (current_zig.order(required_zig) != .eq) {
        @compileError(std.fmt.comptimePrint(
            "Floe requires Zig version {}, but you are using version {}.",
            .{ required_zig, current_zig },
        ));
    }
}

const FlagsBuilder = struct {
    flags: std.ArrayList([]const u8),

    const Options = struct {
        // Enable undefined behaviour sanitizer.
        ubsan: bool = false,

        // Generate JSON fragments for making compile_commands.json.
        gen_cdb_fragments: bool = false,

        // Reduce size of windows.h to speed up compilation. Although it can sometimes cut too much.
        minimise_windows: bool = true,

        // Add all reasonable warnings as possible.
        all_warnings: bool = false,

        // Language modes.
        cpp: bool = false,
        objcpp: bool = false,
    };

    pub fn init(
        ctx: *const BuildContext,
        cfg: *const TargetConfig,
        options: FlagsBuilder.Options,
    ) FlagsBuilder {
        var result = FlagsBuilder{
            .flags = std.ArrayList([]const u8).init(ctx.b.allocator),
        };
        result.addCoreFlags(ctx, cfg, options) catch @panic("OOM");
        return result;
    }

    fn addFlag(self: *FlagsBuilder, flag: []const u8) void {
        self.flags.append(flag) catch @panic("OOM");
    }

    fn addFlags(self: *FlagsBuilder, flags: []const []const u8) void {
        for (flags) |flag| self.addFlag(flag);
    }

    fn addCoreFlags(
        self: *FlagsBuilder,
        ctx: *const BuildContext,
        cfg: *const TargetConfig,
        options: FlagsBuilder.Options,
    ) !void {
        if (options.all_warnings) {
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
            });
        }

        if (options.minimise_windows and cfg.target.os.tag == .windows) {
            // Minimise windows.h size for faster compile times:
            // "Define one or more of the NOapi symbols to exclude the API. For example, NOCOMM excludes
            // the serial communication API. For a list of support NOapi symbols, see Windows.h."
            try self.flags.appendSlice(&.{
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
        try self.flags.append("-fvisibility=hidden");

        // We want __builtin_FILE(), __FILE__ and debug info to be portable because we use this information in
        // stacktraces and logging so we change the absolute paths of all files to be relative to the build root.
        // __FILE__, __builtin_FILE(), and DWARF info should be made relative.
        // -ffile-prefix-map=OLD=NEW is an alias for both -fdebug-prefix-map and -fmacro-prefix-map
        try self.flags.append(ctx.b.fmt("-ffile-prefix-map={s}{s}=", .{
            ctx.b.pathFromRoot(""),
            std.fs.path.sep_str,
        }));

        switch (cfg.target.os.tag) {
            .windows => {
                // On Windows, fix compile errors related to deprecated usage of string in mingw.
                try self.flags.append("-DSTRSAFE_NO_DEPRECATE");
                try self.flags.append("-DUNICODE");
                try self.flags.append("-D_UNICODE");
            },
            .macos => {
                try self.flags.append("-DGL_SILENCE_DEPRECATION"); // disable opengl warnings on macos

                // Stop errors when compiling macOS obj-c SDK headers.
                try self.flags.appendSlice(&.{
                    "-Wno-elaborated-enum-base",
                    "-Wno-missing-method-return-type",
                    "-Wno-deprecated-declarations",
                    "-Wno-deprecated-anon-enum-enum-conversion",
                    "-D__kernel_ptr_semantics=",
                    "-Wno-c99-extensions",
                });
            },
            .linux => {
                // NOTE(Sam, June 2024): workaround for a bug in Zig (most likely) where our shared library always causes a
                // crash after dlclose(), as described here: https://github.com/ziglang/zig/issues/17908
                // The workaround involves adding this flag and also adding a custom bit of code using
                // __attribute__((destructor)) to manually call __cxa_finalize():
                // https://stackoverflow.com/questions/34308720/where-is-dso-handle-defined/48256026#48256026
                try self.flags.append("-fno-use-cxa-atexit");
            },
            else => {},
        }

        // A bit of information about debug symbols:
        // DWARF is a debugging information format. It is used widely, particularly on Linux and macOS.
        // Zig/libbacktrace, which we use for getting nice stack traces can read DWARF information from the
        // executable on any OS. All we need to do is make sure that the DWARF info is available for
        // Zig/libbacktrace to read.
        //
        // On Windows, there is the PDB format, this is a separate file that contains the debug information. Zig
        // generates this too, but we can tell it to also embed DWARF debug info into the executable, that's what the
        // -gdwarf flag does.
        //
        // On Linux, it's easy, just use the same flag.
        //
        // On macOS, there is a slightly different approach. DWARF info is embedded in the compiled .o flags. But
        // it's not aggregated into the final executable. Instead, the final executable contains a 'debug map' which
        // points to all of the object files and shows where the DWARF info is. You can see this map by running
        // 'dsymutil --dump-debug-map my-exe'.
        //
        // In order to aggregate the DWARF info into the final executable, we need to run 'dsymutil my-exe'. This
        // then outputs a .dSYM folder which contains the aggregated DWARF info. Zig/libbacktrace looks for this
        // dSYM folder adjacent to the executable.

        // Include dwarf debug info, even on windows. This means we can use the Zig/libbacktraceeverywhere to get
        // really good stack traces.
        //
        // We use DWARF 4 because Zig has a problem with version 5: https://github.com/ziglang/zig/issues/23732
        try self.flags.append("-gdwarf-4");

        if (options.ubsan) {
            if (ctx.optimise != .ReleaseFast) {
                // By default, zig enables UBSan (unless ReleaseFast mode) in trap mode. Meaning it will catch
                // undefined behaviour and trigger a trap which can be caught by signal handlers. UBSan also has a
                // mode where undefined behaviour will instead call various functions. This is called the UBSan
                // runtime. It's really easy to implement the 'minimal' version of this runtime: we just have to
                // declare a bunch of functions like __ubsan_handle_x. So that's what we do rather than trying to
                // link with the system's version. https://github.com/ziglang/zig/issues/5163#issuecomment-811606110
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

        if (options.gen_cdb_fragments)
            try cdb.addGenerateCdbFragmentFlags(&self.flags, cfg.cdb_fragments_dir_real);

        //
        // Library specific flags.

        try self.flags.appendSlice(&.{
            "-DFLAC__NO_DLL",
        });

        try self.flags.appendSlice(&.{
            "-DPUGL_DISABLE_DEPRECATED",
            "-DPUGL_STATIC",
        });

        try self.flags.append(ctx.b.fmt("-DBX_CONFIG_DEBUG={}", .{@intFromBool(ctx.build_mode == .development)}));

        try self.flags.appendSlice(&.{
            "-DMINIZ_USE_UNALIGNED_LOADS_AND_STORES=0",
            "-DMINIZ_NO_STDIO",
            "-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES",
            ctx.b.fmt("-DMINIZ_LITTLE_ENDIAN={d}", .{@intFromBool(cfg.target.cpu.arch.endian() == .little)}),
            "-DMINIZ_HAS_64BIT_REGISTERS=1",
        });

        try self.flags.appendSlice(&.{
            "-DSTBI_NO_STDIO",
            "-DSTBI_MAX_DIMENSIONS=65535", // we use u16 for dimensions
        });

        if (ctx.build_mode != .production and ctx.enable_tracy) {
            try self.flags.append("-DTRACY_ENABLE");
            try self.flags.append("-DTRACY_MANUAL_LIFETIME");
            try self.flags.append("-DTRACY_DELAYED_INIT");
            try self.flags.append("-DTRACY_ONLY_LOCALHOST");
            if (cfg.target.os.tag == .linux) {
                // Couldn't get these working well so just disabling them
                try self.flags.append("-DTRACY_NO_CALLSTACK");
                try self.flags.append("-DTRACY_NO_SYSTEM_TRACING");
            }
        }
    }
};

fn applyUniversalSettings(
    ctx: *const BuildContext,
    step: *std.Build.Step.Compile,
) void {
    var b = ctx.b;
    // NOTE (May 2025, Zig 0.14): LTO on Windows results in debug_info generation that fails to parse with Zig's
    // Dwarf parser (InvalidDebugInfo). We've previously had issues on macOS too. So we disable it for now.
    step.want_lto = false;
    step.rdynamic = true;
    step.linkLibC();

    step.addIncludePath(ctx.dep_xxhash.path(""));
    step.addIncludePath(ctx.dep_stb.path(""));
    step.addIncludePath(ctx.dep_clap.path("include"));
    step.addIncludePath(ctx.dep_icon_font_cpp_headers.path(""));
    step.addIncludePath(ctx.dep_dr_libs.path(""));
    step.addIncludePath(ctx.dep_flac.path("include"));
    step.addIncludePath(ctx.dep_lua.path(""));
    step.addIncludePath(ctx.dep_pugl.path("include"));
    step.addIncludePath(ctx.dep_pugl.path("src"));
    step.addIncludePath(ctx.dep_clap_wrapper.path("include"));
    step.addIncludePath(ctx.dep_tracy.path("public"));
    step.addIncludePath(ctx.dep_valgrind_h.path(""));
    step.addIncludePath(ctx.dep_portmidi.path("pm_common"));
    step.addIncludePath(ctx.dep_miniz.path(""));
    step.addIncludePath(b.path("third_party_libs/miniz"));

    step.addIncludePath(b.path("."));
    step.addIncludePath(b.path("src"));
    step.addIncludePath(b.path("third_party_libs"));

    if (step.rootModuleTarget().os.tag == .macos) {
        // When using Apple codesign, the binary can silently become invalid. After much investigation it turned out
        // to be a problem with the Mach-O headers not having enough padding for additional load commands. Zig's
        // default is not enough for codesign's needs, and codesign doesn't tell you this is the problem. To resolve
        // this we increase the header padding size significantly. The binaries that had the most problem with this
        // issue were release-mode x86_64 builds. In the cases I tested, either of these 2 settings alone was enough
        // to fix the problem, but I see no harm in having both in case it covers more situations. While Apple's
        // codesign never complains about the problem (and you end up getting segfaults at runtime), an open-source
        // alternative called rcodesign correctly reports the error: "insufficient room to write code signature load
        // command". There's lengthy discussion about this issue here: https://github.com/ziglang/zig/issues/23704
        step.headerpad_size = 0x1000;
        step.headerpad_max_install_names = true;

        if (b.lazyDependency("macos_sdk", .{})) |sdk| {
            step.addSystemIncludePath(sdk.path("usr/include"));
            step.addLibraryPath(sdk.path("usr/lib"));
            step.addFrameworkPath(sdk.path("System/Library/Frameworks"));
        }
    }

    ctx.compile_all.dependOn(&step.step);

    if (step.rootModuleTarget().os.tag == .macos and step.kind == .exe) {
        // TODO: is dsymutil step needed
    }
}

// Floe only supports a certain set of arch/OS/CPU types.
fn resolveTargets(b: *std.Build, user_given_target_presets: ?[]const u8) !std.ArrayList(std.Build.ResolvedTarget) {
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
            std.log.err("unknown target preset: {s}\n", .{preset_string});
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
                    arch_os_abi = "x86_64-linux-gnu.2.31";
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

const linux_use_pkg_config = std.Build.Module.SystemLib.UsePkgConfig.yes;

pub fn build(b: *std.Build) void {
    b.reference_trace = 10; // Improve debugging of build.zig itself.

    std_extras.loadEnvFile(b.build_root.handle, &b.graph.env_map) catch {};

    // IMPORTANT: if you add options here, you may need to also update the config_hash computation below.
    const options: Options = .{
        .build_mode = b.option(
            build_context.BuildMode,
            "build-mode",
            "The preset for building the project, affects optimisation, debug settings, etc.",
        ) orelse .development,

        // Installing plugins to global plugin folders requires admin rights but it's often easier to debug
        // things without requiring admin. For production builds it's always enabled.
        .windows_installer_require_admin = b.option(
            bool,
            "win-installer-elevated",
            "Whether the installer should be set to administrator-required mode",
        ) orelse false,
        .enable_tracy = b.option(bool, "tracy", "Enable Tracy profiler") orelse false,
        .sanitize_thread = b.option(
            bool,
            "sanitize-thread",
            "Enable thread sanitiser",
        ) orelse false,
        .fetch_floe_logos = b.option(
            bool,
            "fetch-floe-logos",
            "Fetch Floe logos from online - these may have a different licence to the rest of Floe",
        ) orelse false,
        .targets = b.option([]const u8, "targets", "Target operating system"),
    };

    const floe_version_string = blk: {
        var ver: []const u8 = b.build_root.handle.readFileAlloc(b.allocator, "version.txt", 256) catch @panic("version.txt error");
        ver = std.mem.trim(u8, ver, " \r\n\t");

        if (options.build_mode != .production) {
            ver = b.fmt("{s}+{s}", .{
                ver,
                std.mem.trim(u8, b.run(&.{ "git", "rev-parse", "--short", "HEAD" }), " \r\n\t"),
            });
        }
        break :blk ver;
    };
    const floe_version = std.SemanticVersion.parse(floe_version_string) catch @panic("invalid version");
    const floe_version_hash = std.hash.Fnv1a_32.hash(floe_version_string);

    var ctx: BuildContext = .{
        .b = b,
        .floe_version_string = floe_version_string,
        .floe_version = floe_version,
        .floe_version_hash = floe_version_hash,
        .native_archiver = null,
        .native_docs_generator = null,
        .enable_tracy = options.enable_tracy,
        .build_mode = options.build_mode,
        .optimise = switch (options.build_mode) {
            .development => std.builtin.OptimizeMode.Debug,
            .performance_profiling, .production => std.builtin.OptimizeMode.ReleaseSafe,
        },
        .windows_installer_require_admin = options.windows_installer_require_admin,

        .dep_floe_logos = if (options.fetch_floe_logos)
            b.lazyDependency("floe_logos", .{})
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
        .dep_bx = b.dependency("bx", .{}),
        .dep_bimg = b.dependency("bimg", .{}),
        .dep_bgfx = b.dependency("bgfx", .{}),

        // Steps are normally on TopLevelSteps, but compile-all is a special case.
        .compile_all = b.step("compile", "Compile all"),
    };

    var top_level_steps: TopLevelSteps = .{
        .build_release = b.step("release", "Create release artifacts (zipped, codesigned, etc.)"),
        .install_plugins = b.getInstallStep(),
        .install_all = b.step("install:all", "Install all; development files as well as plugins"),

        .test_step = b.step("test", "Run unit tests"),
        .coverage = b.step("test-coverage", "Generate code coverage report of unit tests"),
        .clap_val = b.step("test:clap-val", "Test using clap-validator"),
        .test_vst3_validator = b.step("test:vst3-val", "Run VST3 Validator on built VST3 plugin"),
        .pluginval_au = b.step("test:pluginval-au", "Test AU using pluginval"),
        .auval = b.step("test:auval", "Test AU using auval"),
        .pluginval = b.step("test:pluginval", "Test using pluginval"),
        .valgrind = b.step("test:valgrind", "Test using Valgrind"),
        .test_windows_install = b.step("test:windows-install", "Test installation and uninstallation on Windows"),
        .ci = b.step("script:ci", "Run CI checks"),
        .ci_basic = b.step("script:ci-basic", "Run basic CI checks"),

        .clang_tidy = b.step("check:clang-tidy", "Run clang-tidy on source files"),
        .format_step = b.step("script:format", "Format code with clang-format"),
        .create_gh_release = b.step("script:create-gh-release", "Create a GitHub release"),
        .upload_errors = b.step("script:upload-errors", "Upload error reports to Sentry"),
        .shaderc = b.step("script:shaderc", "Compile shaders in src/shaders into .bin.h"),
        .website_gen = b.step("script:website-generate", "Generate the static JSON for the website"),
        .website_build = b.step("script:website-build", "Build the website"),
        .website_dev = b.step("script:website-dev", "Start website dev build locally"),
        .website_promote = b.step("script:website-promote-beta-to-stable", "Promote the 'beta' documentation to be the latest stable version"),
        .remove_unused_gui_defs = b.step("script:remove-unused-gui-defs", "Remove unused size/colour-map entries from def files"),
    };

    // The default is to compile everything.
    b.default_step = ctx.compile_all;

    b.build_root.handle.makeDir(constants.floe_cache_relative) catch {};

    const targets = resolveTargets(b, options.targets) catch @panic("OOM");

    // If we're building for multiple targets at the same time, we need to choose one that gets to be the
    // final compile_commands.json. We just say the first one.
    const target_for_compile_commands = targets.items[0];

    // We do something a little sneaky. We actual replace all the top-level steps with a hidden
    // combine-CDB step. The rest of the code can carry on as if it's still the top level step, but
    // it means that the combining of CDG fragments happens as the last step without the rest of the
    // code having to worry about it.
    var cdb_steps = std.ArrayList(*cdb.CombineCdbFragmentsStep).init(b.allocator);
    cdb.insertHiddenCombineCdbStep(b, &ctx.compile_all, &cdb_steps);
    inline for (@typeInfo(TopLevelSteps).@"struct".fields) |field| {
        cdb.insertHiddenCombineCdbStep(b, &@field(&top_level_steps, field.name), &cdb_steps);
    }

    // We'll try installing the desired compile_commands.json version here in case any previous build already
    // created it.
    cdb.trySetCdb(b, target_for_compile_commands.result);

    const artifacts_list = b.allocator.alloc(release_artifacts.Artifacts, targets.items.len) catch @panic("OOM");

    // Compile/install steps
    for (targets.items, 0..) |target, i| {
        if (target.result.os.tag == .windows and options.sanitize_thread)
            @panic("thread sanitiser is not supported on Windows targets");

        const cfg = TargetConfig.create(&ctx, target, &options);

        for (cdb_steps.items) |cdb_step| {
            cdb_step.addTarget(.{
                .fragments_dir = cfg.cdb_fragments_dir,
                .target = cfg.target,
                .set_as_active = target.query.eql(target_for_compile_commands.query),
            }) catch unreachable;
        }

        artifacts_list[i] = doTarget(&ctx, &cfg, &top_level_steps, &options);
    }

    // check:* steps.
    check_steps.addGlobalCheckSteps(b);

    // Build scripts CLI program and add script steps
    {
        const exe = b.addExecutable(.{
            .name = "scripts",
            .root_module = b.createModule(.{
                .root_source_file = b.path("src/build/scripts.zig"),
                .optimize = .Debug,
                .target = b.graph.host,
            }),
        });
        if (b.graph.host.result.os.tag == .windows) exe.linkLibC(); // GetTempPath2W

        addRunScript(exe, top_level_steps.format_step, "format");
        addRunScript(exe, top_level_steps.create_gh_release, "create-gh-release");
        addRunScript(exe, top_level_steps.upload_errors, "upload-errors");
        addRunScript(exe, top_level_steps.ci, "ci");
        addRunScript(exe, top_level_steps.ci_basic, "ci-basic");
        addRunScript(exe, top_level_steps.website_promote, "website-promote-beta-to-stable");
        addRunScript(exe, top_level_steps.remove_unused_gui_defs, "remove-unused-gui-defs");
    }

    // Shader compiler.
    {
        const target = if (builtin.target.os.tag == .linux)
            // It's only possible to compile DX11 shaders on Windows, so we try to use wine if we can.
            b.resolveTargetQuery(std.Target.Query.parse(.{
                .arch_os_abi = "x86_64-windows.win10",
                .cpu_features = "x86_64",
            }) catch unreachable)
        else
            b.graph.host;

        const cfg = TargetConfig.create(&ctx, target, &options);

        const shaderc = bgfx_shaderc.buildShaderC(&ctx, &cfg, .{
            .bx = buildBx(&ctx, &cfg),
        });
        const run = bgfx_shaderc.shaderCRunSteps(&ctx, target.result, shaderc);
        top_level_steps.shaderc.dependOn(run);
    }

    // Make release artifacts
    {
        const archiver = blk: {
            if (ctx.native_archiver) |a| break :blk a;

            const native_target_cfg = TargetConfig.create(&ctx, b.graph.host, &options);
            break :blk buildArchiver(&ctx, &native_target_cfg, .{
                .miniz = buildMiniz(&ctx, &native_target_cfg),
            });
        };

        for (artifacts_list, 0..) |artifacts, i| {
            const install_steps = release_artifacts.makeRelease(
                b,
                archiver,
                ctx.floe_version,
                targets.items[i].result,
                artifacts,
            );
            top_level_steps.build_release.dependOn(install_steps);
        }
    }

    // Docs generator
    {
        const docs_generator = blk: {
            if (ctx.native_docs_generator) |d| break :blk d;

            const native_target_cfg = TargetConfig.create(&ctx, b.graph.host, &options);
            break :blk buildDocsGenerator(&ctx, &native_target_cfg, .{
                .common_infrastructure = buildCommonInfrastructure(&ctx, &native_target_cfg, .{
                    .dr_wav = buildDrWav(&ctx, &native_target_cfg),
                    .flac = buildFlac(&ctx, &native_target_cfg),
                    .xxhash = buildXxhash(&ctx, &native_target_cfg),
                    .library = buildFloeLibrary(&ctx, &native_target_cfg, .{
                        .stb_sprintf = buildStbSprintf(&ctx, &native_target_cfg),
                        .debug_info_lib = buildDebugInfo(&ctx, &native_target_cfg),
                        .tracy = buildTracy(&ctx, &native_target_cfg),
                    }),
                    .miniz = buildMiniz(&ctx, &native_target_cfg),
                }),
            });
        };

        // Run the docs generator. It takes no args but outputs JSON to stdout.
        {
            const run = std.Build.Step.Run.create(b, b.fmt("run {s}", .{docs_generator.name}));
            run.addFileArg(configure_binaries.nix_helper.maybePatchElfExecutable(docs_generator));

            const copy = b.addUpdateSourceFiles();
            copy.addCopyFileToSource(run.captureStdOut(), "website/static/generated-data.json");
            top_level_steps.website_gen.dependOn(&copy.step);
        }

        // Build the site for production
        {
            const npm_install = b.addSystemCommand(&.{ "npm", "install" });
            npm_install.setCwd(b.path("website"));
            npm_install.expectExitCode(0);

            const create_api = CreateWebsiteApiFiles.create(b);

            const run = std_extras.createCommandWithStdoutToStderr(b, builtin.target, "run docusaurus build");
            run.addArgs(&.{ "npm", "run", "build" });
            run.setCwd(b.path("website"));
            run.step.dependOn(top_level_steps.website_gen);
            run.step.dependOn(&npm_install.step);
            run.step.dependOn(&create_api.step);
            run.expectExitCode(0);
            top_level_steps.website_build.dependOn(&run.step);
        }

        // Start the website locally
        {
            const npm_install = b.addSystemCommand(&.{ "npm", "install" });
            npm_install.setCwd(b.path("website"));
            npm_install.expectExitCode(0);

            const run = std_extras.createCommandWithStdoutToStderr(b, builtin.target, "run docusaurus start");
            run.addArgs(&.{ "npm", "run", "start" });
            run.setCwd(b.path("website"));
            run.step.dependOn(top_level_steps.website_gen);
            run.step.dependOn(&npm_install.step);

            top_level_steps.website_dev.dependOn(&run.step);
        }
    }
}

fn buildStbSprintf(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    const obj = ctx.b.addObject(.{
        .name = "stb_sprintf",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    obj.addCSourceFile(.{
        .file = ctx.b.path("third_party_libs/stb_sprintf.c"),
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    obj.addIncludePath(ctx.dep_stb.path(""));
    return obj;
}

fn buildXxhash(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    const obj = ctx.b.addObject(.{
        .name = "xxhash",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    obj.addCSourceFile(.{
        .file = ctx.dep_xxhash.path("xxhash.c"),
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    obj.linkLibC();
    return obj;
}

fn buildTracy(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    const lib = ctx.b.addStaticLibrary(.{
        .name = "tracy",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    lib.addCSourceFile(.{
        .file = ctx.dep_tracy.path("public/TracyClient.cpp"),
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .gen_cdb_fragments = true,
        }).flags.items,
    });

    switch (cfg.target.os.tag) {
        .windows => {
            lib.linkSystemLibrary("ws2_32");
        },
        .macos => {},
        .linux => {},
        else => {
            unreachable;
        },
    }
    lib.linkLibCpp();
    applyUniversalSettings(ctx, lib);

    return lib;
}

fn buildVitfx(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    const lib = ctx.b.addStaticLibrary(.{
        .name = "vitfx",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    const path = "third_party_libs/vitfx";
    lib.addCSourceFiles(.{
        .root = ctx.b.path(path),
        .files = &.{
            "src/synthesis/effects/reverb.cpp",
            "src/synthesis/effects/phaser.cpp",
            "src/synthesis/effects/delay.cpp",
            "src/synthesis/framework/processor.cpp",
            "src/synthesis/framework/processor_router.cpp",
            "src/synthesis/framework/value.cpp",
            "src/synthesis/framework/feedback.cpp",
            "src/synthesis/framework/operators.cpp",
            "src/synthesis/filters/phaser_filter.cpp",
            "src/synthesis/filters/synth_filter.cpp",
            "src/synthesis/filters/sallen_key_filter.cpp",
            "src/synthesis/filters/comb_filter.cpp",
            "src/synthesis/filters/digital_svf.cpp",
            "src/synthesis/filters/dirty_filter.cpp",
            "src/synthesis/filters/ladder_filter.cpp",
            "src/synthesis/filters/diode_filter.cpp",
            "src/synthesis/filters/formant_filter.cpp",
            "src/synthesis/filters/formant_manager.cpp",
            "wrapper.cpp",
        },
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    lib.addIncludePath(ctx.b.path(path ++ "/src/synthesis"));
    lib.addIncludePath(ctx.b.path(path ++ "/src/synthesis/framework"));
    lib.addIncludePath(ctx.b.path(path ++ "/src/synthesis/filters"));
    lib.addIncludePath(ctx.b.path(path ++ "/src/synthesis/lookups"));
    lib.addIncludePath(ctx.b.path(path ++ "/src/common"));
    lib.linkLibCpp();

    lib.addIncludePath(ctx.dep_tracy.path("public"));

    return lib;
}

fn buildPugl(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    const lib = ctx.b.addStaticLibrary(.{
        .name = "pugl",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    const src_path = ctx.dep_pugl.path("src");
    const pugl_ver_hash = std.hash.Fnv1a_32.hash(ctx.dep_pugl.builder.pkg_hash);

    const pugl_flags = FlagsBuilder.init(ctx, cfg, .{
        .gen_cdb_fragments = true,
        .minimise_windows = false,
    }).flags.items;

    lib.addCSourceFiles(.{
        .root = src_path,
        .files = &.{
            "common.c",
            "internal.c",
            "internal.c",
        },
        .flags = pugl_flags,
    });

    switch (cfg.target.os.tag) {
        .windows => {
            lib.addCSourceFiles(.{
                .root = src_path,
                .files = &.{
                    "win.c",
                    "win_stub.c",
                },
                .flags = pugl_flags,
            });
            lib.linkSystemLibrary("opengl32");
            lib.linkSystemLibrary("gdi32");
            lib.linkSystemLibrary("dwmapi");
        },
        .macos => {
            lib.addCSourceFiles(.{
                .root = src_path,
                .files = &.{
                    "mac.m",
                    "mac_gl.m",
                    "mac_stub.m",
                },
                .flags = pugl_flags,
            });
            lib.root_module.addCMacro("PuglWindow", ctx.b.fmt("PuglWindow{d}", .{pugl_ver_hash}));
            lib.root_module.addCMacro("PuglWindowDelegate", ctx.b.fmt("PuglWindowDelegate{d}", .{pugl_ver_hash}));
            lib.root_module.addCMacro("PuglWrapperView", ctx.b.fmt("PuglWrapperView{d}", .{pugl_ver_hash}));
            lib.root_module.addCMacro("PuglOpenGLView", ctx.b.fmt("PuglOpenGLView{d}", .{pugl_ver_hash}));

            lib.linkFramework("OpenGL");
            lib.linkFramework("CoreVideo");
        },
        .linux => {
            lib.addCSourceFiles(.{
                .root = src_path,
                .files = &[_][]const u8{
                    "x11.c",
                    "x11_stub.c",
                    "x11_gl.c",
                },
                .flags = pugl_flags,
            });
            lib.root_module.addCMacro("USE_XRANDR", "0");
            lib.root_module.addCMacro("USE_XSYNC", "1");
            lib.root_module.addCMacro("USE_XCURSOR", "1");

            lib.linkSystemLibrary2("gl", .{ .use_pkg_config = linux_use_pkg_config });
            lib.linkSystemLibrary2("glx", .{ .use_pkg_config = linux_use_pkg_config });
            lib.linkSystemLibrary2("x11", .{ .use_pkg_config = linux_use_pkg_config });
            lib.linkSystemLibrary2("xcursor", .{ .use_pkg_config = linux_use_pkg_config });
            lib.linkSystemLibrary2("xext", .{ .use_pkg_config = linux_use_pkg_config });
        },
        else => {},
    }

    lib.root_module.addCMacro("PUGL_DISABLE_DEPRECATED", "1");
    lib.root_module.addCMacro("PUGL_STATIC", "1");

    applyUniversalSettings(ctx, lib);

    return lib;
}

fn buildDebugInfo(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    var opts = cfg.module_options;
    opts.root_source_file = ctx.b.path("src/utils/debug_info/debug_info.zig");
    const lib = ctx.b.addObject(.{
        .name = "debug_info_lib",
        .root_module = ctx.b.createModule(opts),
    });
    lib.linkLibC(); // Means better debug info on Linux
    lib.addIncludePath(ctx.b.path("src/utils/debug_info"));
    return lib;
}

// IMPROVE: does this need to be a library? is foundation/os/plugin all linked together?
fn buildFloeLibrary(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    stb_sprintf: *std.Build.Step.Compile,
    debug_info_lib: *std.Build.Step.Compile,
    tracy: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const lib = ctx.b.addStaticLibrary(.{
        .name = "library",
        .root_module = ctx.b.createModule(cfg.module_options),
    });

    const common_source_files = .{
        "src/utils/debug/debug.cpp",
        "src/utils/cli_arg_parse.cpp",
        "src/utils/leak_detecting_allocator.cpp",
        "src/utils/no_hash.cpp",
        "src/tests/framework.cpp",
        "src/utils/logger/logger.cpp",
        "src/foundation/utils/string.cpp",
        "src/os/filesystem.cpp",
        "src/os/misc.cpp",
        "src/os/web.cpp",
        "src/os/threading.cpp",
    };

    const unix_source_files = .{
        "src/os/filesystem_unix.cpp",
        "src/os/misc_unix.cpp",
        "src/os/threading_pthread.cpp",
    };

    const windows_source_files = .{
        "src/os/filesystem_windows.cpp",
        "src/os/misc_windows.cpp",
        "src/os/threading_windows.cpp",
        "src/os/web_windows.cpp",
    };

    const macos_source_files = .{
        "src/os/filesystem_mac.mm",
        "src/os/misc_mac.mm",
        "src/os/threading_mac.cpp",
        "src/os/web_mac.mm",
    };

    const linux_source_files = .{
        "src/os/filesystem_linux.cpp",
        "src/os/misc_linux.cpp",
        "src/os/threading_linux.cpp",
        "src/os/web_linux.cpp",
    };

    const library_flags = FlagsBuilder.init(ctx, cfg, .{
        .all_warnings = true,
        .ubsan = true,
        .cpp = true,
        .gen_cdb_fragments = true,
    }).flags.items;

    switch (cfg.target.os.tag) {
        .windows => {
            lib.addCSourceFiles(.{
                .files = &windows_source_files,
                .flags = library_flags,
            });
            lib.linkSystemLibrary("dbghelp");
            lib.linkSystemLibrary("shlwapi");
            lib.linkSystemLibrary("ole32");
            lib.linkSystemLibrary("crypt32");
            lib.linkSystemLibrary("uuid");
            lib.linkSystemLibrary("winhttp");

            // synchronization.lib (https://github.com/ziglang/zig/issues/14919)
            lib.linkSystemLibrary("api-ms-win-core-synch-l1-2-0");
        },
        .macos => {
            lib.addCSourceFiles(.{
                .files = &unix_source_files,
                .flags = library_flags,
            });
            lib.addCSourceFiles(.{
                .files = &macos_source_files,
                .flags = FlagsBuilder.init(ctx, cfg, .{
                    .all_warnings = true,
                    .ubsan = true,
                    .objcpp = true,
                    .gen_cdb_fragments = true,
                }).flags.items,
            });
            lib.linkFramework("Cocoa");
            lib.linkFramework("CoreFoundation");
            lib.linkFramework("AppKit");
        },
        .linux => {
            lib.addCSourceFiles(.{ .files = &unix_source_files, .flags = library_flags });
            lib.addCSourceFiles(.{ .files = &linux_source_files, .flags = library_flags });
            lib.linkSystemLibrary2("libcurl", .{ .use_pkg_config = linux_use_pkg_config });
        },
        else => {
            unreachable;
        },
    }

    lib.addCSourceFiles(.{ .files = &common_source_files, .flags = library_flags });
    lib.addConfigHeader(cfg.floe_config_h);
    lib.linkLibC();
    lib.linkLibrary(deps.tracy);
    lib.addObject(deps.debug_info_lib);
    lib.addObject(deps.stb_sprintf);
    applyUniversalSettings(ctx, lib);

    return lib;
}

fn buildStbImage(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    const obj = ctx.b.addObject(.{
        .name = "stb_image",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    obj.addCSourceFile(.{
        .file = ctx.b.path("third_party_libs/stb_image_impls.c"),
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    obj.addIncludePath(ctx.dep_stb.path(""));
    obj.linkLibC();
    return obj;
}

fn buildDrWav(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    const obj = ctx.b.addObject(.{
        .name = "dr_wav",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    obj.addCSourceFile(
        .{
            .file = ctx.b.path("third_party_libs/dr_wav_implementation.c"),
            .flags = FlagsBuilder.init(ctx, cfg, .{
                .gen_cdb_fragments = true,
            }).flags.items,
        },
    );
    obj.addIncludePath(ctx.dep_dr_libs.path(""));
    obj.linkLibC();
    return obj;
}

fn buildMiniz(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    const lib = ctx.b.addStaticLibrary(.{
        .name = "miniz",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    lib.addCSourceFiles(.{
        .root = ctx.dep_miniz.path(""),
        .files = &.{
            "miniz.c",
            "miniz_tdef.c",
            "miniz_tinfl.c",
            "miniz_zip.c",
        },
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    lib.addIncludePath(ctx.dep_miniz.path(""));
    lib.linkLibC();
    lib.addIncludePath(ctx.b.path("third_party_libs/miniz"));

    return lib;
}

fn buildArchiver(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    miniz: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    var opts = cfg.module_options;
    opts.root_source_file = ctx.b.path("src/build/archiver.zig");
    const exe = ctx.b.addExecutable(.{
        .name = "archiver",
        .root_module = ctx.b.createModule(opts),
    });
    exe.linkLibrary(deps.miniz);
    exe.linkLibC();
    applyUniversalSettings(ctx, exe);

    return exe;
}

fn buildFlac(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    const lib = ctx.b.addStaticLibrary(.{
        .name = "flac",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    const flags = FlagsBuilder.init(ctx, cfg, .{
        .gen_cdb_fragments = true,
    }).flags.items;

    lib.addCSourceFiles(.{
        .root = ctx.dep_flac.path("src/libFLAC"),
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
        .flags = flags,
    });

    const config_header = ctx.b.addConfigHeader(
        .{
            .style = .{ .cmake = ctx.dep_flac.path("config.cmake.h.in") },
            .include_path = "config.h",
        },
        .{
            .CPU_IS_BIG_ENDIAN = cfg.target.cpu.arch.endian() == .big,
            .ENABLE_64_BIT_WORDS = cfg.target.ptrBitWidth() == 64,
            .FLAC__ALIGN_MALLOC_DATA = cfg.target.cpu.arch.isX86(),
            .FLAC__CPU_ARM64 = cfg.target.cpu.arch.isAARCH64(),
            .FLAC__SYS_DARWIN = cfg.target.os.tag == .macos,
            .FLAC__SYS_LINUX = cfg.target.os.tag == .linux,
            .HAVE_BYTESWAP_H = cfg.target.os.tag == .linux,
            .HAVE_CPUID_H = cfg.target.cpu.arch.isX86(),
            .HAVE_FSEEKO = true,
            .HAVE_ICONV = cfg.target.os.tag != .windows,
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

    lib.linkLibC();
    lib.root_module.addCMacro("HAVE_CONFIG_H", "");
    lib.addConfigHeader(config_header);
    lib.addIncludePath(ctx.dep_flac.path("include"));
    lib.addIncludePath(ctx.dep_flac.path("src/libFLAC/include"));
    if (cfg.target.os.tag == .windows) {
        lib.root_module.addCMacro("FLAC__NO_DLL", "");
        lib.addCSourceFile(.{
            .file = ctx.dep_flac.path("src/share/win_utf8_io/win_utf8_io.c"),
            .flags = flags,
        });
    }

    return lib;
}

fn bgfxFlags(ctx: *const BuildContext, cfg: *const TargetConfig) []const []const u8 {
    var flags_builder: FlagsBuilder = FlagsBuilder.init(ctx, cfg, .{
        .ubsan = false,
        .gen_cdb_fragments = true,
    });
    flags_builder.addFlags(&[_][]const u8{
        ctx.b.fmt("-DBX_CONFIG_DEBUG={}", .{@intFromBool(ctx.build_mode == .development)}),
        "-std=c++20",
        "-fno-sanitize=all",
        "-Wno-date-time",
        "-fno-strict-aliasing",
        "-fno-exceptions",
        "-fno-rtti",
        "-Wno-tautological-constant-compare",
        "-DBGFX_CONFIG_MULTITHREADED=1",
        switch (cfg.bgfx_api) {
            .vulkan => "-DBGFX_CONFIG_RENDERER_VULKAN=1",
            .metal => "-DBGFX_CONFIG_RENDERER_METAL=1",
            .direct3d11 => "-DBGFX_CONFIG_RENDERER_DIRECT3D11=1",
        },
    });
    return flags_builder.flags.items;
}

fn buildBx(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    const lib = ctx.b.addStaticLibrary(.{
        .name = "bx",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    lib.addCSourceFile(.{
        .file = ctx.dep_bx.path("src/amalgamated.cpp"),
        .flags = bgfxFlags(ctx, cfg),
    });
    lib.addIncludePath(ctx.dep_bx.path("include"));
    lib.addIncludePath(ctx.dep_bx.path("3rdparty"));
    switch (cfg.target.os.tag) {
        .macos => {
            lib.linkFramework("CoreFoundation");
            lib.linkFramework("Foundation");
            lib.addIncludePath(ctx.dep_bx.path("include/compat/osx"));
        },
        .windows => {
            lib.linkSystemLibrary("gdi32");
            lib.addIncludePath(ctx.dep_bx.path("include/compat/mingw"));
        },
        .linux => {
            lib.addIncludePath(ctx.dep_bx.path("include/compat/linux"));
        },
        else => {},
    }
    lib.linkLibCpp();
    applyUniversalSettings(ctx, lib);
    return lib;
}

fn buildBimg(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    bx: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const lib = ctx.b.addStaticLibrary(.{
        .name = "bimg",
        .root_module = ctx.b.createModule(cfg.module_options),
    });

    const flags = bgfxFlags(ctx, cfg);

    lib.addCSourceFiles(.{
        .root = ctx.dep_bimg.path("src"),
        .files = &.{
            "image.cpp",
            "image_gnf.cpp",
        },
        .flags = flags,
    });

    lib.addCSourceFiles(.{
        .root = ctx.dep_bimg.path("3rdparty/astc-encoder/source"),
        .files = &.{
            "astcenc_averages_and_directions.cpp",
            "astcenc_block_sizes.cpp",
            "astcenc_color_quantize.cpp",
            "astcenc_color_unquantize.cpp",
            "astcenc_compress_symbolic.cpp",
            "astcenc_compute_variance.cpp",
            "astcenc_decompress_symbolic.cpp",
            "astcenc_diagnostic_trace.cpp",
            "astcenc_entry.cpp",
            "astcenc_find_best_partitioning.cpp",
            "astcenc_ideal_endpoints_and_weights.cpp",
            "astcenc_image.cpp",
            "astcenc_integer_sequence.cpp",
            "astcenc_mathlib.cpp",
            "astcenc_mathlib_softfloat.cpp",
            "astcenc_partition_tables.cpp",
            "astcenc_percentile_tables.cpp",
            "astcenc_pick_best_endpoint_format.cpp",
            "astcenc_quantization.cpp",
            "astcenc_symbolic_physical.cpp",
            "astcenc_weight_align.cpp",
            "astcenc_weight_quant_xfer_tables.cpp",
        },
        .flags = flags,
    });

    lib.linkLibrary(deps.bx);
    lib.addIncludePath(ctx.dep_bx.path("include"));
    lib.addIncludePath(ctx.dep_bx.path("3rdparty"));
    lib.addIncludePath(ctx.dep_bimg.path("include"));
    lib.addIncludePath(ctx.dep_bimg.path("3rdparty"));
    lib.addIncludePath(ctx.dep_bimg.path("3rdparty/astc-encoder/include"));
    lib.linkLibCpp();
    applyUniversalSettings(ctx, lib);

    return lib;
}

fn buildBgfx(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    bx: *std.Build.Step.Compile,
    bimg: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const flags = bgfxFlags(ctx, cfg);

    const lib = ctx.b.addStaticLibrary(.{
        .name = "bgfx",
        .root_module = ctx.b.createModule(cfg.module_options),
    });

    lib.addCSourceFile(.{
        .file = ctx.dep_bgfx.path(
            if (cfg.target.os.tag == .macos) "src/amalgamated.mm" else "src/amalgamated.cpp",
        ),
        .flags = flags,
    });

    switch (cfg.target.os.tag) {
        .linux => {
            lib.linkSystemLibrary2("x11", .{ .use_pkg_config = linux_use_pkg_config });
            lib.linkSystemLibrary2("xcb", .{ .use_pkg_config = linux_use_pkg_config });
            lib.linkSystemLibrary2("vulkan", .{ .use_pkg_config = linux_use_pkg_config });
        },
        .windows => {
            lib.addIncludePath(ctx.dep_bgfx.path("3rdparty/directx-headers/include/directx"));
        },
        .macos => {
            lib.linkFramework("QuartzCore");
            lib.linkFramework("IOKit");
            lib.linkFramework("Metal");
            lib.linkFramework("OpenGL");
            lib.linkFramework("Cocoa");
        },
        else => {},
    }

    lib.linkLibrary(deps.bx);
    lib.linkLibrary(deps.bimg);
    lib.addIncludePath(ctx.dep_bx.path("include"));
    lib.addIncludePath(ctx.dep_bimg.path("include"));
    lib.addIncludePath(ctx.dep_bgfx.path("include"));
    lib.addIncludePath(ctx.dep_bgfx.path("3rdparty"));
    lib.addIncludePath(ctx.dep_bgfx.path("3rdparty/khronos"));
    applyUniversalSettings(ctx, lib);
    return lib;
}

fn buildFftConvolver(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    const lib = ctx.b.addStaticLibrary(.{
        .name = "fftconvolver",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    var flags_builder: FlagsBuilder = FlagsBuilder.init(ctx, cfg, .{
        .gen_cdb_fragments = true,
    });
    if (cfg.target.os.tag == .macos) {
        lib.linkFramework("Accelerate");
        flags_builder.addFlag("-DAUDIOFFT_APPLE_ACCELERATE");
        flags_builder.addFlag("-ObjC++");
    } else {
        lib.addCSourceFile(.{
            .file = ctx.dep_pffft.path("pffft.c"),
            .flags = FlagsBuilder.init(ctx, cfg, .{
                .gen_cdb_fragments = true,
            }).flags.items,
        });
        flags_builder.addFlag("-DAUDIOFFT_PFFFT");
    }

    lib.addCSourceFiles(.{
        .files = &.{
            "third_party_libs/FFTConvolver/AudioFFT.cpp",
            "third_party_libs/FFTConvolver/FFTConvolver.cpp",
            "third_party_libs/FFTConvolver/TwoStageFFTConvolver.cpp",
            "third_party_libs/FFTConvolver/Utilities.cpp",
            "third_party_libs/FFTConvolver/wrapper.cpp",
        },
        .flags = flags_builder.flags.items,
    });
    lib.linkLibCpp();
    lib.addIncludePath(ctx.dep_pffft.path(""));
    applyUniversalSettings(ctx, lib);

    return lib;
}

fn buildCommonInfrastructure(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    dr_wav: *std.Build.Step.Compile,
    flac: *std.Build.Step.Compile,
    xxhash: *std.Build.Step.Compile,
    library: *std.Build.Step.Compile,
    miniz: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const lua = blk2: {
        const lib = ctx.b.addStaticLibrary(.{
            .name = "lua",
            .target = cfg.resolved_target,
            .optimize = ctx.optimise,
        });
        const flags = [_][]const u8{
            switch (cfg.target.os.tag) {
                .linux => "-DLUA_USE_LINUX",
                .macos => "-DLUA_USE_MACOSX",
                .windows => "-DLUA_USE_WINDOWS",
                else => "-DLUA_USE_POSIX",
            },
            if (ctx.optimise == .Debug) "-DLUA_USE_APICHECK" else "",
        };

        // compile as C++ so it uses exceptions instead of setjmp/longjmp. we use try/catch when handling lua
        lib.addCSourceFile(.{
            .file = ctx.b.path("third_party_libs/lua.cpp"),
            .flags = &flags,
        });
        lib.addIncludePath(ctx.dep_lua.path(""));
        lib.linkLibC();

        break :blk2 lib;
    };

    const src_root = ctx.b.path("src/common_infrastructure");

    const lib = ctx.b.addStaticLibrary(.{
        .name = "common_infrastructure",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    lib.addCSourceFiles(.{
        .root = src_root,
        .files = &.{
            "audio_utils.cpp",
            "autosave.cpp",
            "checksum_crc32_file.cpp",
            "common_errors.cpp",
            "descriptors/param_descriptors.cpp",
            "error_reporting.cpp",
            "folder_node.cpp",
            "global.cpp",
            "package_format.cpp",
            "paths.cpp",
            "persistent_store.cpp",
            "preferences.cpp",
            "preset_bank_info.cpp",
            "sample_library/audio_file.cpp",
            "sample_library/sample_library.cpp",
            "sample_library/sample_library_lua.cpp",
            "sample_library/sample_library_mdata.cpp",
            "sentry/sentry.cpp",
            "state/macros.cpp",
            "state/state_coding.cpp",
        },
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .all_warnings = true,
            .ubsan = true,
            .cpp = true,
            .gen_cdb_fragments = true,
        }).flags.items,
    });

    lib.linkLibrary(lua);
    lib.addObject(deps.dr_wav);
    lib.linkLibrary(deps.flac);
    lib.addObject(deps.xxhash);
    lib.addConfigHeader(cfg.floe_config_h);
    lib.addIncludePath(src_root);
    lib.linkLibrary(deps.library);
    lib.linkLibrary(deps.miniz);
    applyUniversalSettings(ctx, lib);

    return lib;
}

fn buildEmbeddedFiles(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    var opts = cfg.module_options;
    const root_path = "build_resources/embedded_files.zig";
    opts.root_source_file = ctx.b.path(root_path);
    const obj = ctx.b.addObject(.{
        .name = "embedded_files",
        .root_module = ctx.b.createModule(opts),
    });
    {
        var embedded_files_options = ctx.b.addOptions();
        if (ctx.dep_floe_logos) |logos| {
            const update = ctx.b.addUpdateSourceFiles();

            // Zig's @embedFile only works with paths lower than the root_source_file, so we have to copy them
            // into a subfolder and work out the relative paths.
            const logo_path = "build_resources/external/logo.png";
            update.addCopyFileToSource(logos.path("rasterized/plugin-gui-logo.png"), logo_path);
            const icon_path = "build_resources/external/icon.png";
            update.addCopyFileToSource(logos.path("rasterized/icon-background-256px.png"), icon_path);
            embedded_files_options.step.dependOn(&update.step);

            const root_dir = std.fs.path.dirname(root_path).?;

            embedded_files_options.addOption(?[]const u8, "logo_file", std.fs.path.relative(ctx.b.allocator, root_dir, logo_path) catch unreachable);
            embedded_files_options.addOption(?[]const u8, "icon_file", std.fs.path.relative(ctx.b.allocator, root_dir, icon_path) catch unreachable);
        } else {
            embedded_files_options.addOption(?[]const u8, "logo_file", null);
            embedded_files_options.addOption(?[]const u8, "icon_file", null);
        }
        obj.root_module.addOptions("build_options", embedded_files_options);
    }
    obj.linkLibC();
    obj.addIncludePath(ctx.b.path("build_resources"));
    return obj;
}

fn buildPluginLib(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    common_infrastructure: *std.Build.Step.Compile,
    library: *std.Build.Step.Compile,
    fft_convolver: *std.Build.Step.Compile,
    embedded_files: *std.Build.Step.Compile,
    tracy: *std.Build.Step.Compile,
    pugl: *std.Build.Step.Compile,
    stb_image: *std.Build.Step.Compile,
    vitfx: *std.Build.Step.Compile,
    bgfx: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const lib = ctx.b.addStaticLibrary(.{
        .name = "plugin",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    const src_root = ctx.b.path("src/plugin");

    const flags = FlagsBuilder.init(ctx, cfg, .{
        .all_warnings = true,
        .ubsan = true,
        .cpp = true,
        .gen_cdb_fragments = true,
    }).flags.items;

    lib.addCSourceFiles(.{
        .root = src_root,
        .files = &(.{
            "engine/check_for_update.cpp",
            "engine/engine.cpp",
            "engine/favourite_items.cpp",
            "engine/package_installation.cpp",
            "engine/shared_engine_systems.cpp",
            "gui/controls/gui_curve_map.cpp",
            "gui/controls/gui_envelope.cpp",
            "gui/controls/gui_keyboard.cpp",
            "gui/controls/gui_waveform.cpp",
            "gui/core/gui_library_images.cpp",
            "gui/core/gui_prefs.cpp",
            "gui/core/gui_state.cpp",
            "gui/core/gui_waveform_images.cpp",
            "gui/debug/gui_developer_panel.cpp",
            "gui/elements/gui2_common_modal_panel.cpp",
            "gui/elements/gui2_parameter_component.cpp",
            "gui/elements/gui_drawing_helpers.cpp",
            "gui/elements/gui_utils.cpp",
            "gui/panels/gui2_bot_panel.cpp",
            "gui/panels/gui2_common_browser.cpp",
            "gui/panels/gui2_inst_browser.cpp",
            "gui/panels/gui2_ir_browser.cpp",
            "gui/panels/gui2_legacy_params_panel.cpp",
            "gui/panels/gui2_library_dev_panel.cpp",
            "gui/panels/gui2_macros.cpp",
            "gui/panels/gui2_preset_browser.cpp",
            "gui/panels/gui2_save_preset_panel.cpp",
            "gui/panels/gui2_top_panel.cpp",
            "gui/panels/gui_effects.cpp",
            "gui/panels/gui_layer.cpp",
            "gui/panels/gui_mid_panel.cpp",
            "gui/old/gui_button_widgets.cpp",
            "gui/old/gui_dragger_widgets.cpp",
            "gui/old/gui_knob_widgets.cpp",
            "gui/old/gui_label_widgets.cpp",
            "gui/old/gui_widget_compounds.cpp",
            "gui/old/gui_widget_helpers.cpp",
            "gui_framework/app_window.cpp",
            "gui_framework/draw_list.cpp",
            "gui_framework/fonts.cpp",
            "gui_framework/gui_builder.cpp",
            "gui_framework/gui_frame.cpp",
            "gui_framework/gui_imgui.cpp",
            "gui_framework/gui_live_edit.cpp",
            "gui_framework/image.cpp",
            "gui_framework/layout.cpp",
            "gui_framework/renderer.cpp",
            "gui_framework/renderer_bgfx.cpp",
            "plugin/hosting_tests.cpp",
            "plugin/plugin.cpp",
            "preset_server/preset_server.cpp",
            "processing_utils/midi.cpp",
            "processing_utils/volume_fade.cpp",
            "processor/layer_processor.cpp",
            "processor/processor.cpp",
            "processor/sample_processing.cpp",
            "processor/voices.cpp",
            "sample_lib_server/sample_library_server.cpp",
            "sample_lib_server/scan_folders.cpp",
        }),
        .flags = flags,
    });

    switch (cfg.target.os.tag) {
        .windows => {
            lib.addCSourceFiles(.{
                .root = src_root,
                .files = &[_][]const u8{
                    "gui_framework/app_window_windows.cpp",
                    "gui_framework/renderer_direct3d9.cpp",
                    "gui_framework/renderer_bgfx_init_window_windows.cpp",
                },
                .flags = flags,
            });
            lib.linkSystemLibrary("d3d9");
        },
        .linux => {
            lib.addCSourceFiles(.{
                .root = src_root,
                .files = &[_][]const u8{
                    "gui_framework/app_window_linux.cpp",
                    "gui_framework/renderer_opengl.cpp",
                    "gui_framework/renderer_bgfx_init_window_linux.cpp",
                },
                .flags = flags,
            });
        },
        .macos => {
            lib.addCSourceFiles(.{
                .root = src_root,
                .files = &[_][]const u8{
                    "gui_framework/app_window_mac.mm",
                    "gui_framework/renderer_opengl.cpp",
                    "gui_framework/renderer_bgfx_init_window_macos.mm",
                },
                .flags = FlagsBuilder.init(ctx, cfg, .{
                    .all_warnings = true,
                    .ubsan = true,
                    .objcpp = true,
                    .gen_cdb_fragments = true,
                }).flags.items,
            });
        },
        else => {
            unreachable;
        },
    }

    {
        const license_files = [_]struct {
            variable: []const u8,
            file: []const u8,
        }{
            .{ .variable = "gpl_3_0_or_later", .file = "GPL-3.0-or-later.txt" },
            .{ .variable = "apache_2_0", .file = "Apache-2.0.txt" },
            .{ .variable = "fftpack", .file = "LicenseRef-FFTPACK.txt" },
            .{ .variable = "ofl_1_1", .file = "OFL-1.1.txt" },
            .{ .variable = "bsd_3_clause", .file = "BSD-3-Clause.txt" },
            .{ .variable = "bsd_2_clause", .file = "BSD-2-Clause.txt" },
            .{ .variable = "isc", .file = "ISC.txt" },
            .{ .variable = "mit", .file = "MIT.txt" },
        };

        lib.addCSourceFile(.{
            .file = ctx.b.addWriteFiles().add("licenses.c", file_content: {
                var data = std.ArrayList(u8).init(ctx.b.allocator);
                const writer = data.writer();
                for (license_files) |l| {
                    std.fmt.format(writer,
                        \\const char {[vari]s}_license[] = {{
                        \\#embed "LICENSES/{[file]s}"
                        \\}};
                        \\const unsigned {[vari]s}_license_size = sizeof({[vari]s}_license);
                        \\
                    , .{ .vari = l.variable, .file = l.file }) catch @panic("OOM");
                }
                break :file_content data.toOwnedSlice() catch @panic("OOM");
            }),
            .flags = &.{"-std=c23"},
        });

        const generated_include_dir = ctx.b.addWriteFiles();
        _ = generated_include_dir.add("license_texts.h", file_content: {
            var data = std.ArrayList(u8).init(ctx.b.allocator);
            const writer = data.writer();
            for (license_files) |l| {
                std.fmt.format(writer,
                    \\extern const char {[vari]s}_license[];
                    \\extern const unsigned {[vari]s}_license_size;
                , .{ .vari = l.variable }) catch @panic("OOM");
            }
            break :file_content data.toOwnedSlice() catch @panic("OOM");
        });
        lib.addIncludePath(generated_include_dir.getDirectory());
    }

    lib.addIncludePath(ctx.b.path("src/plugin"));
    lib.addIncludePath(ctx.b.path("src"));
    lib.addConfigHeader(cfg.floe_config_h);
    lib.linkLibrary(deps.library);
    lib.linkLibrary(deps.common_infrastructure);
    lib.linkLibrary(deps.fft_convolver);
    lib.addObject(deps.embedded_files);
    lib.linkLibrary(deps.tracy);
    lib.linkLibrary(deps.pugl);
    lib.linkLibrary(deps.bgfx);
    lib.addIncludePath(ctx.dep_bx.path("include"));
    lib.addIncludePath(ctx.dep_bimg.path("include"));
    lib.addIncludePath(ctx.dep_bgfx.path("include"));
    lib.addObject(deps.stb_image);
    lib.addIncludePath(ctx.b.path("src/plugin/gui/live_edit_defs"));
    lib.linkLibrary(deps.vitfx);
    applyUniversalSettings(ctx, lib);

    return lib;
}

fn buildDocsGenerator(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    common_infrastructure: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const exe = ctx.b.addExecutable(.{
        .name = "docs_generator",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    exe.addCSourceFiles(.{
        .files = &.{
            "src/docs_generator/docs_generator.cpp",
            "src/common_infrastructure/final_binary_type.cpp",
        },
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .all_warnings = true,
            .ubsan = true,
            .cpp = true,
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    exe.root_module.addCMacro("FINAL_BINARY_TYPE", "DocsGenerator");
    exe.linkLibrary(deps.common_infrastructure);
    exe.addIncludePath(ctx.b.path("src"));
    exe.addConfigHeader(cfg.floe_config_h);
    applyUniversalSettings(ctx, exe);

    return exe;
}

fn buildPackager(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    common_infrastructure: *std.Build.Step.Compile,
    embedded_files: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    var exe = ctx.b.addExecutable(.{
        .name = "floe-packager",
        .root_module = ctx.b.createModule(cfg.module_options),
        .version = ctx.floe_version,
    });
    exe.addCSourceFiles(.{
        .files = &.{
            "src/packager_tool/packager.cpp",
            "src/common_infrastructure/final_binary_type.cpp",
        },
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .all_warnings = true,
            .ubsan = true,
            .cpp = true,
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    exe.root_module.addCMacro("FINAL_BINARY_TYPE", "Packager");
    exe.linkLibrary(deps.common_infrastructure);
    exe.addIncludePath(ctx.b.path("src"));
    exe.addConfigHeader(cfg.floe_config_h);
    exe.addObject(deps.embedded_files);

    applyUniversalSettings(ctx, exe);

    return exe;
}

fn buildPresetEditor(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    common_infrastructure: *std.Build.Step.Compile,
    embedded_files: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    var exe = ctx.b.addExecutable(.{
        .name = "preset-editor",
        .root_module = ctx.b.createModule(cfg.module_options),
        .version = ctx.floe_version,
    });
    exe.addCSourceFiles(.{
        .files = &.{
            "src/preset_editor_tool/preset_editor.cpp",
            "src/common_infrastructure/final_binary_type.cpp",
        },
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .all_warnings = true,
            .ubsan = true,
            .cpp = true,
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    exe.root_module.addCMacro("FINAL_BINARY_TYPE", "PresetEditor");
    exe.linkLibrary(deps.common_infrastructure);
    exe.addIncludePath(ctx.b.path("src"));
    exe.addConfigHeader(cfg.floe_config_h);
    exe.addObject(deps.embedded_files);
    applyUniversalSettings(ctx, exe);
    return exe;
}

fn buildClap(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    plugin: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const dso = ctx.b.addSharedLibrary(.{
        .name = "Floe.clap",
        .root_module = ctx.b.createModule(cfg.module_options),
        .version = ctx.floe_version,
    });
    dso.addCSourceFiles(.{
        .files = &.{
            "src/plugin/plugin/plugin_entry.cpp",
            "src/common_infrastructure/final_binary_type.cpp",
        },
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .all_warnings = true,
            .ubsan = true,
            .cpp = true,
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    dso.root_module.addCMacro("FINAL_BINARY_TYPE", "Clap");
    dso.addConfigHeader(cfg.floe_config_h);
    dso.addIncludePath(ctx.b.path("src"));
    dso.linkLibrary(deps.plugin);

    applyUniversalSettings(ctx, dso);
    addWindowsEmbedInfo(dso, .{
        .name = "Floe CLAP",
        .description = constants.floe_description,
        .icon_path = null,
    }) catch @panic("OOM");

    return dso;
}

fn buildStandalone(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    plugin: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const miniaudio = blk: {
        const lib = ctx.b.addStaticLibrary(.{
            .name = "miniaudio",
            .root_module = ctx.b.createModule(cfg.module_options),
        });
        lib.addCSourceFile(.{
            .file = ctx.b.path("third_party_libs/miniaudio.c"),
            .flags = FlagsBuilder.init(ctx, cfg, .{
                .gen_cdb_fragments = true,
            }).flags.items,
        });
        // NOTE(Sam): disabling alsa because it was causing a deadlock on my machine.
        lib.root_module.addCMacro("MA_NO_ALSA", "1");
        lib.linkLibC();
        lib.addIncludePath(ctx.dep_miniaudio.path(""));
        switch (cfg.target.os.tag) {
            .macos => {
                lib.linkFramework("CoreAudio");
            },
            .windows => {
                lib.linkSystemLibrary("dsound");
            },
            .linux => {
                lib.linkSystemLibrary2("alsa", .{ .use_pkg_config = linux_use_pkg_config });
            },
            else => {
                unreachable;
            },
        }
        applyUniversalSettings(ctx, lib);

        break :blk lib;
    };

    const portmidi = blk: {
        const lib = ctx.b.addStaticLibrary(.{
            .name = "portmidi",
            .root_module = ctx.b.createModule(cfg.module_options),
        });
        const pm_root = ctx.dep_portmidi.path("");
        const pm_flags = FlagsBuilder.init(ctx, cfg, .{
            .gen_cdb_fragments = true,
            .minimise_windows = false,
        }).flags.items;
        lib.addCSourceFiles(.{
            .root = pm_root,
            .files = &.{
                "pm_common/portmidi.c",
                "pm_common/pmutil.c",
                "porttime/porttime.c",
            },
            .flags = pm_flags,
        });
        switch (cfg.target.os.tag) {
            .macos => {
                lib.addCSourceFiles(.{
                    .root = pm_root,
                    .files = &.{
                        "pm_mac/pmmacosxcm.c",
                        "pm_mac/pmmac.c",
                        "porttime/ptmacosx_cf.c",
                        "porttime/ptmacosx_mach.c",
                    },
                    .flags = pm_flags,
                });
                lib.linkFramework("CoreAudio");
                lib.linkFramework("CoreMIDI");
            },
            .windows => {
                lib.addCSourceFiles(.{
                    .root = pm_root,
                    .files = &.{
                        "pm_win/pmwin.c",
                        "pm_win/pmwinmm.c",
                        "porttime/ptwinmm.c",
                    },
                    .flags = pm_flags,
                });
                lib.linkSystemLibrary("winmm");
            },
            .linux => {
                lib.addCSourceFiles(.{
                    .root = pm_root,
                    .files = &.{
                        "pm_linux/pmlinux.c",
                        "pm_linux/pmlinuxalsa.c",
                        "porttime/ptlinux.c",
                    },
                    .flags = pm_flags,
                });
                lib.root_module.addCMacro("PMALSA", "1");
                lib.linkSystemLibrary2("alsa", .{ .use_pkg_config = linux_use_pkg_config });
            },
            else => {
                unreachable;
            },
        }

        lib.linkLibC();
        lib.addIncludePath(ctx.dep_portmidi.path("porttime"));
        lib.addIncludePath(ctx.dep_portmidi.path("pm_common"));
        applyUniversalSettings(ctx, lib);

        break :blk lib;
    };

    const exe = ctx.b.addExecutable(.{
        .name = "floe_standalone",
        .root_module = ctx.b.createModule(cfg.module_options),
    });

    exe.addCSourceFiles(.{
        .files = &.{
            "src/standalone_wrapper/standalone_wrapper.cpp",
            "src/plugin/plugin/plugin_entry.cpp",
            "src/common_infrastructure/final_binary_type.cpp",
        },
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .all_warnings = true,
            .ubsan = true,
            .cpp = true,
            .gen_cdb_fragments = true,
        }).flags.items,
    });

    exe.root_module.addCMacro("FINAL_BINARY_TYPE", "Standalone");
    exe.addConfigHeader(cfg.floe_config_h);
    exe.addIncludePath(ctx.b.path("src"));
    exe.linkLibrary(portmidi);
    exe.linkLibrary(miniaudio);
    exe.addIncludePath(ctx.dep_miniaudio.path(""));
    exe.linkLibrary(deps.plugin);
    applyUniversalSettings(ctx, exe);

    return exe;
}

fn vst3Flags(ctx: *const BuildContext, cfg: *const TargetConfig) [][]const u8 {
    var flags = FlagsBuilder.init(ctx, cfg, .{
        .ubsan = false,
        .gen_cdb_fragments = true,
        .minimise_windows = false,
    });
    if (ctx.optimise == .Debug) {
        flags.addFlag("-DDEVELOPMENT=1");
    } else {
        flags.addFlag("-DRELEASE=1");
    }
    // Ignore warning about non-reproducible __DATE__ usage.
    flags.addFlag("-Wno-date-time");

    return flags.flags.items;
}

fn buildVst3Sdk(ctx: *const BuildContext, cfg: *const TargetConfig) *std.Build.Step.Compile {
    const lib = ctx.b.addStaticLibrary(.{
        .name = "VST3",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    lib.addCSourceFiles(.{
        .root = ctx.dep_vst3_sdk.path(""),
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
        .flags = vst3Flags(ctx, cfg),
    });

    switch (cfg.target.os.tag) {
        .windows => {},
        .linux => {},
        .macos => {
            lib.linkFramework("CoreFoundation");
            lib.linkFramework("Foundation");
        },
        else => {},
    }

    lib.addIncludePath(ctx.dep_vst3_sdk.path(""));
    lib.linkLibCpp();
    applyUniversalSettings(ctx, lib);

    return lib;
}

fn buildVst3Validator(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    library: *std.Build.Step.Compile,
    vst3_sdk: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const exe = ctx.b.addExecutable(.{
        .name = "VST3-Validator",
        .root_module = ctx.b.createModule(cfg.module_options),
    });

    const flags = vst3Flags(ctx, cfg);

    exe.addCSourceFiles(.{
        .root = ctx.dep_vst3_sdk.path("public.sdk"),
        .files = &.{
            "source/common/memorystream.cpp",
            "source/main/moduleinit.cpp",
            "source/vst/moduleinfo/moduleinfoparser.cpp",
            "source/vst/hosting/test/connectionproxytest.cpp",
            "source/vst/hosting/test/eventlisttest.cpp",
            "source/vst/hosting/test/hostclassestest.cpp",
            "source/vst/hosting/test/parameterchangestest.cpp",
            "source/vst/hosting/test/pluginterfacesupporttest.cpp",
            "source/vst/hosting/test/processdatatest.cpp",
            "source/vst/hosting/plugprovider.cpp",
            "source/vst/testsuite/bus/busactivation.cpp",
            "source/vst/testsuite/bus/busconsistency.cpp",
            "source/vst/testsuite/bus/businvalidindex.cpp",
            "source/vst/testsuite/bus/checkaudiobusarrangement.cpp",
            "source/vst/testsuite/bus/scanbusses.cpp",
            "source/vst/testsuite/bus/sidechainarrangement.cpp",
            "source/vst/testsuite/general/editorclasses.cpp",
            "source/vst/testsuite/general/midilearn.cpp",
            "source/vst/testsuite/general/midimapping.cpp",
            "source/vst/testsuite/general/plugcompat.cpp",
            "source/vst/testsuite/general/scanparameters.cpp",
            "source/vst/testsuite/general/suspendresume.cpp",
            "source/vst/testsuite/general/terminit.cpp",
            "source/vst/testsuite/noteexpression/keyswitch.cpp",
            "source/vst/testsuite/noteexpression/noteexpression.cpp",
            "source/vst/testsuite/processing/automation.cpp",
            "source/vst/testsuite/processing/process.cpp",
            "source/vst/testsuite/processing/processcontextrequirements.cpp",
            "source/vst/testsuite/processing/processformat.cpp",
            "source/vst/testsuite/processing/processinputoverwriting.cpp",
            "source/vst/testsuite/processing/processtail.cpp",
            "source/vst/testsuite/processing/processthreaded.cpp",
            "source/vst/testsuite/processing/silenceflags.cpp",
            "source/vst/testsuite/processing/silenceprocessing.cpp",
            "source/vst/testsuite/processing/speakerarrangement.cpp",
            "source/vst/testsuite/processing/variableblocksize.cpp",
            "source/vst/testsuite/state/bypasspersistence.cpp",
            "source/vst/testsuite/state/invalidstatetransition.cpp",
            "source/vst/testsuite/state/repeatidenticalstatetransition.cpp",
            "source/vst/testsuite/state/validstatetransition.cpp",
            "source/vst/testsuite/testbase.cpp",
            "source/vst/testsuite/unit/checkunitstructure.cpp",
            "source/vst/testsuite/unit/scanprograms.cpp",
            "source/vst/testsuite/unit/scanunits.cpp",
            "source/vst/testsuite/vsttestsuite.cpp",
            "source/vst/utility/testing.cpp",

            "samples/vst-hosting/validator/source/main.cpp",
            "samples/vst-hosting/validator/source/usediids.cpp",
            "samples/vst-hosting/validator/source/validator.cpp",

            "source/vst/hosting/connectionproxy.cpp",
            "source/vst/hosting/eventlist.cpp",
            "source/vst/hosting/hostclasses.cpp",
            "source/vst/hosting/module.cpp",
            "source/vst/hosting/parameterchanges.cpp",
            "source/vst/hosting/pluginterfacesupport.cpp",
            "source/vst/hosting/processdata.cpp",
            "source/vst/vstpresetfile.cpp",
        },
        .flags = flags,
    });

    switch (cfg.target.os.tag) {
        .windows => {
            exe.addCSourceFiles(.{
                .root = ctx.dep_vst3_sdk.path(""),
                .files = &.{"public.sdk/source/vst/hosting/module_win32.cpp"},
                .flags = flags,
            });
            exe.linkSystemLibrary("ole32");
        },
        .linux => {
            exe.addCSourceFiles(.{
                .root = ctx.dep_vst3_sdk.path(""),
                .files = &.{"public.sdk/source/vst/hosting/module_linux.cpp"},
                .flags = flags,
            });
        },
        .macos => {
            exe.addCSourceFiles(.{
                .root = ctx.dep_vst3_sdk.path(""),
                .files = &.{"public.sdk/source/vst/hosting/module_mac.mm"},
                .flags = FlagsBuilder.init(ctx, cfg, .{
                    .objcpp = true,
                    .gen_cdb_fragments = true,
                }).flags.items,
            });
        },
        else => {},
    }

    exe.addIncludePath(ctx.dep_vst3_sdk.path(""));
    exe.linkLibCpp();
    exe.linkLibrary(deps.vst3_sdk);
    exe.linkLibrary(deps.library); // for ubsan runtime
    applyUniversalSettings(ctx, exe);

    return exe;
}

fn buildVst3(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    plugin: *std.Build.Step.Compile,
    vst3_sdk: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const dso = ctx.b.addSharedLibrary(.{
        .name = "Floe.vst3",
        .version = ctx.floe_version,
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    switch (cfg.target.os.tag) {
        .windows => {
            dso.root_module.addCMacro("WIN", "1");
        },
        .linux => {
            dso.root_module.addCMacro("LIN", "1");
        },
        .macos => {
            dso.root_module.addCMacro("MAC", "1");
        },
        else => {},
    }
    if (ctx.optimise == .Debug) {
        dso.root_module.addCMacro("DEVELOPMENT", "1");
    } else {
        dso.root_module.addCMacro("RELEASE", "1");
    }
    dso.root_module.addCMacro("MACOS_USE_STD_FILESYSTEM", "1");
    dso.root_module.addCMacro("CLAP_WRAPPER_VERSION", "\"0.11.0\"");
    dso.root_module.addCMacro("STATICALLY_LINKED_CLAP_ENTRY", "1");

    var flags = FlagsBuilder.init(ctx, cfg, .{
        .ubsan = false,
        .gen_cdb_fragments = true,
    });
    flags.addFlag("-fno-char8_t");

    dso.addCSourceFiles(.{
        .files = &.{
            "src/plugin/plugin/plugin_entry.cpp",
            "src/common_infrastructure/final_binary_type.cpp",
        },
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .all_warnings = true,
            .ubsan = true,
            .cpp = true,
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    dso.root_module.addCMacro("FINAL_BINARY_TYPE", "Vst3");

    const wrapper_src_path = ctx.dep_clap_wrapper.path("src");
    dso.addCSourceFiles(.{
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

    switch (cfg.target.os.tag) {
        .windows => {
            dso.addCSourceFile(.{
                .file = ctx.dep_clap_wrapper.path("src/detail/os/windows.cpp"),
                .flags = flags.flags.items,
            });
            dso.addCSourceFiles(.{
                .root = ctx.dep_vst3_sdk.path(""),
                .files = &.{"public.sdk/source/main/dllmain.cpp"},
                .flags = flags.flags.items,
            });
        },
        .linux => {
            dso.addCSourceFile(.{
                .file = ctx.dep_clap_wrapper.path("src/detail/os/linux.cpp"),
                .flags = flags.flags.items,
            });
            dso.addCSourceFiles(.{
                .root = ctx.dep_vst3_sdk.path(""),
                .files = &.{"public.sdk/source/main/linuxmain.cpp"},
                .flags = flags.flags.items,
            });
        },
        .macos => {
            dso.addCSourceFiles(.{
                .root = wrapper_src_path,
                .files = &.{
                    "detail/os/macos.mm",
                    "detail/clap/mac_helpers.mm",
                },
                .flags = flags.flags.items,
            });
            dso.addCSourceFiles(.{
                .root = ctx.dep_vst3_sdk.path(""),
                .files = &.{"public.sdk/source/main/macmain.cpp"},
                .flags = flags.flags.items,
            });
        },
        else => {},
    }

    dso.addIncludePath(ctx.dep_clap_wrapper.path("include"));
    dso.addIncludePath(ctx.dep_clap_wrapper.path("libs/fmt"));
    dso.addIncludePath(ctx.dep_clap_wrapper.path("libs/psl"));
    dso.addIncludePath(ctx.dep_clap_wrapper.path("src"));
    dso.addIncludePath(ctx.dep_vst3_sdk.path(""));
    dso.linkLibCpp();

    dso.linkLibrary(deps.plugin);
    dso.linkLibrary(deps.vst3_sdk);

    dso.addConfigHeader(cfg.floe_config_h);
    dso.addIncludePath(ctx.b.path("src"));

    applyUniversalSettings(ctx, dso);
    addWindowsEmbedInfo(dso, .{
        .name = "Floe VST3",
        .description = constants.floe_description,
        .icon_path = null,
    }) catch @panic("OOM");

    return dso;
}

fn buildAu(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    plugin: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const au_sdk = blk2: {
        const lib = ctx.b.addStaticLibrary(.{
            .name = "AU",
            .root_module = ctx.b.createModule(cfg.module_options),
        });

        lib.addCSourceFiles(.{
            .root = ctx.dep_au_sdk.path("src/AudioUnitSDK"),
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
            .flags = FlagsBuilder.init(ctx, cfg, .{
                .cpp = true,
                .gen_cdb_fragments = true,
            }).flags.items,
        });
        lib.addIncludePath(ctx.dep_au_sdk.path("include"));
        lib.linkLibCpp();
        applyUniversalSettings(ctx, lib);

        break :blk2 lib;
    };

    const flags = blk2: {
        var flags = FlagsBuilder.init(ctx, cfg, .{
            .gen_cdb_fragments = true,
        });
        switch (cfg.target.os.tag) {
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
        if (ctx.optimise == .Debug) {
            flags.addFlag("-DDEVELOPMENT=1");
        } else {
            flags.addFlag("-DRELEASE=1");
        }
        flags.addFlag("-fno-char8_t");
        flags.addFlag("-DMACOS_USE_STD_FILESYSTEM=1");
        flags.addFlag("-DCLAP_WRAPPER_VERSION=\"0.11.0\"");
        flags.addFlag("-DSTATICALLY_LINKED_CLAP_ENTRY=1");
        break :blk2 flags.flags.items;
    };

    const dso = ctx.b.addSharedLibrary(.{
        .name = "Floe.component",
        .root_module = ctx.b.createModule(cfg.module_options),
        .version = ctx.floe_version,
    });
    dso.addCSourceFiles(.{
        .files = &.{
            "src/plugin/plugin/plugin_entry.cpp",
            "src/common_infrastructure/final_binary_type.cpp",
        },
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .all_warnings = true,
            .ubsan = true,
            .objcpp = true,
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    dso.root_module.addCMacro("FINAL_BINARY_TYPE", "AuV2");

    const wrapper_src_path = ctx.dep_clap_wrapper.path("src");

    dso.addCSourceFiles(.{
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
        .flags = flags,
    });

    {
        const generated_files = ctx.b.addWriteFiles();

        _ = generated_files.add("generated_entrypoints.hxx", ctx.b.fmt(
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
        }));

        _ = generated_files.add("generated_cocoaclasses.hxx", ctx.b.fmt(
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
            \\ bool fillAudioUnitCocoaView(AudioUnitCocoaViewInfo* viewInfo, 
            \\                             std::shared_ptr<Clap::Plugin> _plugin) {{
            \\     if (strcmp(_plugin->_plugin->desc->id, "{[clap_id]s}") == 0) {{
            \\         if (!_plugin->_ext._gui) return false;
            \\         return fillAUCV_{[name]s}(viewInfo);
            \\     }}
            \\ }}
        , .{
            .name = ctx.b.fmt("Floe{d}", .{ctx.floe_version_hash}),
            .clap_id = constants.floe_clap_id,
        }));

        dso.addIncludePath(generated_files.getDirectory());
    }

    dso.addIncludePath(ctx.b.path("third_party_libs/clap/include"));
    dso.addIncludePath(ctx.dep_au_sdk.path("include"));
    dso.addIncludePath(ctx.dep_clap_wrapper.path("include"));
    dso.addIncludePath(ctx.dep_clap_wrapper.path("libs/fmt"));
    dso.addIncludePath(ctx.dep_clap_wrapper.path("src"));
    dso.linkLibCpp();

    dso.linkLibrary(deps.plugin);
    dso.linkLibrary(au_sdk);
    dso.linkFramework("AudioToolbox");
    dso.linkFramework("CoreMIDI");

    dso.addConfigHeader(cfg.floe_config_h);
    dso.addIncludePath(ctx.b.path("src"));

    applyUniversalSettings(ctx, dso);

    return dso;
}

fn buildWindowsUninstaller(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    library: *std.Build.Step.Compile,
    stb_image: *std.Build.Step.Compile,
    common_infrastructure: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const exe = ctx.b.addExecutable(.{
        .name = "Floe-Uninstaller",
        .root_module = ctx.b.createModule(cfg.module_options),
        .version = ctx.floe_version,
        .win32_manifest = ctx.b.addWriteFiles().add("xml.manifest", windowsManifestContent(ctx.b, .{
            .name = "Uninstaller",
            .description = "Uninstaller for Floe plugins",
            .require_admin = if (ctx.build_mode == .production) true else ctx.windows_installer_require_admin,
        })),
    });
    exe.subsystem = .Windows;

    exe.root_module.addCMacro(
        "UNINSTALLER_BINARY_NAME",
        ctx.b.fmt("\"{s}\"", .{exe.out_filename}),
    );

    exe.addCSourceFiles(.{
        .files = &.{
            "src/windows_installer/uninstaller.cpp",
            "src/windows_installer/gui.cpp",
            "src/common_infrastructure/final_binary_type.cpp",
        },
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .all_warnings = true,
            .ubsan = true,
            .cpp = true,
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    exe.root_module.addCMacro("FINAL_BINARY_TYPE", "WindowsUninstaller");
    exe.linkSystemLibrary("gdi32");
    exe.linkSystemLibrary("version");
    exe.linkSystemLibrary("comctl32");
    exe.addConfigHeader(cfg.floe_config_h);
    exe.addIncludePath(ctx.b.path("src"));
    exe.addObject(deps.stb_image);
    exe.linkLibrary(deps.library);
    exe.linkLibrary(deps.common_infrastructure);
    applyUniversalSettings(ctx, exe);

    return exe;
}

fn buildWindowsInstaller(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    library: *std.Build.Step.Compile,
    stb_image: *std.Build.Step.Compile,
    common_infrastructure: *std.Build.Step.Compile,
    configured_vst3: ?configure_binaries.ConfiguredPlugin,
    configured_clap: ?configure_binaries.ConfiguredPlugin,
    uninstaller: *std.Build.Step.Compile,
    codesigned_uninstaller: std.Build.LazyPath,
}) *std.Build.Step.Compile {
    const description = "Installer for Floe plugins";

    const exe = ctx.b.addExecutable(.{
        .name = ctx.b.fmt("Floe-Installer-v{s}", .{ .version = ctx.floe_version_string }),
        .root_module = ctx.b.createModule(cfg.module_options),
        .version = ctx.floe_version,
        .win32_manifest = ctx.b.addWriteFiles().add("xml.manifest", windowsManifestContent(ctx.b, .{
            .name = "Installer",
            .description = description,
            .require_admin = if (ctx.build_mode == .production) true else ctx.windows_installer_require_admin,
        })),
    });
    exe.subsystem = .Windows;

    // Add resources.
    {
        var rc_include_path: std.BoundedArray(std.Build.LazyPath, 5) = .{};

        if (ctx.dep_floe_logos) |logos| {
            const sidebar_img = "rasterized/win-installer-sidebar.png";
            const sidebar_img_lazy_path = logos.path(sidebar_img);
            rc_include_path.append(sidebar_img_lazy_path.dirname()) catch @panic("OOM");
            exe.root_module.addCMacro(
                "SIDEBAR_IMAGE_PATH",
                ctx.b.fmt("\"{s}\"", .{std.fs.path.basename(sidebar_img)}),
            );
        }

        if (deps.configured_vst3) |vst3_plugin| {
            exe.root_module.addCMacro("VST3_PLUGIN_BINARY_NAME", "\"Floe.vst3\"");
            rc_include_path.append(vst3_plugin.plugin_path.dirname()) catch @panic("OOM");
        }

        if (deps.configured_clap) |clap_plugin| {
            exe.root_module.addCMacro("CLAP_PLUGIN_BINARY_NAME", "\"Floe.clap\"");
            rc_include_path.append(clap_plugin.plugin_path.dirname()) catch @panic("OOM");
        }

        {
            exe.root_module.addCMacro(
                "UNINSTALLER_BINARY_NAME",
                ctx.b.fmt("\"{s}\"", .{deps.uninstaller.out_filename}),
            );
            rc_include_path.append(deps.codesigned_uninstaller.dirname()) catch @panic("OOM");
        }

        exe.addWin32ResourceFile(.{
            .file = ctx.b.path("src/windows_installer/resources.rc"),
            .include_paths = rc_include_path.slice(),
            .flags = exe.root_module.c_macros.items,
        });
    }

    exe.addCSourceFiles(.{
        .files = &.{
            "src/windows_installer/installer.cpp",
            "src/windows_installer/gui.cpp",
            "src/common_infrastructure/final_binary_type.cpp",
        },
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .all_warnings = true,
            .ubsan = true,
            .cpp = true,
            .gen_cdb_fragments = true,
        }).flags.items,
    });

    exe.root_module.addCMacro("FINAL_BINARY_TYPE", "WindowsInstaller");
    exe.linkSystemLibrary("gdi32");
    exe.linkSystemLibrary("version");
    exe.linkSystemLibrary("comctl32");

    addWindowsEmbedInfo(exe, .{
        .name = "Floe Installer",
        .description = description,
        .icon_path = if (ctx.dep_floe_logos) |logos| logos.path("rasterized/icon.ico") else null,
    }) catch @panic("OOM");
    exe.addConfigHeader(cfg.floe_config_h);
    exe.addIncludePath(ctx.b.path("src"));
    exe.addObject(deps.stb_image);
    exe.linkLibrary(deps.library);
    exe.linkLibrary(deps.common_infrastructure);
    applyUniversalSettings(ctx, exe);

    return exe;
}

fn buildTests(ctx: *const BuildContext, cfg: *const TargetConfig, deps: struct {
    plugin: *std.Build.Step.Compile,
}) *std.Build.Step.Compile {
    const exe = ctx.b.addExecutable(.{
        .name = "tests",
        .root_module = ctx.b.createModule(cfg.module_options),
    });
    exe.addCSourceFiles(.{
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
        .flags = FlagsBuilder.init(ctx, cfg, .{
            .all_warnings = true,
            .ubsan = true,
            .cpp = true,
            .gen_cdb_fragments = true,
        }).flags.items,
    });
    exe.root_module.addCMacro("FINAL_BINARY_TYPE", "Tests");
    exe.addConfigHeader(cfg.floe_config_h);
    exe.linkLibrary(deps.plugin);
    applyUniversalSettings(ctx, exe);

    return exe;
}

fn doTarget(
    ctx: *BuildContext,
    cfg: *const TargetConfig,
    top_level_steps: *const TopLevelSteps,
    options: *const Options,
) release_artifacts.Artifacts {
    const stb_sprintf = buildStbSprintf(ctx, cfg);
    const xxhash = buildXxhash(ctx, cfg);
    const tracy = buildTracy(ctx, cfg);
    const vitfx = buildVitfx(ctx, cfg);
    const pugl = buildPugl(ctx, cfg);
    const debug_info_lib = buildDebugInfo(ctx, cfg);
    const stb_image = buildStbImage(ctx, cfg);
    const dr_wav = buildDrWav(ctx, cfg);
    const miniz = buildMiniz(ctx, cfg);
    const flac = buildFlac(ctx, cfg);
    const fft_convolver = buildFftConvolver(ctx, cfg);

    const embedded_files = buildEmbeddedFiles(ctx, cfg);

    const library = buildFloeLibrary(ctx, cfg, .{
        .stb_sprintf = stb_sprintf,
        .tracy = tracy,
        .debug_info_lib = debug_info_lib,
    });

    if (targetCanRunNatively(cfg.target)) {
        ctx.native_archiver = buildArchiver(ctx, cfg, .{ .miniz = miniz });
    }

    const bx = buildBx(ctx, cfg);
    const bimg = buildBimg(ctx, cfg, .{ .bx = bx });
    const bgfx = buildBgfx(ctx, cfg, .{ .bx = bx, .bimg = bimg });

    const common_infrastructure = buildCommonInfrastructure(ctx, cfg, .{
        .dr_wav = dr_wav,
        .flac = flac,
        .library = library,
        .miniz = miniz,
        .xxhash = xxhash,
    });

    const plugin = buildPluginLib(ctx, cfg, .{
        .common_infrastructure = common_infrastructure,
        .library = library,
        .fft_convolver = fft_convolver,
        .embedded_files = embedded_files,
        .tracy = tracy,
        .pugl = pugl,
        .stb_image = stb_image,
        .vitfx = vitfx,
        .bgfx = bgfx,
    });

    if (targetCanRunNatively(cfg.target)) {
        ctx.native_docs_generator = buildDocsGenerator(ctx, cfg, .{
            .common_infrastructure = common_infrastructure,
        });
    }

    const configured_packager = blk: {
        const exe = buildPackager(ctx, cfg, .{
            .common_infrastructure = common_infrastructure,
            .embedded_files = embedded_files,
        });

        const codesigned_exe = configure_binaries.maybeAddWindowsCodesign(
            exe,
            .{ .description = "Floe Packager" },
        );

        const install = ctx.b.addInstallBinFile(codesigned_exe, exe.out_filename);
        top_level_steps.install_all.dependOn(&install.step);

        break :blk release_artifacts.Artifact{
            .out_filename = exe.out_filename,
            .path = codesigned_exe,
        };
    };

    {
        const exe = buildPresetEditor(ctx, cfg, .{
            .common_infrastructure = common_infrastructure,
            .embedded_files = embedded_files,
        });

        const install = ctx.b.addInstallArtifact(exe, .{});
        top_level_steps.install_all.dependOn(&install.step);

        // IMPROVE: export preset-editor as a production artifact?
    }

    const configured_clap: ?configure_binaries.ConfiguredPlugin = blk: {
        if (!options.sanitize_thread) {
            const dso = buildClap(ctx, cfg, .{ .plugin = plugin });

            const clap = configure_binaries.addConfiguredPlugin(
                ctx.b,
                .clap,
                dso,
                configure_binaries.CodesignInfo{ .description = "Floe CLAP Plugin" },
            );
            top_level_steps.install_plugins.dependOn(clap.install_step);
            top_level_steps.install_all.dependOn(clap.install_step);
            break :blk clap;
        } else {
            break :blk null;
        }
    };

    // Standalone is for development-only at the moment, so we can save a bit of time by not building it
    // in production builds.
    if (ctx.build_mode != .production) {
        const exe = buildStandalone(ctx, cfg, .{ .plugin = plugin });

        const install = ctx.b.addInstallArtifact(exe, .{});
        top_level_steps.install_all.dependOn(&install.step);
    }

    const vst3_sdk = buildVst3Sdk(ctx, cfg);
    const vst3_validator = buildVst3Validator(ctx, cfg, .{
        .library = library,
        .vst3_sdk = vst3_sdk,
    });

    const configured_vst3: ?configure_binaries.ConfiguredPlugin = blk: {
        if (!options.sanitize_thread) {
            const dso = buildVst3(ctx, cfg, .{
                .vst3_sdk = vst3_sdk,
                .plugin = plugin,
            });

            const vst3 = configure_binaries.addConfiguredPlugin(
                ctx.b,
                .vst3,
                dso,
                configure_binaries.CodesignInfo{ .description = "Floe VST3 Plugin" },
            );
            top_level_steps.install_plugins.dependOn(vst3.install_step);
            top_level_steps.install_all.dependOn(vst3.install_step);

            // Test VST3
            {
                const run_tests = std_extras.createCommandWithStdoutToStderr(
                    ctx.b,
                    cfg.target,
                    "run VST3-Validator",
                );
                run_tests.addFileArg(configure_binaries.nix_helper.maybePatchElfExecutable(vst3_validator));
                vst3.addToRunStepArgs(run_tests);
                run_tests.expectExitCode(0);

                top_level_steps.test_vst3_validator.dependOn(&run_tests.step);
            }

            break :blk vst3;
        } else {
            top_level_steps.test_vst3_validator.dependOn(&ctx.b.addFail("VST3 tests not allowed with this configuration").step);
            break :blk null;
        }
    };

    const configured_au: ?configure_binaries.ConfiguredPlugin = blk: {
        if (cfg.target.os.tag == .macos and !options.sanitize_thread) {
            const dso = buildAu(ctx, cfg, .{ .plugin = plugin });

            const au = configure_binaries.addConfiguredPlugin(ctx.b, .au, dso, null);
            top_level_steps.install_plugins.dependOn(au.install_step);
            top_level_steps.install_all.dependOn(au.install_step);

            if (builtin.os.tag == .macos) {
                if (std.mem.endsWith(u8, std.mem.trimRight(u8, ctx.b.install_path, "/"), "Library/Audio/Plug-Ins")) {
                    const installed_au_path = ctx.b.pathJoin(&.{ ctx.b.install_path, "Components/Floe.component" });

                    // Pluginval AU
                    {
                        // Pluginval puts all of it's output in stdout, not stderr.
                        const run = std_extras.createCommandWithStdoutToStderr(ctx.b, cfg.target, "run pluginval AU");

                        addPluginvalCommand(run, cfg.target);

                        run.addArgs(&.{ "--validate", installed_au_path });

                        run.step.dependOn(au.install_step);
                        run.expectExitCode(0);

                        top_level_steps.pluginval_au.dependOn(&run.step);
                    }

                    // auval
                    {
                        const run_auval = std_extras.createCommandWithStdoutToStderr(ctx.b, cfg.target, "run auval");
                        run_auval.addArgs(&.{
                            "auval",
                            "-v",
                            constants.floe_au_type,
                            constants.floe_au_subtype,
                            constants.floe_au_manufacturer_code,
                        });
                        run_auval.step.dependOn(au.install_step);
                        run_auval.expectExitCode(0);

                        // We need to make sure that the audio component service is aware of the new AU.
                        // Unfortunately, it doesn't do this automatically sometimes and if we were to run auval
                        // right now it might say "didn't find the component". We need to kill the service so
                        // that auval will rescan for installed AUs. The command on the terminal to do this is:
                        // killall -9 AudioComponentRegistrar. That is, send SIGKILL to the process named
                        // AudioComponentRegistrar.
                        if (!std_extras.pathExists(installed_au_path)) {
                            const cmd = ctx.b.addSystemCommand(&.{ "killall", "-9", "AudioComponentRegistrar" });

                            // We explicitly set the 'check' to an empty array which means that we do not care
                            // about the exit code or output of this command. Sometimes it can fail with: "No
                            // matching processes belonging to you were found" - which is fine.
                            cmd.stdio = .{
                                .check = std.ArrayListUnmanaged(std.Build.Step.Run.StdIo.Check).empty,
                            };

                            top_level_steps.auval.dependOn(&cmd.step);
                        }

                        top_level_steps.auval.dependOn(&run_auval.step);
                    }
                } else {
                    const fail = ctx.b.addFail("You must specify a global/user Library/Audio/Plug-Ins " ++
                        "--prefix to zig build in order to run AU tests");
                    top_level_steps.pluginval_au.dependOn(&fail.step);
                    top_level_steps.auval.dependOn(&fail.step);
                }
            } else {
                const fail = ctx.b.addFail("AU tests can only be run on macOS hosts");
                top_level_steps.pluginval_au.dependOn(&fail.step);
                top_level_steps.auval.dependOn(&fail.step);
            }

            break :blk au;
        } else {
            const fail = ctx.b.addFail("AU tests not allowed with this configuration");
            top_level_steps.pluginval_au.dependOn(&fail.step);
            top_level_steps.auval.dependOn(&fail.step);
            break :blk null;
        }
    };

    const configured_windows_installer: ?release_artifacts.Artifact = blk: {
        if (cfg.target.os.tag == .windows) {
            const uninstaller = blk2: {
                const exe = buildWindowsUninstaller(ctx, cfg, .{
                    .library = library,
                    .common_infrastructure = common_infrastructure,
                    .stb_image = stb_image,
                });

                const codesigned_exe = configure_binaries.maybeAddWindowsCodesign(
                    exe,
                    .{ .description = "Floe Uninstaller" },
                );

                const install = ctx.b.addInstallBinFile(codesigned_exe, exe.out_filename);
                top_level_steps.install_all.dependOn(&install.step);

                break :blk2 .{
                    .step = exe,
                    .codesigned_path = codesigned_exe,
                };
            };

            const installer = buildWindowsInstaller(ctx, cfg, .{
                .stb_image = stb_image,
                .common_infrastructure = common_infrastructure,
                .library = library,
                .uninstaller = uninstaller.step,
                .codesigned_uninstaller = uninstaller.codesigned_path,
                .configured_clap = configured_clap,
                .configured_vst3 = configured_vst3,
            });

            const codesigned_path = configure_binaries.maybeAddWindowsCodesign(
                installer,
                .{ .description = "Floe Installer" },
            );

            // Installer tests
            {
                const run_installer = std.Build.Step.Run.create(ctx.b, ctx.b.fmt("run {s}", .{installer.name}));
                run_installer.addFileArg(codesigned_path);
                run_installer.addArg("--autorun");
                run_installer.expectExitCode(0);

                // IMPROVE actually test for installation

                const run_uninstaller = std.Build.Step.Run.create(
                    ctx.b,
                    ctx.b.fmt("run {s}", .{uninstaller.step.name}),
                );
                run_uninstaller.addFileArg(uninstaller.codesigned_path);
                run_uninstaller.addArg("--autorun");
                run_uninstaller.expectExitCode(0);
                run_uninstaller.step.dependOn(&run_installer.step);

                top_level_steps.test_windows_install.dependOn(&run_uninstaller.step);
            }

            // Install
            {
                const install = ctx.b.addInstallBinFile(codesigned_path, installer.out_filename);
                top_level_steps.install_all.dependOn(&install.step);
            }

            break :blk release_artifacts.Artifact{
                .out_filename = installer.out_filename,
                .path = codesigned_path,
            };
        } else {
            top_level_steps.test_windows_install.dependOn(
                &ctx.b.addFail("Windows installer tests not allowed with this configuration").step,
            );
            break :blk null;
        }
    };

    // We don't need tests in production builds so we can save some build time here.
    if (ctx.build_mode != .production) {
        const exe = buildTests(ctx, cfg, .{ .plugin = plugin });

        const test_binary = configure_binaries.nix_helper.maybePatchElfExecutable(exe);

        const install = ctx.b.addInstallBinFile(test_binary, exe.out_filename);
        top_level_steps.install_all.dependOn(&install.step);

        const add_tests_args = struct {
            pub fn do(run: *std.Build.Step.Run, clap_plugin: ?configure_binaries.ConfiguredPlugin) void {
                const b2 = run.step.owner;
                run.addArg("--log-level=debug");

                // We output JUnit XML in a unique location so test runs don't clobber each other. These files
                // can be collected by searching the .zig-cache directory for .junit.xml files.
                _ = run.addPrefixedOutputFileArg("--junit-xml-output-path=", "unit-tests.junit.xml");

                if (clap_plugin) |p|
                    p.addToRunStepArgsWithPrefix(run, "--clap-plugin-path=");

                run.addPrefixedDirectoryArg("--test-files-folder-path=", b2.path("test_files"));

                // Add additional arguments to the tests that were given to zig build after "--".
                if (b2.args) |args|
                    run.addArgs(args);
            }
        }.do;

        // Run unit tests
        {
            const run_tests = std.Build.Step.Run.create(ctx.b, "run unit tests");
            run_tests.addFileArg(test_binary);
            add_tests_args(run_tests, configured_clap);

            run_tests.expectExitCode(0);

            top_level_steps.test_step.dependOn(&run_tests.step);
        }

        // Coverage tests
        if (builtin.os.tag == .linux) {
            const run_coverage = ctx.b.addSystemCommand(&.{
                "kcov",
                ctx.b.fmt("--include-pattern={s}", .{ctx.b.pathFromRoot("src")}),
                ctx.b.fmt("{s}/coverage-out", .{constants.floe_cache_relative}),
            });
            run_coverage.addFileArg(test_binary);
            add_tests_args(run_coverage, configured_clap);
            run_coverage.expectExitCode(0);
            top_level_steps.coverage.dependOn(&run_coverage.step);
        } else {
            top_level_steps.coverage.dependOn(&ctx.b.addFail("coverage not supported on this OS").step);
        }

        // Valgrind test
        if (!options.sanitize_thread) {
            const run = ctx.b.addSystemCommand(&.{
                "valgrind",
                "--leak-check=full",
                "--fair-sched=yes",
                "--num-callers=25",
                "--gen-suppressions=all",
                ctx.b.fmt("--suppressions={s}", .{ctx.b.pathFromRoot("valgrind.supp")}),
                "--error-exitcode=1",
                "--exit-on-first-error=no",
            });
            run.addFileArg(test_binary);
            add_tests_args(run, configured_clap);
            run.expectExitCode(0);

            top_level_steps.valgrind.dependOn(&run.step);
        } else {
            top_level_steps.valgrind.dependOn(
                &ctx.b.addFail("valgrind not allowed for this build configuration").step,
            );
        }
    }

    // Clap Validator test
    if (configured_clap) |p| {
        const run = std_extras.createCommandWithStdoutToStderr(ctx.b, cfg.target, "run clap-validator");
        if (ctx.b.findProgram(
            &.{if (cfg.target.os.tag != .windows) "clap-validator" else "clap-validator.exe"},
            &[0][]const u8{},
        ) catch null) |program| {
            run.addArg(program); // Use system-installed clap-validator.
        } else if (cfg.target.os.tag == .windows) {
            if (ctx.b.lazyDependency("clap_validator_windows", .{})) |dep| {
                run.addFileArg(dep.path("clap-validator.exe"));
            }
        } else if (cfg.target.os.tag == .macos) {
            if (ctx.b.lazyDependency("clap_validator_macos", .{})) |dep| {
                const bin_path = dep.path("clap-validator");
                run.addFileArg(bin_path);
                run.step.dependOn(&chmodExeStep(ctx.b, bin_path).step);
            }
        } else if (cfg.target.os.tag == .linux) {
            if (ctx.b.lazyDependency("clap_validator_linux", .{})) |dep| {
                const bin_path = dep.path("clap-validator");
                run.addFileArg(bin_path);
                run.step.dependOn(&chmodExeStep(ctx.b, bin_path).step);
            }
        } else {
            @panic("Unsupported OS for clap-validator");
        }

        run.addArgs(&.{
            "validate",
            // Clap Validator seems to have a bug that crashes the program.
            // https://github.com/free-audio/clap-validator/issues/21
            // We workaround this by skipping process and param tests. Additionally, we disable this test
            // because we have a good reason to behave in a different way. Each instance of our plugin as an
            // ID - we store that in the state so that loading a DAW project retains the instance IDs. But if a
            // new instance is created and only its parameters are set, then our state will differ in terms of
            // the instance ID - and that's okay. We don't want to fail because of this. state-reproducibility-
            // flush: Randomizes a plugin's parameters, saves its state, recreates the plugin instance, sets the
            // same parameters as before, saves the state again, and then asserts that the two states are
            // identical. The parameter values are set updated using the process function to create the first
            // state, and using the flush function to create the second state.
            "--test-filter",
            ".*(process|param|state-reproducibility-flush).*",
            "--invert-filter",
        });
        p.addToRunStepArgs(run);
        run.expectExitCode(0);

        top_level_steps.clap_val.dependOn(&run.step);
    } else {
        top_level_steps.clap_val.dependOn(
            &ctx.b.addFail("clap-validator not allowed for this build configuration").step,
        );
    }

    // Pluginval test
    if (configured_vst3) |p| {
        // Pluginval puts all of it's output in stdout, not stderr.
        const run = std_extras.createCommandWithStdoutToStderr(ctx.b, cfg.target, "run pluginval");

        addPluginvalCommand(run, cfg.target);

        // In headless environments such as CI, GUI tests always fail on Linux so we skip them.
        if (builtin.os.tag == .linux and ctx.b.graph.env_map.get("DISPLAY") == null) {
            run.addArg("--skip-gui-tests");
        }

        run.addArg("--validate");
        p.addToRunStepArgs(run);
        run.expectExitCode(0);

        top_level_steps.pluginval.dependOn(&run.step);
    } else {
        top_level_steps.pluginval.dependOn(
            &ctx.b.addFail("pluginval not allowed for this build configuration").step,
        );
    }

    // clang-tidy
    {
        const clang_tidy_step = check_steps.ClangTidyStep.create(ctx.b, cfg.target);
        clang_tidy_step.step.dependOn(ctx.compile_all);
        top_level_steps.clang_tidy.dependOn(&clang_tidy_step.step);
    }

    return .{
        .windows_installer = configured_windows_installer,
        .au = configured_au,
        .vst3 = configured_vst3,
        .clap = configured_clap,
        .packager = configured_packager,
    };
}

fn addRunScript(
    script_exe: *std.Build.Step.Compile,
    top_level_step: *std.Build.Step,
    command: []const u8,
) void {
    const b = top_level_step.owner;

    const run_step = b.addRunArtifact(script_exe);
    run_step.addArg(command);

    if (b.args) |args|
        run_step.addArgs(args);

    // Our scripts assume they are run from the repository root.
    run_step.setCwd(b.path("."));

    // Provide the path to the Zig executable for any scripts that may need to invoke Zig.
    run_step.setEnvironmentVariable("ZIG_EXE", b.graph.zig_exe);

    top_level_step.dependOn(&run_step.step);
}

fn chmodExeStep(b: *std.Build, path: std.Build.LazyPath) *std.Build.Step.Run {
    const mod = b.addSystemCommand(&.{ "chmod", "+x" });
    mod.addFileArg(path);
    return mod;
}

fn addPluginvalCommand(run: *std.Build.Step.Run, target: std.Target) void {
    const b = run.step.owner;

    if (b.findProgram(
        &.{if (target.os.tag != .windows) "pluginval" else "pluginval.exe"},
        &[0][]const u8{},
    ) catch null) |program| {
        run.addArg(program); // We found a system installation.
    } else if (target.os.tag == .windows) {
        if (b.lazyDependency("pluginval_windows", .{})) |dep| {
            run.addFileArg(dep.path("pluginval.exe"));
        }
    } else if (target.os.tag == .macos) {
        if (b.lazyDependency("pluginval_macos", .{})) |dep| {
            const bin_path = dep.path("Contents/MacOS/pluginval");
            run.addFileArg(bin_path);
            run.step.dependOn(&chmodExeStep(b, bin_path).step);
        }
    } else if (target.os.tag == .linux) {
        if (b.lazyDependency("pluginval_linux", .{})) |dep| {
            const bin_path = dep.path("pluginval");
            run.addFileArg(bin_path);
            run.step.dependOn(&chmodExeStep(b, bin_path).step);
        }
    } else {
        @panic("Unsupported OS for pluginval");
    }
}

fn windowsManifestContent(b: *std.Build, args: struct {
    name: []const u8,
    description: []const u8,
    require_admin: bool,
}) []const u8 {
    return b.fmt(
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
        .name = args.name,
        .description = args.description,
        .execution_level = if (args.require_admin) "requireAdministrator" else "asInvoker",
    });
}

fn addWindowsEmbedInfo(step: *std.Build.Step.Compile, info: struct {
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

// Note that we don't use ResolvedTarget.query.isNative() because it's too strict regarding CPU features. For
// example, even in 'native' mode on Linux we using a baseline x86_64 CPU so valgrind works well; isNative() would
// return false even though the target can run natively.
fn targetCanRunNatively(target: std.Target) bool {
    return target.cpu.arch == builtin.cpu.arch and target.os.tag == builtin.os.tag;
}

const CreateWebsiteApiFiles = struct {
    step: std.Build.Step,
    latest_stable: std.Build.LazyPath,
    latest_edge: std.Build.LazyPath,
    api_dir: std.Build.LazyPath,

    pub fn create(b: *std.Build) *CreateWebsiteApiFiles {
        const latest_release_args = [_][]const u8{
            "gh", "release", "list", "--limit", "1", "--json", "tagName", "--jq", ".[].tagName",
        };
        const latest_stable = b.addSystemCommand(
            latest_release_args ++ &[_][]const u8{"--exclude-pre-releases"},
        ).captureStdOut();
        const latest_edge = b.addSystemCommand(&latest_release_args).captureStdOut();

        const self = b.allocator.create(CreateWebsiteApiFiles) catch @panic("OOM");
        self.* = .{
            .step = std.Build.Step.init(.{
                .id = .custom,
                .name = "Make website API files",
                .owner = b,
                .makeFn = make,
            }),
            .latest_stable = latest_stable,
            .latest_edge = latest_edge,
            .api_dir = b.path("website/static/api/v1"),
        };

        self.latest_stable.addStepDependencies(&self.step);
        self.latest_edge.addStepDependencies(&self.step);
        self.api_dir.addStepDependencies(&self.step);

        return self;
    }

    fn make(step: *std.Build.Step, make_options: std.Build.Step.MakeOptions) !void {
        _ = make_options;
        const self: *CreateWebsiteApiFiles = @fieldParentPtr("step", step);
        const b = step.owner;

        const dir = self.api_dir.getPath3(b, step);
        try dir.makePath(".");

        try dir.root_dir.handle.writeFile(.{
            .sub_path = dir.joinString(b.allocator, "version") catch @panic("OOM"),
            .data = b.fmt(
                \\latest={s}
                \\edge={s}
            , .{
                try readWholeFile(step, self.latest_stable),
                try readWholeFile(step, self.latest_edge),
            }),
        });
    }

    fn readWholeFile(step: *std.Build.Step, path: std.Build.LazyPath) ![]const u8 {
        const p = path.getPath3(step.owner, step);
        const file_data = try p.root_dir.handle.readFileAlloc(step.owner.allocator, p.sub_path, 100);
        return std.mem.trim(u8, file_data, "\n\r \t");
    }
};
