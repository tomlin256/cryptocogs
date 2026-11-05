// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Demonstrates all Binance Spot public REST endpoints via BinanceRestClient.
//
// Usage:
//   binance_rest_client_example ping
//   binance_rest_client_example time
//   binance_rest_client_example exchangeinfo
//   binance_rest_client_example ticker        [--symbol <S> | --symbols <S,S,...>]
//   binance_rest_client_example book <symbol> [--limit <N>]
//   binance_rest_client_example klines <symbol> --interval <I> [--start-time <ms>] [--end-time <ms>] [--limit <N>]
//   binance_rest_client_example trades <symbol> [--limit <N>]
//
// Examples:
//   binance_rest_client_example ping
//   binance_rest_client_example ticker --symbol BTCUSDT
//   binance_rest_client_example ticker --symbols BTCUSDT,ETHBTC
//   binance_rest_client_example book BTCUSDT --limit 10
//   binance_rest_client_example klines BTCUSDT --interval 1m --limit 5
//   binance_rest_client_example trades BTCUSDT --limit 5

#include "exchange/binance/rest_client.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace exchange::binance::rest;

// ─────────────────────────────────────────────────────────────────────────────
// ping
// ─────────────────────────────────────────────────────────────────────────────

static void run_ping(BinanceRestClient& client) {
    spdlog::info("=== Ping ===");

    auto resp = client.execute(BinancePingRequest{});
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    spdlog::info("  pong — connectivity OK");
}

// ─────────────────────────────────────────────────────────────────────────────
// time
// ─────────────────────────────────────────────────────────────────────────────

static void run_time(BinanceRestClient& client) {
    spdlog::info("=== Server Time ===");

    auto resp = client.execute(BinanceServerTimeRequest{});
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    spdlog::info("  serverTime : {} ms", resp.result->server_time);
}

// ─────────────────────────────────────────────────────────────────────────────
// exchangeinfo
// ─────────────────────────────────────────────────────────────────────────────

static void run_exchangeinfo(BinanceRestClient& client) {
    spdlog::info("=== Exchange Info ===");

    auto resp = client.execute(BinanceExchangeInfoRequest{});
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    const auto& info = *resp.result;
    spdlog::info("  timezone   : {}", info.timezone);
    spdlog::info("  serverTime : {} ms", info.server_time);
    spdlog::info("  {} symbol(s) returned", info.symbols.size());

    // Print the first 5 symbols.
    std::size_t n = std::min<std::size_t>(5, info.symbols.size());
    for (std::size_t i = 0; i < n; ++i) {
        const auto& s = info.symbols[i];
        spdlog::info("  {:>10}  status={:<8}  base={:<5} quote={:<5}"
                     "  spot={}  margin={}",
                     s.symbol, s.status, s.base_asset, s.quote_asset,
                     s.is_spot_trading_allowed, s.is_margin_trading_allowed);
    }
    if (info.symbols.size() > n)
        spdlog::info("  (showing first {} of {} symbols)", n, info.symbols.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// ticker
// ─────────────────────────────────────────────────────────────────────────────

static void run_ticker(BinanceRestClient& client,
                       const std::optional<std::string>& symbol,
                       const std::optional<std::vector<std::string>>& symbols) {
    spdlog::info("=== Ticker Price ===");

    BinanceTickerPriceRequest req;
    req.symbol  = symbol;
    req.symbols = symbols;

    auto resp = client.execute(req);
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    spdlog::info("  {} entry(ies) returned", resp.result->entries.size());
    // Print at most 10 entries (an unfiltered query returns 1000+).
    std::size_t n = std::min<std::size_t>(10, resp.result->entries.size());
    for (std::size_t i = 0; i < n; ++i) {
        const auto& e = resp.result->entries[i];
        spdlog::info("  {:>10}  price={:.8f}", e.symbol, e.price);
    }
    if (resp.result->entries.size() > n)
        spdlog::info("  (showing first {} of {} entries)",
                     n, resp.result->entries.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// book
// ─────────────────────────────────────────────────────────────────────────────

static void run_book(BinanceRestClient& client,
                     const std::string& symbol,
                     std::optional<int> limit) {
    spdlog::info("=== Order Book: {} ===", symbol);

    BinanceOrderBookRequest req;
    req.symbol = symbol;
    req.limit  = limit;

    auto resp = client.execute(req);
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    const auto& b = *resp.result;
    spdlog::info("  lastUpdateId={}  bids={}  asks={}",
                 b.last_update_id, b.bids.size(), b.asks.size());

    // Print top 5 asks and bids.
    std::size_t n = std::min<std::size_t>(5, b.asks.size());
    spdlog::info("  --- Top {} asks ---", n);
    for (std::size_t i = 0; i < n; ++i)
        spdlog::info("    ask[{}]  price={:.8f}  qty={:.8f}",
                     i, b.asks[i].price, b.asks[i].quantity);

    n = std::min<std::size_t>(5, b.bids.size());
    spdlog::info("  --- Top {} bids ---", n);
    for (std::size_t i = 0; i < n; ++i)
        spdlog::info("    bid[{}]  price={:.8f}  qty={:.8f}",
                     i, b.bids[i].price, b.bids[i].quantity);
}

// ─────────────────────────────────────────────────────────────────────────────
// klines
// ─────────────────────────────────────────────────────────────────────────────

static void run_klines(BinanceRestClient& client,
                       const std::string& symbol,
                       const std::string& interval,
                       std::optional<int64_t> start_time,
                       std::optional<int64_t> end_time,
                       std::optional<int> limit) {
    spdlog::info("=== Klines: {} interval={} ===", symbol, interval);

    BinanceKlinesRequest req;
    req.symbol     = symbol;
    req.interval   = interval;
    req.start_time = start_time;
    req.end_time   = end_time;
    req.limit      = limit;

    auto resp = client.execute(req);
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    const auto& r = *resp.result;
    spdlog::info("  {} kline(s) returned", r.klines.size());

    // Print the 5 most recent klines.
    std::size_t start = r.klines.size() > 5 ? r.klines.size() - 5 : 0;
    for (std::size_t i = start; i < r.klines.size(); ++i) {
        const auto& k = r.klines[i];
        spdlog::info("  open_ts={} O={:.8f} H={:.8f} L={:.8f} C={:.8f}"
                     " vol={:.4f} trades={}",
                     k.open_time, k.open, k.high, k.low, k.close,
                     k.volume, k.num_trades);
    }
    if (start > 0)
        spdlog::info("  (showing last 5 of {} klines)", r.klines.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// trades
// ─────────────────────────────────────────────────────────────────────────────

static void run_trades(BinanceRestClient& client,
                       const std::string& symbol,
                       std::optional<int> limit) {
    spdlog::info("=== Recent Trades: {} ===", symbol);

    BinanceRecentTradesRequest req;
    req.symbol = symbol;
    req.limit  = limit;

    auto resp = client.execute(req);
    if (!resp.ok || !resp.result) {
        for (const auto& e : resp.errors)
            spdlog::error("  error: {}", e);
        return;
    }

    const auto& r = *resp.result;
    spdlog::info("  {} trade(s) returned", r.trades.size());

    // Print the 5 most recent trades.
    std::size_t start = r.trades.size() > 5 ? r.trades.size() - 5 : 0;
    for (std::size_t i = start; i < r.trades.size(); ++i) {
        const auto& t = r.trades[i];
        spdlog::info("  id={}  price={:.8f}  qty={:.8f}  time={}  buyer_maker={}",
                     t.id, t.price, t.qty, t.time, t.is_buyer_maker);
    }
    if (start > 0)
        spdlog::info("  (showing last 5 of {} trades)", r.trades.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    bool curl_ready = false;  // true once curl_global_init has run: the handlers below then clean up as the normal path does
    try {
        CLI::App app{"Binance REST client demo — all public market-data endpoints"};
        app.require_subcommand(1);

        // ── ping ──────────────────────────────────────────────────────────────────
        auto* ping_cmd = app.add_subcommand("ping", "Connectivity check (GET /api/v3/ping)");

        // ── time ──────────────────────────────────────────────────────────────────
        auto* time_cmd = app.add_subcommand("time", "Server time (GET /api/v3/time)");

        // ── exchangeinfo ──────────────────────────────────────────────────────────
        auto* exchangeinfo_cmd = app.add_subcommand("exchangeinfo",
            "Exchange trading rules and symbol info (GET /api/v3/exchangeInfo)");

        // ── ticker ────────────────────────────────────────────────────────────────
        auto* ticker_cmd = app.add_subcommand("ticker",
            "Latest price; omit options for all symbols (GET /api/v3/ticker/price)");
        std::string ticker_symbol;
        std::vector<std::string> ticker_symbols;
        auto* ticker_symbol_opt = ticker_cmd->add_option("--symbol", ticker_symbol,
            "Single symbol, e.g. BTCUSDT");
        ticker_cmd->add_option("--symbols", ticker_symbols,
            "Comma-separated symbol list, e.g. BTCUSDT,ETHBTC")
            ->delimiter(',')
            ->excludes(ticker_symbol_opt);

        // ── book ──────────────────────────────────────────────────────────────────
        auto* book_cmd = app.add_subcommand("book",
            "Order book (GET /api/v3/depth)");
        std::string book_symbol;
        std::optional<int> book_limit;
        book_cmd->add_option("symbol", book_symbol, "Symbol (e.g. BTCUSDT)")->required();
        book_cmd->add_option("--limit", book_limit,
            "Number of price levels: 1..5000 (default 100)");

        // ── klines ────────────────────────────────────────────────────────────────
        auto* klines_cmd = app.add_subcommand("klines",
            "Candlestick data (GET /api/v3/klines)");
        std::string klines_symbol;
        std::string klines_interval;
        std::optional<int64_t> klines_start_time;
        std::optional<int64_t> klines_end_time;
        std::optional<int> klines_limit;
        klines_cmd->add_option("symbol", klines_symbol, "Symbol (e.g. BTCUSDT)")->required();
        klines_cmd->add_option("--interval", klines_interval,
            "Kline interval: 1m|5m|15m|1h|4h|1d|1w|1M ...")->required();
        klines_cmd->add_option("--start-time", klines_start_time,
            "Start time (Unix ms)");
        klines_cmd->add_option("--end-time", klines_end_time,
            "End time (Unix ms)");
        klines_cmd->add_option("--limit", klines_limit,
            "Number of klines: 1..1000 (default 500)");

        // ── trades ────────────────────────────────────────────────────────────────
        auto* trades_cmd = app.add_subcommand("trades",
            "Recent public trades (GET /api/v3/trades)");
        std::string trades_symbol;
        std::optional<int> trades_limit;
        trades_cmd->add_option("symbol", trades_symbol, "Symbol (e.g. BTCUSDT)")->required();
        trades_cmd->add_option("--limit", trades_limit,
            "Number of trades: 1..1000 (default 500)");

        CLI11_PARSE(app, argc, argv);

        curl_global_init(CURL_GLOBAL_ALL);
        curl_ready = true;

        try {
            BinanceRestClient client{"https://api.binance.com"};

            if (ping_cmd->parsed()) {
                run_ping(client);

            } else if (time_cmd->parsed()) {
                run_time(client);

            } else if (exchangeinfo_cmd->parsed()) {
                run_exchangeinfo(client);

            } else if (ticker_cmd->parsed()) {
                std::optional<std::string> symbol =
                    ticker_symbol.empty() ? std::nullopt : std::make_optional(ticker_symbol);
                std::optional<std::vector<std::string>> symbols =
                    ticker_symbols.empty() ? std::nullopt : std::make_optional(ticker_symbols);
                run_ticker(client, symbol, symbols);

            } else if (book_cmd->parsed()) {
                run_book(client, book_symbol, book_limit);

            } else if (klines_cmd->parsed()) {
                run_klines(client, klines_symbol, klines_interval,
                           klines_start_time, klines_end_time, klines_limit);

            } else if (trades_cmd->parsed()) {
                run_trades(client, trades_symbol, trades_limit);
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
