// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/coinbase/ws_streams.hpp"

#include "../unit/mock_ws_connection.hpp"
#include "../unit/coinbase_ws_example_json.hpp"

#include <benchmark/benchmark.h>

#include <memory>
#include <string>
#include <vector>

using json = nlohmann::json;
using namespace exchange::coinbase::ws;
namespace wf = coinbase_ws_fixtures;

namespace {

// Each BENCHMARK(...) in this namespace is google benchmark's registration macro: it declares a static whose initialiser registers
// the benchmark, which allocates, so bugprone-throwing-static-initialization reports it. There is nothing in this file to fix, so
// that one check is silenced from here to the matching end marker at the bottom of the namespace.
// NOLINTBEGIN(bugprone-throwing-static-initialization)

// ── BM_JsonParse_TickerRaw / BM_TickerEventFromJson ─────────────────────────
// Plan 027's Coinbase analog of Kraken's BM_JsonParse_TickerRaw /
// BM_TickerFromJson (025/026) — "ticker" is Coinbase's own highest-frequency
// public market channel, same role as Kraken's pick.

void BM_JsonParse_TickerRaw(benchmark::State& state) {
    for (auto _ : state) {
        auto j = json::parse(wf::TICKER_JSON);
        benchmark::DoNotOptimize(j);
    }
}
BENCHMARK(BM_JsonParse_TickerRaw);

void BM_TickerEventFromJson(benchmark::State& state) {
    const json j = json::parse(wf::TICKER_JSON);
    for (auto _ : state) {
        auto msg = CoinbaseTickerEvent::from_json(j);
        benchmark::DoNotOptimize(msg);
    }
}
BENCHMARK(BM_TickerEventFromJson);

// ── BM_L2SnapshotFromJson — ranged book depth ───────────────────────────────
// CoinbaseL2Snapshot::from_json (ws_streams.cpp) push_backs into bids/asks
// with no .reserve(), despite j.at("bids")/j.at("asks").size() being known
// upfront — see docs/plans/027 Design decisions. Depths match the
// BM_VectorPushBack_* Args below for a direct, same-N comparison in Step 4.

std::string make_coinbase_snapshot_json(int depth) {
    json bids = json::array();
    json asks = json::array();
    for (int i = 0; i < depth; ++i) {
        bids.push_back({std::to_string(63210.11 - i * 0.1), "1.5"});
        asks.push_back({std::to_string(63210.50 + i * 0.1), "0.8"});
    }
    const json root = {
        {"type", "snapshot"},
        {"product_id", "BTC-USD"},
        {"bids", bids},
        {"asks", asks},
    };
    return root.dump();
}

void BM_L2SnapshotFromJson(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    const json j = json::parse(make_coinbase_snapshot_json(depth));
    for (auto _ : state) {
        auto msg = CoinbaseL2Snapshot::from_json(j);
        benchmark::DoNotOptimize(msg);
    }
    state.SetComplexityN(depth);
}
BENCHMARK(BM_L2SnapshotFromJson)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Complexity(benchmark::oN);

// ── BM_OnRawMessage_TickerPush — full CoinbaseStreamClient dispatch ─────────
// Coinbase's client is bespoke (id-less, optimistic subscribe — not an
// ExchangeWsClient alias), so unlike the Kraken/Binance/Crypto.com dispatch
// benchmarks there is no ack round-trip to set up first. Mirrors
// test_coinbase_ws_client.cpp's CoinbaseWsClient.TickerFrameDispatchedToTypedCallback
// setup order exactly: construct, subscribe, fire_open, then inject pushes.

void BM_OnRawMessage_TickerPush(benchmark::State& state) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_coinbase_stream_client(conn);

    long push_count = 0;
    client->subscribe_ticker({"BTC-USD"},
                              [&](const CoinbaseTickerEvent&) { ++push_count; });
    conn->fire_open();

    for (auto _ : state) {
        conn->inject_message(wf::TICKER_JSON);
    }
    benchmark::DoNotOptimize(push_count);
}
BENCHMARK(BM_OnRawMessage_TickerPush);

// ── BM_VectorPushBack_{No,With}Reserve — isolated reserve() ceiling ─────────
// Independent of JSON entirely: quantifies exactly what .reserve() can save
// for the CoinbaseL2Level{price,size} shape (two doubles) at the same depths
// as BM_L2SnapshotFromJson.

void BM_VectorPushBack_NoReserve(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        std::vector<CoinbaseL2Level> v;
        // No reserve() on purpose: this is the baseline that BM_VectorPushBack_WithReserve below is measured against.
        for (int i = 0; i < n; ++i) v.push_back({100.0 + i, 1.0});  // NOLINT(performance-inefficient-vector-operation)
        benchmark::DoNotOptimize(v);
    }
    state.SetComplexityN(n);
}
BENCHMARK(BM_VectorPushBack_NoReserve)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Complexity(benchmark::oN);

void BM_VectorPushBack_WithReserve(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        std::vector<CoinbaseL2Level> v;
        v.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) v.push_back({100.0 + i, 1.0});
        benchmark::DoNotOptimize(v);
    }
    state.SetComplexityN(n);
}
BENCHMARK(BM_VectorPushBack_WithReserve)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Complexity(benchmark::oN);

// NOLINTEND(bugprone-throwing-static-initialization)

} // namespace
