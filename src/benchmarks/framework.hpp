// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/dynamic_array.hpp"
#include "foundation/container/span.hpp"
#include "os/misc.hpp"

#include "config.h"

namespace benchmarks {

using BenchmarkFunction = void (*)();

struct BenchmarkCase {
    BenchmarkFunction f;
    String name;
};

struct Benchmarker {
    ArenaAllocator arena {PageAllocator::Instance()};
    DynamicArray<BenchmarkCase> benchmark_cases {arena};
};

void RegisterBenchmark(Benchmarker& benchmarker, BenchmarkFunction f, String name);

struct RunConfig {
    Span<String> filter_patterns;
    bool list_only;
};
int RunBenchmarks(Benchmarker& benchmarker, RunConfig const& config);

// Same approach as Google Benchmark.
template <typename T>
inline void DoNotOptimise(T& val) {
    asm volatile("" : "+r,m"(val) : : "memory");
}

} // namespace benchmarks

#if !PRODUCTION_BUILD

// Benchmark functions are void(*)() - they just run the code; no timing is done here (use hyperfine).
// Aim for each benchmark to take at least 0.5 seconds so that process startup costs are negligible.
// If your function needs args, wrap each invocation in a lambda:
//   REGISTER_BENCHMARK_NAMED([](){ BenchmarkFoo(200); }, "Foo200")
#define REGISTER_BENCHMARK(func)             benchmarks::RegisterBenchmark(benchmarker, func, #func)
#define REGISTER_BENCHMARK_NAMED(func, name) benchmarks::RegisterBenchmark(benchmarker, func, name)
#define BENCHMARK_REGISTRATION(name)         void name([[maybe_unused]] benchmarks::Benchmarker& benchmarker)

#else

#define REGISTER_BENCHMARK(func)
#define REGISTER_BENCHMARK_NAMED(func, name)
#define BENCHMARK_REGISTRATION(name)                                                                         \
    template <typename Unused>                                                                               \
    void name(benchmarks::Benchmarker&)

#endif
