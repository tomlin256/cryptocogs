// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Low-level public WebSocket example using raw ixwebsocket + kraken_ws_api.hpp.
// Supports all public channels. Use --dump-json to capture raw frames as
// test fixtures.
//
// Usage:
//   public_ws [--dump-json] ticker    <symbol>
//   public_ws [--dump-json] book      <symbol> [--depth <N>]
//   public_ws [--dump-json] trade     <symbol>
//   public_ws [--dump-json] ohlc      <symbol> [--interval <N>]
//   public_ws [--dump-json] instrument

#include <ixwebsocket/IXWebSocket.h>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "exchange/kraken/ws_api.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Message handler — dispatches to the correct typed from_json
// ─────────────────────────────────────────────────────────────────────────────

static void handle_message(const json& j, bool dump_json)
{
    if (dump_json)
        spdlog::info("[raw] {}", j.dump());

    switch (exchange::kraken::ws::identify_message(j))
    {
        case exchange::kraken::ws::MessageKind::SubscribeResponse:
        {
            auto r = exchange::kraken::ws::SubscribeResponse::from_json(j);
            if (r.success)
                spdlog::info("[subscribe] channel={} OK", r.channel.value_or("?"));
            else
                spdlog::error("[subscribe] FAILED: {}", r.error.value_or("unknown"));
            break;
        }
        case exchange::kraken::ws::MessageKind::Ticker:
        {
            auto m = exchange::kraken::ws::TickerMessage::from_json(j);
            for (const auto& d : m.data)
                spdlog::info("[ticker/{}] {} bid={:.4f} ask={:.4f} last={:.4f} "
                             "vol={:.4f} chg={:+.2f}%",
                             m.type, d.symbol, d.bid, d.ask, d.last,
                             d.volume, d.change_pct);
            break;
        }
        case exchange::kraken::ws::MessageKind::Book:
        {
            auto m = exchange::kraken::ws::BookMessage::from_json(j);
            for (const auto& d : m.data)
            {
                spdlog::info("[book/{}] {} bids={} asks={}",
                             m.type, d.symbol, d.bids.size(), d.asks.size());
                if (m.type == "snapshot")
                {
                    std::size_t n = std::min<std::size_t>(3, d.bids.size());
                    for (std::size_t i = 0; i < n; ++i)
                        spdlog::info("  bid[{}] price={:.4f} qty={:.6f}",
                                     i, d.bids[i].price, d.bids[i].qty);
                    n = std::min<std::size_t>(3, d.asks.size());
                    for (std::size_t i = 0; i < n; ++i)
                        spdlog::info("  ask[{}] price={:.4f} qty={:.6f}",
                                     i, d.asks[i].price, d.asks[i].qty);
                }
            }
            break;
        }
        case exchange::kraken::ws::MessageKind::Trade:
        {
            auto m = exchange::kraken::ws::TradeMessage::from_json(j);
            for (const auto& d : m.data)
                spdlog::info("[trade/{}] {} price={:.4f} qty={:.6f} side={} "
                             "type={} id={}",
                             m.type, d.symbol, d.price, d.qty,
                             d.side, d.ord_type, d.trade_id);
            break;
        }
        case exchange::kraken::ws::MessageKind::OHLC:
        {
            auto m = exchange::kraken::ws::OHLCMessage::from_json(j);
            for (const auto& d : m.data)
                spdlog::info("[ohlc/{}] {} ts={} O={:.4f} H={:.4f} L={:.4f} "
                             "C={:.4f} vol={:.4f} trades={}",
                             m.type, d.symbol, d.timestamp,
                             d.open, d.high, d.low, d.close,
                             d.volume, d.trades);
            break;
        }
        case exchange::kraken::ws::MessageKind::Instrument:
        {
            auto m = exchange::kraken::ws::InstrumentMessage::from_json(j);
            spdlog::info("[instrument/{}] {} asset(s), {} pair(s)",
                         m.type, m.assets.size(), m.pairs.size());
            std::size_t n = std::min<std::size_t>(5, m.pairs.size());
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto& p = m.pairs[i];
                spdlog::info("  {} ({}/{}) status={} qty_min={:.8f} "
                             "price_inc={:.8f}",
                             p.symbol, p.base, p.quote,
                             p.status, p.qty_min, p.price_increment);
            }
            if (m.pairs.size() > n)
                spdlog::info("  … and {} more pairs", m.pairs.size() - n);
            break;
        }
        case exchange::kraken::ws::MessageKind::Status:
        {
            auto m = exchange::kraken::ws::StatusMessage::from_json(j);
            spdlog::info("[status/{}] system={} version={}",
                         m.type, m.system, m.version);
            break;
        }
        case exchange::kraken::ws::MessageKind::Heartbeat:
            spdlog::debug("[heartbeat]");
            break;
        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    try {
        CLI::App app{"Kraken public WebSocket example — all public channels"};
        app.require_subcommand(1);

        bool dump_json = false;
        app.add_flag("--dump-json", dump_json,
            "Print raw JSON for every inbound frame (useful for capturing test fixtures)");

        // ── ticker ────────────────────────────────────────────────────────────────
        auto* ticker_cmd = app.add_subcommand("ticker",
            "Level 1 best bid/ask, last price, 24 h stats");
        std::string ticker_symbol;
        ticker_cmd->add_option("symbol", ticker_symbol,
            "Trading symbol (e.g. BTC/USD)")->required();

        // ── book ──────────────────────────────────────────────────────────────────
        auto* book_cmd = app.add_subcommand("book",
            "Level 2 order book; depth = 10|25|100|500|1000 (default 10)");
        std::string book_symbol;
        std::optional<int> book_depth;
        book_cmd->add_option("symbol", book_symbol,
            "Trading symbol (e.g. BTC/USD)")->required();
        book_cmd->add_option("--depth", book_depth,
            "Order book depth: 10|25|100|500|1000");

        // ── trade ─────────────────────────────────────────────────────────────────
        auto* trade_cmd = app.add_subcommand("trade", "Public trades feed");
        std::string trade_symbol;
        trade_cmd->add_option("symbol", trade_symbol,
            "Trading symbol (e.g. BTC/USD)")->required();

        // ── ohlc ──────────────────────────────────────────────────────────────────
        auto* ohlc_cmd = app.add_subcommand("ohlc",
            "OHLC candles; interval in minutes: "
            "1|5|15|30|60|240|1440|10080|21600 (default 1)");
        std::string ohlc_symbol;
        std::optional<int> ohlc_interval;
        ohlc_cmd->add_option("symbol", ohlc_symbol,
            "Trading symbol (e.g. BTC/USD)")->required();
        ohlc_cmd->add_option("--interval", ohlc_interval,
            "Candle interval in minutes: 1|5|15|30|60|240|1440|10080|21600");

        // ── instrument ────────────────────────────────────────────────────────────
        app.add_subcommand("instrument",
            "Static instrument/market info (no symbol required)");

        CLI11_PARSE(app, argc, argv);

        // ── Build subscribe request ───────────────────────────────────────────────

        exchange::kraken::ws::SubscribeRequest sub_req;

        if (ticker_cmd->parsed()) {
            sub_req.channel = exchange::kraken::ws::SubscribeChannel::Ticker;
            sub_req.symbols = std::vector<std::string>{ticker_symbol};
        } else if (book_cmd->parsed()) {
            sub_req.channel = exchange::kraken::ws::SubscribeChannel::Book;
            sub_req.symbols = std::vector<std::string>{book_symbol};
            if (book_depth) sub_req.depth = book_depth;
        } else if (trade_cmd->parsed()) {
            sub_req.channel = exchange::kraken::ws::SubscribeChannel::Trade;
            sub_req.symbols = std::vector<std::string>{trade_symbol};
        } else if (ohlc_cmd->parsed()) {
            sub_req.channel = exchange::kraken::ws::SubscribeChannel::OHLC;
            sub_req.symbols = std::vector<std::string>{ohlc_symbol};
            if (ohlc_interval) sub_req.interval = ohlc_interval;
        } else {
            sub_req.channel = exchange::kraken::ws::SubscribeChannel::Instrument;
        }

        const std::string sub_json = sub_req.to_json().dump();

        // ── WebSocket ─────────────────────────────────────────────────────────────

        ix::WebSocket ws;
        ws.setUrl("wss://ws.kraken.com/v2");

        ws.setOnMessageCallback(
            [&](const ix::WebSocketMessagePtr& msg)
            {
                if (msg->type == ix::WebSocketMessageType::Open)
                {
                    spdlog::info("ws opened");
                    ws.send(sub_json);
                }
                else if (msg->type == ix::WebSocketMessageType::Close)
                {
                    spdlog::info("ws closed");
                }
                else if (msg->type == ix::WebSocketMessageType::Error)
                {
                    spdlog::error("ws error: {}", msg->errorInfo.reason);
                }
                else if (msg->type == ix::WebSocketMessageType::Message)
                {
                    try {
                        handle_message(json::parse(msg->str), dump_json);
                    } catch (const std::exception& e) {
                        spdlog::error("parse error: {}", e.what());
                    }
                }
            }
        );

        ws.start();
        std::this_thread::sleep_for(std::chrono::seconds{15});
        ws.stop();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "fatal: unknown exception\n";
        return EXIT_FAILURE;
    }
}
