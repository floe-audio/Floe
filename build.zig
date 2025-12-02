// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");
const builtin = @import("builtin");

const std_extras = @import("src/build/std_extras.zig");
const constants = @import("src/build/constants.zig");

const ConcatCompileCommandsStep = @import("src/build/ConcatCompileCommandsStep.zig");
const check_steps = @import("src/build/check_steps.zig");
const configure_binaries = @import("src/build/configure_binaries.zig");
const release_artifacts = @import("src/build/release_artifacts.zig");

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

const BuildMode = enum {
    development,
    performance_profiling,
    production, // a.k.a.: release, end-user, for-distribution
};

const BuildContext = struct {
    b: *std.Build,
    floe_version_string: []const u8,
    floe_version: std.SemanticVersion,
    floe_version_hash: u32,
    native_archiver: ?*std.Build.Step.Compile,
    native_docs_generator: ?*std.Build.Step.Compile,
    enable_tracy: bool,
    build_mode: BuildMode,
    optimise: std.builtin.OptimizeMode,
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
};

const FlagsBuilder = struct {
    flags: std.ArrayList([]const u8),

    const Options = struct {
        ubsan: bool = false,
        compile_commands: ?*ConcatCompileCommandsStep = null,
        full_diagnostics: bool = false,
        cpp: bool = false,
        objcpp: bool = false,
    };

    pub fn init(
        context: *BuildContext,
        target: std.Target,
        options: Options,
    ) FlagsBuilder {
        var result = FlagsBuilder{
            .flags = std.ArrayList([]const u8).init(context.b.allocator),
        };
        result.addCoreFlags(context, target, options) catch @panic("OOM");
        return result;
    }

    fn addFlag(self: *FlagsBuilder, flag: []const u8) void {
        self.flags.append(flag) catch @panic("OOM");
    }

    fn addCoreFlags(
        self: *FlagsBuilder,
        context: *BuildContext,
        target: std.Target,
        options: Options,
    ) !void {
        if (options.full_diagnostics) {
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
                "-DFLAC__NO_DLL",
                "-DPUGL_DISABLE_DEPRECATED",
                "-DPUGL_STATIC",

                // Minimise windows.h size for faster compile times:
                // "Define one or more of the NOapi symbols to exclude the API. For example, NOCOMM excludes
                // the serial communication API. For a list of support NOapi symbols, see Windows.h."
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

        try self.flags.append("-DMINIZ_USE_UNALIGNED_LOADS_AND_STORES=0");
        try self.flags.append("-DMINIZ_NO_STDIO");
        try self.flags.append("-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES");
        try self.flags.append(context.b.fmt("-DMINIZ_LITTLE_ENDIAN={d}", .{@intFromBool(target.cpu.arch.endian() == .little)}));
        try self.flags.append("-DMINIZ_HAS_64BIT_REGISTERS=1");

        try self.flags.append("-DSTBI_NO_STDIO");
        try self.flags.append("-DSTBI_MAX_DIMENSIONS=65535"); // we use u16 for dimensions

        if (target.os.tag == .linux) {
            // NOTE(Sam, June 2024): workaround for a bug in Zig (most likely) where our shared library always causes a
            // crash after dlclose(), as described here: https://github.com/ziglang/zig/issues/17908
            // The workaround involves adding this flag and also adding a custom bit of code using
            // __attribute__((destructor)) to manually call __cxa_finalize():
            // https://stackoverflow.com/questions/34308720/where-is-dso-handle-defined/48256026#48256026
            try self.flags.append("-fno-use-cxa-atexit");
        }

        // We want __builtin_FILE(), __FILE__ and debug info to be portable because we use this information in
        // stacktraces and logging so we change the absolute paths of all files to be relative to the build root.
        // __FILE__, __builtin_FILE(), and DWARF info should be made relative.
        // -ffile-prefix-map=OLD=NEW is an alias for both -fdebug-prefix-map and -fmacro-prefix-map
        try self.flags.append(context.b.fmt("-ffile-prefix-map={s}{s}=", .{
            context.b.pathFromRoot(""),
            std.fs.path.sep_str,
        }));

        try self.flags.append("-fvisibility=hidden");

        if (context.build_mode != .production and context.enable_tracy) {
            try self.flags.append("-DTRACY_ENABLE");
            try self.flags.append("-DTRACY_MANUAL_LIFETIME");
            try self.flags.append("-DTRACY_DELAYED_INIT");
            try self.flags.append("-DTRACY_ONLY_LOCALHOST");
            if (target.os.tag == .linux) {
                // Couldn't get these working well so just disabling them
                try self.flags.append("-DTRACY_NO_CALLSTACK");
                try self.flags.append("-DTRACY_NO_SYSTEM_TRACING");
            }
        }

        // A bit of information about debug symbols:
        //
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
            if (context.optimise != .ReleaseFast) {
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

        if (options.compile_commands) |ccs| {
            try ccs.addClangArgument(&self.flags);
        }

        if (target.os.tag == .windows) {
            // On Windows, fix compile errors related to deprecated usage of string in mingw
            try self.flags.append("-DSTRSAFE_NO_DEPRECATE");
            try self.flags.append("-DUNICODE");
            try self.flags.append("-D_UNICODE");
        } else if (target.os.tag == .macos) {
            try self.flags.append("-DGL_SILENCE_DEPRECATION"); // disable opengl warnings on macos

            // don't fail when compiling macOS obj-c SDK headers
            try self.flags.appendSlice(&.{
                "-Wno-elaborated-enum-base",
                "-Wno-missing-method-return-type",
                "-Wno-deprecated-declarations",
                "-Wno-deprecated-anon-enum-enum-conversion",
                "-D__kernel_ptr_semantics=",
                "-Wno-c99-extensions",
            });
        }
    }
};

fn applyUniversalSettings(
    context: *BuildContext,
    step: *std.Build.Step.Compile,
    compile_commands: *ConcatCompileCommandsStep,
) void {
    var b = context.b;
    // NOTE (May 2025, Zig 0.14): LTO on Windows results in debug_info generation that fails to parse with Zig's
    // Dwarf parser (InvalidDebugInfo). We've previously had issues on macOS too. So we disable it for now.
    step.want_lto = false;
    step.rdynamic = true;
    step.linkLibC();

    step.addIncludePath(context.dep_xxhash.path(""));
    step.addIncludePath(context.dep_stb.path(""));
    step.addIncludePath(context.dep_clap.path("include"));
    step.addIncludePath(context.dep_icon_font_cpp_headers.path(""));
    step.addIncludePath(context.dep_dr_libs.path(""));
    step.addIncludePath(context.dep_flac.path("include"));
    step.addIncludePath(context.dep_lua.path(""));
    step.addIncludePath(context.dep_pugl.path("include"));
    step.addIncludePath(context.dep_clap_wrapper.path("include"));
    step.addIncludePath(context.dep_tracy.path("public"));
    step.addIncludePath(context.dep_valgrind_h.path(""));
    step.addIncludePath(context.dep_portmidi.path("pm_common"));
    step.addIncludePath(context.dep_miniz.path(""));
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

        // b.sysroot = sdk.path("");
    }

    compile_commands.step.dependOn(&step.step);

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
                    arch_os_abi = "x86_64-linux-gnu.2.29";
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
    const options = .{
        .build_mode = b.option(
            BuildMode,
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

    // Top-level steps
    const steps = .{
        .compile_all_step = b.step("compile", "Compile all"),

        .release = b.step("release", "Create release artifacts"),

        .test_step = b.step("test", "Run unit tests"),
        .coverage = b.step("test-coverage", "Generate code coverage report of unit tests"),

        .clap_val = b.step("test:clap-val", "Test using clap-validator"),
        .test_vst3_validator = b.step("test:vst3-val", "Run VST3 Validator on built VST3 plugin"),
        .pluginval_au = b.step("test:pluginval-au", "Test AU using pluginval"),
        .auval = b.step("test:auval", "Test AU using auval"),
        .pluginval = b.step("test:pluginval", "Test using pluginval"),
        .valgrind = b.step("test:valgrind", "Test using Valgrind"),
        .test_windows_install = b.step("test:windows-install", "Test installation and uninstallation on Windows"),

        .clang_tidy = b.step("check:clang-tidy", "Run clang-tidy on source files"),

        .format_step = b.step("script:format", "Format code with clang-format"),
        .gh_release_step = b.step("script:create-gh-release", "Create a GitHub release"),
        .ci_step = b.step("script:ci", "Run CI checks"),
        .ci_basic_step = b.step("script:ci-basic", "Run basic CI checks"),
        .upload_errors_step = b.step("script:upload-errors", "Upload error reports to Sentry"),

        .website_gen_step = b.step("script:website-generate", "Generate the static JSON for the website"),
        .website_build_step = b.step("script:website-build", "Build the website"),
        .website_dev_step = b.step("script:website-dev", "Start website dev build locally"),
        .website_promote_step = b.step("script:website-promote-beta-to-stable", "Promote the 'beta' documentation to be the latest stable version"),

        .install_all_step = b.step("install:all", "Install all; development files as well as plugins"),
    };

    b.default_step = steps.compile_all_step;
    steps.install_all_step.dependOn(b.getInstallStep());

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

    var build_context: BuildContext = .{
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
    };

    b.build_root.handle.makeDir(constants.floe_cache_relative) catch {};

    const targets = resolveTargets(b, options.targets) catch @panic("OOM");

    // If we're building for multiple targets at the same time, we need to choose one that gets to be the
    // final compile_commands.json. We just say the first one.
    const target_for_compile_commands = targets.items[0];

    // We'll try installing the desired compile_commands.json version here in case any previous build already
    // created it.
    ConcatCompileCommandsStep.trySetCdb(b, target_for_compile_commands.result);

    check_steps.addGlobalCheckSteps(b);

    const artifacts_list = b.allocator.alloc(release_artifacts.Artifacts, targets.items.len) catch @panic("OOM");

    for (targets.items, 0..) |target, i| {
        const artifacts = doTarget(
            &build_context,
            target,
            target.query.eql(target_for_compile_commands.query),
            options,
            steps,
        );
        artifacts_list[i] = artifacts;
    }

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

        addRunScript(exe, steps.format_step, "format");
        addRunScript(exe, steps.gh_release_step, "create-gh-release");
        addRunScript(exe, steps.upload_errors_step, "upload-errors");
        addRunScript(exe, steps.ci_step, "ci");
        addRunScript(exe, steps.ci_basic_step, "ci-basic");
        addRunScript(exe, steps.website_promote_step, "website-promote-beta-to-stable");
    }

    // Make release artifacts
    {
        const archiver = blk: {
            if (build_context.native_archiver) |a| break :blk a;

            // We need to add steps for the native target.
            _ = doTarget(&build_context, b.graph.host, false, options, steps);
            break :blk build_context.native_archiver.?;
        };

        for (artifacts_list, 0..) |artifacts, i| {
            const install_steps = release_artifacts.makeRelease(
                b,
                archiver,
                build_context.floe_version,
                targets.items[i].result,
                artifacts,
            );
            steps.release.dependOn(install_steps);
        }
    }

    // Docs generator
    {
        const docs_generator = blk: {
            if (build_context.native_docs_generator) |d| break :blk d;

            // We need to add steps for the native target.
            _ = doTarget(&build_context, b.graph.host, false, options, steps);
            break :blk build_context.native_docs_generator.?;
        };

        // Run the docs generator. It takes no args but outputs JSON to stdout.
        {
            const run = std.Build.Step.Run.create(b, b.fmt("run {s}", .{docs_generator.name}));
            run.addFileArg(configure_binaries.nix_helper.maybePatchElfExecutable(docs_generator));

            const copy = b.addUpdateSourceFiles();
            copy.addCopyFileToSource(run.captureStdOut(), "website/static/generated-data.json");
            steps.website_gen_step.dependOn(&copy.step);
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
            run.step.dependOn(steps.website_gen_step);
            run.step.dependOn(&npm_install.step);
            run.step.dependOn(&create_api.step);
            run.expectExitCode(0);
            steps.website_build_step.dependOn(&run.step);
        }

        // Start the website locally
        {
            const npm_install = b.addSystemCommand(&.{ "npm", "install" });
            npm_install.setCwd(b.path("website"));
            npm_install.expectExitCode(0);

            const run = std_extras.createCommandWithStdoutToStderr(b, builtin.target, "run docusaurus start");
            run.addArgs(&.{ "npm", "run", "start" });
            run.setCwd(b.path("website"));
            run.step.dependOn(steps.website_gen_step);
            run.step.dependOn(&npm_install.step);

            steps.website_dev_step.dependOn(&run.step);
        }
    }
}

fn doTarget(
    build_context: *BuildContext,
    resolved_target: std.Build.ResolvedTarget,
    set_as_cdb: bool,
    options: anytype,
    steps: anytype,
) release_artifacts.Artifacts {
    const b = build_context.b;
    const target = resolved_target.result;

    // Create a unique hash for this configuration. We use when we need to unique generate folders even when
    // multiple zig builds processes are running simultaneously. Ideally we would use hashUserInputOptionsMap
    // from std.Build, but it's private and quite complicated to copy here. This manual approach is simple but
    // not as robust.
    const config_hash = blk: {
        var hasher = std.hash.Wyhash.init(0);
        hasher.update(target.zigTriple(b.allocator) catch "");
        hasher.update(@tagName(options.build_mode));
        hasher.update(std.mem.asBytes(&options.enable_tracy));
        hasher.update(std.mem.asBytes(&options.sanitize_thread));
        hasher.update(std.mem.asBytes(&options.windows_installer_require_admin));
        break :blk hasher.final();
    };

    var concat_cdb = ConcatCompileCommandsStep.create(b, target, set_as_cdb, config_hash);
    steps.compile_all_step.dependOn(&concat_cdb.step);

    const floe_config_h = b.addConfigHeader(.{
        .style = .blank,
    }, .{
        .PRODUCTION_BUILD = build_context.build_mode == .production,
        .OPTIMISED_BUILD = build_context.optimise != .Debug,
        .RUNTIME_SAFETY_CHECKS_ON = build_context.optimise == .Debug or build_context.optimise == .ReleaseSafe,
        .FLOE_VERSION_STRING = build_context.floe_version_string,
        .FLOE_VERSION_HASH = build_context.floe_version_hash,
        .FLOE_DESCRIPTION = constants.floe_description,
        .FLOE_HOMEPAGE_URL = constants.floe_homepage_url,
        .FLOE_MANUAL_URL = constants.floe_manual_url,
        .FLOE_DOWNLOAD_URL = constants.floe_download_url,
        .FLOE_CHANGELOG_URL = constants.floe_changelog_url,
        .FLOE_SOURCE_CODE_URL = constants.floe_source_code_url,
        .FLOE_PROJECT_ROOT_PATH = b.build_root.path.?,
        .FLOE_PROJECT_CACHE_PATH = b.pathJoin(&.{ b.build_root.path.?, constants.floe_cache_relative }),
        .FLOE_VENDOR = constants.floe_vendor,
        .FLOE_CLAP_ID = constants.floe_clap_id,
        .IS_WINDOWS = target.os.tag == .windows,
        .IS_MACOS = target.os.tag == .macos,
        .IS_LINUX = target.os.tag == .linux,
        .OS_DISPLAY_NAME = b.fmt("{s}", .{@tagName(target.os.tag)}),
        .ARCH_DISPLAY_NAME = b.fmt("{s}", .{@tagName(target.cpu.arch)}),
        .MIN_WINDOWS_NTDDI_VERSION = @intFromEnum(std.Target.Os.WindowsVersion.parse(constants.min_windows_version) catch @panic("invalid win ver")),
        .MIN_MACOS_VERSION = constants.min_macos_version,
        .SENTRY_DSN = b.graph.env_map.get("SENTRY_DSN"),
    });

    if (target.os.tag == .windows and options.sanitize_thread) {
        std.log.err("thread sanitiser is not supported on Windows targets", .{});
        @panic("thread sanitiser is not supported on Windows targets");
    }

    const module_options: std.Build.Module.CreateOptions = .{
        .target = resolved_target,
        .optimize = build_context.optimise,
        .strip = false,
        .pic = true,
        .link_libc = true,
        .omit_frame_pointer = false,
        .unwind_tables = .sync,
        .sanitize_thread = options.sanitize_thread,
    };

    const stb_sprintf = blk: {
        const obj = b.addObject(.{
            .name = "stb_sprintf",
            .root_module = b.createModule(module_options),
        });
        obj.addCSourceFile(.{
            .file = b.path("third_party_libs/stb_sprintf.c"),
            .flags = FlagsBuilder.init(build_context, target, .{
                .compile_commands = concat_cdb,
            }).flags.items,
        });
        obj.addIncludePath(build_context.dep_stb.path(""));
        break :blk obj;
    };

    const xxhash = blk: {
        const obj = b.addObject(.{
            .name = "xxhash",
            .root_module = b.createModule(module_options),
        });
        obj.addCSourceFile(.{
            .file = build_context.dep_xxhash.path("xxhash.c"),
            .flags = FlagsBuilder.init(build_context, target, .{
                .compile_commands = concat_cdb,
            }).flags.items,
        });
        obj.linkLibC();
        break :blk obj;
    };

    const tracy = blk: {
        const lib = b.addStaticLibrary(.{
            .name = "tracy",
            .root_module = b.createModule(module_options),
        });
        lib.addCSourceFile(.{
            .file = build_context.dep_tracy.path("public/TracyClient.cpp"),
            .flags = FlagsBuilder.init(build_context, target, .{
                .compile_commands = concat_cdb,
            }).flags.items,
        });

        switch (target.os.tag) {
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
        applyUniversalSettings(build_context, lib, concat_cdb);

        break :blk lib;
    };

    const vitfx = blk: {
        const lib = b.addStaticLibrary(.{
            .name = "vitfx",
            .root_module = b.createModule(module_options),
        });
        const path = "third_party_libs/vitfx";
        lib.addCSourceFiles(.{
            .root = b.path(path),
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
            .flags = FlagsBuilder.init(build_context, target, .{
                .compile_commands = concat_cdb,
            }).flags.items,
        });
        lib.addIncludePath(b.path(path ++ "/src/synthesis"));
        lib.addIncludePath(b.path(path ++ "/src/synthesis/framework"));
        lib.addIncludePath(b.path(path ++ "/src/synthesis/filters"));
        lib.addIncludePath(b.path(path ++ "/src/synthesis/lookups"));
        lib.addIncludePath(b.path(path ++ "/src/common"));
        lib.linkLibCpp();

        lib.addIncludePath(build_context.dep_tracy.path("public"));

        break :blk lib;
    };

    const pugl = blk: {
        const lib = b.addStaticLibrary(.{
            .name = "pugl",
            .root_module = b.createModule(module_options),
        });
        const src_path = build_context.dep_pugl.path("src");
        const pugl_ver_hash = std.hash.Fnv1a_32.hash(build_context.dep_pugl.builder.pkg_hash);

        const pugl_flags = FlagsBuilder.init(build_context, target, .{
            .compile_commands = concat_cdb,
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

        switch (target.os.tag) {
            .windows => {
                lib.addCSourceFiles(.{
                    .root = src_path,
                    .files = &.{
                        "win.c",
                        "win_gl.c",
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
                lib.root_module.addCMacro("PuglWindow", b.fmt("PuglWindow{d}", .{pugl_ver_hash}));
                lib.root_module.addCMacro("PuglWindowDelegate", b.fmt("PuglWindowDelegate{d}", .{pugl_ver_hash}));
                lib.root_module.addCMacro("PuglWrapperView", b.fmt("PuglWrapperView{d}", .{pugl_ver_hash}));
                lib.root_module.addCMacro("PuglOpenGLView", b.fmt("PuglOpenGLView{d}", .{pugl_ver_hash}));

                lib.linkFramework("OpenGL");
                lib.linkFramework("CoreVideo");
            },
            else => {
                lib.addCSourceFiles(.{
                    .root = src_path,
                    .files = &.{
                        "x11.c",
                        "x11_gl.c",
                        "x11_stub.c",
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
        }

        lib.root_module.addCMacro("PUGL_DISABLE_DEPRECATED", "1");
        lib.root_module.addCMacro("PUGL_STATIC", "1");

        applyUniversalSettings(build_context, lib, concat_cdb);

        break :blk lib;
    };

    const debug_info_lib = blk: {
        var opts = module_options;
        opts.root_source_file = b.path("src/utils/debug_info/debug_info.zig");
        const lib = b.addObject(.{
            .name = "debug_info_lib",
            .root_module = b.createModule(opts),
        });
        lib.linkLibC(); // Means better debug info on Linux
        lib.addIncludePath(b.path("src/utils/debug_info"));

        break :blk lib;
    };

    const library = blk: {
        // IMPROVE: does this need to be a library? is foundation/os/plugin all linked together?
        const lib = b.addStaticLibrary(.{
            .name = "library",
            .root_module = b.createModule(module_options),
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

        const library_flags = FlagsBuilder.init(build_context, target, .{
            .full_diagnostics = true,
            .ubsan = true,
            .cpp = true,
            .compile_commands = concat_cdb,
        }).flags.items;

        switch (target.os.tag) {
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
                    .flags = FlagsBuilder.init(build_context, target, .{
                        .full_diagnostics = true,
                        .ubsan = true,
                        .objcpp = true,
                        .compile_commands = concat_cdb,
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
        lib.addConfigHeader(floe_config_h);
        lib.linkLibC();
        lib.linkLibrary(tracy);
        lib.addObject(debug_info_lib);
        lib.addObject(stb_sprintf);
        applyUniversalSettings(build_context, lib, concat_cdb);

        break :blk lib;
    };

    const stb_image = blk: {
        const obj = b.addObject(.{
            .name = "stb_image",
            .root_module = b.createModule(module_options),
        });
        obj.addCSourceFile(.{
            .file = b.path("third_party_libs/stb_image_impls.c"),
            .flags = FlagsBuilder.init(build_context, target, .{
                .compile_commands = concat_cdb,
            }).flags.items,
        });
        obj.addIncludePath(build_context.dep_stb.path(""));
        obj.linkLibC();
        break :blk obj;
    };

    const dr_wav = blk: {
        const obj = b.addObject(.{
            .name = "dr_wav",
            .root_module = b.createModule(module_options),
        });
        obj.addCSourceFile(
            .{
                .file = b.path("third_party_libs/dr_wav_implementation.c"),
                .flags = FlagsBuilder.init(build_context, target, .{
                    .compile_commands = concat_cdb,
                }).flags.items,
            },
        );
        obj.addIncludePath(build_context.dep_dr_libs.path(""));
        obj.linkLibC();
        break :blk obj;
    };

    const miniz = blk: {
        const lib = b.addStaticLibrary(.{
            .name = "miniz",
            .root_module = b.createModule(module_options),
        });
        lib.addCSourceFiles(.{
            .root = build_context.dep_miniz.path(""),
            .files = &.{
                "miniz.c",
                "miniz_tdef.c",
                "miniz_tinfl.c",
                "miniz_zip.c",
            },
            .flags = FlagsBuilder.init(build_context, target, .{
                .compile_commands = concat_cdb,
            }).flags.items,
        });
        lib.addIncludePath(build_context.dep_miniz.path(""));
        lib.linkLibC();
        lib.addIncludePath(b.path("third_party_libs/miniz"));

        break :blk lib;
    };

    if (targetCanRunNatively(target)) {
        var opts = module_options;
        opts.root_source_file = b.path("src/build/archiver.zig");
        const exe = b.addExecutable(.{
            .name = "archiver",
            .root_module = b.createModule(opts),
        });
        exe.linkLibrary(miniz);
        exe.linkLibC();
        applyUniversalSettings(build_context, exe, concat_cdb);

        build_context.native_archiver = exe;
    }

    const flac = blk: {
        const lib = b.addStaticLibrary(.{
            .name = "flac",
            .root_module = b.createModule(module_options),
        });
        const flags = FlagsBuilder.init(build_context, target, .{
            .compile_commands = concat_cdb,
        }).flags.items;

        lib.addCSourceFiles(.{
            .root = build_context.dep_flac.path("src/libFLAC"),
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

        const config_header = b.addConfigHeader(
            .{
                .style = .{ .cmake = build_context.dep_flac.path("config.cmake.h.in") },
                .include_path = "config.h",
            },
            .{
                .CPU_IS_BIG_ENDIAN = target.cpu.arch.endian() == .big,
                .ENABLE_64_BIT_WORDS = target.ptrBitWidth() == 64,
                .FLAC__ALIGN_MALLOC_DATA = target.cpu.arch.isX86(),
                .FLAC__CPU_ARM64 = target.cpu.arch.isAARCH64(),
                .FLAC__SYS_DARWIN = target.os.tag == .macos,
                .FLAC__SYS_LINUX = target.os.tag == .linux,
                .HAVE_BYTESWAP_H = target.os.tag == .linux,
                .HAVE_CPUID_H = target.cpu.arch.isX86(),
                .HAVE_FSEEKO = true,
                .HAVE_ICONV = target.os.tag != .windows,
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
        lib.addIncludePath(build_context.dep_flac.path("include"));
        lib.addIncludePath(build_context.dep_flac.path("src/libFLAC/include"));
        if (target.os.tag == .windows) {
            lib.root_module.addCMacro("FLAC__NO_DLL", "");
            lib.addCSourceFile(.{
                .file = build_context.dep_flac.path("src/share/win_utf8_io/win_utf8_io.c"),
                .flags = flags,
            });
        }

        break :blk lib;
    };

    const fft_convolver = blk: {
        const lib = b.addStaticLibrary(.{
            .name = "fftconvolver",
            .root_module = b.createModule(module_options),
        });
        var flags_builder: FlagsBuilder = FlagsBuilder.init(build_context, target, .{
            .compile_commands = concat_cdb,
        });
        if (target.os.tag == .macos) {
            lib.linkFramework("Accelerate");
            flags_builder.addFlag("-DAUDIOFFT_APPLE_ACCELERATE");
            flags_builder.addFlag("-ObjC++");
        } else {
            lib.addCSourceFile(.{
                .file = build_context.dep_pffft.path("pffft.c"),
                .flags = FlagsBuilder.init(build_context, target, .{
                    .compile_commands = concat_cdb,
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
        lib.addIncludePath(build_context.dep_pffft.path(""));
        applyUniversalSettings(build_context, lib, concat_cdb);

        break :blk lib;
    };

    const common_infrastructure = blk: {
        const lua = blk2: {
            const lib = b.addStaticLibrary(.{
                .name = "lua",
                .target = resolved_target,
                .optimize = build_context.optimise,
            });
            const flags = [_][]const u8{
                switch (target.os.tag) {
                    .linux => "-DLUA_USE_LINUX",
                    .macos => "-DLUA_USE_MACOSX",
                    .windows => "-DLUA_USE_WINDOWS",
                    else => "-DLUA_USE_POSIX",
                },
                if (build_context.optimise == .Debug) "-DLUA_USE_APICHECK" else "",
            };

            // compile as C++ so it uses exceptions instead of setjmp/longjmp. we use try/catch when handling lua
            lib.addCSourceFile(.{
                .file = b.path("third_party_libs/lua.cpp"),
                .flags = &flags,
            });
            lib.addIncludePath(build_context.dep_lua.path(""));
            lib.linkLibC();

            break :blk2 lib;
        };

        const src_root = b.path("src/common_infrastructure");

        const lib = b.addStaticLibrary(.{
            .name = "common_infrastructure",
            .root_module = b.createModule(module_options),
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
            .flags = FlagsBuilder.init(build_context, target, .{
                .full_diagnostics = true,
                .ubsan = true,
                .cpp = true,
                .compile_commands = concat_cdb,
            }).flags.items,
        });

        lib.linkLibrary(lua);
        lib.addObject(dr_wav);
        lib.linkLibrary(flac);
        lib.addObject(xxhash);
        lib.addConfigHeader(floe_config_h);
        lib.addIncludePath(src_root);
        lib.linkLibrary(library);
        lib.linkLibrary(miniz);
        applyUniversalSettings(build_context, lib, concat_cdb);

        break :blk lib;
    };

    const embedded_files = blk: {
        var opts = module_options;
        const root_path = "build_resources/embedded_files.zig";
        opts.root_source_file = b.path(root_path);
        const obj = b.addObject(.{
            .name = "embedded_files",
            .root_module = b.createModule(opts),
        });
        {
            var embedded_files_options = b.addOptions();
            if (build_context.dep_floe_logos) |logos| {
                const update = b.addUpdateSourceFiles();

                // Zig's @embedFile only works with paths lower than the root_source_file, so we have to copy them
                // into a subfolder and work out the relative paths.
                const logo_path = "build_resources/external/logo.png";
                update.addCopyFileToSource(logos.path("rasterized/plugin-gui-logo.png"), logo_path);
                const icon_path = "build_resources/external/icon.png";
                update.addCopyFileToSource(logos.path("rasterized/icon-background-256px.png"), icon_path);
                embedded_files_options.step.dependOn(&update.step);

                const root_dir = std.fs.path.dirname(root_path).?;

                embedded_files_options.addOption(?[]const u8, "logo_file", std.fs.path.relative(b.allocator, root_dir, logo_path) catch unreachable);
                embedded_files_options.addOption(?[]const u8, "icon_file", std.fs.path.relative(b.allocator, root_dir, icon_path) catch unreachable);
            } else {
                embedded_files_options.addOption(?[]const u8, "logo_file", null);
                embedded_files_options.addOption(?[]const u8, "icon_file", null);
            }
            obj.root_module.addOptions("build_options", embedded_files_options);
        }
        obj.linkLibC();
        obj.addIncludePath(b.path("build_resources"));

        break :blk obj;
    };

    const plugin = blk: {
        const lib = b.addStaticLibrary(.{
            .name = "plugin",
            .root_module = b.createModule(module_options),
        });
        const src_root = b.path("src/plugin");

        const flags = FlagsBuilder.init(build_context, target, .{
            .full_diagnostics = true,
            .ubsan = true,
            .cpp = true,
            .compile_commands = concat_cdb,
        }).flags.items;

        lib.addCSourceFiles(.{
            .root = src_root,
            .files = &(.{
                "engine/check_for_update.cpp",
                "engine/engine.cpp",
                "engine/package_installation.cpp",
                "engine/shared_engine_systems.cpp",
                "gui/gui.cpp",
                "gui/gui2_bot_panel.cpp",
                "gui/gui2_common_browser.cpp",
                "gui/gui2_inst_browser.cpp",
                "gui/gui2_ir_browser.cpp",
                "gui/gui2_library_dev_panel.cpp",
                "gui/gui2_macros.cpp",
                "gui/gui2_parameter_component.cpp",
                "gui/gui2_preset_browser.cpp",
                "gui/gui2_save_preset_panel.cpp",
                "gui/gui2_top_panel.cpp",
                "gui/gui_button_widgets.cpp",
                "gui/gui_dragger_widgets.cpp",
                "gui/gui_draw_knob.cpp",
                "gui/gui_drawing_helpers.cpp",
                "gui/gui_editor_widgets.cpp",
                "gui/gui_effects.cpp",
                "gui/gui_envelope.cpp",
                "gui/gui_keyboard.cpp",
                "gui/gui_knob_widgets.cpp",
                "gui/gui_label_widgets.cpp",
                "gui/gui_layer.cpp",
                "gui/gui_library_images.cpp",
                "gui/gui_waveform_images.cpp",
                "gui/gui_mid_panel.cpp",
                "gui/gui_modal_windows.cpp",
                "gui/gui_peak_meter_widget.cpp",
                "gui/gui_prefs.cpp",
                "gui/gui_waveform.cpp",
                "gui/gui_widget_compounds.cpp",
                "gui/gui_widget_helpers.cpp",
                "gui/gui_window.cpp",
                "gui_framework/draw_list.cpp",
                "gui_framework/draw_list_opengl.cpp",
                "gui_framework/gui_box_system.cpp",
                "gui_framework/gui_imgui.cpp",
                "gui_framework/gui_platform.cpp",
                "gui_framework/layout.cpp",
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
            }),
            .flags = flags,
        });

        switch (target.os.tag) {
            .windows => {
                lib.addCSourceFiles(.{
                    .root = src_root,
                    .files = &.{
                        "gui_framework/gui_platform_windows.cpp",
                    },
                    .flags = flags,
                });
            },
            .linux => {
                lib.addCSourceFiles(.{
                    .root = src_root,
                    .files = &.{
                        "gui_framework/gui_platform_linux.cpp",
                    },
                    .flags = flags,
                });
            },
            .macos => {
                lib.addCSourceFiles(.{
                    .root = src_root,
                    .files = &.{
                        "gui_framework/gui_platform_mac.mm",
                    },
                    .flags = FlagsBuilder.init(build_context, target, .{
                        .full_diagnostics = true,
                        .ubsan = true,
                        .objcpp = true,
                        .compile_commands = concat_cdb,
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
                .file = b.addWriteFiles().add("licenses.c", file_content: {
                    var data = std.ArrayList(u8).init(b.allocator);
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

            const generated_include_dir = b.addWriteFiles();
            _ = generated_include_dir.add("license_texts.h", file_content: {
                var data = std.ArrayList(u8).init(b.allocator);
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

        lib.addIncludePath(b.path("src/plugin"));
        lib.addIncludePath(b.path("src"));
        lib.addConfigHeader(floe_config_h);
        lib.linkLibrary(library);
        lib.linkLibrary(common_infrastructure);
        lib.linkLibrary(fft_convolver);
        lib.addObject(embedded_files);
        lib.linkLibrary(tracy);
        lib.linkLibrary(pugl);
        lib.addObject(stb_image);
        lib.addIncludePath(b.path("src/plugin/gui/live_edit_defs"));
        lib.linkLibrary(vitfx);
        lib.linkLibrary(miniz);
        applyUniversalSettings(build_context, lib, concat_cdb);

        break :blk lib;
    };

    if (targetCanRunNatively(target)) {
        const exe = b.addExecutable(.{
            .name = "docs_generator",
            .root_module = b.createModule(module_options),
        });
        exe.addCSourceFiles(.{
            .files = &.{
                "src/docs_generator/docs_generator.cpp",
                "src/common_infrastructure/final_binary_type.cpp",
            },
            .flags = FlagsBuilder.init(build_context, target, .{
                .full_diagnostics = true,
                .ubsan = true,
                .cpp = true,
                .compile_commands = concat_cdb,
            }).flags.items,
        });
        exe.root_module.addCMacro("FINAL_BINARY_TYPE", "DocsGenerator");
        exe.linkLibrary(common_infrastructure);
        exe.addIncludePath(b.path("src"));
        exe.addConfigHeader(floe_config_h);
        applyUniversalSettings(build_context, exe, concat_cdb);
        steps.compile_all_step.dependOn(&exe.step);

        build_context.native_docs_generator = exe;
    }

    const configured_packager = blk: {
        var exe = b.addExecutable(.{
            .name = "floe-packager",
            .root_module = b.createModule(module_options),
            .version = build_context.floe_version,
        });
        exe.addCSourceFiles(.{
            .files = &.{
                "src/packager_tool/packager.cpp",
                "src/common_infrastructure/final_binary_type.cpp",
            },
            .flags = FlagsBuilder.init(build_context, target, .{
                .full_diagnostics = true,
                .ubsan = true,
                .cpp = true,
                .compile_commands = concat_cdb,
            }).flags.items,
        });
        exe.root_module.addCMacro("FINAL_BINARY_TYPE", "Packager");
        exe.linkLibrary(common_infrastructure);
        exe.addIncludePath(b.path("src"));
        exe.addConfigHeader(floe_config_h);
        exe.linkLibrary(miniz);
        exe.addObject(embedded_files);

        applyUniversalSettings(build_context, exe, concat_cdb);

        const codesigned_exe = configure_binaries.maybeAddWindowsCodesign(
            exe,
            .{ .description = "Floe Packager" },
        );

        const install = b.addInstallBinFile(codesigned_exe, exe.out_filename);
        steps.install_all_step.dependOn(&install.step);

        break :blk release_artifacts.Artifact{
            .out_filename = exe.out_filename,
            .path = codesigned_exe,
        };
    };

    {
        var exe = b.addExecutable(.{
            .name = "preset-editor",
            .root_module = b.createModule(module_options),
            .version = build_context.floe_version,
        });
        exe.addCSourceFiles(.{
            .files = &.{
                "src/preset_editor_tool/preset_editor.cpp",
                "src/common_infrastructure/final_binary_type.cpp",
            },
            .flags = FlagsBuilder.init(build_context, target, .{
                .full_diagnostics = true,
                .ubsan = true,
                .cpp = true,
                .compile_commands = concat_cdb,
            }).flags.items,
        });
        exe.root_module.addCMacro("FINAL_BINARY_TYPE", "PresetEditor");
        exe.linkLibrary(common_infrastructure);
        exe.addIncludePath(b.path("src"));
        exe.addConfigHeader(floe_config_h);
        exe.linkLibrary(miniz);
        exe.addObject(embedded_files);
        applyUniversalSettings(build_context, exe, concat_cdb);

        const install = b.addInstallArtifact(exe, .{});
        steps.install_all_step.dependOn(&install.step);

        // IMPROVE: export preset-editor as a production artifact?
    }

    const configured_clap: ?configure_binaries.ConfiguredPlugin = blk: {
        if (!options.sanitize_thread) {
            const dso = b.addSharedLibrary(.{
                .name = "Floe.clap",
                .root_module = b.createModule(module_options),
                .version = build_context.floe_version,
            });
            dso.addCSourceFiles(.{
                .files = &.{
                    "src/plugin/plugin/plugin_entry.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                    .compile_commands = concat_cdb,
                }).flags.items,
            });
            dso.root_module.addCMacro("FINAL_BINARY_TYPE", "Clap");
            dso.addConfigHeader(floe_config_h);
            dso.addIncludePath(b.path("src"));
            dso.linkLibrary(plugin);

            applyUniversalSettings(build_context, dso, concat_cdb);
            addWindowsEmbedInfo(dso, .{
                .name = "Floe CLAP",
                .description = constants.floe_description,
                .icon_path = null,
            }) catch @panic("OOM");

            break :blk configure_binaries.addConfiguredPlugin(
                b,
                .clap,
                dso,
                configure_binaries.CodesignInfo{ .description = "Floe CLAP Plugin" },
            );
        } else {
            break :blk null;
        }
    };

    // Standalone is for development-only at the moment, so we can save a bit of time by not building it
    // in production builds.
    if (build_context.build_mode != .production) {
        const miniaudio = blk: {
            const lib = b.addStaticLibrary(.{
                .name = "miniaudio",
                .root_module = b.createModule(module_options),
            });
            lib.addCSourceFile(.{
                .file = b.path("third_party_libs/miniaudio.c"),
                .flags = FlagsBuilder.init(build_context, target, .{
                    .compile_commands = concat_cdb,
                }).flags.items,
            });
            // NOTE(Sam): disabling pulse audio because it was causing lots of stutters on my machine.
            lib.root_module.addCMacro("MA_NO_PULSEAUDIO", "1");
            lib.linkLibC();
            lib.addIncludePath(build_context.dep_miniaudio.path(""));
            switch (target.os.tag) {
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
            applyUniversalSettings(build_context, lib, concat_cdb);

            break :blk lib;
        };

        const portmidi = blk: {
            const lib = b.addStaticLibrary(.{
                .name = "portmidi",
                .root_module = b.createModule(module_options),
            });
            const pm_root = build_context.dep_portmidi.path("");
            const pm_flags = FlagsBuilder.init(build_context, target, .{
                .compile_commands = concat_cdb,
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
            switch (target.os.tag) {
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
            lib.addIncludePath(build_context.dep_portmidi.path("porttime"));
            lib.addIncludePath(build_context.dep_portmidi.path("pm_common"));
            applyUniversalSettings(build_context, lib, concat_cdb);

            break :blk lib;
        };

        const exe = b.addExecutable(.{
            .name = "floe_standalone",
            .root_module = b.createModule(module_options),
        });

        exe.addCSourceFiles(.{
            .files = &.{
                "src/standalone_wrapper/standalone_wrapper.cpp",
                "src/plugin/plugin/plugin_entry.cpp",
                "src/common_infrastructure/final_binary_type.cpp",
            },
            .flags = FlagsBuilder.init(build_context, target, .{
                .full_diagnostics = true,
                .ubsan = true,
                .cpp = true,
                .compile_commands = concat_cdb,
            }).flags.items,
        });

        exe.root_module.addCMacro("FINAL_BINARY_TYPE", "Standalone");
        exe.addConfigHeader(floe_config_h);
        exe.addIncludePath(b.path("src"));
        exe.linkLibrary(portmidi);
        exe.linkLibrary(miniaudio);
        exe.addIncludePath(build_context.dep_miniaudio.path(""));
        exe.linkLibrary(plugin);
        applyUniversalSettings(build_context, exe, concat_cdb);

        const install = b.addInstallArtifact(exe, .{});
        steps.install_all_step.dependOn(&install.step);
    }

    const vst3_flags = blk: {
        var flags = FlagsBuilder.init(build_context, target, .{
            .ubsan = false,
            .compile_commands = concat_cdb,
        });
        if (build_context.optimise == .Debug) {
            flags.addFlag("-DDEVELOPMENT=1");
        } else {
            flags.addFlag("-DRELEASE=1");
        }
        // Ignore warning about non-reproducible __DATE__ usage.
        flags.addFlag("-Wno-date-time");

        break :blk flags.flags.items;
    };

    const vst3_sdk = blk: {
        const lib = b.addStaticLibrary(.{
            .name = "VST3",
            .root_module = b.createModule(module_options),
        });
        lib.addCSourceFiles(.{
            .root = build_context.dep_vst3_sdk.path(""),
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
            .flags = vst3_flags,
        });

        switch (target.os.tag) {
            .windows => {},
            .linux => {},
            .macos => {
                lib.linkFramework("CoreFoundation");
                lib.linkFramework("Foundation");
            },
            else => {},
        }

        lib.addIncludePath(build_context.dep_vst3_sdk.path(""));
        lib.linkLibCpp();
        applyUniversalSettings(build_context, lib, concat_cdb);

        break :blk lib;
    };

    const vst3_validator = blk: {
        const vst3_validator = b.addExecutable(.{
            .name = "VST3-Validator",
            .root_module = b.createModule(module_options),
        });

        vst3_validator.addCSourceFiles(.{
            .root = build_context.dep_vst3_sdk.path("public.sdk"),
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
            .flags = vst3_flags,
        });

        switch (target.os.tag) {
            .windows => {
                vst3_validator.addCSourceFiles(.{
                    .root = build_context.dep_vst3_sdk.path(""),
                    .files = &.{"public.sdk/source/vst/hosting/module_win32.cpp"},
                    .flags = vst3_flags,
                });
                vst3_validator.linkSystemLibrary("ole32");
            },
            .linux => {
                vst3_validator.addCSourceFiles(.{
                    .root = build_context.dep_vst3_sdk.path(""),
                    .files = &.{"public.sdk/source/vst/hosting/module_linux.cpp"},
                    .flags = vst3_flags,
                });
            },
            .macos => {
                vst3_validator.addCSourceFiles(.{
                    .root = build_context.dep_vst3_sdk.path(""),
                    .files = &.{"public.sdk/source/vst/hosting/module_mac.mm"},
                    .flags = FlagsBuilder.init(build_context, target, .{
                        .objcpp = true,
                        .compile_commands = concat_cdb,
                    }).flags.items,
                });
            },
            else => {},
        }

        vst3_validator.addIncludePath(build_context.dep_vst3_sdk.path(""));
        vst3_validator.linkLibCpp();
        vst3_validator.linkLibrary(vst3_sdk);
        vst3_validator.linkLibrary(library); // for ubsan runtime
        applyUniversalSettings(build_context, vst3_validator, concat_cdb);

        break :blk vst3_validator;
    };

    const configured_vst3: ?configure_binaries.ConfiguredPlugin = blk: {
        if (!options.sanitize_thread) {
            const vst3 = b.addSharedLibrary(.{
                .name = "Floe.vst3",
                .version = build_context.floe_version,
                .root_module = b.createModule(module_options),
            });
            switch (target.os.tag) {
                .windows => {
                    vst3.root_module.addCMacro("WIN", "1");
                },
                .linux => {
                    vst3.root_module.addCMacro("LIN", "1");
                },
                .macos => {
                    vst3.root_module.addCMacro("MAC", "1");
                },
                else => {},
            }
            if (build_context.optimise == .Debug) {
                vst3.root_module.addCMacro("DEVELOPMENT", "1");
            } else {
                vst3.root_module.addCMacro("RELEASE", "1");
            }
            vst3.root_module.addCMacro("MACOS_USE_STD_FILESYSTEM", "1");
            vst3.root_module.addCMacro("CLAP_WRAPPER_VERSION", "\"0.11.0\"");
            vst3.root_module.addCMacro("STATICALLY_LINKED_CLAP_ENTRY", "1");

            var flags = FlagsBuilder.init(build_context, target, .{
                .ubsan = false,
                .compile_commands = concat_cdb,
            });
            flags.addFlag("-fno-char8_t");

            vst3.addCSourceFiles(.{
                .files = &.{
                    "src/plugin/plugin/plugin_entry.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                    .compile_commands = concat_cdb,
                }).flags.items,
            });
            vst3.root_module.addCMacro("FINAL_BINARY_TYPE", "Vst3");

            const wrapper_src_path = build_context.dep_clap_wrapper.path("src");
            vst3.addCSourceFiles(.{
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

            switch (target.os.tag) {
                .windows => {
                    vst3.addCSourceFile(.{
                        .file = build_context.dep_clap_wrapper.path("src/detail/os/windows.cpp"),
                        .flags = flags.flags.items,
                    });
                    vst3.addCSourceFiles(.{
                        .root = build_context.dep_vst3_sdk.path(""),
                        .files = &.{"public.sdk/source/main/dllmain.cpp"},
                        .flags = flags.flags.items,
                    });
                },
                .linux => {
                    vst3.addCSourceFile(.{
                        .file = build_context.dep_clap_wrapper.path("src/detail/os/linux.cpp"),
                        .flags = flags.flags.items,
                    });
                    vst3.addCSourceFiles(.{
                        .root = build_context.dep_vst3_sdk.path(""),
                        .files = &.{"public.sdk/source/main/linuxmain.cpp"},
                        .flags = flags.flags.items,
                    });
                },
                .macos => {
                    vst3.addCSourceFiles(.{
                        .root = wrapper_src_path,
                        .files = &.{
                            "detail/os/macos.mm",
                            "detail/clap/mac_helpers.mm",
                        },
                        .flags = flags.flags.items,
                    });
                    vst3.addCSourceFiles(.{
                        .root = build_context.dep_vst3_sdk.path(""),
                        .files = &.{"public.sdk/source/main/macmain.cpp"},
                        .flags = flags.flags.items,
                    });
                },
                else => {},
            }

            vst3.addIncludePath(build_context.dep_clap_wrapper.path("include"));
            vst3.addIncludePath(build_context.dep_clap_wrapper.path("libs/fmt"));
            vst3.addIncludePath(build_context.dep_clap_wrapper.path("libs/psl"));
            vst3.addIncludePath(build_context.dep_clap_wrapper.path("src"));
            vst3.addIncludePath(build_context.dep_vst3_sdk.path(""));
            vst3.linkLibCpp();

            vst3.linkLibrary(plugin);
            vst3.linkLibrary(vst3_sdk);

            vst3.addConfigHeader(floe_config_h);
            vst3.addIncludePath(b.path("src"));

            applyUniversalSettings(build_context, vst3, concat_cdb);
            addWindowsEmbedInfo(vst3, .{
                .name = "Floe VST3",
                .description = constants.floe_description,
                .icon_path = null,
            }) catch @panic("OOM");

            const configured_vst3 = configure_binaries.addConfiguredPlugin(
                b,
                .vst3,
                vst3,
                configure_binaries.CodesignInfo{ .description = "Floe VST3 Plugin" },
            );

            // Test VST3
            {
                const run_tests = std_extras.createCommandWithStdoutToStderr(b, target, "run VST3-Validator");
                run_tests.addFileArg(configure_binaries.nix_helper.maybePatchElfExecutable(vst3_validator));
                configured_vst3.addToRunStepArgs(run_tests);
                run_tests.expectExitCode(0);

                steps.test_vst3_validator.dependOn(&run_tests.step);
            }

            break :blk configured_vst3;
        } else {
            steps.test_vst3_validator.dependOn(&b.addFail("VST3 tests not allowed with this configuration").step);
            break :blk null;
        }
    };

    const configured_au: ?configure_binaries.ConfiguredPlugin = blk: {
        if (target.os.tag == .macos and !options.sanitize_thread) {
            const au_sdk = blk2: {
                const lib = b.addStaticLibrary(.{
                    .name = "AU",
                    .root_module = b.createModule(module_options),
                });

                lib.addCSourceFiles(.{
                    .root = build_context.dep_au_sdk.path("src/AudioUnitSDK"),
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
                    .flags = FlagsBuilder.init(build_context, target, .{
                        .cpp = true,
                        .compile_commands = concat_cdb,
                    }).flags.items,
                });
                lib.addIncludePath(build_context.dep_au_sdk.path("include"));
                lib.linkLibCpp();
                applyUniversalSettings(build_context, lib, concat_cdb);

                break :blk2 lib;
            };

            const flags = blk2: {
                var flags = FlagsBuilder.init(build_context, target, .{
                    .compile_commands = concat_cdb,
                });
                switch (target.os.tag) {
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
                if (build_context.optimise == .Debug) {
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

            const au = b.addSharedLibrary(.{
                .name = "Floe.component",
                .root_module = b.createModule(module_options),
                .version = build_context.floe_version,
            });
            au.addCSourceFiles(.{
                .files = &.{
                    "src/plugin/plugin/plugin_entry.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .objcpp = true,
                    .compile_commands = concat_cdb,
                }).flags.items,
            });
            au.root_module.addCMacro("FINAL_BINARY_TYPE", "AuV2");

            const wrapper_src_path = build_context.dep_clap_wrapper.path("src");

            au.addCSourceFiles(.{
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
                const generated_files = b.addWriteFiles();

                _ = generated_files.add("generated_entrypoints.hxx", b.fmt(
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

                _ = generated_files.add("generated_cocoaclasses.hxx", b.fmt(
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
                    .name = b.fmt("Floe{d}", .{build_context.floe_version_hash}),
                    .clap_id = constants.floe_clap_id,
                }));

                au.addIncludePath(generated_files.getDirectory());
            }

            au.addIncludePath(b.path("third_party_libs/clap/include"));
            au.addIncludePath(build_context.dep_au_sdk.path("include"));
            au.addIncludePath(build_context.dep_clap_wrapper.path("include"));
            au.addIncludePath(build_context.dep_clap_wrapper.path("libs/fmt"));
            au.addIncludePath(build_context.dep_clap_wrapper.path("src"));
            au.linkLibCpp();

            au.linkLibrary(plugin);
            au.linkLibrary(au_sdk);
            au.linkFramework("AudioToolbox");
            au.linkFramework("CoreMIDI");

            au.addConfigHeader(floe_config_h);
            au.addIncludePath(b.path("src"));

            applyUniversalSettings(build_context, au, concat_cdb);

            const configured_au = configure_binaries.addConfiguredPlugin(b, .au, au, null);

            if (builtin.os.tag == .macos) {
                if (std.mem.endsWith(u8, std.mem.trimRight(u8, b.install_path, "/"), "Library/Audio/Plug-Ins")) {
                    const installed_au_path = b.pathJoin(&.{ b.install_path, "Components/Floe.component" });

                    // Pluginval AU
                    {
                        // Pluginval puts all of it's output in stdout, not stderr.
                        const run = std_extras.createCommandWithStdoutToStderr(b, target, "run pluginval AU");

                        addPluginvalCommand(run, target);

                        run.addArgs(&.{ "--validate", installed_au_path });

                        run.step.dependOn(configured_au.install_step);
                        run.expectExitCode(0);

                        steps.pluginval_au.dependOn(&run.step);
                    }

                    // auval
                    {
                        const run_auval = std_extras.createCommandWithStdoutToStderr(b, target, "run auval");
                        run_auval.addArgs(&.{
                            "auval",
                            "-v",
                            constants.floe_au_type,
                            constants.floe_au_subtype,
                            constants.floe_au_manufacturer_code,
                        });
                        run_auval.step.dependOn(configured_au.install_step);
                        run_auval.expectExitCode(0);

                        // We need to make sure that the audio component service is aware of the new AU.
                        // Unfortunately, it doesn't do this automatically sometimes and if we were to run auval
                        // right now it might say "didn't find the component". We need to kill the service so
                        // that auval will rescan for installed AUs. The command on the terminal to do this is:
                        // killall -9 AudioComponentRegistrar. That is, send SIGKILL to the process named
                        // AudioComponentRegistrar.
                        if (!std_extras.pathExists(installed_au_path)) {
                            const cmd = b.addSystemCommand(&.{ "killall", "-9", "AudioComponentRegistrar" });

                            // We explicitly set the 'check' to an empty array which means that we do not care
                            // about the exit code or output of this command. Sometimes it can fail with: "No
                            // matching processes belonging to you were found" - which is fine.
                            cmd.stdio = .{
                                .check = std.ArrayListUnmanaged(std.Build.Step.Run.StdIo.Check).empty,
                            };

                            steps.auval.dependOn(&cmd.step);
                        }

                        steps.auval.dependOn(&run_auval.step);
                    }
                } else {
                    const fail = b.addFail("You must specify a global/user Library/Audio/Plug-Ins " ++
                        "--prefix to zig build in order to run AU tests");
                    steps.pluginval_au.dependOn(&fail.step);
                    steps.auval.dependOn(&fail.step);
                }
            } else {
                const fail = b.addFail("AU tests can only be run on macOS hosts");
                steps.pluginval_au.dependOn(&fail.step);
                steps.auval.dependOn(&fail.step);
            }

            break :blk configured_au;
        } else {
            const fail = b.addFail("AU tests not allowed with this configuration");
            steps.pluginval_au.dependOn(&fail.step);
            steps.auval.dependOn(&fail.step);
            break :blk null;
        }
    };

    const configured_windows_installer: ?release_artifacts.Artifact = blk: {
        if (target.os.tag == .windows) {
            const generated_manifests = b.addWriteFiles();

            const flags = FlagsBuilder.init(build_context, target, .{
                .full_diagnostics = true,
                .ubsan = true,
                .cpp = true,
                .compile_commands = concat_cdb,
            });

            const uninstaller = blk2: {
                const exe = b.addExecutable(.{
                    .name = "Floe-Uninstaller",
                    .root_module = b.createModule(module_options),
                    .version = build_context.floe_version,
                    .win32_manifest = generated_manifests.add("xml.manifest", windowsManifestContent(b, .{
                        .name = "Uninstaller",
                        .description = "Uninstaller for Floe plugins",
                        .require_admin = if (options.build_mode == .production) true else options.windows_installer_require_admin,
                    })),
                });
                exe.subsystem = .Windows;

                exe.root_module.addCMacro(
                    "UNINSTALLER_BINARY_NAME",
                    b.fmt("\"{s}\"", .{exe.out_filename}),
                );

                exe.addCSourceFiles(.{
                    .files = &.{
                        "src/windows_installer/uninstaller.cpp",
                        "src/windows_installer/gui.cpp",
                        "src/common_infrastructure/final_binary_type.cpp",
                    },
                    .flags = flags.flags.items,
                });
                exe.root_module.addCMacro("FINAL_BINARY_TYPE", "WindowsUninstaller");
                exe.linkSystemLibrary("gdi32");
                exe.linkSystemLibrary("version");
                exe.linkSystemLibrary("comctl32");
                exe.addConfigHeader(floe_config_h);
                exe.addIncludePath(b.path("src"));
                exe.addObject(stb_image);
                exe.linkLibrary(library);
                exe.linkLibrary(miniz);
                exe.linkLibrary(common_infrastructure);
                applyUniversalSettings(build_context, exe, concat_cdb);

                const codesigned_exe = configure_binaries.maybeAddWindowsCodesign(
                    exe,
                    .{ .description = "Floe Uninstaller" },
                );

                const install = b.addInstallBinFile(codesigned_exe, exe.out_filename);
                steps.install_all_step.dependOn(&install.step);

                break :blk2 .{
                    .step = exe,
                    .codesigned_path = codesigned_exe,
                };
            };

            const description = "Installer for Floe plugins";

            const exe = b.addExecutable(.{
                .name = b.fmt("Floe-Installer-v{s}", .{ .version = build_context.floe_version_string }),
                .root_module = b.createModule(module_options),
                .version = build_context.floe_version,
                .win32_manifest = generated_manifests.add("xml.manifest", windowsManifestContent(b, .{
                    .name = "Installer",
                    .description = description,
                    .require_admin = if (options.build_mode == .production) true else options.windows_installer_require_admin,
                })),
            });
            exe.subsystem = .Windows;

            // Add resources.
            {
                var rc_include_path: std.BoundedArray(std.Build.LazyPath, 5) = .{};

                if (build_context.dep_floe_logos) |logos| {
                    const sidebar_img = "rasterized/win-installer-sidebar.png";
                    const sidebar_img_lazy_path = logos.path(sidebar_img);
                    rc_include_path.append(sidebar_img_lazy_path.dirname()) catch @panic("OOM");
                    exe.root_module.addCMacro(
                        "SIDEBAR_IMAGE_PATH",
                        b.fmt("\"{s}\"", .{std.fs.path.basename(sidebar_img)}),
                    );
                }

                if (configured_vst3) |vst3_plugin| {
                    exe.root_module.addCMacro("VST3_PLUGIN_BINARY_NAME", "\"Floe.vst3\"");
                    rc_include_path.append(vst3_plugin.plugin_path.dirname()) catch @panic("OOM");
                }

                if (configured_clap) |clap_plugin| {
                    exe.root_module.addCMacro("CLAP_PLUGIN_BINARY_NAME", "\"Floe.clap\"");
                    rc_include_path.append(clap_plugin.plugin_path.dirname()) catch @panic("OOM");
                }

                {
                    exe.root_module.addCMacro(
                        "UNINSTALLER_BINARY_NAME",
                        b.fmt("\"{s}\"", .{uninstaller.step.out_filename}),
                    );
                    rc_include_path.append(uninstaller.codesigned_path.dirname()) catch @panic("OOM");
                }

                exe.addWin32ResourceFile(.{
                    .file = b.path("src/windows_installer/resources.rc"),
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
                .flags = flags.flags.items,
            });

            exe.root_module.addCMacro("FINAL_BINARY_TYPE", "WindowsInstaller");
            exe.linkSystemLibrary("gdi32");
            exe.linkSystemLibrary("version");
            exe.linkSystemLibrary("comctl32");

            addWindowsEmbedInfo(exe, .{
                .name = "Floe Installer",
                .description = description,
                .icon_path = if (build_context.dep_floe_logos) |logos| logos.path("rasterized/icon.ico") else null,
            }) catch @panic("OOM");
            exe.addConfigHeader(floe_config_h);
            exe.addIncludePath(b.path("src"));
            exe.addObject(stb_image);
            exe.linkLibrary(library);
            exe.linkLibrary(miniz);
            exe.linkLibrary(common_infrastructure);
            applyUniversalSettings(build_context, exe, concat_cdb);

            const codesigned_path = configure_binaries.maybeAddWindowsCodesign(
                exe,
                .{ .description = "Floe Installer" },
            );

            // Installer tests
            {
                const run_installer = std.Build.Step.Run.create(b, b.fmt("run {s}", .{exe.name}));
                run_installer.addFileArg(codesigned_path);
                run_installer.addArg("--autorun");
                run_installer.expectExitCode(0);

                // IMPROVE actually test for installation

                const run_uninstaller = std.Build.Step.Run.create(b, b.fmt("run {s}", .{uninstaller.step.name}));
                run_uninstaller.addFileArg(uninstaller.codesigned_path);
                run_uninstaller.addArg("--autorun");
                run_uninstaller.expectExitCode(0);
                run_uninstaller.step.dependOn(&run_installer.step);

                steps.test_windows_install.dependOn(&run_uninstaller.step);
            }

            // Install
            {
                const install = b.addInstallBinFile(codesigned_path, exe.out_filename);
                steps.install_all_step.dependOn(&install.step);
            }

            break :blk release_artifacts.Artifact{
                .out_filename = exe.out_filename,
                .path = codesigned_path,
            };
        } else {
            steps.test_windows_install.dependOn(&b.addFail("Windows installer tests not allowed with this configuration").step);
            break :blk null;
        }
    };

    // We don't need tests in production builds so we can save some build time here.
    if (build_context.build_mode != .production) {
        const exe = b.addExecutable(.{
            .name = "tests",
            .root_module = b.createModule(module_options),
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
            .flags = FlagsBuilder.init(build_context, target, .{
                .full_diagnostics = true,
                .ubsan = true,
                .cpp = true,
                .compile_commands = concat_cdb,
            }).flags.items,
        });
        exe.root_module.addCMacro("FINAL_BINARY_TYPE", "Tests");
        exe.addConfigHeader(floe_config_h);
        exe.linkLibrary(plugin);
        applyUniversalSettings(build_context, exe, concat_cdb);

        const test_binary = configure_binaries.nix_helper.maybePatchElfExecutable(exe);

        const install = b.addInstallBinFile(test_binary, exe.out_filename);
        steps.install_all_step.dependOn(&install.step);

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
            const run_tests = std.Build.Step.Run.create(b, "run unit tests");
            run_tests.addFileArg(test_binary);
            add_tests_args(run_tests, configured_clap);

            run_tests.expectExitCode(0);

            steps.test_step.dependOn(&run_tests.step);
        }

        // Coverage tests
        if (builtin.os.tag == .linux) {
            const run_coverage = b.addSystemCommand(&.{
                "kcov",
                b.fmt("--include-pattern={s}", .{b.pathFromRoot("src")}),
                b.fmt("{s}/coverage-out", .{constants.floe_cache_relative}),
            });
            run_coverage.addFileArg(test_binary);
            add_tests_args(run_coverage, configured_clap);
            run_coverage.expectExitCode(0);
            steps.coverage.dependOn(&run_coverage.step);
        } else {
            steps.coverage.dependOn(&b.addFail("coverage not supported on this OS").step);
        }

        // Valgrind test
        if (!options.sanitize_thread) {
            const run = b.addSystemCommand(&.{
                "valgrind",
                "--leak-check=full",
                "--fair-sched=yes",
                "--num-callers=25",
                "--gen-suppressions=all",
                b.fmt("--suppressions={s}", .{b.pathFromRoot("valgrind.supp")}),
                "--error-exitcode=1",
                "--exit-on-first-error=no",
            });
            run.addFileArg(test_binary);
            add_tests_args(run, configured_clap);
            run.expectExitCode(0);

            steps.valgrind.dependOn(&run.step);
        } else {
            steps.valgrind.dependOn(&b.addFail("valgrind not allowed for this build configuration").step);
        }
    }

    // Clap Validator test
    if (configured_clap) |p| {
        const run = std_extras.createCommandWithStdoutToStderr(b, target, "run clap-validator");
        if (b.findProgram(
            &.{if (target.os.tag != .windows) "clap-validator" else "clap-validator.exe"},
            &[0][]const u8{},
        ) catch null) |program| {
            run.addArg(program); // Use system-installed clap-validator.
        } else if (target.os.tag == .windows) {
            if (b.lazyDependency("clap_validator_windows", .{})) |dep| {
                run.addFileArg(dep.path("clap-validator.exe"));
            }
        } else if (target.os.tag == .macos) {
            if (b.lazyDependency("clap_validator_macos", .{})) |dep| {
                const bin_path = dep.path("clap-validator");
                run.addFileArg(bin_path);
                run.step.dependOn(&chmodExeStep(b, bin_path).step);
            }
        } else if (target.os.tag == .linux) {
            if (b.lazyDependency("clap_validator_linux", .{})) |dep| {
                const bin_path = dep.path("clap-validator");
                run.addFileArg(bin_path);
                run.step.dependOn(&chmodExeStep(b, bin_path).step);
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

        steps.clap_val.dependOn(&run.step);
    } else {
        steps.clap_val.dependOn(&b.addFail("clap-validator not allowed for this build configuration").step);
    }

    // Pluginval test
    if (configured_vst3) |p| {
        // Pluginval puts all of it's output in stdout, not stderr.
        const run = std_extras.createCommandWithStdoutToStderr(b, target, "run pluginval");

        addPluginvalCommand(run, target);

        // In headless environments such as CI, GUI tests always fail on Linux so we skip them.
        if (builtin.os.tag == .linux and b.graph.env_map.get("DISPLAY") == null) {
            run.addArg("--skip-gui-tests");
        }

        run.addArg("--validate");
        p.addToRunStepArgs(run);
        run.expectExitCode(0);

        steps.pluginval.dependOn(&run.step);
    } else {
        steps.pluginval.dependOn(&b.addFail("pluginval not allowed for this build configuration").step);
    }

    // clang-tidy
    {
        const clang_tidy_step = check_steps.ClangTidyStep.create(b, target);
        clang_tidy_step.step.dependOn(&concat_cdb.step);
        steps.clang_tidy.dependOn(&clang_tidy_step.step);
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
