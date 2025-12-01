// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "foundation/foundation.hpp"
#include "os/threading.hpp"
#include "tests/framework.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/debug/tracy_wrapped.hpp"

#include "common_infrastructure/global.hpp"

#define TEST_REGISTER_FUNCTIONS                                                                              \
    X(RegisterAlgorithmTests)                                                                                \
    X(RegisterAllocatorTests)                                                                                \
    X(RegisterAssertFTests)                                                                                  \
    X(RegisterAtomicQueueTests)                                                                              \
    X(RegisterAtomicRefListTests)                                                                            \
    X(RegisterAtomicSwapBufferTests)                                                                         \
    X(RegisterAudioFileTests)                                                                                \
    X(RegisterAudioUtilsTests)                                                                               \
    X(RegisterAutosaveTests)                                                                                 \
    X(RegisterBitsetTests)                                                                                   \
    X(RegisterBoundedListTests)                                                                              \
    X(RegisterChecksumFileTests)                                                                             \
    X(RegisterCircularBufferTests)                                                                           \
    X(RegisterCliArgParseTests)                                                                              \
    X(RegisterDebugTests)                                                                                    \
    X(RegisterDynamicArrayTests)                                                                             \
    X(RegisterErrorCodeTests)                                                                                \
    X(RegisterErrorNotificationsTests)                                                                       \
    X(RegisterFilesystemTests)                                                                               \
    X(RegisterFolderNodeTests)                                                                               \
    X(RegisterFormatTests)                                                                                   \
    X(RegisterFunctionQueueTests)                                                                            \
    X(RegisterFunctionTests)                                                                                 \
    X(RegisterGeometryTests)                                                                                 \
    X(RegisterHashTableTests)                                                                                \
    X(RegisterHostingTests)                                                                                  \
    X(RegisterJsonReaderTests)                                                                               \
    X(RegisterJsonWriterTests)                                                                               \
    X(RegisterLayerProcessorTests)                                                                           \
    X(RegisterLayoutTests)                                                                                   \
    X(RegisterLibraryLuaTests)                                                                               \
    X(RegisterLibraryMdataTests)                                                                             \
    X(RegisterLinkedListTests)                                                                               \
    X(RegisterLogRingBufferTests)                                                                            \
    X(RegisterMathsTests)                                                                                    \
    X(RegisterMemoryTests)                                                                                   \
    X(RegisterMiscTests)                                                                                     \
    X(RegisterOptionalTests)                                                                                 \
    X(RegisterPackageFormatTests)                                                                            \
    X(RegisterPackageInstallationTests)                                                                      \
    X(RegisterParamDescriptorTests)                                                                          \
    X(RegisterPathPoolTests)                                                                                 \
    X(RegisterPathTests)                                                                                     \
    X(RegisterPersistentStoreTests)                                                                          \
    X(RegisterPreferencesTests)                                                                              \
    X(RegisterRandomTests)                                                                                   \
    X(RegisterSampleLibraryServerTests)                                                                      \
    X(RegisterSentryTests)                                                                                   \
    X(RegisterStateCodingTests)                                                                              \
    X(RegisterStringTests)                                                                                   \
    X(RegisterTaggedUnionTests)                                                                              \
    X(RegisterThreadPoolTests)                                                                               \
    X(RegisterThreadingTests)                                                                                \
    X(RegisterVersionTests)                                                                                  \
    X(RegisterVolumeFadeTests)                                                                               \
    X(RegisterWebTests)                                                                                      \
    X(RegisterWriterTests)

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

    StdPrintF(StdStream::Err, "Unknown log level: {}", *log_level);
    return ErrorCode {CliError::InvalidArguments};
}

ErrorCodeOr<int> Main(ArgsCstr args) {
    GlobalInit({.init_error_reporting = false, .set_main_thread = true});
    DEFER { GlobalDeinit({.shutdown_error_reporting = false}); };

    ZoneScoped;

    tests::Tester tester;

    enum class CommandLineArgId : u32 {
        Filter,
        LogLevel,
        Repeats,
        JUnitXmlOutputPath,
        GithubActionsAnnotationsOutputPath,
        TestFilesFolderPath,
        ClapPluginPath,
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
        {
            .id = (u32)CommandLineArgId::Repeats,
            .key = "repeats",
            .description = "Number of times to repeat the tests",
            .value_type = "count",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::JUnitXmlOutputPath,
            .key = "junit-xml-output-path",
            .description = "Path to write JUnit XML test results to",
            .value_type = "path",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::GithubActionsAnnotationsOutputPath,
            .key = "gha-annotations-output-path",
            .description = "Path to write GitHub Actions annotations to",
            .value_type = "path",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::TestFilesFolderPath,
            .key = "test-files-folder-path",
            .description =
                "Path to the test_files folder. Alternatively set FLOE_TEST_FILES_FOLDER_PATH env var or let it be auto-detected upwards from the exe.",
            .value_type = "path",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::ClapPluginPath,
            .key = "clap-plugin-path",
            .description = "Path to the Floe CLAP plugin. Alternatively set FLOE_CLAP_PLUGIN_PATH env var.",
            .value_type = "path",
            .required = false,
            .num_values = 1,
        },
    });

    ArenaAllocatorWithInlineStorage<1000> arena {PageAllocator::Instance()};
    auto const cli_args = TRY(ParseCommandLineArgsStandard(arena,
                                                           args,
                                                           k_cli_arg_defs,
                                                           {
                                                               .handle_help_option = true,
                                                               .print_usage_on_error = true,
                                                           }));

    TRY(SetLogLevel(tester, cli_args[ToInt(CommandLineArgId::LogLevel)].Value()));

    if (auto const repeats_str = cli_args[ToInt(CommandLineArgId::Repeats)].Value()) {
        auto const parsed_int = ParseInt(*repeats_str, ParseIntBase::Decimal);
        if (!parsed_int || (*parsed_int < 1 || *parsed_int > LargestRepresentableValue<u16>())) {
            StdPrintF(StdStream::Err, "Invalid number of repeats: {}\n", *repeats_str);
            return ErrorCode {CliError::InvalidArguments};
        }
        tester.repeat_tests = (u16)*parsed_int;
    }

    // Register the test functions
#define X(fn) fn(tester);
    TEST_REGISTER_FUNCTIONS
#if _WIN32
    WINDOWS_FP_TEST_REGISTER_FUNCTIONS
#endif
#undef X

    return RunAllTests(
        tester,
        {
            .filter_patterns = cli_args[ToInt(CommandLineArgId::Filter)].values,
            .junit_xml_output_path = cli_args[ToInt(CommandLineArgId::JUnitXmlOutputPath)].Value(),
            .gha_annotations_output_path =
                cli_args[ToInt(CommandLineArgId::GithubActionsAnnotationsOutputPath)].Value(),
            .test_files_folder = cli_args[ToInt(CommandLineArgId::TestFilesFolderPath)].Value(),
            .clap_plugin_path = cli_args[ToInt(CommandLineArgId::ClapPluginPath)].Value(),
        });
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    auto const result = Main({argc, argv});
    if (result.HasError()) {
        StdPrintF(StdStream::Err, "Error: {}", result.Error());
        return 1;
    }
    return result.Value();
}
