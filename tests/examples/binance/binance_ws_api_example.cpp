// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Demonstrates the Binance WebSocket API (trading) endpoint via
// BinanceWsApiClient (ExchangeWsClient + binance_ws_api_frame_descriptor) on
// wss://ws-api.binance.com/ws-api/v3.
//
// Only the `ping` method is exercised here — it is public (no credentials), so
// the example needs no API keys and is safe to run against the live endpoint.
// order.place / order.cancel are covered by the unit tests with a mock
// connection; placing real orders from an example is intentionally out of
// scope.
//
// Usage:
//   binance_ws_api_example ping

#include "exchange/binance/ws_api.hpp"
#include "exchange/common/ix_ws_connection.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>

using namespace exchange::binance::ws;

namespace {

constexpr auto PING_TIMEOUT = std::chrono::milliseconds{10000};

bool run_ping() {
    spdlog::info("=== Binance WS API ping: {} ===", WS_API_URL);

    auto client = exchange::ws::make_exchange_ws_client(
        std::string(WS_API_URL), binance_ws_api_frame_descriptor);

    const auto resp = client->execute(BinanceWsPingRequest{}, PING_TIMEOUT);

    if (!resp.ok) {
        spdlog::error("ping failed: {}", resp.error.value_or("unknown"));
        return false;
    }

    spdlog::info("ping ok — status {}", resp.result ? resp.result->status : 0);
    if (resp.result) {
        for (const auto& rl : resp.result->rate_limits) {
            spdlog::info("  rateLimit {} per {}x{} limit={} count={}",
                         rl.rate_limit_type, rl.interval_num, rl.interval,
                         rl.limit, rl.count);
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    try {
        CLI::App app{"Binance WebSocket API (trading endpoint) demo"};
        app.require_subcommand(1);

        auto* ping = app.add_subcommand("ping", "Heartbeat against the WS API endpoint");

        CLI11_PARSE(app, argc, argv);

        bool ok = false;
        if (*ping) ok = run_ping();

        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "fatal: unknown exception\n";
        return EXIT_FAILURE;
    }
}
