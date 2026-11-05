// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/binance/ws_streams.hpp"

#include "../unit/mock_ws_connection.hpp"
#include "../unit/binance_ws_stream_example_json.hpp"

#include <benchmark/benchmark.h>

#include <memory>
#include <string>
#include <vector>

using json = nlohmann::json;
using namespace exchange::binance::ws;
using namespace exchange::binance;  // BinanceBookLevel
namespace fixtures = exchange::binance::ws::test;

namespace {

// ── BM_JsonParse_AggTradeRaw / BM_AggTradeFromJson ──────────────────────────
// Plan 027's Binance analog of Kraken's BM_JsonParse_TickerRaw /
// BM_TickerFromJson (025/026) — aggTrade is Binance's highest-*frequency*
// public market stream (one push per executed trade), the closest analog to
// Kraken's ticker pick.

void BM_JsonParse_AggTradeRaw(benchmark::State& state) {
    for (auto _ : state) {
        auto j = json::parse(fixtures::kAggTradeJson);
        benchmark::DoNotOptimize(j);
    }
}
BENCHMARK(BM_JsonParse_AggTradeRaw);

void BM_AggTradeFromJson(benchmark::State& state) {
    const json j = json::parse(fixtures::kAggTradeJson);
    for (auto _ : state) {
        auto msg = BinanceAggTradeEvent::from_json(j);
        benchmark::DoNotOptimize(msg);
    }
}
BENCHMARK(BM_AggTradeFromJson);

// ── BM_DepthUpdateFromJson — ranged book depth ──────────────────────────────
// BinanceDepthUpdateEvent::from_json (ws_streams.cpp) push_backs into
// bids/asks with no .reserve(), despite j["b"]/j["a"].size() being known
// upfront — see docs/plans/027 Design decisions. Depths match the
// BM_VectorPushBack_* Args below for a direct, same-N comparison in Step 4.

std::string make_binance_depth_json(int depth) {
    json bids = json::array();
    json asks = json::array();
    for (int i = 0; i < depth; ++i) {
        bids.push_back({std::to_string(71770.4 - i * 0.1), "0.5"});
        asks.push_back({std::to_string(71770.5 + i * 0.1), "0.5"});
    }
    const json root = {
        {"e", "depthUpdate"},
        {"E", 1672515782136LL},
        {"s", "BNBBTC"},
        {"U", 157},
        {"u", 160},
        {"b", bids},
        {"a", asks},
    };
    return root.dump();
}

void BM_DepthUpdateFromJson(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    const json j = json::parse(make_binance_depth_json(depth));
    for (auto _ : state) {
        auto msg = BinanceDepthUpdateEvent::from_json(j);
        benchmark::DoNotOptimize(msg);
    }
    state.SetComplexityN(depth);
}
BENCHMARK(BM_DepthUpdateFromJson)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Complexity(benchmark::oN);

// ── BM_OnRawMessage_AggTradePush — full ExchangeWsClient dispatch ───────────
// Mirrors test_binance_ws_client.cpp's Subscribe_Lifecycle setup (same
// make_ack/wrap shapes) — subscribe, ack, then the timed loop injects exactly
// what a live aggTrade feed sends per trade.

std::string make_ack(int64_t id) {
    return json{{"result", nullptr}, {"id", id}}.dump();
}

std::string wrap(const std::string& stream, const char* bare_payload) {
    return json{{"stream", stream}, {"data", json::parse(bare_payload)}}.dump();
}

void BM_OnRawMessage_AggTradePush(benchmark::State& state) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_binance_stream_client(conn);
    conn->fire_open();

    BinanceAggTradeSubscribe sub_req;
    sub_req.stream = agg_trade_stream("BNBBTC");
    long push_count = 0;
    auto fut = client->subscribe_async(
        sub_req,
        [&](const BinanceAggTradeEvent&) { ++push_count; });

    const int64_t id = json::parse(conn->sent()[0]).at("id").get<int64_t>();
    conn->inject_message(make_ack(id));
    if (!fut.get().first.ok) {
        state.SkipWithError("the subscribe ack failed");
        return;
    }

    const std::string pushed = wrap(sub_req.stream, fixtures::kAggTradeJson);
    for (auto _ : state) {
        conn->inject_message(pushed);
    }
    benchmark::DoNotOptimize(push_count);
}
BENCHMARK(BM_OnRawMessage_AggTradePush);

// ── BM_VectorPushBack_{No,With}Reserve — isolated reserve() ceiling ─────────
// Independent of JSON entirely: quantifies exactly what .reserve() can save
// for the BinanceBookLevel{price,quantity} shape (two doubles) at the same
// depths as BM_DepthUpdateFromJson.

void BM_VectorPushBack_NoReserve(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        std::vector<BinanceBookLevel> v;
        for (int i = 0; i < n; ++i) v.push_back({100.0 + i, 1.0});
        benchmark::DoNotOptimize(v);
    }
    state.SetComplexityN(n);
}
BENCHMARK(BM_VectorPushBack_NoReserve)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Complexity(benchmark::oN);

void BM_VectorPushBack_WithReserve(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        std::vector<BinanceBookLevel> v;
        v.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) v.push_back({100.0 + i, 1.0});
        benchmark::DoNotOptimize(v);
    }
    state.SetComplexityN(n);
}
BENCHMARK(BM_VectorPushBack_WithReserve)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Complexity(benchmark::oN);

} // namespace
