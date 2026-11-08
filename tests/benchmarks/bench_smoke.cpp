// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include <benchmark/benchmark.h>

// Canary benchmark proving the harness (CMake wiring, FetchContent'd
// google/benchmark, benchmark_main) works end to end. Kept permanently as a
// cheap sanity check alongside the real Kraken benchmarks added in later
// steps of docs/plans/kraken-performance-profiling.md.
namespace {

// BENCHMARK(...) is google benchmark's registration macro: it declares a static whose initialiser registers the benchmark, which
// allocates, so bugprone-throwing-static-initialization reports it. There is nothing in this file to fix, so that one check is
// silenced from here to the matching end marker at the bottom of the namespace.
// NOLINTBEGIN(bugprone-throwing-static-initialization)

void BM_Noop(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(state.iterations());
    }
}
BENCHMARK(BM_Noop);

// NOLINTEND(bugprone-throwing-static-initialization)

} // namespace
