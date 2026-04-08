// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "foundation/foundation.hpp"
#include "os/threading.hpp"
#include "utils/cli_arg_parse.hpp"

#include "common_infrastructure/global.hpp"

#include "benchmarks/framework.hpp"

// X-macro list of benchmark registration functions.
#define BENCHMARK_REGISTER_FUNCTIONS                                                                         \
    X(RegisterAllocatorBenchmarks) X(RegisterSampleProcessingBenchmarks) X(RegisterLayoutBenchmarks)

// Declare the registration functions.
#define X(fn) void fn(benchmarks::Benchmarker&);
BENCHMARK_REGISTER_FUNCTIONS
#undef X

ErrorCodeOr<int> Main(ArgsCstr args) {
    GlobalInit({.init_error_reporting = false, .set_main_thread = true});
    DEFER { GlobalDeinit({.shutdown_error_reporting = false}); };

    benchmarks::Benchmarker benchmarker;

    enum class CommandLineArgId : u32 {
        Filter,
        List,
        Count,
    };

    auto constexpr k_cli_arg_defs = MakeCommandLineArgDefs<CommandLineArgId>({
        {
            .id = (u32)CommandLineArgId::Filter,
            .key = "filter",
            .description = "Wildcard pattern to filter benchmarks by name",
            .value_type = "pattern",
            .required = false,
            .num_values = -1,
        },
        {
            .id = (u32)CommandLineArgId::List,
            .key = "list",
            .description = "List available benchmarks and exit",
            .value_type = "flag",
            .required = false,
            .num_values = 0,
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

    // Register the benchmark functions.
#define X(fn) fn(benchmarker);
    BENCHMARK_REGISTER_FUNCTIONS
#undef X

    return benchmarks::RunBenchmarks(benchmarker,
                                     {
                                         .filter_patterns = cli_args[ToInt(CommandLineArgId::Filter)].values,
                                         .list_only = cli_args[ToInt(CommandLineArgId::List)].was_provided,
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
