// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/cryptocom/ws.hpp
// Crypto.com Exchange v1 WebSocket — push event types, the subscribe scaffold,
// the user-channel auth request, the frame descriptor, and client factories for
// the market + user endpoints. Bodies are defined in src/cryptocom/ws.cpp.
//
// Namespace: exchange::cryptocom::ws
//
// Design (plan cryptocom-exchange-adapter §2, Option A): Crypto.com's WS is id-correlated like
// Binance/Kraken, so it reuses the generic exchange::ws::ExchangeWsClient
// unchanged. The one misfit — the mandatory heartbeat — is handled by the
// HeartbeatResponder IWsConnection decorator (heartbeat_connection.hpp), bound by
// the factories below; exchange_common is untouched.
//
// Wire model (pinned from live captures, plan cryptocom-exchange-adapter step 6):
//   - SUBSCRIBE: {"id":N,"method":"subscribe","params":{"channels":[C]}}.
//   - The ACK / initial frame echoes id N: {"id":N,"method":"subscribe","code":0,
//     ...} (sometimes with a snapshot in result, sometimes bare). It resolves
//     subscribe()'s future.
//   - Ongoing updates arrive as {"id":-1,"method":"subscribe","result":{
//     "subscription":C,"channel":...,"data":[...]}} and route to the callback by
//     result.subscription. (Crypto.com reuses method "subscribe" + id -1 for
//     updates; it does NOT use a "push" method.)
//   - The initial snapshot therefore arrives as the ACK, not the callback; the
//     callback receives every subsequent update. Channels resend full state, so
//     one channel per subscribe and canonical channel strings (the exact form the
//     server echoes — e.g. candlestick period "1m", not "M1") are required.
//
// This header does not include ix_ws_connection.hpp; for the real transport use
// the URL constants below with the conn-based factories over an IxWsConnection.

#include "exchange/cryptocom/auth.hpp"
#include "exchange/common/ws_client.hpp"

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace exchange::cryptocom::ws {

using json = nlohmann::json;

// ── Re-export common client types ────────────────────────────────────────────

using exchange::ws::IWsConnection;
using exchange::ws::IWsErrorHandler;
using exchange::ws::RateLimitedWsErrorHandler;
using exchange::ws::WsResponse;
using exchange::ws::SubscriptionHandle;
using exchange::ws::ExchangeWsClient;
using exchange::ws::MessageIdentifier;
using exchange::ws::WsRequestBase;
using exchange::ws::TypedWsRequest;
using exchange::ws::FrameDescriptor;
using exchange::ws::FrameKind;
using exchange::ws::BaseWsResponse;

using exchange::cryptocom::rest::CryptoComCredentials;

// ── Endpoint URL constants ────────────────────────────────────────────────────

inline constexpr std::string_view MARKET_WS_URL = "wss://stream.crypto.com/exchange/v1/market";
inline constexpr std::string_view USER_WS_URL   = "wss://stream.crypto.com/exchange/v1/user";

// Crypto.com's WS gateway rejects (HTTP 400) the default "Host: <host>:443"
// header ixwebsocket emits for wss. Construct the IxWsConnection with
// {{"Host", std::string(WS_HOST)}} so the port is omitted.
inline constexpr std::string_view WS_HOST = "stream.crypto.com";

// ── Channel-name helpers ──────────────────────────────────────────────────────
//
// Build the canonical channel string the server echoes in result.subscription
// (which the push route_key must match). Defined in src/cryptocom/ws.cpp.

std::string ticker_channel(const std::string& instrument);                       // ticker.<i>
std::string trade_channel(const std::string& instrument);                        // trade.<i>
std::string book_channel(const std::string& instrument, int depth);              // book.<i>.<depth>
std::string candlestick_channel(const std::string& period,
                                const std::string& instrument);                  // candlestick.<period>.<i>
std::string user_order_channel(const std::string& instrument);                   // user.order.<i>
inline constexpr std::string_view USER_BALANCE_CHANNEL = "user.balance";
// (user.trade is not modelled — private fills are available via REST get-trades
//  and via user.order fill updates; its WS row schema is left for a later plan.)

// ── Market push events (live-captured shapes) ─────────────────────────────────
//
// Each from_json takes the FULL frame and reads result / result.data. Monetary
// fields are JSON strings (std::stod); ids `d`/`m` are strings; ms timestamps are
// numbers. Ticker/book carry one row per frame; trade/candlestick carry a list.

struct CryptoComTickerEvent {
    std::string subscription;       // result.subscription, e.g. "ticker.BTC_USD"
    std::string instrument_name;    // result.instrument_name (or data.i)
    double      high{0.0};          // h
    double      low{0.0};           // l
    double      last{0.0};          // a
    double      change{0.0};        // c
    double      bid{0.0};           // b
    double      bid_size{0.0};      // bs
    double      ask{0.0};           // k
    double      ask_size{0.0};      // ks
    double      volume{0.0};        // v
    double      value{0.0};         // vv
    double      open_interest{0.0}; // oi
    int64_t     timestamp{0};       // t (ms)
    static CryptoComTickerEvent from_json(const json& frame);
};

struct CryptoComWsTrade {
    std::string trade_id;       // d
    int64_t     timestamp{0};   // t (ms)
    double      price{0.0};     // p
    double      quantity{0.0};  // q
    std::string side;           // s (raw — UPPERCASE on the WS feed)
    std::string match_id;       // m
    static CryptoComWsTrade from_json(const json& row);
};

struct CryptoComTradeEvent {
    std::string                   subscription;
    std::string                   instrument_name;
    std::vector<CryptoComWsTrade> trades;  // result.data[]
    static CryptoComTradeEvent from_json(const json& frame);
};

struct CryptoComWsBookLevel {
    double  price{0.0};
    double  size{0.0};
    int64_t num_orders{0};
    static CryptoComWsBookLevel from_json(const json& row);  // ["price","size","count"]
};

struct CryptoComBookEvent {
    std::string                       subscription;
    std::string                       instrument_name;
    int                               depth{0};
    int64_t                           t{0};  // ms
    int64_t                           u{0};  // update sequence
    std::vector<CryptoComWsBookLevel> bids;
    std::vector<CryptoComWsBookLevel> asks;
    static CryptoComBookEvent from_json(const json& frame);
};

struct CryptoComWsCandle {
    int64_t t{0};   // bucket start, ms
    int64_t ut{0};  // last update time, ms
    double  o{0.0};
    double  h{0.0};
    double  l{0.0};
    double  c{0.0};
    double  v{0.0};
    static CryptoComWsCandle from_json(const json& row);
};

struct CryptoComCandlestickEvent {
    std::string                    subscription;
    std::string                    instrument_name;
    std::string                    interval;  // result.interval, e.g. "1m"
    std::vector<CryptoComWsCandle> candles;   // result.data[]
    static CryptoComCandlestickEvent from_json(const json& frame);
};

// ── User push events (doc-derived; verified against synthetic fixtures) ───────
//
// The user feed requires public/auth first (see CryptoComAuthRequest). No live
// credentials are available, so these mirror the documented field names.

struct CryptoComOrderUpdate {
    std::string order_id;
    std::string client_oid;
    std::string instrument_name;
    std::string status;                // raw (e.g. "ACTIVE"/"FILLED")
    std::string side;                  // raw "BUY"/"SELL"
    std::string order_type;            // raw "LIMIT"/"MARKET"
    double      quantity{0.0};
    double      limit_price{0.0};
    double      cumulative_quantity{0.0};
    double      avg_price{0.0};
    int64_t     create_time{0};
    int64_t     update_time{0};
    static CryptoComOrderUpdate from_json(const json& row);
};

struct CryptoComUserOrderEvent {
    std::string                       subscription;
    std::vector<CryptoComOrderUpdate> orders;  // result.data[]
    static CryptoComUserOrderEvent from_json(const json& frame);
};

struct CryptoComBalanceUpdate {
    std::string instrument_name;
    double      total_available_balance{0.0};
    double      total_cash_balance{0.0};
    static CryptoComBalanceUpdate from_json(const json& row);
};

struct CryptoComUserBalanceEvent {
    std::string                         subscription;
    std::vector<CryptoComBalanceUpdate> balances;  // result.data[]
    static CryptoComUserBalanceEvent from_json(const json& frame);
};

// ── Subscribe ack ─────────────────────────────────────────────────────────────
//
// The id-echoing initial frame. success = (code == 0); derives BaseWsResponse so
// the generic client derives WsResponse::ok for free.
struct CryptoComSubscribeAck : BaseWsResponse {
    std::optional<int64_t> id;
    std::string            channel;       // e.g. "ticker"
    std::string            subscription;  // e.g. "ticker.BTC_USD" (empty for a bare ack)
    static CryptoComSubscribeAck from_json(const json& j);
};

// ── Subscribe request scaffold ────────────────────────────────────────────────
//
// Non-template base holds the channel + req_id slot and the wire methods the
// generic subscribe_async calls; the template only binds push/response types.
struct CryptoComSubscribeBase : WsRequestBase {
    std::string channel;  // canonical channel string, e.g. "ticker.BTC_USD"

    std::string route_key() const;        // == channel (matches result.subscription)
    json        to_json() const;          // {id, method:"subscribe", params:{channels:[channel]}}
    json        unsubscribe_json() const; // matching "unsubscribe" frame
};

template<typename PushMsg>
struct CryptoComSubscribeRequest : CryptoComSubscribeBase {
    using push_type     = PushMsg;
    using response_type = CryptoComSubscribeAck;
};

using CryptoComTickerSubscribe      = CryptoComSubscribeRequest<CryptoComTickerEvent>;
using CryptoComTradeSubscribe       = CryptoComSubscribeRequest<CryptoComTradeEvent>;
using CryptoComBookSubscribe        = CryptoComSubscribeRequest<CryptoComBookEvent>;
using CryptoComCandlestickSubscribe = CryptoComSubscribeRequest<CryptoComCandlestickEvent>;
using CryptoComUserOrderSubscribe   = CryptoComSubscribeRequest<CryptoComUserOrderEvent>;
using CryptoComUserBalanceSubscribe = CryptoComSubscribeRequest<CryptoComUserBalanceEvent>;

// ── User-channel authentication (public/auth) ─────────────────────────────────
//
// Sent over the USER endpoint via execute() before any user.* subscribe. Signs
// method + id + api_key + nonce (no params); the client assigns req_id, used as
// both the frame id and the signature id.

struct CryptoComAuthResponse : BaseWsResponse {
    std::optional<int64_t> id;
    static CryptoComAuthResponse from_json(const json& j);  // success = (code == 0)
};

struct CryptoComAuthRequest : TypedWsRequest<CryptoComAuthResponse> {
    CryptoComCredentials creds;
    json to_json() const;
};

// ── Frame descriptor (MessageIdentifier) ──────────────────────────────────────
//
// id == -1 → PushMessage routed by result.subscription; any other id-bearing
// frame (subscribe ack / public/auth) → MethodResponse by id. Heartbeats are
// handled by HeartbeatResponder and never reach here. Defined in ws.cpp.
FrameDescriptor cryptocom_frame_descriptor(const json& j);

// ── Client aliases and factories ──────────────────────────────────────────────
//
// CryptoComMarketClient / CryptoComUserClient are the generic ExchangeWsClient
// (same runtime type). The factories wrap conn in a HeartbeatResponder and bind
// cryptocom_frame_descriptor. For the real transport, build an IxWsConnection
// over MARKET_WS_URL / USER_WS_URL and pass it in. Defined in ws.cpp.

using CryptoComMarketClient = ExchangeWsClient;
using CryptoComUserClient   = ExchangeWsClient;

std::shared_ptr<CryptoComMarketClient>
make_cryptocom_market_client(std::shared_ptr<IWsConnection>   conn,
                             std::shared_ptr<IWsErrorHandler>  error_handler = nullptr);

std::shared_ptr<CryptoComUserClient>
make_cryptocom_user_client(std::shared_ptr<IWsConnection>   conn,
                           std::shared_ptr<IWsErrorHandler>  error_handler = nullptr);

} // namespace exchange::cryptocom::ws
