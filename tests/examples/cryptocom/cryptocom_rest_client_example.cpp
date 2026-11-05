// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// CLI demo of every public Crypto.com Exchange v1 REST endpoint via
// CryptoComRestClient. All endpoints here are public — no credentials required.
//
// Usage:
//   cryptocom_rest_client_example instruments
//   cryptocom_rest_client_example tickers      [<instrument>]
//   cryptocom_rest_client_example book         <instrument> [--depth N]
//   cryptocom_rest_client_example candlestick  <instrument> [--timeframe 1m]
//   cryptocom_rest_client_example trades       <instrument> [--count N]
//
// Examples:
//   cryptocom_rest_client_example tickers BTC_USD
//   cryptocom_rest_client_example book BTC_USD --depth 10
//   cryptocom_rest_client_example candlestick BTC_USD --timeframe 5m

#include "exchange/cryptocom/rest_client.hpp"

#include <CLI/CLI.hpp>
#include <curl/curl.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

using namespace exchange::cryptocom::rest;

namespace {

// Logs the error list of a failed response and returns nullptr; on success
// returns the result. Callers read the result only through that pointer
// (`const auto* const r = report(resp);`), and `return report(resp) != nullptr`
// forwards the success flag as a process exit code. An ok response with no
// result, which the client never produces, is logged as a failure too instead
// of being read.
template<typename Resp>
const Resp* report(const exchange::rest::RestResponse<Resp>& resp) {
    if (!resp.ok) {
        spdlog::error("request failed: {}",
                      resp.errors.empty() ? "unknown" : resp.errors.front());
        return nullptr;
    }
    if (!resp.result.has_value()) {
        spdlog::error("request failed: response has no result");
        return nullptr;
    }
    return &*resp.result;
}

} // namespace

int main(int argc, char** argv) {
    try {
        curl_global_init(CURL_GLOBAL_ALL);

        CLI::App app{"Crypto.com Exchange public REST demo"};
        app.require_subcommand(1);

        std::string instrument;
        std::string timeframe = "1m";
        int         depth     = 0;
        int         count     = 0;

        app.add_subcommand("instruments", "All tradable instruments");

        auto* tickers = app.add_subcommand("tickers", "Tickers (all, or one instrument)");
        tickers->add_option("instrument", instrument, "e.g. BTC_USD");

        auto* book = app.add_subcommand("book", "Order book");
        book->add_option("instrument", instrument, "e.g. BTC_USD")->required();
        book->add_option("--depth", depth, "Levels per side, e.g. 10");

        auto* candle = app.add_subcommand("candlestick", "OHLC candles");
        candle->add_option("instrument", instrument, "e.g. BTC_USD")->required();
        candle->add_option("--timeframe", timeframe, "e.g. 1m/5m/1h/1D (default 1m)");

        auto* trades = app.add_subcommand("trades", "Recent public trades");
        trades->add_option("instrument", instrument, "e.g. BTC_USD")->required();
        trades->add_option("--count", count, "Max trades to return");

        CLI11_PARSE(app, argc, argv);

        CryptoComRestClient client;
        bool ok = true;

        if (app.got_subcommand("instruments")) {
            const auto resp = client.execute(CryptoComInstrumentsRequest{});
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r) {
                spdlog::info("{} instruments", r->instruments.size());
                for (size_t i = 0; i < r->instruments.size() && i < 5; ++i) {
                    const auto& in = r->instruments[i];
                    spdlog::info("  {} ({}/{}) type={} tradable={}", in.symbol, in.base_ccy,
                                 in.quote_ccy, in.inst_type, in.tradable);
                }
            }

        } else if (*tickers) {
            CryptoComTickersRequest req;
            if (!instrument.empty()) req.instrument_name = instrument;
            const auto resp = client.execute(req);
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r) {
                spdlog::info("{} tickers", r->tickers.size());
                for (size_t i = 0; i < r->tickers.size() && i < 5; ++i) {
                    const auto& t = r->tickers[i];
                    spdlog::info("  {}: last={:.2f} bid={:.2f} ask={:.2f} vol24h={:.4f}",
                                 t.instrument_name, t.last, t.bid, t.ask, t.volume);
                }
            }

        } else if (*book) {
            CryptoComOrderBookRequest req;
            req.instrument_name = instrument;
            if (depth > 0) req.depth = depth;
            const auto resp = client.execute(req);
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r) {
                spdlog::info("{}: depth={} bids={} asks={}", r->instrument_name,
                             r->depth, r->bids.size(),
                             r->asks.size());
                if (!r->bids.empty() && !r->asks.empty())
                    spdlog::info("  top bid {:.2f} x {:.8f} | top ask {:.2f} x {:.8f}",
                                 r->bids[0].price, r->bids[0].size,
                                 r->asks[0].price, r->asks[0].size);
            }

        } else if (*candle) {
            CryptoComCandlesRequest req;
            req.instrument_name = instrument;
            req.timeframe       = timeframe;
            const auto resp = client.execute(req);
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r) {
                spdlog::info("{} {}: {} candles", r->instrument_name,
                             r->interval, r->candles.size());
                if (!r->candles.empty()) {
                    const auto& c = r->candles.back();
                    spdlog::info("  t={} o={:.2f} h={:.2f} l={:.2f} c={:.2f} v={:.4f}",
                                 c.t, c.o, c.h, c.l, c.c, c.v);
                }
            }

        } else if (*trades) {
            CryptoComTradesRequest req;
            req.instrument_name = instrument;
            if (count > 0) req.count = count;
            const auto resp = client.execute(req);
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r) {
                spdlog::info("{} trades", r->trades.size());
                for (size_t i = 0; i < r->trades.size() && i < 5; ++i) {
                    const auto& t = r->trades[i];
                    spdlog::info("  {} {:.2f} x {:.8f} @ {}", t.side, t.price, t.quantity,
                                 t.timestamp);
                }
            }
        }

        curl_global_cleanup();
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        curl_global_cleanup();
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "fatal: unknown exception\n";
        curl_global_cleanup();
        return EXIT_FAILURE;
    }
}
