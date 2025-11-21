// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Notes on compiling/cross-compiling:
// Windows:
// - Zig has MinGW built-in. So Windows.h and pretty much all win32 API is available by default.
//   Cross-compiling is therefore easy.
// macOS:
// - The macOS SDKs are not included in Zig, we have to ensure they are available ourselves, and tell
//   zig where they are. We use Nix for this. See flake.nix. After that, cross-compiling works
//   well. And as a bonus, we don't have to install Xcode if we're on a mac.
// Linux:
// - We don't support cross-compiling for Linux at the moment.
// - For native compiling, we rely on nix and pkg-config. Use 'pkg-config --list-all | fzf' to find
//   the names of libraries to use when doing linkSystemLibrary2().
//
// NOTE: this whole file needs to be refactored.
// We need to more closely follow Zig's build system patterns: steps, step dependencies, enable
// parallelism of steps where possible, etc.
// Additionally, I think it will pay off to move all tests and scripts to zig too, so we can have
// a single point of truth for all configuration, building and testing

const std = @import("std");
const builtin = @import("builtin");

const std_extras = @import("src/build/std_extras.zig");
const constants = @import("src/build/constants.zig");

const ConcatCompileCommandsStep = @import("src/build/ConcatCompileCommandsStep.zig");
const check_steps = @import("src/build/check_steps.zig");
const scripts = @import("src/build/scripts.zig");
const configure_binaries = @import("src/build/configure_binaries.zig");

comptime {
    const current_zig = builtin.zig_version;
    const required_zig = std.SemanticVersion.parse("0.14.0") catch unreachable;
    if (current_zig.order(required_zig) != .eq) {
        @compileError(std.fmt.comptimePrint("Floe requires Zig version {}, but you are using version {}.", .{ required_zig, current_zig }));
    }
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

const BuildMode = enum {
    development,
    performance_profiling,
    production, // a.k.a.: release, end-user, for-distribution
};

const BuildContext = struct {
    b: *std.Build,
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
        target: std.Build.ResolvedTarget,
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
        target: std.Build.ResolvedTarget,
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
        try self.flags.append(context.b.fmt("-DMINIZ_LITTLE_ENDIAN={d}", .{@intFromBool(target.result.cpu.arch.endian() == .little)}));
        try self.flags.append("-DMINIZ_HAS_64BIT_REGISTERS=1");

        try self.flags.append("-DSTBI_NO_STDIO");
        try self.flags.append("-DSTBI_MAX_DIMENSIONS=65535"); // we use u16 for dimensions

        if (target.result.os.tag == .linux) {
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
            if (target.result.os.tag == .linux) {
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

        if (target.result.os.tag == .windows) {
            // On Windows, fix compile errors related to deprecated usage of string in mingw
            try self.flags.append("-DSTRSAFE_NO_DEPRECATE");
            try self.flags.append("-DUNICODE");
            try self.flags.append("-D_UNICODE");
        } else if (target.result.os.tag == .macos) {
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

fn applyUniversalSettings(context: *BuildContext, step: *std.Build.Step.Compile, compile_commands: *ConcatCompileCommandsStep) void {
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
        if (b.lazyDependency("macos_sdk", .{})) |sdk| {
            step.addSystemIncludePath(sdk.path("usr/include"));
            step.addLibraryPath(sdk.path("usr/lib"));
            step.addFrameworkPath(sdk.path("System/Library/Frameworks"));
        }

        // b.sysroot = sdk.path("");
    }

    compile_commands.step.dependOn(&step.step);

    if (context.b.graph.env_map.get("FLOE_RPATH")) |rpath| {
        step.root_module.addRPathSpecial(rpath);
    }

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

            var target = b.resolveTargetQuery(try std.Target.Query.parse(.{
                .arch_os_abi = arch_os_abi,
                .cpu_features = cpu_features,
            }));
            if (target.result.os.tag == .linux) {
                if (b.graph.env_map.get("FLOE_DYNAMIC_LINKER")) |dl| {
                    target.result.dynamic_linker.set(dl);
                }
            }
            try targets.append(target);
        }
    }

    return targets;
}

fn getLicenceText(b: *std.Build, filename: []const u8) ![]const u8 {
    const file = try b.build_root.handle.openFile(
        b.pathJoin(&.{ "LICENSES", filename }),
        .{ .mode = std.fs.File.OpenMode.read_only },
    );
    defer file.close();

    return try file.readToEndAlloc(b.allocator, 1024 * 1024 * 1024);
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

    const compile_all_step = b.step("compile", "Compiles all into zig-out folder");
    b.default_step = compile_all_step;

    const test_step = b.step("test", "Run unit tests");
    const coverage = b.step("test-coverage", "Generate code coverage report of unit tests");

    const clap_val = b.step("test:clap-val", "Test using clap-validator");
    const test_vst3_validator = b.step("test:vst3-val", "Run VST3 Validator on built VST3 plugin");
    const pluginval_au = b.step("test:pluginval-au", "Test AU using pluginval");
    const auval = b.step("test:auval", "Test AU using auval");
    const pluginval = b.step("test:pluginval", "Test using pluginval");
    const valgrind = b.step("test:valgrind", "Test using Valgrind");
    const test_windows_install = b.step("test:windows-install", "Test installation and uninstallation on Windows");

    const clang_tidy = b.step("check:clang-tidy", "Run clang-tidy on source files");

    const format_step = b.step("script:format", "Format code with clang-format");
    const echo_step = b.step("script:echo-latest-changes", "Echo latest changes from changelog");
    const ci_step = b.step("script:ci", "Run CI checks");
    const upload_errors_step = b.step("script:upload-errors", "Upload error reports to Sentry");

    const website_gen_step = b.step("script:website-generate", "Generate the static JSON for the website");
    const website_build_step = b.step("script:website-build", "Build the website");
    const website_dev_step = b.step("script:website-dev", "Start website dev build locally");
    const website_promote_step = b.step("script:website-promote-beta-to-stable", "Promote the 'beta' documentation to be the latest stable version");

    var build_context: BuildContext = .{
        .b = b,
        .enable_tracy = options.enable_tracy,
        .build_mode = options.build_mode,
        .optimise = switch (options.build_mode) {
            .development => std.builtin.OptimizeMode.Debug,
            .performance_profiling, .production => std.builtin.OptimizeMode.ReleaseSafe,
        },
        .dep_floe_logos = if (options.fetch_floe_logos)
            b.dependency("floe_logos", .{})
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

    const floe_version_string = blk: {
        var ver: []const u8 = b.build_root.handle.readFileAlloc(b.allocator, "version.txt", 256) catch @panic("version.txt error");
        ver = std.mem.trim(u8, ver, " \r\n\t");

        if (build_context.build_mode != .production) {
            ver = b.fmt("{s}+{s}", .{
                ver,
                std.mem.trim(u8, b.run(&.{ "git", "rev-parse", "--short", "HEAD" }), " \r\n\t"),
            });
        }
        break :blk ver;
    };
    const floe_version = std.SemanticVersion.parse(floe_version_string) catch @panic("invalid version");
    const floe_version_hash = std.hash.Fnv1a_32.hash(floe_version_string);

    for (targets.items) |target| {
        // Create a unique hash for this configuration. We use when we need to unique generate folders even when
        // multiple zig builds processes are running simultaneously. Ideally we would use hashUserInputOptionsMap
        // from std.Build, but it's private and quite complicated to copy here. This manual approach is simple but
        // not as robust.
        const config_hash = blk: {
            var hasher = std.hash.Wyhash.init(0);
            hasher.update(target.query.zigTriple(b.allocator) catch "");
            hasher.update(@tagName(options.build_mode));
            hasher.update(if (options.enable_tracy) "tracy" else "no_tracy");
            hasher.update(if (options.sanitize_thread) "sanitize_thread" else "no_sanitize_thread");
            hasher.update(
                if (options.windows_installer_require_admin) "win_installer_elevated" else "win_installer_not_elevated",
            );
            break :blk hasher.final();
        };

        var concat_cdb = ConcatCompileCommandsStep.create(
            b,
            target,
            target.query.eql(target_for_compile_commands.query),
            config_hash,
        );
        compile_all_step.dependOn(&concat_cdb.step);

        const build_config_step = b.addConfigHeader(.{
            .style = .blank,
        }, .{
            .PRODUCTION_BUILD = build_context.build_mode == .production,
            .OPTIMISED_BUILD = build_context.optimise != .Debug,
            .RUNTIME_SAFETY_CHECKS_ON = build_context.optimise == .Debug or build_context.optimise == .ReleaseSafe,
            .FLOE_VERSION_STRING = floe_version_string,
            .FLOE_VERSION_HASH = floe_version_hash,
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
            .IS_WINDOWS = target.result.os.tag == .windows,
            .IS_MACOS = target.result.os.tag == .macos,
            .IS_LINUX = target.result.os.tag == .linux,
            .OS_DISPLAY_NAME = b.fmt("{s}", .{@tagName(target.result.os.tag)}),
            .ARCH_DISPLAY_NAME = b.fmt("{s}", .{@tagName(target.result.cpu.arch)}),
            .MIN_WINDOWS_NTDDI_VERSION = @intFromEnum(std.Target.Os.WindowsVersion.parse(constants.min_windows_version) catch @panic("invalid win ver")),
            .MIN_MACOS_VERSION = constants.min_macos_version,
            .SENTRY_DSN = b.graph.env_map.get("SENTRY_DSN"),
        });

        if (target.result.os.tag == .windows and options.sanitize_thread) {
            std.log.err("thread sanitiser is not supported on Windows targets", .{});
            @panic("thread sanitiser is not supported on Windows targets");
        }

        const module_options: std.Build.Module.CreateOptions = .{
            .target = target,
            .optimize = build_context.optimise,
            .strip = false,
            .pic = true,
            .link_libc = true,
            .omit_frame_pointer = false,
            .unwind_tables = .sync,
            .sanitize_thread = options.sanitize_thread,
        };

        var stb_sprintf = b.addObject(.{
            .name = "stb_sprintf",
            .root_module = b.createModule(module_options),
        });
        stb_sprintf.addCSourceFile(.{
            .file = b.path("third_party_libs/stb_sprintf.c"),
            .flags = FlagsBuilder.init(&build_context, target, .{
                .compile_commands = concat_cdb,
            }).flags.items,
        });
        stb_sprintf.addIncludePath(build_context.dep_stb.path(""));

        var xxhash = b.addObject(.{
            .name = "xxhash",
            .root_module = b.createModule(module_options),
        });
        xxhash.addCSourceFile(.{
            .file = build_context.dep_xxhash.path("xxhash.c"),
            .flags = FlagsBuilder.init(&build_context, target, .{
                .compile_commands = concat_cdb,
            }).flags.items,
        });
        xxhash.linkLibC();

        const tracy = b.addStaticLibrary(.{
            .name = "tracy",
            .root_module = b.createModule(module_options),
        });
        {
            tracy.addCSourceFile(.{
                .file = build_context.dep_tracy.path("public/TracyClient.cpp"),
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .compile_commands = concat_cdb,
                }).flags.items,
            });

            switch (target.result.os.tag) {
                .windows => {
                    tracy.linkSystemLibrary("ws2_32");
                },
                .macos => {},
                .linux => {},
                else => {
                    unreachable;
                },
            }
            tracy.linkLibCpp();
            applyUniversalSettings(&build_context, tracy, concat_cdb);
        }

        const vitfx = b.addStaticLibrary(.{
            .name = "vitfx",
            .root_module = b.createModule(module_options),
        });
        {
            const vitfx_path = "third_party_libs/vitfx";
            vitfx.addCSourceFiles(.{
                .files = &.{
                    vitfx_path ++ "/src/synthesis/effects/reverb.cpp",
                    vitfx_path ++ "/src/synthesis/effects/phaser.cpp",
                    vitfx_path ++ "/src/synthesis/effects/delay.cpp",
                    vitfx_path ++ "/src/synthesis/framework/processor.cpp",
                    vitfx_path ++ "/src/synthesis/framework/processor_router.cpp",
                    vitfx_path ++ "/src/synthesis/framework/value.cpp",
                    vitfx_path ++ "/src/synthesis/framework/feedback.cpp",
                    vitfx_path ++ "/src/synthesis/framework/operators.cpp",
                    vitfx_path ++ "/src/synthesis/filters/phaser_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/synth_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/sallen_key_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/comb_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/digital_svf.cpp",
                    vitfx_path ++ "/src/synthesis/filters/dirty_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/ladder_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/diode_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/formant_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/formant_manager.cpp",
                    vitfx_path ++ "/wrapper.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .compile_commands = concat_cdb,
                }).flags.items,
            });
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/synthesis"));
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/synthesis/framework"));
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/synthesis/filters"));
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/synthesis/lookups"));
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/common"));
            vitfx.linkLibCpp();

            vitfx.addIncludePath(build_context.dep_tracy.path("public"));
        }

        const pugl = b.addStaticLibrary(.{
            .name = "pugl",
            .root_module = b.createModule(module_options),
        });
        {
            const pugl_path = build_context.dep_pugl.path("src");
            const pugl_version = std.hash.Fnv1a_32.hash(build_context.dep_pugl.builder.pkg_hash);

            const pugl_flags = FlagsBuilder.init(&build_context, target, .{
                .compile_commands = concat_cdb,
            }).flags.items;

            pugl.addCSourceFiles(.{
                .root = pugl_path,
                .files = &.{
                    "common.c",
                    "internal.c",
                    "internal.c",
                },
                .flags = pugl_flags,
            });

            switch (target.result.os.tag) {
                .windows => {
                    pugl.addCSourceFiles(.{
                        .root = pugl_path,
                        .files = &.{
                            "win.c",
                            "win_gl.c",
                            "win_stub.c",
                        },
                        .flags = pugl_flags,
                    });
                    pugl.linkSystemLibrary("opengl32");
                    pugl.linkSystemLibrary("gdi32");
                    pugl.linkSystemLibrary("dwmapi");
                },
                .macos => {
                    pugl.addCSourceFiles(.{
                        .root = pugl_path,
                        .files = &.{
                            "mac.m",
                            "mac_gl.m",
                            "mac_stub.m",
                        },
                        .flags = pugl_flags,
                    });
                    pugl.root_module.addCMacro("PuglWindow", b.fmt("PuglWindow{d}", .{pugl_version}));
                    pugl.root_module.addCMacro("PuglWindowDelegate", b.fmt("PuglWindowDelegate{d}", .{pugl_version}));
                    pugl.root_module.addCMacro("PuglWrapperView", b.fmt("PuglWrapperView{d}", .{pugl_version}));
                    pugl.root_module.addCMacro("PuglOpenGLView", b.fmt("PuglOpenGLView{d}", .{pugl_version}));

                    pugl.linkFramework("OpenGL");
                    pugl.linkFramework("CoreVideo");
                },
                else => {
                    pugl.addCSourceFiles(.{
                        .root = pugl_path,
                        .files = &.{
                            "x11.c",
                            "x11_gl.c",
                            "x11_stub.c",
                        },
                        .flags = pugl_flags,
                    });
                    pugl.root_module.addCMacro("USE_XRANDR", "0");
                    pugl.root_module.addCMacro("USE_XSYNC", "1");
                    pugl.root_module.addCMacro("USE_XCURSOR", "1");

                    pugl.linkSystemLibrary2("gl", .{ .use_pkg_config = linux_use_pkg_config });
                    pugl.linkSystemLibrary2("glx", .{ .use_pkg_config = linux_use_pkg_config });
                    pugl.linkSystemLibrary2("x11", .{ .use_pkg_config = linux_use_pkg_config });
                    pugl.linkSystemLibrary2("xcursor", .{ .use_pkg_config = linux_use_pkg_config });
                    pugl.linkSystemLibrary2("xext", .{ .use_pkg_config = linux_use_pkg_config });
                },
            }

            pugl.root_module.addCMacro("PUGL_DISABLE_DEPRECATED", "1");
            pugl.root_module.addCMacro("PUGL_STATIC", "1");

            applyUniversalSettings(&build_context, pugl, concat_cdb);
        }

        var debug_info_module_options = module_options;
        debug_info_module_options.root_source_file = b.path("src/utils/debug_info/debug_info.zig");
        const debug_info_lib = b.addObject(.{
            .name = "debug_info_lib",
            .root_module = b.createModule(debug_info_module_options),
        });
        debug_info_lib.linkLibC(); // Means better debug info on Linux
        debug_info_lib.addIncludePath(b.path("src/utils/debug_info"));

        // IMPROVE: does this need to be a library? is foundation/os/plugin all linked together?
        const library = b.addStaticLibrary(.{
            .name = "library",
            .root_module = b.createModule(module_options),
        });
        {
            const library_path = "src";

            const common_source_files = .{
                library_path ++ "/utils/debug/debug.cpp",
                library_path ++ "/utils/cli_arg_parse.cpp",
                library_path ++ "/utils/leak_detecting_allocator.cpp",
                library_path ++ "/utils/no_hash.cpp",
                library_path ++ "/tests/framework.cpp",
                library_path ++ "/utils/logger/logger.cpp",
                library_path ++ "/foundation/utils/string.cpp",
                library_path ++ "/os/filesystem.cpp",
                library_path ++ "/os/misc.cpp",
                library_path ++ "/os/web.cpp",
                library_path ++ "/os/threading.cpp",
            };

            const unix_source_files = .{
                library_path ++ "/os/filesystem_unix.cpp",
                library_path ++ "/os/misc_unix.cpp",
                library_path ++ "/os/threading_pthread.cpp",
            };

            const windows_source_files = .{
                library_path ++ "/os/filesystem_windows.cpp",
                library_path ++ "/os/misc_windows.cpp",
                library_path ++ "/os/threading_windows.cpp",
                library_path ++ "/os/web_windows.cpp",
            };

            const macos_source_files = .{
                library_path ++ "/os/filesystem_mac.mm",
                library_path ++ "/os/misc_mac.mm",
                library_path ++ "/os/threading_mac.cpp",
                library_path ++ "/os/web_mac.mm",
            };

            const linux_source_files = .{
                library_path ++ "/os/filesystem_linux.cpp",
                library_path ++ "/os/misc_linux.cpp",
                library_path ++ "/os/threading_linux.cpp",
                library_path ++ "/os/web_linux.cpp",
            };

            const library_flags = FlagsBuilder.init(&build_context, target, .{
                .full_diagnostics = true,
                .ubsan = true,
                .cpp = true,
                .compile_commands = concat_cdb,
            }).flags.items;

            switch (target.result.os.tag) {
                .windows => {
                    library.addCSourceFiles(.{
                        .files = &windows_source_files,
                        .flags = library_flags,
                    });
                    library.linkSystemLibrary("dbghelp");
                    library.linkSystemLibrary("shlwapi");
                    library.linkSystemLibrary("ole32");
                    library.linkSystemLibrary("crypt32");
                    library.linkSystemLibrary("uuid");
                    library.linkSystemLibrary("winhttp");

                    // synchronization.lib (https://github.com/ziglang/zig/issues/14919)
                    library.linkSystemLibrary("api-ms-win-core-synch-l1-2-0");
                },
                .macos => {
                    library.addCSourceFiles(.{
                        .files = &unix_source_files,
                        .flags = library_flags,
                    });
                    library.addCSourceFiles(.{
                        .files = &macos_source_files,
                        .flags = FlagsBuilder.init(&build_context, target, .{
                            .full_diagnostics = true,
                            .ubsan = true,
                            .objcpp = true,
                            .compile_commands = concat_cdb,
                        }).flags.items,
                    });
                    library.linkFramework("Cocoa");
                    library.linkFramework("CoreFoundation");
                    library.linkFramework("AppKit");
                },
                .linux => {
                    library.addCSourceFiles(.{ .files = &unix_source_files, .flags = library_flags });
                    library.addCSourceFiles(.{ .files = &linux_source_files, .flags = library_flags });
                    library.linkSystemLibrary2("libcurl", .{ .use_pkg_config = linux_use_pkg_config });
                },
                else => {
                    unreachable;
                },
            }

            library.addCSourceFiles(.{ .files = &common_source_files, .flags = library_flags });
            library.addConfigHeader(build_config_step);
            library.linkLibC();
            library.linkLibrary(tracy);
            library.addObject(debug_info_lib);
            library.addObject(stb_sprintf);
            applyUniversalSettings(&build_context, library, concat_cdb);
        }

        var stb_image = b.addObject(.{
            .name = "stb_image",
            .root_module = b.createModule(module_options),
        });
        stb_image.addCSourceFile(.{
            .file = b.path("third_party_libs/stb_image_impls.c"),
            .flags = FlagsBuilder.init(&build_context, target, .{
                .compile_commands = concat_cdb,
            }).flags.items,
        });
        stb_image.addIncludePath(build_context.dep_stb.path(""));
        stb_image.linkLibC();

        var dr_wav = b.addObject(.{
            .name = "dr_wav",
            .root_module = b.createModule(module_options),
        });
        dr_wav.addCSourceFile(
            .{
                .file = b.path("third_party_libs/dr_wav_implementation.c"),
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .compile_commands = concat_cdb,
                }).flags.items,
            },
        );
        dr_wav.addIncludePath(build_context.dep_dr_libs.path(""));
        dr_wav.linkLibC();

        var miniz = b.addStaticLibrary(.{
            .name = "miniz",
            .root_module = b.createModule(module_options),
        });
        {
            miniz.addCSourceFiles(.{
                .root = build_context.dep_miniz.path(""),
                .files = &.{
                    "miniz.c",
                    "miniz_tdef.c",
                    "miniz_tinfl.c",
                    "miniz_zip.c",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .compile_commands = concat_cdb,
                }).flags.items,
            });
            miniz.addIncludePath(build_context.dep_miniz.path(""));
            miniz.linkLibC();
            miniz.addIncludePath(b.path("third_party_libs/miniz"));
        }

        const flac = b.addStaticLibrary(.{
            .name = "flac",
            .root_module = b.createModule(module_options),
        });
        {
            const flac_flags = FlagsBuilder.init(&build_context, target, .{
                .compile_commands = concat_cdb,
            }).flags.items;

            flac.addCSourceFiles(.{
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
                .flags = flac_flags,
            });

            const config_header = b.addConfigHeader(
                .{
                    .style = .{ .cmake = build_context.dep_flac.path("config.cmake.h.in") },
                    .include_path = "config.h",
                },
                .{
                    .CPU_IS_BIG_ENDIAN = target.result.cpu.arch.endian() == .big,
                    .ENABLE_64_BIT_WORDS = target.result.ptrBitWidth() == 64,
                    .FLAC__ALIGN_MALLOC_DATA = target.result.cpu.arch.isX86(),
                    .FLAC__CPU_ARM64 = target.result.cpu.arch.isAARCH64(),
                    .FLAC__SYS_DARWIN = target.result.os.tag == .macos,
                    .FLAC__SYS_LINUX = target.result.os.tag == .linux,
                    .HAVE_BYTESWAP_H = target.result.os.tag == .linux,
                    .HAVE_CPUID_H = target.result.cpu.arch.isX86(),
                    .HAVE_FSEEKO = true,
                    .HAVE_ICONV = target.result.os.tag != .windows,
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

            flac.linkLibC();
            flac.root_module.addCMacro("HAVE_CONFIG_H", "");
            flac.addConfigHeader(config_header);
            flac.addIncludePath(build_context.dep_flac.path("include"));
            flac.addIncludePath(build_context.dep_flac.path("src/libFLAC/include"));
            if (target.result.os.tag == .windows) {
                flac.root_module.addCMacro("FLAC__NO_DLL", "");
                flac.addCSourceFile(.{
                    .file = build_context.dep_flac.path("src/share/win_utf8_io/win_utf8_io.c"),
                    .flags = flac_flags,
                });
            }
        }

        const fft_convolver = b.addStaticLibrary(.{
            .name = "fftconvolver",
            .root_module = b.createModule(module_options),
        });
        {
            var fft_flags: FlagsBuilder = FlagsBuilder.init(&build_context, target, .{
                .compile_commands = concat_cdb,
            });
            if (target.result.os.tag == .macos) {
                fft_convolver.linkFramework("Accelerate");
                fft_flags.addFlag("-DAUDIOFFT_APPLE_ACCELERATE");
                fft_flags.addFlag("-ObjC++");
            } else {
                fft_convolver.addCSourceFile(.{
                    .file = build_context.dep_pffft.path("pffft.c"),
                    .flags = FlagsBuilder.init(&build_context, target, .{
                        .compile_commands = concat_cdb,
                    }).flags.items,
                });
                fft_flags.addFlag("-DAUDIOFFT_PFFFT");
            }

            fft_convolver.addCSourceFiles(.{
                .files = &.{
                    "third_party_libs/FFTConvolver/AudioFFT.cpp",
                    "third_party_libs/FFTConvolver/FFTConvolver.cpp",
                    "third_party_libs/FFTConvolver/TwoStageFFTConvolver.cpp",
                    "third_party_libs/FFTConvolver/Utilities.cpp",
                    "third_party_libs/FFTConvolver/wrapper.cpp",
                },
                .flags = fft_flags.flags.items,
            });
            fft_convolver.linkLibCpp();
            fft_convolver.addIncludePath(build_context.dep_pffft.path(""));
            applyUniversalSettings(&build_context, fft_convolver, concat_cdb);
        }

        const common_infrastructure = b.addStaticLibrary(.{
            .name = "common_infrastructure",
            .root_module = b.createModule(module_options),
        });
        {
            const lua = b.addStaticLibrary(.{
                .name = "lua",
                .target = target,
                .optimize = build_context.optimise,
            });
            {
                const flags = [_][]const u8{
                    switch (target.result.os.tag) {
                        .linux => "-DLUA_USE_LINUX",
                        .macos => "-DLUA_USE_MACOSX",
                        .windows => "-DLUA_USE_WINDOWS",
                        else => "-DLUA_USE_POSIX",
                    },
                    if (build_context.optimise == .Debug) "-DLUA_USE_APICHECK" else "",
                };

                // compile as C++ so it uses exceptions instead of setjmp/longjmp. we use try/catch when handling lua
                lua.addCSourceFile(.{
                    .file = b.path("third_party_libs/lua.cpp"),
                    .flags = &flags,
                });
                lua.addIncludePath(build_context.dep_lua.path(""));
                lua.linkLibC();
            }

            const path = "src/common_infrastructure";
            common_infrastructure.addCSourceFiles(.{
                .files = &.{
                    path ++ "/audio_utils.cpp",
                    path ++ "/autosave.cpp",
                    path ++ "/checksum_crc32_file.cpp",
                    path ++ "/common_errors.cpp",
                    path ++ "/descriptors/param_descriptors.cpp",
                    path ++ "/error_reporting.cpp",
                    path ++ "/folder_node.cpp",
                    path ++ "/global.cpp",
                    path ++ "/package_format.cpp",
                    path ++ "/paths.cpp",
                    path ++ "/persistent_store.cpp",
                    path ++ "/preferences.cpp",
                    path ++ "/preset_bank_info.cpp",
                    path ++ "/sample_library/audio_file.cpp",
                    path ++ "/sample_library/sample_library.cpp",
                    path ++ "/sample_library/sample_library_lua.cpp",
                    path ++ "/sample_library/sample_library_mdata.cpp",
                    path ++ "/sentry/sentry.cpp",
                    path ++ "/state/macros.cpp",
                    path ++ "/state/state_coding.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                    .compile_commands = concat_cdb,
                }).flags.items,
            });

            common_infrastructure.linkLibrary(lua);
            common_infrastructure.addObject(dr_wav);
            common_infrastructure.linkLibrary(flac);
            common_infrastructure.addObject(xxhash);
            common_infrastructure.addConfigHeader(build_config_step);
            common_infrastructure.addIncludePath(b.path(path));
            common_infrastructure.linkLibrary(library);
            common_infrastructure.linkLibrary(miniz);
            applyUniversalSettings(&build_context, common_infrastructure, concat_cdb);
        }

        var embedded_files: ?*std.Build.Step.Compile = null;
        if (!constants.embed_files_workaround) {
            var emdedded_files_module_options = module_options;
            emdedded_files_module_options.root_source_file = b.path("build_resources/embedded_files.zig");
            embedded_files = b.addObject(.{
                .name = "embedded_files",
                .root_module = b.createModule(emdedded_files_module_options),
            });
            {
                var embedded_files_options = b.addOptions();
                if (build_context.dep_floe_logos) |logos| {
                    embedded_files_options.addOptionPath("logo_file", logos.path("rasterized/plugin-gui-logo.png"));
                    embedded_files_options.addOptionPath("icon_file", logos.path("rasterized/icon-background-256px.png"));
                } else {
                    embedded_files_options.addOption(?[]const u8, "logo_file", null);
                    embedded_files_options.addOption(?[]const u8, "icon_file", null);
                }
                embedded_files.?.root_module.addOptions("build_options", embedded_files_options);
            }
            embedded_files.?.linkLibC();
            embedded_files.?.addIncludePath(b.path("build_resources"));
        }

        const plugin = b.addStaticLibrary(.{
            .name = "plugin",
            .root_module = b.createModule(module_options),
        });
        {
            const plugin_path = "src/plugin";

            const plugin_flags = FlagsBuilder.init(&build_context, target, .{
                .full_diagnostics = true,
                .ubsan = true,
                .cpp = true,
                .compile_commands = concat_cdb,
            }).flags.items;

            plugin.addCSourceFiles(.{
                .files = &(.{
                    plugin_path ++ "/engine/check_for_update.cpp",
                    plugin_path ++ "/engine/engine.cpp",
                    plugin_path ++ "/engine/package_installation.cpp",
                    plugin_path ++ "/engine/shared_engine_systems.cpp",
                    plugin_path ++ "/gui/gui.cpp",
                    plugin_path ++ "/gui/gui2_bot_panel.cpp",
                    plugin_path ++ "/gui/gui2_common_browser.cpp",
                    plugin_path ++ "/gui/gui2_inst_browser.cpp",
                    plugin_path ++ "/gui/gui2_ir_browser.cpp",
                    plugin_path ++ "/gui/gui2_library_dev_panel.cpp",
                    plugin_path ++ "/gui/gui2_macros.cpp",
                    plugin_path ++ "/gui/gui2_parameter_component.cpp",
                    plugin_path ++ "/gui/gui2_preset_browser.cpp",
                    plugin_path ++ "/gui/gui2_save_preset_panel.cpp",
                    plugin_path ++ "/gui/gui2_top_panel.cpp",
                    plugin_path ++ "/gui/gui_button_widgets.cpp",
                    plugin_path ++ "/gui/gui_dragger_widgets.cpp",
                    plugin_path ++ "/gui/gui_draw_knob.cpp",
                    plugin_path ++ "/gui/gui_drawing_helpers.cpp",
                    plugin_path ++ "/gui/gui_editor_widgets.cpp",
                    plugin_path ++ "/gui/gui_effects.cpp",
                    plugin_path ++ "/gui/gui_envelope.cpp",
                    plugin_path ++ "/gui/gui_keyboard.cpp",
                    plugin_path ++ "/gui/gui_knob_widgets.cpp",
                    plugin_path ++ "/gui/gui_label_widgets.cpp",
                    plugin_path ++ "/gui/gui_layer.cpp",
                    plugin_path ++ "/gui/gui_library_images.cpp",
                    plugin_path ++ "/gui/gui_waveform_images.cpp",
                    plugin_path ++ "/gui/gui_mid_panel.cpp",
                    plugin_path ++ "/gui/gui_modal_windows.cpp",
                    plugin_path ++ "/gui/gui_peak_meter_widget.cpp",
                    plugin_path ++ "/gui/gui_prefs.cpp",
                    plugin_path ++ "/gui/gui_waveform.cpp",
                    plugin_path ++ "/gui/gui_widget_compounds.cpp",
                    plugin_path ++ "/gui/gui_widget_helpers.cpp",
                    plugin_path ++ "/gui/gui_window.cpp",
                    plugin_path ++ "/gui_framework/draw_list.cpp",
                    plugin_path ++ "/gui_framework/draw_list_opengl.cpp",
                    plugin_path ++ "/gui_framework/gui_box_system.cpp",
                    plugin_path ++ "/gui_framework/gui_imgui.cpp",
                    plugin_path ++ "/gui_framework/gui_platform.cpp",
                    plugin_path ++ "/gui_framework/layout.cpp",
                    plugin_path ++ "/plugin/hosting_tests.cpp",
                    plugin_path ++ "/plugin/plugin.cpp",
                    plugin_path ++ "/preset_server/preset_server.cpp",
                    plugin_path ++ "/processing_utils/midi.cpp",
                    plugin_path ++ "/processing_utils/volume_fade.cpp",
                    plugin_path ++ "/processor/layer_processor.cpp",
                    plugin_path ++ "/processor/processor.cpp",
                    plugin_path ++ "/processor/voices.cpp",
                    plugin_path ++ "/sample_lib_server/sample_library_server.cpp",
                }),
                .flags = plugin_flags,
            });

            switch (target.result.os.tag) {
                .windows => {
                    plugin.addCSourceFiles(.{
                        .files = &.{
                            plugin_path ++ "/gui_framework/gui_platform_windows.cpp",
                        },
                        .flags = plugin_flags,
                    });
                },
                .linux => {
                    plugin.addCSourceFiles(.{
                        .files = &.{
                            plugin_path ++ "/gui_framework/gui_platform_linux.cpp",
                        },
                        .flags = plugin_flags,
                    });
                },
                .macos => {
                    plugin.addCSourceFiles(.{
                        .files = &.{
                            plugin_path ++ "/gui_framework/gui_platform_mac.mm",
                        },
                        .flags = FlagsBuilder.init(&build_context, target, .{
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

            // TODO: license texts should be embedded in a better way, we are currently reading loads of files at
            // the build script generation phase.
            const licences_header = b.addConfigHeader(.{
                .include_path = "licence_texts.h",
                .style = .blank,
            }, .{
                .GPL_3_LICENSE = getLicenceText(b, "GPL-3.0-or-later.txt") catch @panic("missing license text"),
                .APACHE_2_0_LICENSE = getLicenceText(b, "Apache-2.0.txt") catch @panic("missing license text"),
                .FFTPACK_LICENSE = getLicenceText(b, "LicenseRef-FFTPACK.txt") catch @panic("missing license text"),
                .OFL_1_1_LICENSE = getLicenceText(b, "OFL-1.1.txt") catch @panic("missing license text"),
                .BSD_3_CLAUSE_LICENSE = getLicenceText(b, "BSD-3-Clause.txt") catch @panic("missing license text"),
                .BSD_2_CLAUSE_LICENSE = getLicenceText(b, "BSD-2-Clause.txt") catch @panic("missing license text"),
                .ISC_LICENSE = getLicenceText(b, "ISC.txt") catch @panic("missing license text"),
                .MIT_LICENSE = getLicenceText(b, "MIT.txt") catch @panic("missing license text"),
            });
            plugin.addConfigHeader(licences_header);

            plugin.addIncludePath(b.path("src/plugin"));
            plugin.addIncludePath(b.path("src"));
            plugin.addConfigHeader(build_config_step);
            plugin.linkLibrary(library);
            plugin.linkLibrary(common_infrastructure);
            plugin.linkLibrary(fft_convolver);
            if (constants.embed_files_workaround) {
                plugin.addCSourceFile(.{
                    .file = b.dependency("embedded_files_workaround", .{}).path("embedded_files.cpp"),
                });
            } else {
                plugin.addObject(embedded_files.?);
            }
            plugin.linkLibrary(tracy);
            plugin.linkLibrary(pugl);
            plugin.addObject(stb_image);
            plugin.addIncludePath(b.path("src/plugin/gui/live_edit_defs"));
            plugin.linkLibrary(vitfx);
            plugin.linkLibrary(miniz);
            applyUniversalSettings(&build_context, plugin, concat_cdb);
        }

        if (!constants.clap_only and build_context.build_mode != .production) {
            var docs_generator = b.addExecutable(.{
                .name = "docs_generator",
                .root_module = b.createModule(module_options),
            });
            docs_generator.addCSourceFiles(.{
                .files = &.{
                    "src/docs_generator/docs_generator.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                    .compile_commands = concat_cdb,
                }).flags.items,
            });
            docs_generator.root_module.addCMacro("FINAL_BINARY_TYPE", "DocsGenerator");
            docs_generator.linkLibrary(common_infrastructure);
            docs_generator.addIncludePath(b.path("src"));
            docs_generator.addConfigHeader(build_config_step);
            applyUniversalSettings(&build_context, docs_generator, concat_cdb);
            compile_all_step.dependOn(&docs_generator.step);

            // Run the docs generator. It takes no args but outputs JSON to stdout.
            {
                const run = std.Build.Step.Run.create(b, b.fmt("run {s}", .{docs_generator.name}));
                run.addFileArg(configure_binaries.nix_helper.maybePatchElfExecutable(docs_generator));

                const copy = b.addUpdateSourceFiles();
                copy.addCopyFileToSource(run.captureStdOut(), "website/static/generated-data.json");
                website_gen_step.dependOn(&copy.step);
            }

            // Build the site for production
            {
                const npm_install = b.addSystemCommand(&.{ "npm", "install" });
                npm_install.setCwd(b.path("website"));
                npm_install.expectExitCode(0);

                const run = std_extras.createCommandWithStdoutToStderr(b, target, "run docusaurus build");
                run.addArgs(&.{ "npm", "run", "build" });
                run.setCwd(b.path("website"));
                run.step.dependOn(website_gen_step);
                run.step.dependOn(&npm_install.step);
                run.expectExitCode(0);
                website_build_step.dependOn(&run.step);
            }

            // Start the website locally
            {
                const npm_install = b.addSystemCommand(&.{ "npm", "install" });
                npm_install.setCwd(b.path("website"));
                npm_install.expectExitCode(0);

                const run = std_extras.createCommandWithStdoutToStderr(b, target, "run docusaurus start");
                run.addArgs(&.{ "npm", "run", "start" });
                run.setCwd(b.path("website"));
                run.step.dependOn(website_gen_step);
                run.step.dependOn(&npm_install.step);

                website_dev_step.dependOn(&run.step);
            }
        } else {
            const fail_step = &b.addFail("Website scripts not available with this build configuration").step;
            website_gen_step.dependOn(fail_step);
            website_build_step.dependOn(fail_step);
            website_dev_step.dependOn(fail_step);
        }

        if (!constants.clap_only) {
            var packager = b.addExecutable(.{
                .name = "floe-packager",
                .root_module = b.createModule(module_options),
                .version = floe_version,
            });
            packager.addCSourceFiles(.{
                .files = &.{
                    "src/packager_tool/packager.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                    .compile_commands = concat_cdb,
                }).flags.items,
            });
            packager.root_module.addCMacro("FINAL_BINARY_TYPE", "Packager");
            packager.linkLibrary(common_infrastructure);
            packager.addIncludePath(b.path("src"));
            packager.addConfigHeader(build_config_step);
            packager.linkLibrary(miniz);
            if (constants.embed_files_workaround) {
                packager.addCSourceFile(.{
                    .file = b.dependency("embedded_files_workaround", .{}).path("embedded_files.cpp"),
                });
            } else {
                packager.addObject(embedded_files.?);
            }
            applyUniversalSettings(&build_context, packager, concat_cdb);

            const install = b.addInstallBinFile(configure_binaries.maybeAddWindowsCodesign(
                packager,
                .{ .description = "Floe Packager" },
            ), packager.out_filename);

            b.getInstallStep().dependOn(&install.step);
        }

        if (!constants.clap_only) {
            var preset_editor = b.addExecutable(.{
                .name = "preset-editor",
                .root_module = b.createModule(module_options),
                .version = floe_version,
            });
            preset_editor.addCSourceFiles(.{
                .files = &.{
                    "src/preset_editor_tool/preset_editor.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                    .compile_commands = concat_cdb,
                }).flags.items,
            });
            preset_editor.root_module.addCMacro("FINAL_BINARY_TYPE", "PresetEditor");
            preset_editor.linkLibrary(common_infrastructure);
            preset_editor.addIncludePath(b.path("src"));
            preset_editor.addConfigHeader(build_config_step);
            preset_editor.linkLibrary(miniz);
            if (constants.embed_files_workaround) {
                preset_editor.addCSourceFile(.{
                    .file = b.dependency("embedded_files_workaround", .{}).path("embedded_files.cpp"),
                });
            } else {
                preset_editor.addObject(embedded_files.?);
            }
            applyUniversalSettings(&build_context, preset_editor, concat_cdb);

            const install = b.addInstallArtifact(preset_editor, .{});
            b.getInstallStep().dependOn(&install.step);

            // IMPROVE: export preset-editor as a production artifact?
        }

        var clap_plugin_path: ?std.Build.LazyPath = null;
        if (!options.sanitize_thread) {
            const clap = b.addSharedLibrary(.{
                .name = "Floe.clap",
                .root_module = b.createModule(module_options),
                .version = floe_version,
            });
            clap.addCSourceFiles(.{
                .files = &.{
                    "src/plugin/plugin/plugin_entry.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                    .compile_commands = concat_cdb,
                }).flags.items,
            });
            clap.root_module.addCMacro("FINAL_BINARY_TYPE", "Clap");
            clap.addConfigHeader(build_config_step);
            clap.addIncludePath(b.path("src"));
            clap.linkLibrary(plugin);

            applyUniversalSettings(&build_context, clap, concat_cdb);
            addWindowsEmbedInfo(clap, .{
                .name = "Floe CLAP",
                .description = constants.floe_description,
                .icon_path = null,
            }) catch @panic("OOM");

            clap_plugin_path = configure_binaries.addConfiguredPlugin(
                b,
                .clap,
                clap,
                configure_binaries.CodesignInfo{ .description = "Floe CLAP Plugin" },
            ).plugin_path;
        }

        // standalone is for development-only at the moment
        if (!constants.clap_only and build_context.build_mode != .production) {
            const miniaudio = b.addStaticLibrary(.{
                .name = "miniaudio",
                .root_module = b.createModule(module_options),
            });
            {
                miniaudio.addCSourceFile(.{
                    .file = b.path("third_party_libs/miniaudio.c"),
                    .flags = FlagsBuilder.init(&build_context, target, .{
                        .compile_commands = concat_cdb,
                    }).flags.items,
                });
                // Disabling pulse audio because it was causing lots of stutters on my machine.
                miniaudio.root_module.addCMacro("MA_NO_PULSEAUDIO", "1");
                miniaudio.linkLibC();
                miniaudio.addIncludePath(build_context.dep_miniaudio.path(""));
                switch (target.result.os.tag) {
                    .macos => {
                        miniaudio.linkFramework("CoreAudio");
                    },
                    .windows => {
                        miniaudio.linkSystemLibrary("dsound");
                    },
                    .linux => {
                        miniaudio.linkSystemLibrary2("alsa", .{ .use_pkg_config = linux_use_pkg_config });
                    },
                    else => {
                        unreachable;
                    },
                }
                applyUniversalSettings(&build_context, miniaudio, concat_cdb);
            }

            const portmidi = b.addStaticLibrary(.{
                .name = "portmidi",
                .root_module = b.createModule(module_options),
            });
            {
                const pm_root = build_context.dep_portmidi.path("");
                const pm_flags = FlagsBuilder.init(&build_context, target, .{
                    .compile_commands = concat_cdb,
                }).flags.items;
                portmidi.addCSourceFiles(.{
                    .root = pm_root,
                    .files = &.{
                        "pm_common/portmidi.c",
                        "pm_common/pmutil.c",
                        "porttime/porttime.c",
                    },
                    .flags = pm_flags,
                });
                switch (target.result.os.tag) {
                    .macos => {
                        portmidi.addCSourceFiles(.{
                            .root = pm_root,
                            .files = &.{
                                "pm_mac/pmmacosxcm.c",
                                "pm_mac/pmmac.c",
                                "porttime/ptmacosx_cf.c",
                                "porttime/ptmacosx_mach.c",
                            },
                            .flags = pm_flags,
                        });
                        portmidi.linkFramework("CoreAudio");
                        portmidi.linkFramework("CoreMIDI");
                    },
                    .windows => {
                        portmidi.addCSourceFiles(.{
                            .root = pm_root,
                            .files = &.{
                                "pm_win/pmwin.c",
                                "pm_win/pmwinmm.c",
                                "porttime/ptwinmm.c",
                            },
                            .flags = pm_flags,
                        });
                        portmidi.linkSystemLibrary("winmm");
                    },
                    .linux => {
                        portmidi.addCSourceFiles(.{
                            .root = pm_root,
                            .files = &.{
                                "pm_linux/pmlinux.c",
                                "pm_linux/pmlinuxalsa.c",
                                "porttime/ptlinux.c",
                            },
                            .flags = pm_flags,
                        });
                        portmidi.root_module.addCMacro("PMALSA", "1");
                        portmidi.linkSystemLibrary2("alsa", .{ .use_pkg_config = linux_use_pkg_config });
                    },
                    else => {
                        unreachable;
                    },
                }

                portmidi.linkLibC();
                portmidi.addIncludePath(build_context.dep_portmidi.path("porttime"));
                portmidi.addIncludePath(build_context.dep_portmidi.path("pm_common"));
                applyUniversalSettings(&build_context, portmidi, concat_cdb);
            }

            const floe_standalone = b.addExecutable(.{
                .name = "floe_standalone",
                .root_module = b.createModule(module_options),
            });

            floe_standalone.addCSourceFiles(.{
                .files = &.{
                    "src/standalone_wrapper/standalone_wrapper.cpp",
                    "src/plugin/plugin/plugin_entry.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                    .compile_commands = concat_cdb,
                }).flags.items,
            });

            floe_standalone.root_module.addCMacro("FINAL_BINARY_TYPE", "Standalone");
            floe_standalone.addConfigHeader(build_config_step);
            floe_standalone.addIncludePath(b.path("src"));
            floe_standalone.linkLibrary(portmidi);
            floe_standalone.linkLibrary(miniaudio);
            floe_standalone.addIncludePath(build_context.dep_miniaudio.path(""));
            floe_standalone.linkLibrary(plugin);
            applyUniversalSettings(&build_context, floe_standalone, concat_cdb);

            const install = b.addInstallArtifact(floe_standalone, .{});
            b.getInstallStep().dependOn(&install.step);
        }

        const vst3_sdk = b.addStaticLibrary(.{
            .name = "VST3",
            .root_module = b.createModule(module_options),
        });
        const vst3_validator = b.addExecutable(.{
            .name = "VST3-Validator",
            .root_module = b.createModule(module_options),
        });
        if (!constants.clap_only) {
            var flags = FlagsBuilder.init(&build_context, target, .{
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

            {
                vst3_sdk.addCSourceFiles(.{
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
                    .flags = flags.flags.items,
                });

                switch (target.result.os.tag) {
                    .windows => {},
                    .linux => {},
                    .macos => {
                        vst3_sdk.linkFramework("CoreFoundation");
                        vst3_sdk.linkFramework("Foundation");
                    },
                    else => {},
                }

                vst3_sdk.addIncludePath(build_context.dep_vst3_sdk.path(""));
                vst3_sdk.linkLibCpp();
                applyUniversalSettings(&build_context, vst3_sdk, concat_cdb);
            }

            {
                vst3_validator.addCSourceFiles(.{
                    .root = build_context.dep_vst3_sdk.path(""),
                    .files = &.{
                        "public.sdk/source/common/memorystream.cpp",
                        "public.sdk/source/main/moduleinit.cpp",
                        "public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp",
                        "public.sdk/source/vst/hosting/test/connectionproxytest.cpp",
                        "public.sdk/source/vst/hosting/test/eventlisttest.cpp",
                        "public.sdk/source/vst/hosting/test/hostclassestest.cpp",
                        "public.sdk/source/vst/hosting/test/parameterchangestest.cpp",
                        "public.sdk/source/vst/hosting/test/pluginterfacesupporttest.cpp",
                        "public.sdk/source/vst/hosting/test/processdatatest.cpp",
                        "public.sdk/source/vst/hosting/plugprovider.cpp",
                        "public.sdk/source/vst/testsuite/bus/busactivation.cpp",
                        "public.sdk/source/vst/testsuite/bus/busconsistency.cpp",
                        "public.sdk/source/vst/testsuite/bus/businvalidindex.cpp",
                        "public.sdk/source/vst/testsuite/bus/checkaudiobusarrangement.cpp",
                        "public.sdk/source/vst/testsuite/bus/scanbusses.cpp",
                        "public.sdk/source/vst/testsuite/bus/sidechainarrangement.cpp",
                        "public.sdk/source/vst/testsuite/general/editorclasses.cpp",
                        "public.sdk/source/vst/testsuite/general/midilearn.cpp",
                        "public.sdk/source/vst/testsuite/general/midimapping.cpp",
                        "public.sdk/source/vst/testsuite/general/plugcompat.cpp",
                        "public.sdk/source/vst/testsuite/general/scanparameters.cpp",
                        "public.sdk/source/vst/testsuite/general/suspendresume.cpp",
                        "public.sdk/source/vst/testsuite/general/terminit.cpp",
                        "public.sdk/source/vst/testsuite/noteexpression/keyswitch.cpp",
                        "public.sdk/source/vst/testsuite/noteexpression/noteexpression.cpp",
                        "public.sdk/source/vst/testsuite/processing/automation.cpp",
                        "public.sdk/source/vst/testsuite/processing/process.cpp",
                        "public.sdk/source/vst/testsuite/processing/processcontextrequirements.cpp",
                        "public.sdk/source/vst/testsuite/processing/processformat.cpp",
                        "public.sdk/source/vst/testsuite/processing/processinputoverwriting.cpp",
                        "public.sdk/source/vst/testsuite/processing/processtail.cpp",
                        "public.sdk/source/vst/testsuite/processing/processthreaded.cpp",
                        "public.sdk/source/vst/testsuite/processing/silenceflags.cpp",
                        "public.sdk/source/vst/testsuite/processing/silenceprocessing.cpp",
                        "public.sdk/source/vst/testsuite/processing/speakerarrangement.cpp",
                        "public.sdk/source/vst/testsuite/processing/variableblocksize.cpp",
                        "public.sdk/source/vst/testsuite/state/bypasspersistence.cpp",
                        "public.sdk/source/vst/testsuite/state/invalidstatetransition.cpp",
                        "public.sdk/source/vst/testsuite/state/repeatidenticalstatetransition.cpp",
                        "public.sdk/source/vst/testsuite/state/validstatetransition.cpp",
                        "public.sdk/source/vst/testsuite/testbase.cpp",
                        "public.sdk/source/vst/testsuite/unit/checkunitstructure.cpp",
                        "public.sdk/source/vst/testsuite/unit/scanprograms.cpp",
                        "public.sdk/source/vst/testsuite/unit/scanunits.cpp",
                        "public.sdk/source/vst/testsuite/vsttestsuite.cpp",
                        "public.sdk/source/vst/utility/testing.cpp",
                        "public.sdk/samples/vst-hosting/validator/source/main.cpp",
                        "public.sdk/samples/vst-hosting/validator/source/usediids.cpp",
                        "public.sdk/samples/vst-hosting/validator/source/validator.cpp",

                        "public.sdk/source/vst/hosting/connectionproxy.cpp",
                        "public.sdk/source/vst/hosting/eventlist.cpp",
                        "public.sdk/source/vst/hosting/hostclasses.cpp",
                        "public.sdk/source/vst/hosting/module.cpp",
                        "public.sdk/source/vst/hosting/parameterchanges.cpp",
                        "public.sdk/source/vst/hosting/pluginterfacesupport.cpp",
                        "public.sdk/source/vst/hosting/processdata.cpp",
                        "public.sdk/source/vst/vstpresetfile.cpp",
                    },
                    .flags = flags.flags.items,
                });

                switch (target.result.os.tag) {
                    .windows => {
                        vst3_validator.addCSourceFiles(.{
                            .root = build_context.dep_vst3_sdk.path(""),
                            .files = &.{"public.sdk/source/vst/hosting/module_win32.cpp"},
                            .flags = flags.flags.items,
                        });
                        vst3_validator.linkSystemLibrary("ole32");
                    },
                    .linux => {
                        vst3_validator.addCSourceFiles(.{
                            .root = build_context.dep_vst3_sdk.path(""),
                            .files = &.{"public.sdk/source/vst/hosting/module_linux.cpp"},
                            .flags = flags.flags.items,
                        });
                    },
                    .macos => {
                        vst3_validator.addCSourceFiles(.{
                            .root = build_context.dep_vst3_sdk.path(""),
                            .files = &.{"public.sdk/source/vst/hosting/module_mac.mm"},
                            .flags = FlagsBuilder.init(&build_context, target, .{
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
                applyUniversalSettings(&build_context, vst3_validator, concat_cdb);
            }
        }

        var vst3_plugin_path: ?std.Build.LazyPath = null;
        if (!constants.clap_only and !options.sanitize_thread) {
            const vst3 = b.addSharedLibrary(.{
                .name = "Floe.vst3",
                .version = floe_version,
                .root_module = b.createModule(module_options),
            });
            switch (target.result.os.tag) {
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

            var flags = FlagsBuilder.init(&build_context, target, .{
                .ubsan = false,
                .compile_commands = concat_cdb,
            });
            flags.addFlag("-fno-char8_t");

            vst3.addCSourceFiles(.{
                .files = &.{
                    "src/plugin/plugin/plugin_entry.cpp",
                    "src/common_infrastructure/final_binary_type.cpp",
                },
                .flags = FlagsBuilder.init(&build_context, target, .{
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

            switch (target.result.os.tag) {
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

            vst3.addConfigHeader(build_config_step);
            vst3.addIncludePath(b.path("src"));

            applyUniversalSettings(&build_context, vst3, concat_cdb);
            addWindowsEmbedInfo(vst3, .{
                .name = "Floe VST3",
                .description = constants.floe_description,
                .icon_path = null,
            }) catch @panic("OOM");

            vst3_plugin_path = configure_binaries.addConfiguredPlugin(
                b,
                .vst3,
                vst3,
                configure_binaries.CodesignInfo{ .description = "Floe VST3 Plugin" },
            ).plugin_path;

            // Test VST3
            {
                const run_tests = std_extras.createCommandWithStdoutToStderr(b, target, "run VST3-Validator");
                run_tests.addFileArg(configure_binaries.nix_helper.maybePatchElfExecutable(vst3_validator));
                run_tests.addFileArg(vst3_plugin_path.?);
                run_tests.expectExitCode(0);

                test_vst3_validator.dependOn(&run_tests.step);
            }
        } else {
            test_vst3_validator.dependOn(&b.addFail("VST3 tests not allowed with this configuration").step);
        }

        if (!constants.clap_only and target.result.os.tag == .macos and !options.sanitize_thread) {
            const au_sdk = b.addStaticLibrary(.{
                .name = "AU",
                .root_module = b.createModule(module_options),
            });
            {
                au_sdk.addCSourceFiles(.{
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
                    .flags = FlagsBuilder.init(&build_context, target, .{
                        .cpp = true,
                        .compile_commands = concat_cdb,
                    }).flags.items,
                });
                au_sdk.addIncludePath(build_context.dep_au_sdk.path("include"));
                au_sdk.linkLibCpp();
                applyUniversalSettings(&build_context, au_sdk, concat_cdb);
            }

            {
                const wrapper_src_path = build_context.dep_clap_wrapper.path("src");

                var flags = FlagsBuilder.init(&build_context, target, .{
                    .compile_commands = concat_cdb,
                });
                switch (target.result.os.tag) {
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

                const au = b.addSharedLibrary(.{
                    .name = "Floe.component",
                    .root_module = b.createModule(module_options),
                    .version = floe_version,
                });
                au.addCSourceFiles(.{
                    .files = &.{
                        "src/plugin/plugin/plugin_entry.cpp",
                        "src/common_infrastructure/final_binary_type.cpp",
                    },
                    .flags = FlagsBuilder.init(&build_context, target, .{
                        .full_diagnostics = true,
                        .ubsan = true,
                        .objcpp = true,
                        .compile_commands = concat_cdb,
                    }).flags.items,
                });
                au.root_module.addCMacro("FINAL_BINARY_TYPE", "AuV2");

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
                    .flags = flags.flags.items,
                });

                // TODO: we should be using addWriteFiles step here. addIncludePath supports LazyPath.
                {
                    const file = b.build_root.handle.createFile(
                        b.pathJoin(&.{ constants.floe_cache_relative, "generated_entrypoints.hxx" }),
                        .{ .truncate = true },
                    ) catch @panic("could not create file");
                    defer file.close();
                    file.writeAll(b.fmt(
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
                    })) catch @panic("could not write to file");
                }

                // TODO: we should be using addWriteFiles step here. addIncludePath supports LazyPath.
                {
                    const file = b.build_root.handle.createFile(
                        b.pathJoin(&.{ constants.floe_cache_relative, "generated_cocoaclasses.hxx" }),
                        .{ .truncate = true },
                    ) catch @panic("could not create file");
                    defer file.close();
                    file.writeAll(b.fmt(
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
                        \\ bool fillAudioUnitCocoaView(AudioUnitCocoaViewInfo* viewInfo, std::shared_ptr<Clap::Plugin> _plugin) {{
                        \\     if (strcmp(_plugin->_plugin->desc->id, "{[clap_id]s}") == 0) {{
                        \\         if (!_plugin->_ext._gui) return false;
                        \\         return fillAUCV_{[name]s}(viewInfo);
                        \\     }}
                        \\ }}
                    , .{
                        .name = b.fmt("Floe{d}", .{floe_version_hash}),
                        .clap_id = constants.floe_clap_id,
                    })) catch @panic("could not write to file");
                }

                au.addIncludePath(b.path("third_party_libs/clap/include"));
                au.addIncludePath(build_context.dep_au_sdk.path("include"));
                au.addIncludePath(build_context.dep_clap_wrapper.path("include"));
                au.addIncludePath(build_context.dep_clap_wrapper.path("libs/fmt"));
                au.addIncludePath(build_context.dep_clap_wrapper.path("src"));
                au.addIncludePath(b.path(constants.floe_cache_relative));
                au.linkLibCpp();

                au.linkLibrary(plugin);
                au.linkLibrary(au_sdk);
                au.linkFramework("AudioToolbox");
                au.linkFramework("CoreMIDI");

                au.addConfigHeader(build_config_step);
                au.addIncludePath(b.path("src"));

                applyUniversalSettings(&build_context, au, concat_cdb);

                const au_install = configure_binaries.addConfiguredPlugin(b, .au, au, null).install_step;

                if (builtin.os.tag == .macos) {
                    if (std.mem.endsWith(u8, std.mem.trimRight(u8, b.install_path, "/"), "Library/Audio/Plug-Ins")) {
                        const installed_au_path = b.pathJoin(&.{ b.install_path, "Components/Floe.component" });

                        // Pluginval AU
                        {
                            // Pluginval puts all of it's output in stdout, not stderr.
                            const run = std_extras.createCommandWithStdoutToStderr(b, target, "run pluginval AU");

                            addPluginvalCommand(run, target.result);

                            run.addArgs(&.{ "--validate", installed_au_path });

                            run.step.dependOn(au_install);
                            run.expectExitCode(0);

                            pluginval_au.dependOn(&run.step);
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
                            run_auval.step.dependOn(au_install);
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

                                auval.dependOn(&cmd.step);
                            }

                            auval.dependOn(&run_auval.step);
                        }
                    } else {
                        const fail = b.addFail("You must specify a global/user Library/Audio/Plug-Ins " ++
                            "--prefix to zig build in order to run AU tests");
                        pluginval_au.dependOn(&fail.step);
                        auval.dependOn(&fail.step);
                    }
                } else {
                    const fail = b.addFail("AU tests can only be run on macOS hosts");
                    pluginval_au.dependOn(&fail.step);
                    auval.dependOn(&fail.step);
                }
            }
        } else {
            const fail = b.addFail("AU tests not allowed with this configuration");
            pluginval_au.dependOn(&fail.step);
            auval.dependOn(&fail.step);
        }

        if (!constants.clap_only and target.result.os.tag == .windows) {
            const installer_path = "src/windows_installer";

            {
                // TODO: we should be using addWriteFiles step here.
                const writeManifest = (struct {
                    fn writeManifest(
                        builder: *std.Build,
                        name: []const u8,
                        require_admin: bool,
                        description: []const u8,
                    ) []const u8 {
                        const manifest_path = builder.pathJoin(&.{
                            constants.floe_cache_relative,
                            builder.fmt(
                                "{s}.manifest",
                                .{name},
                            ),
                        });
                        const file = builder.build_root.handle.createFile(
                            manifest_path,
                            .{ .truncate = true },
                        ) catch @panic("could not create file");
                        defer file.close();
                        file.writeAll(builder.fmt(
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
                            .name = name,
                            .description = description,
                            .execution_level = if (require_admin) "requireAdministrator" else "asInvoker",
                        })) catch @panic("could not write to file");
                        return manifest_path;
                    }
                }).writeManifest;

                const flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                    .compile_commands = concat_cdb,
                });

                const uninstaller_name = "Floe-Uninstaller";
                const win_uninstaller = b.addExecutable(.{
                    .name = uninstaller_name,
                    .root_module = b.createModule(module_options),
                    .version = floe_version,
                    .win32_manifest = b.path(writeManifest(
                        b,
                        "Uninstaller",
                        if (options.build_mode == .production) true else options.windows_installer_require_admin,
                        "Uninstaller for Floe plugins",
                    )),
                });
                win_uninstaller.subsystem = .Windows;

                win_uninstaller.root_module.addCMacro(
                    "UNINSTALLER_BINARY_NAME",
                    b.fmt("\"zig-out/x86_64-windows/{s}\"", .{win_uninstaller.out_filename}),
                );

                win_uninstaller.addCSourceFiles(.{
                    .files = &.{
                        installer_path ++ "/uninstaller.cpp",
                        installer_path ++ "/gui.cpp",
                        "src/common_infrastructure/final_binary_type.cpp",
                    },
                    .flags = flags.flags.items,
                });
                win_uninstaller.root_module.addCMacro("FINAL_BINARY_TYPE", "WindowsUninstaller");
                win_uninstaller.linkSystemLibrary("gdi32");
                win_uninstaller.linkSystemLibrary("version");
                win_uninstaller.linkSystemLibrary("comctl32");
                win_uninstaller.addConfigHeader(build_config_step);
                win_uninstaller.addIncludePath(b.path("src"));
                win_uninstaller.addObject(stb_image);
                win_uninstaller.linkLibrary(library);
                win_uninstaller.linkLibrary(miniz);
                win_uninstaller.linkLibrary(common_infrastructure);
                applyUniversalSettings(&build_context, win_uninstaller, concat_cdb);

                compile_all_step.dependOn(&win_uninstaller.step);

                const uninstaller_bin_path = configure_binaries.maybeAddWindowsCodesign(
                    win_uninstaller,
                    .{ .description = "Floe Uninstaller" },
                );

                // Install
                {
                    const install = b.addInstallBinFile(uninstaller_bin_path, win_uninstaller.out_filename);
                    b.getInstallStep().dependOn(&install.step);
                }

                const win_installer_description = "Installer for Floe plugins";
                const win_installer = b.addExecutable(.{
                    .name = b.fmt("Floe-Installer-v{s}", .{ .version = floe_version_string }),
                    .root_module = b.createModule(module_options),
                    .version = floe_version,
                    .win32_manifest = b.path(writeManifest(
                        b,
                        "Installer",
                        if (options.build_mode == .production) true else options.windows_installer_require_admin,
                        win_installer_description,
                    )),
                });
                win_installer.subsystem = .Windows;

                var rc_include_path: std.BoundedArray(std.Build.LazyPath, 5) = .{};

                if (build_context.dep_floe_logos) |logos| {
                    const sidebar_img = "rasterized/win-installer-sidebar.png";
                    const sidebar_img_lazy_path = logos.path(sidebar_img);
                    rc_include_path.append(sidebar_img_lazy_path.dirname()) catch @panic("OOM");
                    win_installer.root_module.addCMacro(
                        "SIDEBAR_IMAGE_PATH",
                        b.fmt("\"{s}\"", .{std.fs.path.basename(sidebar_img)}),
                    );
                }
                if (vst3_plugin_path) |vst3_plugin| {
                    win_installer.root_module.addCMacro("VST3_PLUGIN_BINARY_NAME", "\"Floe.vst3\"");
                    rc_include_path.append(vst3_plugin.dirname()) catch @panic("OOM");
                }
                if (clap_plugin_path) |clap_plugin| {
                    win_installer.root_module.addCMacro("CLAP_PLUGIN_BINARY_NAME", "\"Floe.clap\"");
                    rc_include_path.append(clap_plugin.dirname()) catch @panic("OOM");
                }
                {
                    win_installer.root_module.addCMacro(
                        "UNINSTALLER_BINARY_NAME",
                        b.fmt("\"{s}\"", .{win_uninstaller.out_filename}),
                    );
                    rc_include_path.append(uninstaller_bin_path.dirname()) catch @panic("OOM");
                }

                win_installer.addWin32ResourceFile(.{
                    .file = b.path(installer_path ++ "/resources.rc"),
                    .include_paths = rc_include_path.slice(),
                    .flags = win_installer.root_module.c_macros.items,
                });

                win_installer.addCSourceFiles(.{
                    .files = &.{
                        installer_path ++ "/installer.cpp",
                        installer_path ++ "/gui.cpp",
                        "src/common_infrastructure/final_binary_type.cpp",
                    },
                    .flags = flags.flags.items,
                });

                win_installer.root_module.addCMacro("FINAL_BINARY_TYPE", "WindowsInstaller");
                win_installer.linkSystemLibrary("gdi32");
                win_installer.linkSystemLibrary("version");
                win_installer.linkSystemLibrary("comctl32");

                addWindowsEmbedInfo(win_installer, .{
                    .name = "Floe Installer",
                    .description = win_installer_description,
                    .icon_path = if (build_context.dep_floe_logos) |logos| logos.path("rasterized/icon.ico") else null,
                }) catch @panic("OOM");
                win_installer.addConfigHeader(build_config_step);
                win_installer.addIncludePath(b.path("src"));
                win_installer.addObject(stb_image);
                win_installer.linkLibrary(library);
                win_installer.linkLibrary(miniz);
                win_installer.linkLibrary(common_infrastructure);
                applyUniversalSettings(&build_context, win_installer, concat_cdb);

                const installer_bin_path = configure_binaries.maybeAddWindowsCodesign(
                    win_installer,
                    .{ .description = "Floe Installer" },
                );

                // Installer tests
                {
                    const run_installer = std.Build.Step.Run.create(b, b.fmt("run {s}", .{win_installer.name}));
                    run_installer.addFileArg(installer_bin_path);
                    run_installer.addArg("--autorun");
                    run_installer.expectExitCode(0);

                    // IMPROVE actually test for installation

                    const run_uninstaller = std.Build.Step.Run.create(b, b.fmt("run {s}", .{uninstaller_name}));
                    run_uninstaller.addFileArg(uninstaller_bin_path);
                    run_uninstaller.addArg("--autorun");
                    run_uninstaller.expectExitCode(0);
                    run_uninstaller.step.dependOn(&run_installer.step);

                    test_windows_install.dependOn(&run_uninstaller.step);
                }

                // Install
                {
                    const install = b.addInstallBinFile(installer_bin_path, win_installer.out_filename);
                    b.getInstallStep().dependOn(&install.step);
                }
            }
        } else {
            test_windows_install.dependOn(&b.addFail("Windows installer tests not allowed with this configuration").step);
        }

        var tests_compile_step: ?*std.Build.Step.Compile = null;
        if (!constants.clap_only and build_context.build_mode != .production) {
            const tests = b.addExecutable(.{
                .name = "tests",
                .root_module = b.createModule(module_options),
            });
            tests_compile_step = tests;
            tests.addCSourceFiles(.{
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
                .flags = FlagsBuilder.init(&build_context, target, .{
                    .full_diagnostics = true,
                    .ubsan = true,
                    .cpp = true,
                    .compile_commands = concat_cdb,
                }).flags.items,
            });
            tests.root_module.addCMacro("FINAL_BINARY_TYPE", "Tests");
            tests.addConfigHeader(build_config_step);
            tests.linkLibrary(plugin);
            applyUniversalSettings(&build_context, tests, concat_cdb);

            const test_binary = configure_binaries.nix_helper.maybePatchElfExecutable(tests);

            const add_tests_args = struct {
                pub fn do(run: *std.Build.Step.Run, clap_plugin: ?std.Build.LazyPath) void {
                    const b2 = run.step.owner;
                    run.addArg("--log-level=debug");

                    // We output JUnit XML in a unique location so test runs don't clobber each other. These files
                    // can be collected by searching the .zig-cache directory for .junit.xml files.
                    _ = run.addPrefixedOutputFileArg("--junit-xml-output-path=", "unit-tests.junit.xml");

                    if (clap_plugin) |path|
                        run.addPrefixedFileArg("--clap-plugin-path=", path);

                    run.addArg(b2.fmt("--test-files-folder-path={s}", .{b2.pathFromRoot("test_files")}));
                }
            }.do;

            // Run unit tests
            {
                const run_tests = std.Build.Step.Run.create(b, "run unit tests");
                run_tests.addFileArg(test_binary);
                add_tests_args(run_tests, clap_plugin_path);

                run_tests.expectExitCode(0);

                test_step.dependOn(&run_tests.step);
            }

            // Coverage tests
            if (builtin.os.tag == .linux) {
                const run_coverage = b.addSystemCommand(&.{
                    "kcov",
                    b.fmt("--include-pattern={s}", .{b.pathFromRoot("src")}),
                    b.fmt("{s}/coverage-out", .{constants.floe_cache_relative}),
                });
                run_coverage.addFileArg(test_binary);
                add_tests_args(run_coverage, clap_plugin_path);
                run_coverage.expectExitCode(0);
                coverage.dependOn(&run_coverage.step);
            } else {
                coverage.dependOn(&b.addFail("coverage not supported on this OS").step);
            }

            // Valgrind test
            if (tests_compile_step != null and !options.sanitize_thread) {
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
                add_tests_args(run, clap_plugin_path);
                run.expectExitCode(0);

                valgrind.dependOn(&run.step);
            } else {
                valgrind.dependOn(&b.addFail("valgrind not allowed for this build configuration").step);
            }
        }

        // Clap Validator test
        if (clap_plugin_path) |clap_path| {
            const run = std_extras.createCommandWithStdoutToStderr(b, target, "run clap-validator");
            if (b.findProgram(
                &.{if (target.result.os.tag != .windows) "clap-validator" else "clap-validator.exe"},
                &[0][]const u8{},
            ) catch null) |program| {
                run.addArg(program);
            } else if (target.result.os.tag == .windows) {
                run.addFileArg(std_extras.fetch(b, .{
                    .url = "https://github.com/free-audio/clap-validator/releases/download/0.3.2/clap-validator-0.3.2-windows.zip",
                    .file_name = "clap-validator.exe",
                    .hash = "N-V-__8AAACYMwAKpkDTKEWrhJhUyBs1LxycLWN8iFpe5p6r",
                }));
            } else if (target.result.os.tag == .macos) {
                // Use downloaded binary for macOS
                const clap_validator_fetch = std_extras.fetch(b, .{
                    .url = "https://github.com/free-audio/clap-validator/releases/download/0.3.2/clap-validator-0.3.2-macos-universal.tar.gz",
                    .file_name = "clap-validator",
                    .hash = "N-V-__8AALwZfgBlaKnVwge3d221LJA9s_vQixy9c6OBvGhQ",
                    .executable = true,
                });
                run.addFileArg(clap_validator_fetch);
            } else if (target.result.os.tag == .linux) {
                // Linux - use downloaded binary.
                // NOTE: we're using floe-audio repo with a re-uploaded ZIP because we needed to workaround a
                // zig fetch bug with tar.gz files.
                const clap_validator_fetch = std_extras.fetch(b, .{
                    .url = "https://github.com/floe-audio/clap-validator/releases/download/v0.3.2/clap-validator-0.3.2-ubuntu-18.04.zip",
                    .file_name = "clap-validator",
                    .hash = "N-V-__8AAFDvhAD7wsMQHzT9s_hiRLUTXJp4mBwyx_O7gZxZ",
                    .executable = true,
                });
                run.addFileArg(clap_validator_fetch);
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
            run.addFileArg(clap_path);
            run.expectExitCode(0);

            clap_val.dependOn(&run.step);
        } else {
            clap_val.dependOn(&b.addFail("clap-validator not allowed for this build configuration").step);
        }

        // Pluginval test
        if (vst3_plugin_path) |vst3_path| {
            // Pluginval puts all of it's output in stdout, not stderr.
            const run = std_extras.createCommandWithStdoutToStderr(b, target, "run pluginval");

            addPluginvalCommand(run, target.result);

            // In headless environments such as CI, GUI tests always fail on Linux so we skip them.
            if (builtin.os.tag == .linux and b.graph.env_map.get("DISPLAY") == null) {
                run.addArg("--skip-gui-tests");
            }

            run.addArg("--validate");
            run.addFileArg(vst3_path);
            run.expectExitCode(0);

            pluginval.dependOn(&run.step);
        } else {
            pluginval.dependOn(&b.addFail("pluginval not allowed for this build configuration").step);
        }

        // clang-tidy
        {
            const clang_tidy_step = check_steps.ClangTidyStep.create(b, target);
            clang_tidy_step.step.dependOn(&concat_cdb.step);
            clang_tidy.dependOn(&clang_tidy_step.step);
        }

        // Build scripts CLI program and add script steps
        if (target.result.os.tag == builtin.os.tag and target.result.cpu.arch == builtin.cpu.arch) {
            var scripts_module = module_options;
            scripts_module.root_source_file = b.path("src/build/scripts.zig");
            scripts_module.optimize = .ReleaseSafe;
            const scripts_exe = b.addExecutable(.{
                .name = "scripts",
                .root_module = b.createModule(scripts_module),
            });

            // Format script
            {
                const run_format = b.addRunArtifact(scripts_exe);
                run_format.addArg("format");
                applyScriptsConfig(b, run_format);
                format_step.dependOn(&run_format.step);
            }

            // Echo latest changes script
            {
                const run_echo = b.addRunArtifact(scripts_exe);
                run_echo.addArg("echo-latest-changes");
                applyScriptsConfig(b, run_echo);
                echo_step.dependOn(&run_echo.step);
            }

            // Upload errors
            {
                const run_upload_errors = b.addRunArtifact(scripts_exe);
                run_upload_errors.addArg("upload-errors");
                applyScriptsConfig(b, run_upload_errors);
                upload_errors_step.dependOn(&run_upload_errors.step);
            }

            // CI
            {
                const run_ci = b.addRunArtifact(scripts_exe);
                run_ci.addArg("ci");
                applyScriptsConfig(b, run_ci);
                ci_step.dependOn(&run_ci.step);
            }

            // Website promote
            {
                const run_promote = b.addRunArtifact(scripts_exe);
                run_promote.addArg("website-promote-beta-to-stable");
                applyScriptsConfig(b, run_promote);
                website_promote_step.dependOn(&run_promote.step);
            }
        }
    }

    check_steps.addGlobalCheckSteps(b);
}

fn applyScriptsConfig(b: *std.Build, run_step: *std.Build.Step.Run) void {
    // Our scripts assume they are run from the repository root.
    run_step.setCwd(b.path("."));

    // Provide the path to the Zig executable for any scripts that may need to invoke Zig.
    run_step.setEnvironmentVariable("ZIG_EXE", b.graph.zig_exe);
}

fn installPath(install_step: *std.Build.Step.InstallArtifact, absolute: bool) []const u8 {
    var b = install_step.step.owner;
    var result = b.getInstallPath(install_step.dest_dir.?, install_step.dest_sub_path);
    if (absolute) {
        if (std.fs.path.isAbsolute(result)) return result;
        // The install path maybe relative if, for example, the --prefix was specified as a relative folder.
        const root = b.build_root.handle.realpathAlloc(b.allocator, ".") catch "";
        return b.pathJoin(&.{ root, result });
    }
    result = std.fs.path.relative(b.allocator, b.install_prefix, result) catch @panic("failed to get relative path");
    return result;
}

fn addPluginvalCommand(run: *std.Build.Step.Run, target: std.Target) void {
    const b = run.step.owner;

    if (b.findProgram(
        &.{if (target.os.tag != .windows) "pluginval" else "pluginval.exe"},
        &[0][]const u8{},
    ) catch null) |program| {
        // Use system-installed pluginval when explicitly requested
        run.addArg(program);
    } else if (target.os.tag == .windows) {
        // On Windows, we use a downloaded binary.
        run.addFileArg(std_extras.fetch(b, .{
            .url = "https://github.com/Tracktion/pluginval/releases/download/v1.0.3/pluginval_Windows.zip",
            .file_name = "pluginval.exe",
            .hash = "N-V-__8AAABcNACEKUY1SsEfHGFybDSKUo4JGhYN5bgZ146c",
        }));
    } else if (target.os.tag == .macos) {
        // Use downloaded binary for macOS
        const pluginval_fetch = std_extras.fetch(b, .{
            .url = "https://github.com/Tracktion/pluginval/releases/download/v1.0.3/pluginval_macOS.zip",
            .file_name = "Contents/MacOS/pluginval",
            .hash = "N-V-__8AAF8tGQHuEhO2q5y6oj6foKiCHCXCQWbfpY6ehS5e",
            .executable = true,
        });
        run.addFileArg(pluginval_fetch);
    } else if (target.os.tag == .linux) {
        // Linux - use downloaded binary
        const pluginval_fetch = std_extras.fetch(b, .{
            .url = "https://github.com/Tracktion/pluginval/releases/download/v1.0.3/pluginval_Linux.zip",
            .file_name = "pluginval",
            .hash = "N-V-__8AAHiZqACvZuwhiWbvPBeJQd-K_5xpafp_Pi_6228J",
            .executable = true,
        });
        run.addFileArg(pluginval_fetch);
    } else {
        @panic("Unsupported OS for pluginval");
    }
}
