// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/binance/rest_api.hpp
// Binance Spot REST API — request bases, response envelope, and endpoint types.
// Member/function bodies are defined in src/binance/rest_api.cpp (non-template)
// and binance/rest_api.inl (parse_binance_response<T>).
//
// Namespace: exchange::binance::rest

#include "exchange/binance/types.hpp"
#include "exchange/common/rest.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace exchange::binance::rest {

using json = nlohmann::json;
using exchange::rest::HttpRequest;

// ── Request base classes ──────────────────────────────────────────────────────

// Marker base for Binance public requests (no authentication).
struct PublicRequest {
    virtual ~PublicRequest() = default;
    virtual HttpRequest build() const = 0;
};

// Marker base for Binance private requests (requires BinanceAuth).
// build() constructs the request WITHOUT auth; BinanceRestClient calls
// auth.sign() before dispatch.
struct PrivateRequest {
    virtual ~PrivateRequest() = default;
    virtual HttpRequest build() const = 0;
};

// Typed wrappers — link each request type to its response type at compile time.
template<typename R>
struct TypedPublicRequest : PublicRequest {
    using response_type = R;
};

template<typename R>
struct TypedPrivateRequest : PrivateRequest {
    using response_type = R;
};

// ── Response envelope (parse_binance_response<T> defined in rest_api.inl) ─────

template<typename T>
exchange::rest::RestResponse<T>
parse_binance_response(int http_status, const json& j);

// ── Helpers ────────────────────────────────────────────────────────────────────

namespace detail {

// Builds the percent-encoded query value Binance expects for multi-symbol
// requests, e.g. ["BTCUSDT","ETHBTC"] -> "%5B%22BTCUSDT%22%2C%22ETHBTC%22%5D".
// Hand-escapes only `[`, `]`, `"`, `,`. Defined in src/binance/rest_api.cpp.
std::string symbols_query_value(const std::vector<std::string>& symbols);

} // namespace detail

// ── GET /api/v3/ping ─────────────────────────────────────────────────────────

struct BinancePing {
    static BinancePing from_json(const json&);
};

struct BinancePingRequest : TypedPublicRequest<BinancePing> {
    HttpRequest build() const override;
};

// ── GET /api/v3/time ─────────────────────────────────────────────────────────

struct BinanceServerTime {
    int64_t server_time{0};
    static BinanceServerTime from_json(const json& j);
};

struct BinanceServerTimeRequest : TypedPublicRequest<BinanceServerTime> {
    HttpRequest build() const override;
};

// ── GET /api/v3/ticker/price ─────────────────────────────────────────────────

struct BinanceTickerPriceEntry {
    std::string symbol;
    double      price{0.0};
    static BinanceTickerPriceEntry from_json(const json& j);
};

struct BinanceTickerPrice {
    std::vector<BinanceTickerPriceEntry> entries;
    static BinanceTickerPrice from_json(const json& j);
};

struct BinanceTickerPriceRequest : TypedPublicRequest<BinanceTickerPrice> {
    std::optional<std::string>              symbol;
    std::optional<std::vector<std::string>> symbols;

    // Prefers `symbol` if set, else `symbols`, else neither.
    HttpRequest build() const override;
};

// ── GET /api/v3/depth ────────────────────────────────────────────────────────

// Hoisted to exchange/binance/types.hpp (shared with the WS depth streams);
// re-exported here so existing exchange::binance::rest spellings keep resolving.
using exchange::binance::BinanceBookLevel;

struct BinanceOrderBook {
    int64_t                       last_update_id{0};
    std::vector<BinanceBookLevel> bids;
    std::vector<BinanceBookLevel> asks;
    static BinanceOrderBook from_json(const json& j);
};

struct BinanceOrderBookRequest : TypedPublicRequest<BinanceOrderBook> {
    std::string        symbol;
    std::optional<int> limit;

    HttpRequest build() const override;
};

// ── GET /api/v3/trades ───────────────────────────────────────────────────────

struct BinanceTrade {
    int64_t id{0};
    double  price{0.0};
    double  qty{0.0};
    double  quote_qty{0.0};
    int64_t time{0};
    bool    is_buyer_maker{false};
    bool    is_best_match{false};
    static BinanceTrade from_json(const json& j);
};

struct BinanceTradesResult {
    std::vector<BinanceTrade> trades;
    // Top-level array response.
    static BinanceTradesResult from_json(const json& j);
};

struct BinanceRecentTradesRequest : TypedPublicRequest<BinanceTradesResult> {
    std::string        symbol;
    std::optional<int> limit;

    HttpRequest build() const override;
};

// ── GET /api/v3/klines ───────────────────────────────────────────────────────

struct BinanceKline {
    int64_t open_time{0};
    double  open{0.0};
    double  high{0.0};
    double  low{0.0};
    double  close{0.0};
    double  volume{0.0};
    int64_t close_time{0};
    double  quote_asset_volume{0.0};
    int64_t num_trades{0};
    double  taker_buy_base_volume{0.0};
    double  taker_buy_quote_volume{0.0};
    // Parses a 12-element positional row; field 11 ("ignore") is dropped.
    static BinanceKline from_json(const json& row);
};

struct BinanceKlinesResult {
    std::vector<BinanceKline> klines;
    // Top-level array response.
    static BinanceKlinesResult from_json(const json& j);
};

struct BinanceKlinesRequest : TypedPublicRequest<BinanceKlinesResult> {
    std::string            symbol;
    std::string            interval;
    std::optional<int64_t> start_time;
    std::optional<int64_t> end_time;
    std::optional<int>     limit;

    HttpRequest build() const override;
};

// ── GET /api/v3/exchangeInfo ─────────────────────────────────────────────────

// First-cut scope: `filters`, `permissions`, `permissionSets`, `rateLimits`,
// `exchangeFilters`, and self-trade-prevention fields are deliberately
// omitted (see plan step-5-binance-rest-public design decision 6).
struct BinanceSymbolInfo {
    std::string              symbol;
    std::string              status;
    std::string              base_asset;
    int                      base_asset_precision{0};
    std::string              quote_asset;
    int                      quote_precision{0};
    int                      quote_asset_precision{0};
    std::vector<std::string> order_types;
    bool                     iceberg_allowed{false};
    bool                     oco_allowed{false};
    bool                     is_spot_trading_allowed{false};
    bool                     is_margin_trading_allowed{false};
    static BinanceSymbolInfo from_json(const json& j);
};

struct BinanceExchangeInfo {
    std::string                    timezone;
    int64_t                        server_time{0};
    std::vector<BinanceSymbolInfo> symbols;
    static BinanceExchangeInfo from_json(const json& j);
};

struct BinanceExchangeInfoRequest : TypedPublicRequest<BinanceExchangeInfo> {
    HttpRequest build() const override;
};

// ── GET /api/v3/ticker/24hr ──────────────────────────────────────────────────

struct BinanceTicker24hrEntry {
    std::string symbol;
    double      price_change{0.0};
    double      price_change_percent{0.0};
    double      weighted_avg_price{0.0};
    double      prev_close_price{0.0};
    double      last_price{0.0};
    double      last_qty{0.0};
    double      bid_price{0.0};
    double      bid_qty{0.0};
    double      ask_price{0.0};
    double      ask_qty{0.0};
    double      open_price{0.0};
    double      high_price{0.0};
    double      low_price{0.0};
    double      volume{0.0};
    double      quote_volume{0.0};
    int64_t     open_time{0};
    int64_t     close_time{0};
    int64_t     first_id{0};
    int64_t     last_id{0};
    int64_t     count{0};
    static BinanceTicker24hrEntry from_json(const json& j);
};

struct BinanceTicker24hr {
    std::vector<BinanceTicker24hrEntry> entries;
    // Single object (symbol= query) or array (symbols=/no query).
    static BinanceTicker24hr from_json(const json& j);
};

struct BinanceTicker24hrRequest : TypedPublicRequest<BinanceTicker24hr> {
    std::optional<std::string>              symbol;
    std::optional<std::vector<std::string>> symbols;

    // Prefers `symbol` if set, else `symbols`, else neither.
    HttpRequest build() const override;
};

// ═════════════════════════════════════════════════════════════════════════════
// Signed (private) endpoints — Step 6.
//
// build() constructs the request WITHOUT auth params; BinanceRestClient's
// execute(req, auth) calls auth.sign(), which appends timestamp/recvWindow/
// signature to the query (GET/DELETE) or body (POST) and signs the raw
// `query + body` concatenation. POST requests must therefore put ALL params
// in `body` and leave `query` empty; GET/DELETE use only `query`.
// ═════════════════════════════════════════════════════════════════════════════

// ── GET /api/v3/account ──────────────────────────────────────────────────────

struct BinanceCommissionRates {
    double maker{0.0};
    double taker{0.0};
    double buyer{0.0};
    double seller{0.0};

    static BinanceCommissionRates from_json(const json& j);
};

struct BinanceBalance {
    std::string asset;
    double      free{0.0};
    double      locked{0.0};

    static BinanceBalance from_json(const json& j);
};

struct BinanceAccount {
    int                         maker_commission{0};
    int                         taker_commission{0};
    int                         buyer_commission{0};
    int                         seller_commission{0};
    BinanceCommissionRates      commission_rates;
    bool                        can_trade{false};
    bool                        can_withdraw{false};
    bool                        can_deposit{false};
    bool                        brokered{false};
    bool                        require_self_trade_prevention{false};
    bool                        prevent_sor{false};
    int64_t                     update_time{0};
    std::string                 account_type;
    std::vector<BinanceBalance> balances;
    std::vector<std::string>    permissions;
    int64_t                     uid{0};

    static BinanceAccount from_json(const json& j);
};

struct BinanceAccountRequest : TypedPrivateRequest<BinanceAccount> {
    HttpRequest build() const override;
};

// ── GET /api/v3/openOrders and /api/v3/allOrders ─────────────────────────────

// Shared row shape of both order-query endpoints.
//
// Enum fields follow plan step-6-binance-rest-private decision 3 ("enum where the wire↔enum mapping
// is total, raw string otherwise"): `status` folds unmapped values to
// OrderStatus::Unknown, `time_in_force`/`side` are total — but `type` stays
// a raw wire string because Binance's LIMIT_MAKER (and any future order
// type) has no canonical OrderType value to map to.
struct BinanceOrderInfo {
    std::string symbol;
    int64_t     order_id{0};
    int64_t     order_list_id{-1};
    std::string client_order_id;
    double      price{0.0};
    double      orig_qty{0.0};
    double      executed_qty{0.0};
    double      cummulative_quote_qty{0.0};  // wire: "cummulativeQuoteQty" (Binance's typo)
    double      orig_quote_order_qty{0.0};
    OrderStatus status{OrderStatus::Unknown};
    TimeInForce time_in_force{TimeInForce::GTC};
    std::string type;                        // raw wire string — see note above
    Side        side{Side::Buy};
    double      stop_price{0.0};
    double      iceberg_qty{0.0};
    int64_t     time{0};
    int64_t     update_time{0};
    bool        is_working{false};
    int64_t     working_time{0};
    std::string self_trade_prevention_mode;

    static BinanceOrderInfo from_json(const json& j);
};

struct BinanceOpenOrdersResult {
    std::vector<BinanceOrderInfo> orders;

    // Top-level array.
    static BinanceOpenOrdersResult from_json(const json& j);
};

// Same row shape and wrapper — distinct name per plan multi-exchange-abstraction's endpoint table.
using BinanceAllOrdersResult = BinanceOpenOrdersResult;

struct BinanceOpenOrdersRequest : TypedPrivateRequest<BinanceOpenOrdersResult> {
    std::optional<std::string> symbol;  // omit -> open orders across all symbols

    HttpRequest build() const override;
};

struct BinanceAllOrdersRequest : TypedPrivateRequest<BinanceAllOrdersResult> {
    std::string            symbol;      // required
    std::optional<int64_t> order_id;    // return orders >= this id
    std::optional<int64_t> start_time;
    std::optional<int64_t> end_time;
    std::optional<int>     limit;       // default 500, max 1000

    HttpRequest build() const override;
};

// ── GET /api/v3/myTrades ─────────────────────────────────────────────────────

struct BinanceMyTrade {
    std::string symbol;
    int64_t     id{0};
    int64_t     order_id{0};
    int64_t     order_list_id{-1};
    double      price{0.0};
    double      qty{0.0};
    double      quote_qty{0.0};
    double      commission{0.0};
    std::string commission_asset;
    int64_t     time{0};
    bool        is_buyer{false};
    bool        is_maker{false};
    bool        is_best_match{false};

    static BinanceMyTrade from_json(const json& j);
};

struct BinanceMyTradesResult {
    std::vector<BinanceMyTrade> trades;

    // Top-level array.
    static BinanceMyTradesResult from_json(const json& j);
};

struct BinanceMyTradesRequest : TypedPrivateRequest<BinanceMyTradesResult> {
    std::string            symbol;      // required
    std::optional<int64_t> order_id;
    std::optional<int64_t> start_time;
    std::optional<int64_t> end_time;
    std::optional<int64_t> from_id;     // trade id to fetch from
    std::optional<int>     limit;       // default 500, max 1000

    HttpRequest build() const override;
};

// ── POST /api/v3/order ───────────────────────────────────────────────────────

struct BinanceFill {
    double      price{0.0};
    double      qty{0.0};
    double      commission{0.0};
    std::string commission_asset;
    int64_t     trade_id{0};

    static BinanceFill from_json(const json& j);
};

// Covers all three newOrderRespType shapes: ACK fields are always present;
// RESULT/FULL fields are std::optional and only set when the server sent
// them; `fills` is non-empty only for FULL.
struct BinanceNewOrderResponse {
    // ACK and up:
    std::string symbol;
    int64_t     order_id{0};
    int64_t     order_list_id{-1};
    std::string client_order_id;
    int64_t     transact_time{0};
    // RESULT/FULL only:
    std::optional<double>      price;
    std::optional<double>      orig_qty;
    std::optional<double>      executed_qty;
    std::optional<double>      orig_quote_order_qty;
    std::optional<double>      cummulative_quote_qty;  // wire: "cummulativeQuoteQty"
    std::optional<OrderStatus> status;
    std::optional<TimeInForce> time_in_force;
    std::optional<std::string> type;                   // raw wire string
    std::optional<Side>        side;
    std::optional<int64_t>     working_time;
    std::optional<std::string> self_trade_prevention_mode;
    // FULL only (empty otherwise):
    std::vector<BinanceFill> fills;

    static BinanceNewOrderResponse from_json(const json& j);
};

struct BinanceNewOrderRequest : TypedPrivateRequest<BinanceNewOrderResponse> {
    std::string symbol;                              // required
    Side        side{Side::Buy};                     // required
    OrderType   type{OrderType::Limit};              // required
    std::optional<TimeInForce>          time_in_force;
    std::optional<std::string>          quantity;          // caller-formatted exact
    std::optional<std::string>          quote_order_qty;   //   decimals (plan step-6-binance-rest-private
    std::optional<std::string>          price;             //   decision 6)
    std::optional<std::string>          new_client_order_id;
    std::optional<std::string>          stop_price;
    std::optional<std::string>          iceberg_qty;
    std::optional<BinanceOrderRespType> new_order_resp_type;

    // POST — all params go in the body (query stays empty; see the signed-
    // endpoint note above). Values are appended verbatim: valid Binance
    // symbols ([A-Z0-9]) and client order ids ([\.A-Z:/a-z0-9_-]{1,36})
    // contain no characters needing form-encoding — same minimal-escaping
    // stance as detail::symbols_query_value. Which params Binance *requires*
    // per order type (e.g. LIMIT needs timeInForce+quantity+price) is
    // enforced server-side; this struct stays a faithful wire mapping.
    HttpRequest build() const override;
};

// ── DELETE /api/v3/order and /api/v3/openOrders ──────────────────────────────

// Shape shared by DELETE /api/v3/order's single object and each row of
// DELETE /api/v3/openOrders' array.
struct BinanceCancelOrderResponse {
    std::string symbol;
    std::string orig_client_order_id;
    int64_t     order_id{0};
    int64_t     order_list_id{-1};
    std::string client_order_id;
    int64_t     transact_time{0};
    double      price{0.0};
    double      orig_qty{0.0};
    double      executed_qty{0.0};
    double      orig_quote_order_qty{0.0};
    double      cummulative_quote_qty{0.0};  // wire: "cummulativeQuoteQty"
    OrderStatus status{OrderStatus::Unknown};
    TimeInForce time_in_force{TimeInForce::GTC};
    std::string type;                        // raw wire string
    Side        side{Side::Buy};
    std::string self_trade_prevention_mode;

    static BinanceCancelOrderResponse from_json(const json& j);
};

// Note: DELETE /api/v3/openOrders can interleave OCO cancellation objects
// (contingencyType/orderReports) among plain rows; those parse as
// part-populated BinanceCancelOrderResponse rows (j.value defaults) — full
// OCO support is out of scope for the first cut (plan step-6-binance-rest-private decision 9).
struct BinanceCancelAllResponse {
    std::vector<BinanceCancelOrderResponse> orders;

    // Top-level array.
    static BinanceCancelAllResponse from_json(const json& j);
};

struct BinanceCancelOrderRequest : TypedPrivateRequest<BinanceCancelOrderResponse> {
    std::string                symbol;                // required
    std::optional<int64_t>     order_id;              // one of order_id /
    std::optional<std::string> orig_client_order_id;  //   orig_client_order_id
    std::optional<std::string> new_client_order_id;

    // DELETE — params go in the query (BinanceAuth signs query-side for
    // non-POST methods).
    HttpRequest build() const override;
};

struct BinanceCancelAllOpenOrdersRequest
    : TypedPrivateRequest<BinanceCancelAllResponse> {
    std::string symbol;  // required

    HttpRequest build() const override;
};

} // namespace exchange::binance::rest

#include "exchange/binance/rest_api.inl"
