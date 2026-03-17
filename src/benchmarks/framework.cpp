// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "framework.hpp"

namespace benchmarks {

void RegisterBenchmark(Benchmarker& benchmarker, BenchmarkFunction f, String name) {
    dyn::Append(benchmarker.benchmark_cases, BenchmarkCase {f, name});
}

static bool MatchesFilter(Span<String> filter_patterns, String name) {
    if (!filter_patterns.size) return true;
    for (auto const& pattern : filter_patterns)
        if (MatchWildcard(pattern, name)) return true;
    return false;
}

int RunBenchmarks(Benchmarker& benchmarker, RunConfig const& config) {
    if (config.list_only) {
        for (auto const& bench : benchmarker.benchmark_cases)
            if (MatchesFilter(config.filter_patterns, bench.name))
                StdPrintF(StdStream::Out, "{}\n", bench.name);
        return 0;
    }

    DynamicArray<BenchmarkCase*> matched {benchmarker.arena};
    for (auto& bench : benchmarker.benchmark_cases)
        if (MatchesFilter(config.filter_patterns, bench.name)) dyn::Append(matched, &bench);

    if (matched.size == 0) {
        StdPrintF(StdStream::Err, "No benchmarks matched the filter\n");
        return 1;
    }

    for (auto* bench : matched)
        bench->f();

    return 0;
}

} // namespace benchmarks
