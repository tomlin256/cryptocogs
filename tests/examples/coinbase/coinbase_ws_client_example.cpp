// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Demonstrates the Coinbase Exchange WebSocket feed via the bespoke
// CoinbaseStreamClient (plan 018 §2 Option A) over the real IxWsConnection
// transport. All channels here are public — no credentials required.
//
// Usage:
//   coinbase_ws_client_example ticker    <product-id>
//   coinbase_ws_client_example level2    <product-id>
//   coinbase_ws_client_example matches   <product-id>
//   coinbase_ws_client_example heartbeat <product-id>
//   coinbase_ws_client_example multi     <product-id>   # ticker + matches, one socket
//
// Examples:
//   coinbase_ws_client_example ticker BTC-USD
//   coinbase_ws_client_example multi BTC-USD

#include "exchange/coinbase/ws_streams.hpp"
#include "exchange/common/ix_ws_connection.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace exchange::coinbase::ws;
using exchange::ws::IxWsConnection;

namespace {

constexpr auto STREAM_SECONDS = std::chrono::seconds{10};

std::shared_ptr<CoinbaseStreamClient> make_client() {
    auto conn   = std::make_shared<IxWsConnection>(std::string(STREAM_URL));
    auto client = make_coinbase_stream_client(
        conn, std::make_shared<RateLimitedWsErrorHandler>());
    client->set_on_error([](const CoinbaseErrorEvent& e) {
        spdlog::error("[error] {}", e.message);
    });
    client->set_on_subscriptions([](const json& j) {
        spdlog::info("[subscriptions] {}", j.dump());
    });
    return client;
}

void run_ticker(const std::string& product) {
    auto client = make_client();
    auto handle = client->subscribe_ticker({product}, [](const CoinbaseTickerEvent& e) {
        spdlog::info("[ticker] {} price={:.2f} bid={:.2f} ask={:.2f} vol24h={:.4f}",
                     e.product_id, e.price, e.best_bid, e.best_ask, e.volume_24h);
    });
    client->connect();
    spdlog::info("ticker streaming for {} s...", STREAM_SECONDS.count());
    std::this_thread::sleep_for(STREAM_SECONDS);
    handle.cancel();
    client->disconnect();
}

void run_level2(const std::string& product) {
    auto client = make_client();
    auto handle = client->subscribe_level2(
        {product},
        [](const CoinbaseL2Snapshot& s) {
            spdlog::info("[snapshot] {} bids={} asks={}", s.product_id,
                         s.bids.size(), s.asks.size());
        },
        [](const CoinbaseL2Update& u) {
            spdlog::info("[l2update] {} changes={}", u.product_id, u.changes.size());
        });
    client->connect();
    spdlog::info("level2 streaming for {} s...", STREAM_SECONDS.count());
    std::this_thread::sleep_for(STREAM_SECONDS);
    handle.cancel();
    client->disconnect();
}

void run_matches(const std::string& product) {
    auto client = make_client();
    auto handle = client->subscribe_matches({product}, [](const CoinbaseMatchEvent& e) {
        spdlog::info("[match] {} {} {:.2f} x {:.8f} (taker {})", e.product_id,
                     e.side, e.price, e.size, e.taker_order_id);
    });
    client->connect();
    spdlog::info("matches streaming for {} s...", STREAM_SECONDS.count());
    std::this_thread::sleep_for(STREAM_SECONDS);
    handle.cancel();
    client->disconnect();
}

void run_heartbeat(const std::string& product) {
    auto client = make_client();
    auto handle = client->subscribe_heartbeat({product}, [](const CoinbaseHeartbeatEvent& e) {
        spdlog::info("[heartbeat] {} seq={} last_trade_id={} {}", e.product_id,
                     e.sequence, e.last_trade_id, e.time);
    });
    client->connect();
    spdlog::info("heartbeat streaming for {} s...", STREAM_SECONDS.count());
    std::this_thread::sleep_for(STREAM_SECONDS);
    handle.cancel();
    client->disconnect();
}

// multi — ticker + matches on ONE client/socket; cancels are staggered to show
// connection reuse (the same model the unit suite pins).
void run_multi(const std::string& product) {
    auto client = make_client();
    auto th = client->subscribe_ticker({product}, [](const CoinbaseTickerEvent& e) {
        spdlog::info("[ticker] {} price={:.2f}", e.product_id, e.price);
    });
    auto mh = client->subscribe_matches({product}, [](const CoinbaseMatchEvent& e) {
        spdlog::info("[match]  {} {:.2f} x {:.8f} {}", e.product_id, e.price, e.size, e.side);
    });
    client->connect();
    spdlog::info("ticker + matches on one socket for {} s...", STREAM_SECONDS.count());
    std::this_thread::sleep_for(STREAM_SECONDS);
    th.cancel();
    spdlog::info("ticker unsubscribed — matches continues 3 s");
    std::this_thread::sleep_for(std::chrono::seconds{3});
    mh.cancel();
    client->disconnect();
}

} // namespace

int main(int argc, char** argv) {
    try {
        CLI::App app{"Coinbase Exchange WebSocket feed demo"};
        app.require_subcommand(1);

        std::string product;
        auto add = [&](const char* name, const char* desc) {
            auto* sub = app.add_subcommand(name, desc);
            sub->add_option("product_id", product, "e.g. BTC-USD")->required();
            return sub;
        };
        auto* ticker    = add("ticker", "Ticker channel");
        auto* level2    = add("level2", "Level2 book (snapshot + updates)");
        auto* matches   = add("matches", "Match (trade) channel");
        auto* heartbeat = add("heartbeat", "Heartbeat channel");
        auto* multi     = add("multi", "ticker + matches on one socket");

        CLI11_PARSE(app, argc, argv);

        if (*ticker)         run_ticker(product);
        else if (*level2)    run_level2(product);
        else if (*matches)   run_matches(product);
        else if (*heartbeat) run_heartbeat(product);
        else if (*multi)     run_multi(product);

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "fatal: unknown exception\n";
        return EXIT_FAILURE;
    }
}
