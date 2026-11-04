// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/kraken/ws_client.hpp"

#include "../unit/mock_ws_connection.hpp"
#include "../unit/ws_client_example_json.hpp"

#include <benchmark/benchmark.h>

#include <memory>
#include <string>
#include <vector>

using json = nlohmann::json;
using namespace exchange::kraken::ws::test;

namespace {

// ── BM_TickerFromJson / BM_FrameDescriptor_Ticker ───────────────────────────
// Candidate #4: the highest-*frequency* real-world path (market data ticks),
// using the captured live fixture from tests/unit/ws_client_example_json.hpp
// rather than a synthetic payload.

// Isolates raw-bytes-to-DOM parse cost, separate from DOM-to-struct
// extraction below — added after BM_OnRawMessage_TickerPush's total (2762ns)
// turned out far larger than BM_TickerFromJson's pre-parsed-DOM cost (292ns)
// alone could explain; this confirms/quantifies the gap instead of leaving it
// as an inference from subtraction.
void BM_JsonParse_TickerRaw(benchmark::State& state) {
    for (auto _ : state) {
        auto j = json::parse(kTickerSnapshotJson);
        benchmark::DoNotOptimize(j);
    }
}
BENCHMARK(BM_JsonParse_TickerRaw);

void BM_TickerFromJson(benchmark::State& state) {
    const json j = json::parse(kTickerSnapshotJson);
    for (auto _ : state) {
        auto msg = exchange::kraken::ws::TickerMessage::from_json(j);
        benchmark::DoNotOptimize(msg);
    }
}
BENCHMARK(BM_TickerFromJson);

void BM_FrameDescriptor_Ticker(benchmark::State& state) {
    const json j = json::parse(kTickerSnapshotJson);
    for (auto _ : state) {
        auto desc = exchange::kraken::ws::kraken_frame_descriptor(j);
        benchmark::DoNotOptimize(desc);
    }
}
BENCHMARK(BM_FrameDescriptor_Ticker);

// ── BM_BookFromJson — ranged book depth ──────────────────────────────────────
// BookData::from_json (ws_api.cpp:390-403) push_backs into bids/asks with no
// .reserve(), despite j["bids"]/j["asks"].size() being known upfront — see
// docs/plans/025 Design decisions. Depths chosen to match the
// BM_VectorPushBack_* Args below for a direct, same-N comparison in Step 4.

std::string make_book_json(int depth) {
    json bids = json::array();
    json asks = json::array();
    for (int i = 0; i < depth; ++i) {
        bids.push_back({{"price", 71770.4 - i * 0.1}, {"qty", 0.5}});
        asks.push_back({{"price", 71770.5 + i * 0.1}, {"qty", 0.5}});
    }
    const json root = {
        {"channel", "book"},
        {"type", "snapshot"},
        {"data", json::array({
            {{"symbol", "BTC/USD"}, {"bids", bids}, {"asks", asks}, {"checksum", 2552662837u}},
        })},
    };
    return root.dump();
}

void BM_BookFromJson(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    const json j = json::parse(make_book_json(depth));
    for (auto _ : state) {
        auto msg = exchange::kraken::ws::BookMessage::from_json(j);
        benchmark::DoNotOptimize(msg);
    }
    state.SetComplexityN(depth);
}
BENCHMARK(BM_BookFromJson)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Complexity(benchmark::oN);

// ── BM_OnRawMessage_TickerPush — full ExchangeWsClient dispatch ─────────────
// Candidate #5: json::parse + mutex + map lookup (route_key) + std::function
// copy + callback invoke — not just the parse in isolation. Setup (connect,
// subscribe, ack) happens once outside the timed loop, mirroring
// tests/unit/test_ws_client.cpp's make_test_client()/make_subscribe_ack()
// pattern; the timed loop is exactly what a live ticker feed does per tick.

void BM_OnRawMessage_TickerPush(benchmark::State& state) {
    auto conn = std::make_shared<MockWsConnection>();
    auto client = exchange::ws::make_exchange_ws_client(
        conn, exchange::kraken::ws::kraken_frame_descriptor);
    conn->fire_open();

    exchange::kraken::ws::TickerSubscribeRequest sub_req;
    sub_req.symbols = std::vector<std::string>{"BTC/USD"};
    long push_count = 0;
    auto fut = client->subscribe_async(
        sub_req,
        [&](const exchange::kraken::ws::TickerMessage&) { ++push_count; });

    const int64_t id =
        json::parse(conn->sent()[0]).at("req_id").get<int64_t>();
    json ack;
    ack["method"]             = "subscribe";
    ack["req_id"]             = id;
    ack["success"]            = true;
    ack["result"]["channel"]  = "ticker";
    conn->inject_message(ack.dump());
    fut.get();

    for (auto _ : state) {
        conn->inject_message(kTickerSnapshotJson);
    }
    benchmark::DoNotOptimize(push_count);
}
BENCHMARK(BM_OnRawMessage_TickerPush);

// ── BM_VectorPushBack_{No,With}Reserve — isolated reserve() ceiling ─────────
// Independent of JSON entirely: quantifies exactly what .reserve() can save
// for the BookEntry{price,qty} shape (two doubles — same layout as
// std::pair<double,double>) at the same depths as BM_BookFromJson, so Step 4
// can judge whether the win is a meaningful fraction of total parse cost.

void BM_VectorPushBack_NoReserve(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        std::vector<exchange::kraken::ws::BookEntry> v;
        for (int i = 0; i < n; ++i) v.push_back({100.0 + i, 1.0});
        benchmark::DoNotOptimize(v);
    }
    state.SetComplexityN(n);
}
BENCHMARK(BM_VectorPushBack_NoReserve)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Complexity(benchmark::oN);

void BM_VectorPushBack_WithReserve(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        std::vector<exchange::kraken::ws::BookEntry> v;
        v.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) v.push_back({100.0 + i, 1.0});
        benchmark::DoNotOptimize(v);
    }
    state.SetComplexityN(n);
}
BENCHMARK(BM_VectorPushBack_WithReserve)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Complexity(benchmark::oN);

} // namespace
