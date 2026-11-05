// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Demonstrates the Binance WebSocket market streams via BinanceStreamClient
// (ExchangeWsClient + binance_stream_frame_descriptor) on the combined
// endpoint. All streams are public — no credentials required.
//
// Usage:
//   binance_ws_client_example aggtrade   <symbol>
//   binance_ws_client_example trade      <symbol>
//   binance_ws_client_example kline      <symbol> [--interval 1m]
//   binance_ws_client_example ticker     <symbol>
//   binance_ws_client_example miniticker <symbol>
//   binance_ws_client_example bookticker <symbol>
//   binance_ws_client_example depth      <symbol> [--levels <N>]
//   binance_ws_client_example multi      <symbol>
//
// Examples:
//   binance_ws_client_example aggtrade BTCUSDT
//   binance_ws_client_example kline BTCUSDT --interval 1m
//   binance_ws_client_example depth BTCUSDT --levels 5
//   binance_ws_client_example multi BTCUSDT   # aggTrade + bookTicker, one socket

#include "exchange/binance/ws_streams.hpp"
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

using namespace exchange::binance::ws;

namespace {

constexpr auto STREAM_SECONDS = std::chrono::seconds{10};
constexpr auto ACK_TIMEOUT    = std::chrono::milliseconds{10000};

std::shared_ptr<BinanceStreamClient> make_client() {
    return exchange::ws::make_exchange_ws_client(
        std::string(STREAM_URL), binance_stream_frame_descriptor);
}

// Subscribes, streams for STREAM_SECONDS, cancels. Returns false on a failed
// ack so main can exit non-zero.
template<typename Req, typename Callback>
bool stream_for_a_while(const std::string& what, Req req, Callback cb) {
    spdlog::info("=== {} subscription: {} ===", what, req.stream);

    auto client        = make_client();
    auto [ack, handle] = client->subscribe(std::move(req), std::move(cb), ACK_TIMEOUT);

    if (!ack.ok) {
        spdlog::error("Subscription failed: {}", ack.error.value_or("unknown"));
        return false;
    }
    spdlog::info("Subscribed — receiving push data for {} s",
                 STREAM_SECONDS.count());
    std::this_thread::sleep_for(STREAM_SECONDS);
    handle.cancel();
    spdlog::info("Unsubscribed");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-stream runners
// ─────────────────────────────────────────────────────────────────────────────

bool run_aggtrade(const std::string& symbol) {
    BinanceAggTradeSubscribe req;
    req.stream = agg_trade_stream(symbol);
    return stream_for_a_while("aggTrade", req, [](const BinanceAggTradeEvent& e) {
        spdlog::info("[aggTrade] {} id={} px={:.8f} qty={:.8f} maker={}",
                     e.symbol, e.agg_trade_id, e.price, e.qty, e.is_buyer_maker);
    });
}

bool run_trade(const std::string& symbol) {
    BinanceTradeSubscribe req;
    req.stream = trade_stream(symbol);
    return stream_for_a_while("trade", req, [](const BinanceTradeEvent& e) {
        spdlog::info("[trade] {} id={} px={:.8f} qty={:.8f} maker={}",
                     e.symbol, e.trade_id, e.price, e.qty, e.is_buyer_maker);
    });
}

bool run_kline(const std::string& symbol, const std::string& interval) {
    BinanceKlineSubscribe req;
    req.stream = kline_stream(symbol, interval);
    return stream_for_a_while("kline", req, [](const BinanceKlineEvent& e) {
        const auto& k = e.kline;
        spdlog::info("[kline] {} {} o={:.8f} h={:.8f} l={:.8f} c={:.8f} "
                     "v={:.4f} trades={} closed={}",
                     e.symbol, k.interval, k.open, k.high, k.low, k.close,
                     k.volume, k.num_trades, k.is_closed);
    });
}

bool run_ticker(const std::string& symbol) {
    BinanceTickerSubscribe req;
    req.stream = ticker_stream(symbol);
    return stream_for_a_while("24hrTicker", req, [](const BinanceTickerEvent& e) {
        spdlog::info("[ticker] {} last={:.8f} chg={:+.2f}% bid={:.8f} ask={:.8f} "
                     "vol={:.4f} trades={}",
                     e.symbol, e.last_price, e.price_change_pct, e.bid_price,
                     e.ask_price, e.volume, e.num_trades);
    });
}

bool run_miniticker(const std::string& symbol) {
    BinanceMiniTickerSubscribe req;
    req.stream = mini_ticker_stream(symbol);
    return stream_for_a_while("24hrMiniTicker", req, [](const BinanceMiniTickerEvent& e) {
        spdlog::info("[miniTicker] {} c={:.8f} o={:.8f} h={:.8f} l={:.8f} v={:.4f}",
                     e.symbol, e.close, e.open, e.high, e.low, e.volume);
    });
}

bool run_bookticker(const std::string& symbol) {
    BinanceBookTickerSubscribe req;
    req.stream = book_ticker_stream(symbol);
    return stream_for_a_while("bookTicker", req, [](const BinanceBookTickerEvent& e) {
        spdlog::info("[bookTicker] {} u={} bid={:.8f}x{:.4f} ask={:.8f}x{:.4f}",
                     e.symbol, e.update_id, e.bid_price, e.bid_qty,
                     e.ask_price, e.ask_qty);
    });
}

bool run_depth(const std::string& symbol, int levels) {
    if (levels > 0) {
        BinancePartialDepthSubscribe req;
        req.stream = partial_depth_stream(symbol, levels);
        return stream_for_a_while("partial depth", req, [levels](const BinancePartialDepth& d) {
            spdlog::info("[depth{}] lastUpdateId={} bids={} asks={} "
                         "top bid={:.8f}x{:.4f} top ask={:.8f}x{:.4f}",
                         levels, d.last_update_id, d.bids.size(), d.asks.size(),
                         d.bids.empty() ? 0.0 : d.bids[0].price,
                         d.bids.empty() ? 0.0 : d.bids[0].quantity,
                         d.asks.empty() ? 0.0 : d.asks[0].price,
                         d.asks.empty() ? 0.0 : d.asks[0].quantity);
        });
    }
    BinanceDepthSubscribe req;
    req.stream = depth_stream(symbol);
    return stream_for_a_while("diff depth", req, [](const BinanceDepthUpdateEvent& e) {
        spdlog::info("[depthUpdate] {} U={} u={} bids={} asks={}",
                     e.symbol, e.first_update_id, e.final_update_id,
                     e.bids.size(), e.asks.size());
    });
}

// multi — aggTrade + bookTicker on ONE client/socket: the connection-reuse
// model the unit suite pins with TwoStreamsOneConnection.
bool run_multi(const std::string& symbol) {
    spdlog::info("=== Multi-stream demo: aggTrade + bookTicker on one socket ===");

    auto client = make_client();

    BinanceAggTradeSubscribe agg_req;
    agg_req.stream = agg_trade_stream(symbol);
    auto [agg_ack, agg_handle] = client->subscribe(
        agg_req,
        [](const BinanceAggTradeEvent& e) {
            spdlog::info("[aggTrade] {} px={:.8f} qty={:.8f}",
                         e.symbol, e.price, e.qty);
        },
        ACK_TIMEOUT);
    if (!agg_ack.ok) {
        spdlog::error("aggTrade subscription failed: {}",
                      agg_ack.error.value_or("unknown"));
        return false;
    }

    BinanceBookTickerSubscribe bt_req;
    bt_req.stream = book_ticker_stream(symbol);
    auto [bt_ack, bt_handle] = client->subscribe(
        bt_req,
        [](const BinanceBookTickerEvent& e) {
            spdlog::info("[bookTicker] {} bid={:.8f} ask={:.8f}",
                         e.symbol, e.bid_price, e.ask_price);
        },
        ACK_TIMEOUT);
    if (!bt_ack.ok) {
        spdlog::error("bookTicker subscription failed: {}",
                      bt_ack.error.value_or("unknown"));
        agg_handle.cancel();
        return false;
    }

    spdlog::info("Both streams live on one connection — receiving for {} s",
                 STREAM_SECONDS.count());
    std::this_thread::sleep_for(STREAM_SECONDS);

    agg_handle.cancel();
    spdlog::info("aggTrade unsubscribed — bookTicker continues for 3 s");
    std::this_thread::sleep_for(std::chrono::seconds{3});
    bt_handle.cancel();
    spdlog::info("bookTicker unsubscribed");
    return true;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    try {
        CLI::App app{"Binance WebSocket market-stream demo (combined endpoint)"};
        app.require_subcommand(1);

        std::string symbol;
        std::string interval = "1m";
        int         levels   = 0;

        auto* aggtrade = app.add_subcommand("aggtrade", "Aggregated trade stream");
        aggtrade->add_option("symbol", symbol, "Symbol, e.g. BTCUSDT")->required();

        auto* trade = app.add_subcommand("trade", "Raw trade stream");
        trade->add_option("symbol", symbol, "Symbol, e.g. BTCUSDT")->required();

        auto* kline = app.add_subcommand("kline", "Kline/candlestick stream");
        kline->add_option("symbol", symbol, "Symbol, e.g. BTCUSDT")->required();
        kline->add_option("--interval", interval, "Kline interval (default 1m)");

        auto* ticker = app.add_subcommand("ticker", "Rolling 24 h ticker stream");
        ticker->add_option("symbol", symbol, "Symbol, e.g. BTCUSDT")->required();

        auto* miniticker = app.add_subcommand("miniticker", "Rolling 24 h mini-ticker stream");
        miniticker->add_option("symbol", symbol, "Symbol, e.g. BTCUSDT")->required();

        auto* bookticker = app.add_subcommand("bookticker", "Best bid/ask stream");
        bookticker->add_option("symbol", symbol, "Symbol, e.g. BTCUSDT")->required();

        auto* depth = app.add_subcommand("depth", "Order book depth stream");
        depth->add_option("symbol", symbol, "Symbol, e.g. BTCUSDT")->required();
        depth->add_option("--levels", levels,
                          "Partial book levels (5/10/20); omit for diff stream");

        auto* multi = app.add_subcommand("multi", "aggTrade + bookTicker on one socket");
        multi->add_option("symbol", symbol, "Symbol, e.g. BTCUSDT")->required();

        CLI11_PARSE(app, argc, argv);

        bool ok = false;
        if (*aggtrade)        ok = run_aggtrade(symbol);
        else if (*trade)      ok = run_trade(symbol);
        else if (*kline)      ok = run_kline(symbol, interval);
        else if (*ticker)     ok = run_ticker(symbol);
        else if (*miniticker) ok = run_miniticker(symbol);
        else if (*bookticker) ok = run_bookticker(symbol);
        else if (*depth)      ok = run_depth(symbol, levels);
        else if (*multi)      ok = run_multi(symbol);

        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "fatal: unknown exception\n";
        return EXIT_FAILURE;
    }
}
