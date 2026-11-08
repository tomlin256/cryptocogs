// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/common/tick_price.hpp"

#include <benchmark/benchmark.h>

using exchange::TickPrice;

namespace {

// Each BENCHMARK(...) in this namespace is google benchmark's registration macro: it declares a static whose initialiser registers
// the benchmark, which allocates, so bugprone-throwing-static-initialization reports it. There is nothing in this file to fix, so
// that one check is silenced from here to the matching end marker at the bottom of the namespace.
// NOLINTBEGIN(bugprone-throwing-static-initialization)

// Candidate #6: small, but called once per price field on every hot message
// if a caller re-derives it (docs/plans/kraken-performance-profiling.md Design decisions).

void BM_TickPrice_From(benchmark::State& state) {
    for (auto _ : state) {
        auto tp = TickPrice::from(71770.4, 4);
        benchmark::DoNotOptimize(tp);
    }
}
BENCHMARK(BM_TickPrice_From);

void BM_TickPrice_Str(benchmark::State& state) {
    const auto tp = TickPrice::from(71770.4, 4);
    for (auto _ : state) {
        auto s = tp.str();
        benchmark::DoNotOptimize(s);
    }
}
BENCHMARK(BM_TickPrice_Str);

// NOLINTEND(bugprone-throwing-static-initialization)

} // namespace
