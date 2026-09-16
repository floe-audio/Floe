// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "framework.hpp"

namespace benchmarks {

void RegisterBenchmark(Benchmarker& benchmarker, BenchmarkFunction f, String name) {
    dyn::Append(benchmarker.benchmark_cases, BenchmarkCase {.f = f, .name = name});
}

void RegisterBenchmark(Benchmarker& benchmarker, ContextualBenchmarkFunction f, String name) {
    dyn::Append(benchmarker.benchmark_cases, BenchmarkCase {.contextual_f = f, .name = name});
}

static bool MatchesFilter(Span<String> filter_patterns, String name) {
    if (!filter_patterns.size) return true;
    for (auto const& pattern : filter_patterns)
        if (MatchWildcard(pattern, name)) return true;
    return false;
}

int RunBenchmarks(Benchmarker& benchmarker, RunConfig const& config) {
    if (config.list_only) {
        // Benchmarks that need --arg are marked with a tab-separated suffix so that unattended runners can
        // skip them.
        for (auto const& bench : benchmarker.benchmark_cases)
            if (MatchesFilter(config.filter_patterns, bench.name))
                StdPrintF(StdStream::Out, "{}{}\n", bench.name, bench.f ? ""_s : "\t(needs --arg)"_s);
        return 0;
    }

    DynamicArray<BenchmarkCase*> matched {benchmarker.arena};
    for (auto& bench : benchmarker.benchmark_cases)
        if (MatchesFilter(config.filter_patterns, bench.name)) dyn::Append(matched, &bench);

    if (matched.size == 0) {
        StdPrintF(StdStream::Err, "No benchmarks matched the filter\n");
        return 1;
    }

    BenchmarkContext const context {.arena = benchmarker.arena, .args = config.args};

    for (auto* bench : matched)
        if (bench->f)
            bench->f();
        else
            bench->contextual_f(context);

    return 0;
}

} // namespace benchmarks
