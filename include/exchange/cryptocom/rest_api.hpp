// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/cryptocom/rest_api.hpp
// Crypto.com Exchange v1 REST API — request bases and endpoint request/response
// types. Bodies are defined in src/cryptocom/rest_api.cpp; the response envelope
// helper parse_cryptocom_response<T> lives in exchange/cryptocom/types.hpp.
//
// Namespace: exchange::cryptocom::rest
//
// Crypto.com wraps every response as {id, method, code, result}; the client
// hands the inner `result` value to each T::from_json (parse_cryptocom_response
// unwraps it). Most list endpoints nest their rows under result.data.
//
// Wire-format notes (pinned from live captures, plan cryptocom-exchange-adapter step 3):
//   - Monetary / size / price fields arrive as JSON *strings* → std::stod.
//   - Millisecond timestamps arrive as JSON *numbers* → int64.
//   - Order-book rows are positional ["price","size","num_orders"] string arrays.
//   - Tickers use terse single-letter keys (i/h/l/a/v/vv/c/b/k/oi/t).
//
// Public GET requests need no signing (build() sets only method/path/query).
// Private requests build the signed JSON envelope in
// build(const CryptoComCredentials&) — Kraken-style, since the `sig` lives in
// the body and depends on the params. Every signed param value is a JSON
// *string* (the API requires it; see auth.hpp), so the params stay flat
// scalars — except CryptoComCreateOrderRequest::exec_inst, a JSON array of
// strings; see plan cryptocom-create-order-exec-inst for why params_to_str's array-recursion path
// serialises it unambiguously.

#include "exchange/cryptocom/auth.hpp"
#include "exchange/cryptocom/types.hpp"
#include "exchange/common/rest.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace exchange::cryptocom::rest {

using json = nlohmann::json;
using exchange::rest::HttpRequest;

// Path prefix shared by every Crypto.com v1 REST endpoint.
inline constexpr const char* API_PREFIX = "/exchange/v1/";

// ── Request base classes ──────────────────────────────────────────────────────

// Marker base for Crypto.com public requests (no authentication).
struct PublicRequest {
    virtual ~PublicRequest() = default;
    virtual HttpRequest build() const = 0;
};

// Typed wrapper — links each request type to its response type at compile time.
template<typename R>
struct TypedPublicRequest : PublicRequest {
    using response_type = R;
};

// ═════════════════════════════════════════════════════════════════════════════
// Public (unauthenticated) endpoints
// ═════════════════════════════════════════════════════════════════════════════

// ── public/get-instruments ───────────────────────────────────────────────────

struct CryptoComInstrument {
    std::string symbol;          // "BTC_USD" (spot), "BTCUSD-PERP" (perp)
    std::string inst_type;       // "CCY_PAIR" (spot), "PERPETUAL_SWAP", ...
    std::string display_name;
    std::string base_ccy;
    std::string quote_ccy;
    int         quote_decimals{0};
    int         quantity_decimals{0};
    double      price_tick_size{0.0};
    double      qty_tick_size{0.0};
    bool        tradable{false};
    static CryptoComInstrument from_json(const json& j);
};

struct CryptoComInstrumentsResult {
    std::vector<CryptoComInstrument> instruments;  // result.data[]
    static CryptoComInstrumentsResult from_json(const json& j);
};

struct CryptoComInstrumentsRequest : TypedPublicRequest<CryptoComInstrumentsResult> {
    HttpRequest build() const override;
};

// ── public/get-tickers ────────────────────────────────────────────────────────

struct CryptoComTicker {
    std::string instrument_name;   // i
    double      high{0.0};         // h  (24h high)
    double      low{0.0};          // l  (24h low)
    double      last{0.0};         // a  (latest trade price)
    double      volume{0.0};       // v  (24h base volume)
    double      value{0.0};        // vv (24h quote value)
    double      change{0.0};       // c  (24h price change)
    double      bid{0.0};          // b  (best bid)
    double      ask{0.0};          // k  (best ask)
    double      open_interest{0.0};// oi
    int64_t     timestamp{0};      // t  (ms)
    static CryptoComTicker from_json(const json& j);
};

struct CryptoComTickersResult {
    std::vector<CryptoComTicker> tickers;  // result.data[]
    static CryptoComTickersResult from_json(const json& j);
};

struct CryptoComTickersRequest : TypedPublicRequest<CryptoComTickersResult> {
    std::optional<std::string> instrument_name;  // omit → every instrument
    HttpRequest build() const override;
};

// ── public/get-book ───────────────────────────────────────────────────────────

struct CryptoComBookLevel {
    double  price{0.0};
    double  size{0.0};
    int64_t num_orders{0};
    static CryptoComBookLevel from_json(const json& row);  // ["price","size","count"]
};

struct CryptoComOrderBook {
    std::string                     instrument_name;
    int                             depth{0};
    int64_t                         t{0};  // ms
    std::vector<CryptoComBookLevel> bids;
    std::vector<CryptoComBookLevel> asks;
    // result = {depth, instrument_name, data:[{bids, asks, t}]}.
    static CryptoComOrderBook from_json(const json& j);
};

struct CryptoComOrderBookRequest : TypedPublicRequest<CryptoComOrderBook> {
    std::string        instrument_name;  // required
    std::optional<int> depth;            // e.g. 10 / 50
    HttpRequest build() const override;
};

// ── public/get-candlestick ────────────────────────────────────────────────────

struct CryptoComCandle {
    int64_t t{0};   // bucket start, ms
    double  o{0.0};
    double  h{0.0};
    double  l{0.0};
    double  c{0.0};
    double  v{0.0};
    static CryptoComCandle from_json(const json& j);  // {o,h,l,c,v,t}
};

struct CryptoComCandlesResult {
    std::string                  instrument_name;
    std::string                  interval;
    std::vector<CryptoComCandle> candles;  // result.data[]
    static CryptoComCandlesResult from_json(const json& j);
};

struct CryptoComCandlesRequest : TypedPublicRequest<CryptoComCandlesResult> {
    std::string                instrument_name;  // required
    std::optional<std::string> timeframe;        // "1m","5m","1h","1D",...
    std::optional<int>         count;
    HttpRequest build() const override;
};

// ── public/get-trades ─────────────────────────────────────────────────────────

struct CryptoComPublicTrade {
    std::string trade_id;         // d
    int64_t     timestamp{0};     // t (ms)
    double      quantity{0.0};    // q
    double      price{0.0};       // p
    std::string side;             // s ("buy"/"sell") — kept raw (lowercase wire)
    std::string instrument_name;  // i
    static CryptoComPublicTrade from_json(const json& j);
};

struct CryptoComTradesResult {
    std::vector<CryptoComPublicTrade> trades;  // result.data[]
    static CryptoComTradesResult from_json(const json& j);
};

struct CryptoComTradesRequest : TypedPublicRequest<CryptoComTradesResult> {
    std::string        instrument_name;  // required
    std::optional<int> count;
    HttpRequest build() const override;
};

// ═════════════════════════════════════════════════════════════════════════════
// Private (authenticated) endpoints
//
// build(creds) constructs the full signed envelope {id, method, api_key, params,
// nonce, sig} as the POST body (path /exchange/v1/private/...). `id` comes from
// the request's id member (CryptoComRestClient assigns a unique value); `nonce`
// is fresh per build. Monetary params are caller-formatted exact decimal strings
// — produce them with TickPrice::str() when exactness matters.
// ═════════════════════════════════════════════════════════════════════════════

// Marker base for Crypto.com private requests. build() signs the envelope itself.
struct PrivateRequest {
    int64_t id{1};  // correlation id; CryptoComRestClient assigns a unique value
    virtual ~PrivateRequest() = default;
    virtual HttpRequest build(const CryptoComCredentials& creds) const = 0;
};

template<typename R>
struct TypedPrivateRequest : PrivateRequest {
    using response_type = R;
};

// ── Shared order record (get-order-detail / get-open-orders / get-order-history)

struct CryptoComOrder {
    std::string account_id;
    std::string order_id;
    std::string client_oid;
    std::string order_type;            // "LIMIT" / "MARKET" (raw)
    std::string time_in_force;         // raw
    std::string side;                  // "BUY" / "SELL" (raw)
    double      quantity{0.0};
    double      limit_price{0.0};
    double      order_value{0.0};
    double      avg_price{0.0};
    double      cumulative_quantity{0.0};
    double      cumulative_value{0.0};
    double      cumulative_fee{0.0};
    std::string status;                // raw wire status
    OrderStatus status_enum{OrderStatus::Unknown};  // cryptocom_order_status_from_string(status)
    int64_t     create_time{0};        // ms
    int64_t     update_time{0};        // ms
    std::string instrument_name;
    std::string fee_instrument_name;
    static CryptoComOrder from_json(const json& j);
};

struct CryptoComOrdersResult {
    std::vector<CryptoComOrder> orders;  // result.data[]
    static CryptoComOrdersResult from_json(const json& j);
};

// ── private/user-balance ──────────────────────────────────────────────────────

struct CryptoComPositionBalance {
    std::string instrument_name;
    double      quantity{0.0};
    double      market_value{0.0};
    double      collateral_amount{0.0};
    double      max_withdrawal_balance{0.0};
    double      reserved_qty{0.0};
    static CryptoComPositionBalance from_json(const json& j);
};

struct CryptoComUserBalance {
    double      total_available_balance{0.0};
    double      total_margin_balance{0.0};
    double      total_initial_margin{0.0};
    double      total_cash_balance{0.0};
    double      total_collateral_value{0.0};
    double      total_session_unrealized_pnl{0.0};
    double      total_session_realized_pnl{0.0};
    std::string instrument_name;  // settlement ccy, e.g. "USD"
    std::vector<CryptoComPositionBalance> position_balances;
    static CryptoComUserBalance from_json(const json& j);
};

struct CryptoComUserBalanceResult {
    std::vector<CryptoComUserBalance> data;  // result.data[]
    static CryptoComUserBalanceResult from_json(const json& j);
};

struct CryptoComUserBalanceRequest : TypedPrivateRequest<CryptoComUserBalanceResult> {
    HttpRequest build(const CryptoComCredentials& creds) const override;
};

// ── private/create-order ──────────────────────────────────────────────────────

struct CryptoComCreateOrderResult {
    std::string order_id;
    std::string client_oid;
    static CryptoComCreateOrderResult from_json(const json& j);
};

// Convenience constant for CryptoComCreateOrderRequest::exec_inst — avoids a raw
// string literal at call sites, e.g. req.exec_inst = {EXEC_INST_POST_ONLY}.
inline constexpr const char* EXEC_INST_POST_ONLY = "POST_ONLY";

struct CryptoComCreateOrderRequest : TypedPrivateRequest<CryptoComCreateOrderResult> {
    std::string instrument_name;             // required
    Side        side{Side::Buy};             // required
    OrderType   type{OrderType::Limit};      // LIMIT / MARKET
    std::optional<std::string> price;        // required for LIMIT (exact decimal string)
    std::optional<std::string> quantity;     // base quantity (LIMIT, MARKET SELL)
    std::optional<std::string> notional;     // quote amount (MARKET BUY)
    std::optional<std::string> client_oid;   // max 36 chars
    std::optional<TimeInForce> time_in_force;
    std::optional<std::vector<std::string>> exec_inst;  // e.g. {"POST_ONLY"}; omitted when unset
    HttpRequest build(const CryptoComCredentials& creds) const override;
};

// ── private/cancel-order and private/cancel-all-orders ────────────────────────
//
// Both return code 0 with no meaningful result body on success.

struct CryptoComCancelResult {
    static CryptoComCancelResult from_json(const json& j);
};

struct CryptoComCancelOrderRequest : TypedPrivateRequest<CryptoComCancelResult> {
    std::string order_id;  // required
    HttpRequest build(const CryptoComCredentials& creds) const override;
};

struct CryptoComCancelAllOrdersRequest : TypedPrivateRequest<CryptoComCancelResult> {
    std::optional<std::string> instrument_name;  // optional filter
    HttpRequest build(const CryptoComCredentials& creds) const override;
};

// ── private/get-order-detail ──────────────────────────────────────────────────

struct CryptoComGetOrderDetailRequest : TypedPrivateRequest<CryptoComOrder> {
    std::string order_id;  // required
    HttpRequest build(const CryptoComCredentials& creds) const override;
};

// ── private/get-open-orders ───────────────────────────────────────────────────

struct CryptoComGetOpenOrdersRequest : TypedPrivateRequest<CryptoComOrdersResult> {
    std::optional<std::string> instrument_name;  // optional filter
    HttpRequest build(const CryptoComCredentials& creds) const override;
};

// ── private/get-order-history ─────────────────────────────────────────────────

struct CryptoComGetOrderHistoryRequest : TypedPrivateRequest<CryptoComOrdersResult> {
    std::optional<std::string> instrument_name;  // optional filter
    std::optional<int>         limit;
    HttpRequest build(const CryptoComCredentials& creds) const override;
};

// ── private/get-trades ────────────────────────────────────────────────────────

struct CryptoComUserTrade {
    std::string account_id;
    std::string event_date;
    double      traded_quantity{0.0};
    double      traded_price{0.0};
    double      fees{0.0};
    std::string order_id;
    std::string trade_id;
    std::string trade_match_id;
    int64_t     create_time{0};       // ms
    std::string create_time_ns;       // nanosecond string
    std::string side;                 // "BUY" / "SELL"
    std::string instrument_name;
    std::string fee_instrument_name;
    std::string client_oid;
    std::string taker_side;
    std::string liquidity_indicator;  // "MAKER" / "TAKER"
    static CryptoComUserTrade from_json(const json& j);
};

struct CryptoComUserTradesResult {
    std::vector<CryptoComUserTrade> trades;  // result.data[]
    static CryptoComUserTradesResult from_json(const json& j);
};

struct CryptoComGetTradesRequest : TypedPrivateRequest<CryptoComUserTradesResult> {
    std::optional<std::string> instrument_name;  // optional filter
    std::optional<int>         limit;
    HttpRequest build(const CryptoComCredentials& creds) const override;
};

} // namespace exchange::cryptocom::rest
