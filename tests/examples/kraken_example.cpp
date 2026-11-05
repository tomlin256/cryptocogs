// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Demonstrates REST and WebSocket API usage.
// Both layers share types from kraken_types.hpp.

#include "exchange/kraken/types.hpp"
#include "exchange/kraken/rest_api.hpp"
#include "exchange/kraken/ws_api.hpp"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <utility>

using json = nlohmann::json;

// ============================================================
// Hypothetical HTTP send helper (provide your own, e.g. libcurl)
// ============================================================
std::string http_send(const exchange::kraken::rest::HttpRequest& req) {
    // transport implementation not shown
    (void)req;
    return R"({"error":[],"result":{}})";
}

// ============================================================
// Hypothetical WebSocket send helper
// ============================================================
void ws_send(const json& msg, [[maybe_unused]] exchange::kraken::ws::KrakenWsCredentials& creds) {
    std::cout << "[WS SEND] " << msg << "\n";
}

int main(int argc, char* argv[]) {
    try {
        CLI::App app{"Kraken API usage demonstration — REST and WebSocket"};
        CLI11_PARSE(app, argc, argv);

        using namespace exchange::kraken;

        // --------------------------------------------------------
        // KrakenCredentials
        // --------------------------------------------------------
        rest::KrakenCredentials creds{
            .api_key    = "YOUR_API_KEY",
            .api_secret = "YOUR_BASE64_SECRET"
        };

        // WebSocket token is fetched from REST first:
        rest::GetWebSocketsTokenRequest tok_req;
        auto tok_http = tok_req.build(creds);
        // std::string tok_body = http_send(tok_http);
        // auto tok_result = parse_rest_response<rest::WebSocketsTokenResult>(json::parse(tok_body));
        // ws::KrakenWsCredentials ws_creds{ tok_result.result->token };

        // For this demo, use a placeholder:
        ws::KrakenWsCredentials ws_creds{ "my_ws_token" };

        // ============================================================
        // REST: Public endpoints
        // ============================================================
        {
            rest::GetServerTimeRequest req;
            auto http = req.build();
            std::cout << "GET " << http.path << "\n";
        }

        {
            rest::GetTickerRequest req;
            req.pairs = std::vector<std::string>{"XBTUSD", "ETHUSD"};
            auto http = req.build();
            std::cout << "GET " << http.path << "?" << http.query << "\n";

            // Parse response
            json resp = json::parse(R"({"error":[],"result":{"XXBTZUSD":{"a":["26490.00000","1","1.000"],"b":["26489.90000","1","1.000"],"c":["26490.00000","0.00123456"],"v":["1234.56789000","5678.90123456"],"p":["26450.12345","26450.23456"],"t":[12345,67890],"l":["26100.00000","26100.00000"],"h":["26900.00000","26900.00000"],"o":"26340.00000"}}})");
            auto result = parse_rest_response<rest::TickerResult>(resp);
            if (result.ok && result.result) {
                const auto& btc = result.result->tickers["XXBTZUSD"];
                std::cout << "BTC last=" << btc.last << " bid=" << btc.bid << " ask=" << btc.ask << "\n";
            }
        }

        {
            rest::GetOHLCRequest req;
            req.pair     = "XBTUSD";
            req.interval = 60;
            auto http = req.build();
            std::cout << "GET " << http.path << "?" << http.query << "\n";
        }

        // ============================================================
        // REST: Private – account data
        // ============================================================
        {
            rest::GetAccountBalanceRequest req;
            auto http = req.build(creds);
            std::cout << "POST " << http.path << " (signed)\n";
            std::cout << "  API-Key:  " << http.headers.at("API-Key") << "\n";
            std::cout << "  API-Sign: " << http.headers.at("API-Sign").substr(0, 16) << "...\n";

            // Parse response
            json resp = json::parse(R"({"error":[],"result":{"ZUSD":"12345.6789","XXBT":"0.50000000"}})");
            auto result = parse_rest_response<rest::AccountBalanceResult>(resp);
            if (result.ok && result.result) {
                for (const auto& [asset, bal] : result.result->balances)
                    std::cout << "  " << asset << " = " << bal << "\n";
            }
        }

        {
            rest::GetOpenOrdersRequest req;
            req.trades = true;
            auto http = req.build(creds);
            std::cout << "POST " << http.path << "\n";
        }

        // ============================================================
        // REST: Trading – place, amend, cancel
        // ============================================================
        {
            // Build shared OrderParams once; use for BOTH REST and WebSocket.
            OrderParams op;
            op.order_type  = OrderType::Limit;
            op.side        = Side::Buy;
            op.symbol      = "XBTUSD";     // REST pair name
            op.order_qty   = 1.2;
            op.limit_price = TickPrice::from(26500.0, 1);
            op.order_userref = 100054;

            // --- REST ---
            rest::AddOrderRequest rest_req;
            rest_req.params = op;
            auto http = rest_req.build(creds);
            std::cout << "POST " << http.path << " body=" << http.body.substr(0,60) << "...\n";

            // --- WebSocket: same params, just change the symbol to WS format ---
            op.symbol = "BTC/USD";
            ws::AddOrderRequest ws_req;
            ws_req.order_type = op.order_type;
            ws_req.req_id = 123456;
            ws_send(ws_req.to_json(), ws_creds);

            // Parse REST response
            json resp = json::parse(R"({"error":[],"result":{"descr":{"order":"buy 1.20000000 XBTUSD @ limit 26500.00000"},"txid":["OA5JGQ-SBMRC-SCJ7J7"]}})");
            auto result = parse_rest_response<rest::AddOrderResult>(resp);
            if (result.ok && result.result)
                std::cout << "REST order placed: " << result.result->txids[0] << "\n";
        }

        {
            // AmendOrder – same structure in both REST and WS
            // REST:
            rest::AmendOrderRequest rest_amend;
            rest_amend.txid        = "OA5JGQ-SBMRC-SCJ7J7";
            rest_amend.limit_price = 27000.0;
            auto http = rest_amend.build(creds);
            std::cout << "POST " << http.path << "\n";

            // WS:
            ws::AmendOrderRequest ws_amend;
            ws_amend.order_id    = "AA5JGQ-SBMRC-SCJ7J7";
            ws_amend.limit_price = TickPrice::from(27000.0, 1);
            ws_send(ws_amend.to_json(), ws_creds);
        }

        {
            // CancelOrder
            rest::CancelOrderRequest rest_cancel;
            rest_cancel.txid = "OA5JGQ-SBMRC-SCJ7J7";
            auto http = rest_cancel.build(creds);
            std::cout << "POST " << http.path << "\n";

            ws::CancelOrderRequest ws_cancel;
            ws_cancel.order_ids = std::vector<std::string>{"AA5JGQ-SBMRC-SCJ7J7"};
            ws_send(ws_cancel.to_json(), ws_creds);
        }

        {
            // CancelAll / CancelAllOrdersAfter (Dead-Man's Switch)
            rest::CancelAllOrdersAfterRequest dms;
            dms.timeout = 300; // 5 minutes
            auto http = dms.build(creds);
            std::cout << "POST " << http.path << " timeout=300\n";

            ws::CancelOnDisconnectRequest ws_dms;
            ws_dms.timeout = 300;
            ws_send(ws_dms.to_json(), ws_creds);
        }

        {
            // BatchAdd – build shared OrderParams vector, use for both REST and WS
            // (Built by assignment: a designated-initialiser list that leaves members out draws
            // -Wmissing-field-initializers from GCC, and -Wmissing-designated-field-initializers from clang 23.)
            auto limit_buy = [](double price) {
                OrderParams p;
                p.order_type  = OrderType::Limit;
                p.side        = Side::Buy;
                p.order_qty   = 0.5;
                p.limit_price = TickPrice::from(price, 1);
                return p;
            };
            std::vector<OrderParams> ops;
            ops.push_back(limit_buy(26000.0));
            ops.push_back(limit_buy(25500.0));

            // REST
            rest::AddOrderBatchRequest rest_batch;
            rest_batch.pair   = "XBTUSD";
            rest_batch.orders = ops;
            auto http = rest_batch.build(creds);
            std::cout << "POST " << http.path << "\n";

            // WS (symbol in WS format)
            for (auto& o : ops) o.symbol = "BTC/USD";
            ws::BatchAddRequest ws_batch;
            ws_batch.symbol = "BTC/USD";
            ws_batch.orders = std::move(ops);
            ws_send(ws_batch.to_json(), ws_creds);
        }

        // ============================================================
        // WebSocket: subscriptions
        // ============================================================

        // Public market data (no token needed)
        {
            ws::SubscribeRequest sub;
            sub.channel = ws::SubscribeChannel::Ticker;
            sub.symbols = std::vector<std::string>{"BTC/USD", "ETH/USD"};
            ws_send(sub.to_json(), ws_creds);
        }

        {
            ws::SubscribeRequest sub;
            sub.channel  = ws::SubscribeChannel::Book;
            sub.symbols  = std::vector<std::string>{"BTC/USD"};
            sub.depth    = 10;
            ws_send(sub.to_json(), ws_creds);
        }

        {
            ws::SubscribeRequest sub;
            sub.channel  = ws::SubscribeChannel::OHLC;
            sub.symbols  = std::vector<std::string>{"BTC/USD"};
            sub.interval = 60;
            ws_send(sub.to_json(), ws_creds);
        }

        // Private channels (token required)
        {
            ws::SubscribeRequest sub;
            sub.channel         = ws::SubscribeChannel::Executions;
            sub.snapshot        = true;
            sub.snapshot_trades = true;
            ws_send(sub.to_json(), ws_creds);
        }

        {
            ws::SubscribeRequest sub;
            sub.channel = ws::SubscribeChannel::Balances;
            ws_send(sub.to_json(), ws_creds);
        }

        // ============================================================
        // WebSocket: parsing incoming messages
        // ============================================================
        {
            json ticker_push = json::parse(R"({
            "channel": "ticker", "type": "update",
            "data": [{"symbol":"BTC/USD","bid":26490.0,"bid_qty":0.5,
                      "ask":26495.0,"ask_qty":1.2,"last":26492.5,
                      "volume":1234.56,"vwap":26450.0,"low":26100.0,
                      "high":26900.0,"change":150.0,"change_pct":0.57}]
        })");

            switch (ws::identify_message(ticker_push)) {
                case ws::MessageKind::Ticker: {
                    auto m = ws::TickerMessage::from_json(ticker_push);
                    std::cout << "Ticker " << m.data[0].symbol << " last=" << m.data[0].last << "\n";
                    break;
                }
                default: break;
            }
        }

        {
            json exec_push = json::parse(R"({
            "channel": "executions", "type": "update",
            "data": [{"exec_id":"EX1","exec_type":"filled","order_id":"ORD1",
                      "symbol":"BTC/USD","side":"buy","order_type":"limit",
                      "order_qty":1.2,"cum_qty":1.2,"leaves_qty":0.0,
                      "last_qty":1.2,"last_price":26492.5,"avg_price":26492.5,
                      "cost":31791.0,"order_status":"filled","timestamp":"2024-01-01T00:00:00Z"}]
        })");

            if (ws::identify_message(exec_push) == ws::MessageKind::Executions) {
                auto m = ws::ExecutionsMessage::from_json(exec_push);
                std::cout << "Execution: order=" << m.data[0].order_id
                          << " status=" << m.data[0].order_status << "\n";
            }
        }

        // ============================================================
        // Ping
        // ============================================================
        {
            ws::PingRequest ping;
            ping.req_id = 42;
            ws_send(ping.to_json(), ws_creds);
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "fatal: unknown exception\n";
        return EXIT_FAILURE;
    }
}
