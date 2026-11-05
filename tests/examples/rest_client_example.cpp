// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Demonstrates all public REST endpoints via KrakenRestClient.
//
// Usage:
//   rest_client_example time
//   rest_client_example status
//   rest_client_example assets      [--assets <XBT,ETH,...>]
//   rest_client_example pairs       [--pairs <XBTUSD,ETHUSD,...>]
//   rest_client_example ticker      [--pairs <XBTUSD,ETHUSD,...>]
//   rest_client_example ohlc        <pair> [--interval <N>] [--since <ts>]
//   rest_client_example depth       <pair> [--count <N>]
//   rest_client_example trades      <pair> [--since <ts>] [--count <N>]
//
// Examples:
//   rest_client_example time
//   rest_client_example assets      --assets XBT,ETH
//   rest_client_example pairs       --pairs XBTUSD,ETHUSD
//   rest_client_example ticker      --pairs XXBTZUSD,XETHZUSD
//   rest_client_example ohlc        XXBTZUSD --interval 60
//   rest_client_example depth       XXBTZUSD --count 10
//   rest_client_example trades      XXBTZUSD --count 5

#include "exchange/kraken/rest_client.hpp"
#include "exchange/kraken/rest_api.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace exchange::kraken::rest;

// ─────────────────────────────────────────────────────────────────────────────
// time
// ─────────────────────────────────────────────────────────────────────────────

static void run_time(KrakenRestClient& client) {
    spdlog::info("=== Server Time ===");

    auto resp = client.execute(GetServerTimeRequest{});
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    const auto& t = *resp.result;
    spdlog::info("  unixtime : {}", t.unixtime);
    spdlog::info("  rfc1123  : {}", t.rfc1123);
}

// ─────────────────────────────────────────────────────────────────────────────
// status
// ─────────────────────────────────────────────────────────────────────────────

static void run_status(KrakenRestClient& client) {
    spdlog::info("=== System Status ===");

    auto resp = client.execute(GetSystemStatusRequest{});
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    const auto& s = *resp.result;
    spdlog::info("  status    : {}", s.status);
    spdlog::info("  timestamp : {}", s.timestamp);
}

// ─────────────────────────────────────────────────────────────────────────────
// assets
// ─────────────────────────────────────────────────────────────────────────────

static void run_assets(KrakenRestClient& client,
                       const std::optional<std::vector<std::string>>& filter) {
    if (filter)
        spdlog::info("=== Asset Info ({}) ===",
                     [&]{ std::string s; for (auto& a : *filter) { if (!s.empty()) s+=','; s+=a; } return s; }());
    else
        spdlog::info("=== Asset Info (all) ===");

    GetAssetInfoRequest req;
    req.assets = filter;

    auto resp = client.execute(req);
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    spdlog::info("  {} asset(s) returned", resp.result->assets.size());
    for (const auto& [name, info] : resp.result->assets)
        spdlog::info("  {:>8}  altname={:<8}  class={:<10}  decimals={}/{}",
                     name, info.altname, info.aclass,
                     info.display_decimals, info.decimals);
}

// ─────────────────────────────────────────────────────────────────────────────
// pairs
// ─────────────────────────────────────────────────────────────────────────────

static void run_pairs(KrakenRestClient& client,
                      const std::optional<std::vector<std::string>>& filter) {
    if (filter)
        spdlog::info("=== Asset Pairs ({}) ===",
                     [&]{ std::string s; for (auto& p : *filter) { if (!s.empty()) s+=','; s+=p; } return s; }());
    else
        spdlog::info("=== Asset Pairs (all) ===");

    GetAssetPairsRequest req;
    req.pairs = filter;

    auto resp = client.execute(req);
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    spdlog::info("  {} pair(s) returned", resp.result->pairs.size());
    for (const auto& [name, info] : resp.result->pairs)
        spdlog::info("  {:>12}  wsname={:<12}  base={:<5}  quote={:<5}"
                     "  pair_dec={}  lot_dec={}  ordermin={:.8f}",
                     name, info.wsname, info.base, info.quote,
                     info.pair_decimals, info.lot_decimals, info.ordermin);
}

// ─────────────────────────────────────────────────────────────────────────────
// ticker
// ─────────────────────────────────────────────────────────────────────────────

static void run_ticker(KrakenRestClient& client,
                       const std::optional<std::vector<std::string>>& filter) {
    if (filter)
        spdlog::info("=== Ticker ({}) ===",
                     [&]{ std::string s; for (auto& p : *filter) { if (!s.empty()) s+=','; s+=p; } return s; }());
    else
        spdlog::info("=== Ticker (all) ===");

    GetTickerRequest req;
    req.pairs = filter;

    auto resp = client.execute(req);
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    spdlog::info("  {} ticker(s) returned", resp.result->tickers.size());
    for (const auto& [pair, t] : resp.result->tickers)
        spdlog::info("  {:>12}  bid={:.4f}  ask={:.4f}  last={:.4f}"
                     "  vol_24h={:.2f}  vwap_24h={:.4f}"
                     "  low_24h={:.4f}  high_24h={:.4f}  open={:.4f}",
                     pair, t.bid, t.ask, t.last,
                     t.volume_24h, t.vwap_24h,
                     t.low_24h, t.high_24h, t.open);
}

// ─────────────────────────────────────────────────────────────────────────────
// ohlc
// ─────────────────────────────────────────────────────────────────────────────

static void run_ohlc(KrakenRestClient& client,
                     const std::string& pair,
                     std::optional<int32_t> interval,
                     std::optional<int64_t> since) {
    spdlog::info("=== OHLC: {} interval={}m ===",
                 pair, interval.value_or(1));

    GetOHLCRequest req;
    req.pair     = pair;
    req.interval = interval;
    req.since    = since;

    auto resp = client.execute(req);
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    const auto& r = *resp.result;
    spdlog::info("  pair={} candles={} last={}",
                 r.pair, r.candles.size(), r.last);

    // Print the 5 most recent candles.
    std::size_t start = r.candles.size() > 5 ? r.candles.size() - 5 : 0;
    for (std::size_t i = start; i < r.candles.size(); ++i) {
        const auto& c = r.candles[i];
        spdlog::info("  ts={} O={:.4f} H={:.4f} L={:.4f} C={:.4f}"
                     " vwap={:.4f} vol={:.4f} trades={}",
                     c.time, c.open, c.high, c.low, c.close,
                     c.vwap, c.volume, c.count);
    }
    if (start > 0)
        spdlog::info("  (showing last 5 of {} candles)", r.candles.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// depth
// ─────────────────────────────────────────────────────────────────────────────

static void run_depth(KrakenRestClient& client,
                      const std::string& pair,
                      std::optional<int32_t> count) {
    spdlog::info("=== Order Book: {} count={} ===",
                 pair, count.value_or(10));

    GetOrderBookRequest req;
    req.pair  = pair;
    req.count = count;

    auto resp = client.execute(req);
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    const auto& r = *resp.result;
    spdlog::info("  pair={}  asks={}  bids={}", r.pair, r.asks.size(), r.bids.size());

    // Print top 5 asks and bids.
    std::size_t n = std::min<std::size_t>(5, r.asks.size());
    spdlog::info("  --- Top {} asks ---", n);
    for (std::size_t i = 0; i < n; ++i)
        spdlog::info("    ask[{}]  price={:.4f}  vol={:.6f}  ts={}",
                     i, r.asks[i].price, r.asks[i].volume, r.asks[i].timestamp);

    n = std::min<std::size_t>(5, r.bids.size());
    spdlog::info("  --- Top {} bids ---", n);
    for (std::size_t i = 0; i < n; ++i)
        spdlog::info("    bid[{}]  price={:.4f}  vol={:.6f}  ts={}",
                     i, r.bids[i].price, r.bids[i].volume, r.bids[i].timestamp);
}

// ─────────────────────────────────────────────────────────────────────────────
// trades
// ─────────────────────────────────────────────────────────────────────────────

static void run_trades(KrakenRestClient& client,
                       const std::string& pair,
                       std::optional<int64_t> since,
                       std::optional<int32_t> count) {
    spdlog::info("=== Recent Trades: {} ===", pair);

    GetRecentTradesRequest req;
    req.pair  = pair;
    req.since = since;
    req.count = count;

    auto resp = client.execute(req);
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    const auto& r = *resp.result;
    spdlog::info("  pair={}  trades={}  last={}", r.pair, r.trades.size(), r.last);

    for (const auto& t : r.trades)
        spdlog::info("  price={:.4f}  vol={:.6f}  time={:.3f}  side={}  type={}",
                     t.price, t.volume, t.time,
                     t.side == exchange::kraken::Side::Buy ? "buy" : "sell",
                     t.order_type == "l" ? "limit" : "market");
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    bool curl_ready = false;  // true once curl_global_init has run: the handlers below then clean up as the normal path does
    try {
        CLI::App app{"Kraken REST client demo — all public endpoints"};
        app.require_subcommand(1);

        // ── time ──────────────────────────────────────────────────────────────────
        auto* time_cmd = app.add_subcommand("time", "Server time (GET /0/public/Time)");

        // ── status ────────────────────────────────────────────────────────────────
        auto* status_cmd = app.add_subcommand("status", "System status (GET /0/public/SystemStatus)");

        // ── assets ────────────────────────────────────────────────────────────────
        auto* assets_cmd = app.add_subcommand("assets",
            "Asset info; omit --assets for all (GET /0/public/Assets)");
        std::vector<std::string> assets_filter;
        assets_cmd->add_option("--assets", assets_filter,
            "Comma-separated asset filter, e.g. XBT,ETH")
            ->delimiter(',');

        // ── pairs ─────────────────────────────────────────────────────────────────
        auto* pairs_cmd = app.add_subcommand("pairs",
            "Trading pair info; omit --pairs for all (GET /0/public/AssetPairs)");
        std::vector<std::string> pairs_filter;
        pairs_cmd->add_option("--pairs", pairs_filter,
            "Comma-separated pair filter, e.g. XBTUSD,ETHUSD")
            ->delimiter(',');

        // ── ticker ────────────────────────────────────────────────────────────────
        auto* ticker_cmd = app.add_subcommand("ticker",
            "Level 1 price data; omit --pairs for all (GET /0/public/Ticker)");
        std::vector<std::string> ticker_pairs;
        ticker_cmd->add_option("--pairs", ticker_pairs,
            "Comma-separated pair filter, e.g. XXBTZUSD,XETHZUSD")
            ->delimiter(',');

        // ── ohlc ──────────────────────────────────────────────────────────────────
        auto* ohlc_cmd = app.add_subcommand("ohlc",
            "OHLC candles; interval in minutes: 1|5|15|30|60|240|1440|10080|21600 "
            "(GET /0/public/OHLC)");
        std::string ohlc_pair;
        std::optional<int32_t> ohlc_interval;
        std::optional<int64_t> ohlc_since;
        ohlc_cmd->add_option("pair", ohlc_pair, "Trading pair (e.g. XXBTZUSD)")->required();
        ohlc_cmd->add_option("--interval", ohlc_interval,
            "Candle interval in minutes: 1|5|15|30|60|240|1440|10080|21600");
        ohlc_cmd->add_option("--since", ohlc_since,
            "Return committed OHLC data since given ID");

        // ── depth ─────────────────────────────────────────────────────────────────
        auto* depth_cmd = app.add_subcommand("depth",
            "Order book; count = 1..500 (GET /0/public/Depth)");
        std::string depth_pair;
        std::optional<int32_t> depth_count;
        depth_cmd->add_option("pair", depth_pair, "Trading pair (e.g. XXBTZUSD)")->required();
        depth_cmd->add_option("--count", depth_count,
            "Number of asks/bids to return: 1..500");

        // ── trades ────────────────────────────────────────────────────────────────
        auto* trades_cmd = app.add_subcommand("trades",
            "Recent public trades (GET /0/public/Trades)");
        std::string trades_pair;
        std::optional<int64_t> trades_since;
        std::optional<int32_t> trades_count;
        trades_cmd->add_option("pair", trades_pair, "Trading pair (e.g. XXBTZUSD)")->required();
        trades_cmd->add_option("--since", trades_since,
            "Return trade data since given timestamp");
        trades_cmd->add_option("--count", trades_count,
            "Maximum number of trades to return");

        CLI11_PARSE(app, argc, argv);

        curl_global_init(CURL_GLOBAL_ALL);
        curl_ready = true;
        KrakenRestClient client;

        try {
            if (time_cmd->parsed()) {
                run_time(client);

            } else if (status_cmd->parsed()) {
                run_status(client);

            } else if (assets_cmd->parsed()) {
                std::optional<std::vector<std::string>> filter =
                    assets_filter.empty() ? std::nullopt : std::make_optional(assets_filter);
                run_assets(client, filter);

            } else if (pairs_cmd->parsed()) {
                std::optional<std::vector<std::string>> filter =
                    pairs_filter.empty() ? std::nullopt : std::make_optional(pairs_filter);
                run_pairs(client, filter);

            } else if (ticker_cmd->parsed()) {
                std::optional<std::vector<std::string>> filter =
                    ticker_pairs.empty() ? std::nullopt : std::make_optional(ticker_pairs);
                run_ticker(client, filter);

            } else if (ohlc_cmd->parsed()) {
                run_ohlc(client, ohlc_pair, ohlc_interval, ohlc_since);

            } else if (depth_cmd->parsed()) {
                run_depth(client, depth_pair, depth_count);

            } else if (trades_cmd->parsed()) {
                run_trades(client, trades_pair, trades_since, trades_count);
            }
        } catch (const std::exception& e) {
            spdlog::error("Fatal: {}", e.what());
            curl_global_cleanup();
            return 1;
        }

        curl_global_cleanup();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        if (curl_ready) curl_global_cleanup();
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "fatal: unknown exception\n";
        if (curl_ready) curl_global_cleanup();
        return EXIT_FAILURE;
    }
}
