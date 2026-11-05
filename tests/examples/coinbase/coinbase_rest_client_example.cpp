// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// CLI demo of every public Coinbase Exchange REST endpoint via CoinbaseRestClient.
// All endpoints here are public — no credentials required.
//
// Usage:
//   coinbase_rest_client_example time
//   coinbase_rest_client_example products
//   coinbase_rest_client_example product  <product-id>
//   coinbase_rest_client_example book     <product-id> [--level N]
//   coinbase_rest_client_example ticker   <product-id>
//   coinbase_rest_client_example trades   <product-id> [--limit N]
//   coinbase_rest_client_example candles  <product-id> [--granularity S]
//   coinbase_rest_client_example stats    <product-id>
//
// Examples:
//   coinbase_rest_client_example ticker BTC-USD
//   coinbase_rest_client_example book BTC-USD --level 2
//   coinbase_rest_client_example candles BTC-USD --granularity 3600

#include "exchange/coinbase/rest_client.hpp"

#include <CLI/CLI.hpp>
#include <curl/curl.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

using namespace exchange::coinbase::rest;

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

        CLI::App app{"Coinbase Exchange public REST demo"};
        app.require_subcommand(1);

        std::string product_id;
        int         level       = 0;
        int         limit       = 0;
        int         granularity = 0;

        app.add_subcommand("time", "Server time");
        app.add_subcommand("products", "All tradable products");

        auto* product = app.add_subcommand("product", "One product");
        product->add_option("product_id", product_id, "e.g. BTC-USD")->required();

        auto* book = app.add_subcommand("book", "Order book");
        book->add_option("product_id", product_id, "e.g. BTC-USD")->required();
        book->add_option("--level", level, "1 (best) or 2 (top 50 aggregated)");

        auto* ticker = app.add_subcommand("ticker", "Best bid/ask + last trade");
        ticker->add_option("product_id", product_id, "e.g. BTC-USD")->required();

        auto* trades = app.add_subcommand("trades", "Recent trades");
        trades->add_option("product_id", product_id, "e.g. BTC-USD")->required();
        trades->add_option("--limit", limit, "Max trades to return");

        auto* candles = app.add_subcommand("candles", "Historic OHLC candles");
        candles->add_option("product_id", product_id, "e.g. BTC-USD")->required();
        candles->add_option("--granularity", granularity,
                            "Seconds: 60/300/900/3600/21600/86400");

        auto* stats = app.add_subcommand("stats", "24h stats");
        stats->add_option("product_id", product_id, "e.g. BTC-USD")->required();

        CLI11_PARSE(app, argc, argv);

        CoinbaseRestClient client;
        bool ok = true;

        if (app.got_subcommand("time")) {
            const auto resp = client.execute(CoinbaseServerTimeRequest{});
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r)
                spdlog::info("server time: {} (epoch {:.3f})", r->iso, r->epoch);

        } else if (app.got_subcommand("products")) {
            const auto resp = client.execute(CoinbaseProductsRequest{});
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r) {
                spdlog::info("{} products", r->products.size());
                for (size_t i = 0; i < r->products.size() && i < 5; ++i) {
                    const auto& p = r->products[i];
                    spdlog::info("  {} ({}/{}) status={}", p.id, p.base_currency,
                                 p.quote_currency, p.status);
                }
            }

        } else if (*product) {
            CoinbaseProductRequest req;
            req.product_id = product_id;
            const auto resp = client.execute(req);
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r)
                spdlog::info("{}: base_inc={} quote_inc={} status={}", r->id,
                             r->base_increment, r->quote_increment,
                             r->status);

        } else if (*book) {
            CoinbaseOrderBookRequest req;
            req.product_id = product_id;
            if (level > 0) req.level = level;
            const auto resp = client.execute(req);
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r) {
                spdlog::info("seq={} bids={} asks={}", r->sequence,
                             r->bids.size(), r->asks.size());
                if (!r->bids.empty() && !r->asks.empty())
                    spdlog::info("  top bid {:.2f} x {:.8f} | top ask {:.2f} x {:.8f}",
                                 r->bids[0].price, r->bids[0].size,
                                 r->asks[0].price, r->asks[0].size);
            }

        } else if (*ticker) {
            CoinbaseTickerRequest req;
            req.product_id = product_id;
            const auto resp = client.execute(req);
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r)
                spdlog::info("{}: price={:.2f} bid={:.2f} ask={:.2f} vol={:.4f} time={}",
                             product_id, r->price, r->bid,
                             r->ask, r->volume, r->time);

        } else if (*trades) {
            CoinbaseTradesRequest req;
            req.product_id = product_id;
            if (limit > 0) req.limit = limit;
            const auto resp = client.execute(req);
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r) {
                spdlog::info("{} trades", r->trades.size());
                for (size_t i = 0; i < r->trades.size() && i < 5; ++i) {
                    const auto& t = r->trades[i];
                    spdlog::info("  id={} {} {:.2f} x {:.8f} @ {}", t.trade_id, t.side,
                                 t.price, t.size, t.time);
                }
            }

        } else if (*candles) {
            CoinbaseCandlesRequest req;
            req.product_id = product_id;
            if (granularity > 0) req.granularity = granularity;
            const auto resp = client.execute(req);
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r) {
                spdlog::info("{} candles", r->candles.size());
                if (!r->candles.empty()) {
                    const auto& c = r->candles[0];
                    spdlog::info("  t={} o={:.2f} h={:.2f} l={:.2f} c={:.2f} v={:.4f}",
                                 c.time, c.open, c.high, c.low, c.close, c.volume);
                }
            }

        } else if (*stats) {
            CoinbaseStatsRequest req;
            req.product_id = product_id;
            const auto resp = client.execute(req);
            const auto* const r = report(resp);
            ok = (r != nullptr);
            if (r)
                spdlog::info("{}: open={:.2f} high={:.2f} low={:.2f} last={:.2f} vol={:.4f}",
                             product_id, r->open, r->high,
                             r->low, r->last, r->volume);
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
