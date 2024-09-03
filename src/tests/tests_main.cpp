// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "foundation/foundation.hpp"
#include "os/threading.hpp"
#include "tests/framework.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/debug/tracy_wrapped.hpp"

#include "foundation_tests.hpp"
#include "hosting_tests.hpp"
#include "os_tests.hpp"
#include "utils_tests.hpp"

#define TEST_REGISTER_FUNCTIONS                                                                              \
    X(RegisterFoundationTests)                                                                               \
    X(RegisterOsTests)                                                                                       \
    X(RegisterUtilsTests)                                                                                    \
    X(RegisterPackageFormatTests)                                                                            \
    X(RegisterHostingTests)                                                                                  \
    X(RegisterAudioUtilsTests)                                                                               \
    X(RegisterVolumeFadeTests)                                                                               \
    X(RegisterStateCodingTests)                                                                              \
    X(RegisterAudioFileTests)                                                                                \
    X(RegisterChecksumFileTests)                                                                             \
    X(RegisterPresetTests)                                                                                   \
    X(RegisterLibraryLuaTests)                                                                               \
    X(RegisterLibraryMdataTests)                                                                             \
    X(RegisterSampleLibraryLoaderTests)                                                                      \
    X(RegisterParamInfoTests)                                                                                \
    X(RegisterSettingsFileTests)

#define WINDOWS_FP_TEST_REGISTER_FUNCTIONS X(RegisterWindowsSpecificTests)

// Declare the test functions
#define X(fn) void fn(tests::Tester&);
TEST_REGISTER_FUNCTIONS
#if _WIN32
WINDOWS_FP_TEST_REGISTER_FUNCTIONS
#endif
#undef X

static ErrorCodeOr<void> SetLogLevel(tests::Tester& tester, Optional<String> log_level) {
    if (!log_level) return k_success; // use default

    for (auto const& [level, name] : Array {
             Pair {LogLevel::Debug, "debug"_s},
             Pair {LogLevel::Info, "info"_s},
             Pair {LogLevel::Warning, "warning"_s},
             Pair {LogLevel::Error, "error"_s},
         }) {
        if (IsEqualToCaseInsensitiveAscii(*log_level, name)) {
            tester.log.max_level_allowed = level;
            return k_success;
        }
    }

    g_cli_out.Error({}, "Unknown log level: {}", *log_level);
    return ErrorCode {CliError::InvalidArguments};
}

ErrorCodeOr<int> Main(ArgsCstr args) {
    SetThreadName("main");
    DebugSetThreadAsMainThread();
#ifdef TRACY_ENABLE
    ___tracy_startup_profiler();
    DEFER { ___tracy_shutdown_profiler(); };
#endif

    StartupCrashHandler();
    DEFER { ShutdownCrashHandler(); };

    ZoneScoped;

    tests::Tester tester;

    enum class CommandLineArgId : u32 {
        Filter,
        LogLevel,
        Count,
    };

    auto constexpr k_cli_arg_defs = MakeCommandLineArgDefs<CommandLineArgId>({
        {
            .id = (u32)CommandLineArgId::Filter,
            .key = "filter",
            .description = "Wildcard pattern to filter tests by name",
            .value_type = "pattern",
            .required = false,
            .num_values = -1,
        },
        {
            .id = (u32)CommandLineArgId::LogLevel,
            .key = "log-level",
            .description = "Log level: debug, info, warning, error",
            .value_type = "level",
            .required = false,
            .num_values = 1,
        },
    });

    ArenaAllocatorWithInlineStorage<1000> arena;
    auto const cli_args = TRY(ParseCommandLineArgs(StdWriter(g_cli_out.stream),
                                                   arena,
                                                   args,
                                                   k_cli_arg_defs,
                                                   {
                                                       .handle_help_option = true,
                                                       .print_usage_on_error = true,
                                                   }));

    TRY(SetLogLevel(tester, cli_args[ToInt(CommandLineArgId::LogLevel)].Value()));

    // Register the test functions
#define X(fn) fn(tester);
    TEST_REGISTER_FUNCTIONS
#if _WIN32
    WINDOWS_FP_TEST_REGISTER_FUNCTIONS
#endif
#undef X

    return RunAllTests(tester, cli_args[ToInt(CommandLineArgId::Filter)].values);
}

int main(int argc, char** argv) {
    auto result = Main({argc, argv});
    if (result.HasError()) return 1;
    return result.Value();
}
