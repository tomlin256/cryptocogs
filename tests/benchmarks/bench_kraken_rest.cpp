// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/kraken/rest_api.hpp"

#include <benchmark/benchmark.h>

#include <string>

using namespace exchange::kraken;
using namespace exchange::kraken::rest;

namespace {

// Each BENCHMARK(...) in this namespace is google benchmark's registration macro: it declares a static whose initialiser registers
// the benchmark, which allocates, so bugprone-throwing-static-initialization reports it. There is nothing in this file to fix, so
// that one check is silenced from here to the matching end marker at the bottom of the namespace.
// NOLINTBEGIN(bugprone-throwing-static-initialization)

// Same dummy-creds idiom as tests/unit/test_rest_requests.cpp — the secret
// only needs to be valid base64, its content is irrelevant to timing.
KrakenCredentials make_dummy_creds() {
    return KrakenCredentials{"test-api-key", "dGVzdA=="};
}

// ── BM_KrakenSign — KrakenCredentials::sign() in isolation ──────────────────
// Candidate #1 from docs/plans/025: five intermediate std::strings plus a
// hand-rolled base64 encode/decode per call.

void BM_KrakenSign(benchmark::State& state) {
    const auto creds = make_dummy_creds();
    const std::string path  = "/0/private/AddOrder";
    const std::string nonce = "1700000000000000";
    const std::string body  =
        "nonce=1700000000000000&pair=XBTUSD&type=buy&ordertype=limit"
        "&price=30000.0&volume=0.001";
    for (auto _ : state) {
        auto sig = creds.sign(path, nonce, body);
        benchmark::DoNotOptimize(sig);
    }
}
BENCHMARK(BM_KrakenSign);

// ── BM_AddOrderRequest_Build — request construction/serialization ───────────
// Candidate #2: the order-placement path (build() + sign()).

void BM_AddOrderRequest_Build(benchmark::State& state) {
    const auto creds = make_dummy_creds();
    AddOrderRequest req;
    req.params.order_type  = exchange::OrderType::Limit;
    req.params.side        = exchange::Side::Buy;
    req.params.symbol      = "XBTUSD";
    req.params.order_qty   = 0.001;
    req.params.limit_price = exchange::kraken::TickPrice::from(30000.0, 4);
    for (auto _ : state) {
        auto http = req.build(creds);
        benchmark::DoNotOptimize(http);
    }
}
BENCHMARK(BM_AddOrderRequest_Build);

// ── BM_ParseOpenOrdersResponse — JSON parse + deserialize, ranged over N ────
// Candidate #3. OpenOrdersResult::open is a std::map<std::string, OrderInfo>
// (rest_api.cpp OpenOrdersResult::from_json: `r.open[k] = ...`) — a tree
// insert, not a push_back, so this is not a reserve() candidate (std::map has
// no reserve()); it measures JSON-parse-plus-O(N log N)-map-insertion cost on
// its own terms. See bench_kraken_ws.cpp for the vector/reserve story.

std::string make_open_orders_json(int n) {
    json open = json::object();
    for (int i = 0; i < n; ++i) {
        const std::string txid = "TXID" + std::to_string(i);
        open[txid] = {
            {"status", "open"},
            {"vol", "0.00100000"},
            {"vol_exec", "0.00000000"},
            {"cost", "0.00000"},
            {"fee", "0.00000"},
            {"price", "0.00000"},
            {"stopprice", "0.00000"},
            {"limitprice", "0.00000"},
            {"misc", ""},
            {"oflags", "fciq"},
            {"opentm", 1700000000.1234},
            {"descr", {
                {"pair", "XBTUSD"},
                {"type", "buy"},
                {"ordertype", "limit"},
                {"price", "30000.0"},
                {"price2", "0"},
                {"leverage", "none"},
                {"order", "buy 0.00100000 XBTUSD @ limit 30000.0"},
            }},
        };
    }
    const json root = {{"error", json::array()}, {"result", {{"open", open}}}};
    return root.dump();
}

void BM_ParseOpenOrdersResponse(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    const std::string body = make_open_orders_json(n);
    for (auto _ : state) {
        auto resp = parse_rest_response<OpenOrdersResult>(json::parse(body));
        benchmark::DoNotOptimize(resp);
    }
    state.SetComplexityN(n);
}
BENCHMARK(BM_ParseOpenOrdersResponse)
    ->RangeMultiplier(10)
    ->Range(1, 100)
    ->Complexity(benchmark::oNLogN);

// NOLINTEND(bugprone-throwing-static-initialization)

} // namespace
