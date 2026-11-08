// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Demonstrates the Crypto.com Exchange v1 market WebSocket via
// CryptoComMarketClient (ExchangeWsClient + cryptocom_frame_descriptor) over the
// real IxWsConnection, wrapped in a HeartbeatResponder so the mandatory keepalive
// is answered automatically (plan cryptocom-exchange-adapter §2 Option A). All channels are public.
//
// Usage:
//   cryptocom_ws_client_example ticker      <instrument> [--seconds N]
//   cryptocom_ws_client_example trade       <instrument> [--seconds N]
//   cryptocom_ws_client_example book        <instrument> [--depth 10] [--seconds N]
//   cryptocom_ws_client_example candlestick <instrument> [--timeframe 1m] [--seconds N]
//   cryptocom_ws_client_example multi       <instrument> [--seconds N]   # ticker + trade, one socket
//
// Tip: run with --seconds 35 to span a heartbeat cycle and watch the decorator
// keep the connection alive.
//
// Examples:
//   cryptocom_ws_client_example ticker BTC_USD
//   cryptocom_ws_client_example book BTC_USD --depth 10 --seconds 35

#include "exchange/cryptocom/ws.hpp"
#include "exchange/common/ix_ws_connection.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>

using namespace exchange::cryptocom::ws;
using exchange::ws::IxWsConnection;

namespace {

constexpr auto ACK_TIMEOUT = std::chrono::milliseconds{10000};
int stream_seconds = 12;

// Builds a heartbeat-wrapped market client over a fresh IxWsConnection and
// returns it together with the underlying connection (the caller drives connect).
std::pair<std::shared_ptr<CryptoComMarketClient>, std::shared_ptr<IxWsConnection>>
make_client() {
    // Override Host so ixwebsocket omits the ":443" Crypto.com rejects.
    auto conn   = std::make_shared<IxWsConnection>(
        std::string(MARKET_WS_URL), std::map<std::string, std::string>{{"Host", std::string(WS_HOST)}});
    auto client = make_cryptocom_market_client(
        conn, std::make_shared<RateLimitedWsErrorHandler>());
    return {client, conn};
}

// Subscribe one channel, stream for stream_seconds, then cancel. Returns false on
// a failed ack so main can exit non-zero.
template<typename Req, typename Callback>
bool stream_for_a_while(const std::string& what, Req req, Callback cb) {
    spdlog::info("=== {} subscription: {} ===", what, req.channel);

    auto [client, conn] = make_client();
    conn->connect();  // async; the SUBSCRIBE queues until on_open fires

    auto [ack, handle] = client->subscribe(std::move(req), std::move(cb), ACK_TIMEOUT);
    if (!ack.ok) {
        spdlog::error("subscription failed: {}", ack.error.value_or("unknown"));
        conn->disconnect();
        return false;
    }
    spdlog::info("subscribed — streaming for {} s (heartbeats answered automatically)",
                 stream_seconds);
    std::this_thread::sleep_for(std::chrono::seconds{stream_seconds});
    handle.cancel();
    conn->disconnect();
    return true;
}

bool run_ticker(const std::string& instrument) {
    CryptoComTickerSubscribe req;
    req.channel = ticker_channel(instrument);
    return stream_for_a_while("ticker", req, [](const CryptoComTickerEvent& e) {
        spdlog::info("[ticker] {} last={:.2f} bid={:.2f} ask={:.2f} vol24h={:.4f}",
                     e.instrument_name, e.last, e.bid, e.ask, e.volume);
    });
}

bool run_trade(const std::string& instrument) {
    CryptoComTradeSubscribe req;
    req.channel = trade_channel(instrument);
    return stream_for_a_while("trade", req, [](const CryptoComTradeEvent& e) {
        for (const auto& t : e.trades)
            spdlog::info("[trade] {} {} {:.2f} x {:.8f} @ {}", e.instrument_name,
                         t.side, t.price, t.quantity, t.timestamp);
    });
}

bool run_book(const std::string& instrument, int depth) {
    CryptoComBookSubscribe req;
    req.channel = book_channel(instrument, depth);
    return stream_for_a_while("book", req, [](const CryptoComBookEvent& e) {
        spdlog::info("[book] {} u={} bids={} asks={} top bid={:.2f} top ask={:.2f}",
                     e.instrument_name, e.u, e.bids.size(), e.asks.size(),
                     e.bids.empty() ? 0.0 : e.bids[0].price,
                     e.asks.empty() ? 0.0 : e.asks[0].price);
    });
}

bool run_candlestick(const std::string& instrument, const std::string& timeframe) {
    CryptoComCandlestickSubscribe req;
    req.channel = candlestick_channel(timeframe, instrument);
    return stream_for_a_while("candlestick", req, [](const CryptoComCandlestickEvent& e) {
        for (const auto& c : e.candles)
            spdlog::info("[candle] {} {} o={:.2f} h={:.2f} l={:.2f} c={:.2f} v={:.4f}",
                         e.instrument_name, e.interval, c.o, c.h, c.l, c.c, c.v);
    });
}

// multi — ticker + trade on ONE client/socket; cancels staggered to show reuse.
bool run_multi(const std::string& instrument) {
    spdlog::info("=== multi: ticker + trade on one socket ===");
    auto [client, conn] = make_client();
    conn->connect();

    CryptoComTickerSubscribe treq;
    treq.channel = ticker_channel(instrument);
    auto [tack, th] = client->subscribe(treq, [](const CryptoComTickerEvent& e) {
        spdlog::info("[ticker] {} last={:.2f}", e.instrument_name, e.last);
    }, ACK_TIMEOUT);
    if (!tack.ok) {
        spdlog::error("ticker subscription failed: {}", tack.error.value_or("unknown"));
        conn->disconnect();
        return false;
    }

    CryptoComTradeSubscribe dreq;
    dreq.channel = trade_channel(instrument);
    auto [dack, dh] = client->subscribe(dreq, [](const CryptoComTradeEvent& e) {
        for (const auto& t : e.trades)
            spdlog::info("[trade]  {} {} {:.2f} x {:.8f}", e.instrument_name, t.side,
                         t.price, t.quantity);
    }, ACK_TIMEOUT);
    if (!dack.ok) {
        spdlog::error("trade subscription failed: {}", dack.error.value_or("unknown"));
        th.cancel();
        conn->disconnect();
        return false;
    }

    spdlog::info("both channels live on one connection — {} s", stream_seconds);
    std::this_thread::sleep_for(std::chrono::seconds{stream_seconds});
    th.cancel();
    spdlog::info("ticker unsubscribed — trade continues 3 s");
    std::this_thread::sleep_for(std::chrono::seconds{3});
    dh.cancel();
    conn->disconnect();
    return true;
}

} // namespace

int main(int argc, char** argv) {
    try {
        CLI::App app{"Crypto.com Exchange market WebSocket demo"};
        app.require_subcommand(1);

        std::string instrument;
        std::string timeframe = "1m";
        int         depth     = 10;

        auto add = [&](const char* name, const char* desc) {
            auto* sub = app.add_subcommand(name, desc);
            sub->add_option("instrument", instrument, "e.g. BTC_USD")->required();
            sub->add_option("--seconds", stream_seconds, "Stream duration (default 12)");
            return sub;
        };
        auto* ticker = add("ticker", "Ticker channel");
        auto* trade  = add("trade", "Public trade channel");
        auto* book   = add("book", "Order book channel");
        book->add_option("--depth", depth, "Levels per side (default 10)");
        auto* candle = add("candlestick", "Candlestick channel");
        candle->add_option("--timeframe", timeframe, "e.g. 1m/5m/1h (default 1m)");
        auto* multi  = add("multi", "ticker + trade on one socket");

        CLI11_PARSE(app, argc, argv);

        bool ok = false;
        if (*ticker)        ok = run_ticker(instrument);
        else if (*trade)    ok = run_trade(instrument);
        else if (*book)     ok = run_book(instrument, depth);
        else if (*candle)   ok = run_candlestick(instrument, timeframe);
        else if (*multi)    ok = run_multi(instrument);

        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "fatal: unknown exception\n";
        return EXIT_FAILURE;
    }
}
