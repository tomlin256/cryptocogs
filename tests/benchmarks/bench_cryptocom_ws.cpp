// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/cryptocom/ws.hpp"

#include "../unit/mock_ws_connection.hpp"
#include "../unit/cryptocom_ws_example_json.hpp"

#include <benchmark/benchmark.h>

#include <memory>
#include <string>
#include <vector>

using json = nlohmann::json;
using namespace exchange::cryptocom::ws;
namespace fx = cryptocom_ws_fixtures;

namespace {

// Each BENCHMARK(...) in this namespace is google benchmark's registration macro: it declares a static whose initialiser registers
// the benchmark, which allocates, so bugprone-throwing-static-initialization reports it. There is nothing in this file to fix, so
// that one check is silenced from here to the matching end marker at the bottom of the namespace.
// NOLINTBEGIN(bugprone-throwing-static-initialization)

// ── BM_JsonParse_TickerRaw / BM_TickerEventFromJson ─────────────────────────
// The Crypto.com analog (plan remaining-adapters-reserve-vector-growth) of Kraken's BM_JsonParse_TickerRaw /
// BM_TickerFromJson (plans kraken-performance-profiling and kraken-reserve-vector-growth) — "ticker" is
// Crypto.com's own highest-frequency public market channel, same role as Kraken's pick.

void BM_JsonParse_TickerRaw(benchmark::State& state) {
    for (auto _ : state) {
        auto j = json::parse(fx::TICKER_PUSH);
        benchmark::DoNotOptimize(j);
    }
}
BENCHMARK(BM_JsonParse_TickerRaw);

void BM_TickerEventFromJson(benchmark::State& state) {
    const json j = json::parse(fx::TICKER_PUSH);
    for (auto _ : state) {
        auto msg = CryptoComTickerEvent::from_json(j);
        benchmark::DoNotOptimize(msg);
    }
}
BENCHMARK(BM_TickerEventFromJson);

// ── BM_BookEventFromJson — ranged book depth ────────────────────────────────
// CryptoComBookEvent::from_json (ws.cpp) push_backs into bids/asks with no
// .reserve(), despite d.at("bids")/d.at("asks").size() being known upfront —
// see docs/plans/remaining-adapters-reserve-vector-growth.md Design decisions. Depths match the BM_VectorPushBack_*
// Args below for a direct, same-N comparison in Step 4. Wire shape mirrors
// the nested result.data[0].bids/asks CryptoComBookEvent::from_json actually
// reads (result_obj/result_data helpers), each row a 3-element
// [price, size, num_orders] string array (CryptoComWsBookLevel::from_json).

std::string make_cryptocom_book_json(int depth) {
    json bids = json::array();
    json asks = json::array();
    for (int i = 0; i < depth; ++i) {
        bids.push_back({std::to_string(71770.4 - i * 0.1), "0.5", "1"});
        asks.push_back({std::to_string(71770.5 + i * 0.1), "0.5", "1"});
    }
    const json root = {
        {"id", -1},
        {"method", "subscribe"},
        {"code", 0},
        {"result", {
            {"instrument_name", "BTC_USD"},
            {"subscription", "book.BTC_USD.500"},
            {"channel", "book"},
            {"depth", depth},
            {"data", json::array({
                {{"t", 1782032455141LL}, {"u", 1782032455142LL}, {"bids", bids}, {"asks", asks}},
            })},
        }},
    };
    return root.dump();
}

void BM_BookEventFromJson(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    const json j = json::parse(make_cryptocom_book_json(depth));
    for (auto _ : state) {
        auto msg = CryptoComBookEvent::from_json(j);
        benchmark::DoNotOptimize(msg);
    }
    state.SetComplexityN(depth);
}
BENCHMARK(BM_BookEventFromJson)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Complexity(benchmark::oN);

// ── BM_OnRawMessage_TickerPush — full ExchangeWsClient dispatch ─────────────
// Undecorated: exchange::ws::make_exchange_ws_client(conn,
// cryptocom_frame_descriptor) directly, NOT make_cryptocom_market_client's
// HeartbeatResponder wrapper — the decorator's overhead is a separate,
// orthogonal concern from parse/dispatch cost (see Design decisions). Mirrors
// test_cryptocom_ws_client.cpp's Subscribe_Ack_Push_Cancel setup otherwise
// (ticker_channel, make_ack, push_from — local copies below, same bodies).

std::string push_from(const char* fixture) { return std::string(fixture); }

std::string make_ack(int64_t id, const std::string& subscription, const std::string& channel) {
    return json{{"id", id}, {"method", "subscribe"}, {"code", 0},
                {"result", {{"subscription", subscription}, {"channel", channel}}}}.dump();
}

void BM_OnRawMessage_TickerPush(benchmark::State& state) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = exchange::ws::make_exchange_ws_client(conn, cryptocom_frame_descriptor);
    conn->fire_open();

    CryptoComTickerSubscribe req;
    req.channel = ticker_channel("BTC_USD");
    long push_count = 0;
    auto fut = client->subscribe_async(
        req, [&](const CryptoComTickerEvent&) { ++push_count; });

    const int64_t id = json::parse(conn->sent()[0]).at("id").get<int64_t>();
    conn->inject_message(make_ack(id, "ticker.BTC_USD", "ticker"));
    if (!fut.get().first.ok) {
        state.SkipWithError("the subscribe ack failed");
        return;
    }

    const std::string pushed = push_from(fx::TICKER_PUSH);
    for (auto _ : state) {
        conn->inject_message(pushed);
    }
    benchmark::DoNotOptimize(push_count);
}
BENCHMARK(BM_OnRawMessage_TickerPush);

// ── BM_VectorPushBack_{No,With}Reserve — isolated reserve() ceiling ─────────
// Independent of JSON entirely: quantifies exactly what .reserve() can save
// for the CryptoComWsBookLevel{price,size,num_orders} shape at the same
// depths as BM_BookEventFromJson.

void BM_VectorPushBack_NoReserve(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        std::vector<CryptoComWsBookLevel> v;
        // No reserve() on purpose: this is the baseline that BM_VectorPushBack_WithReserve below is measured against.
        for (int i = 0; i < n; ++i) v.push_back({100.0 + i, 1.0, 1});  // NOLINT(performance-inefficient-vector-operation)
        benchmark::DoNotOptimize(v);
    }
    state.SetComplexityN(n);
}
BENCHMARK(BM_VectorPushBack_NoReserve)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Complexity(benchmark::oN);

void BM_VectorPushBack_WithReserve(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        std::vector<CryptoComWsBookLevel> v;
        v.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) v.push_back({100.0 + i, 1.0, 1});
        benchmark::DoNotOptimize(v);
    }
    state.SetComplexityN(n);
}
BENCHMARK(BM_VectorPushBack_WithReserve)->Arg(10)->Arg(50)->Arg(100)->Arg(500)->Complexity(benchmark::oN);

// NOLINTEND(bugprone-throwing-static-initialization)

} // namespace
