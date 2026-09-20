// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/kraken/ws_api.hpp
// Kraken WebSocket v2 API — request/response types and message identification.
// All member/function bodies are defined in src/kraken/ws_api.cpp (non-template)
// and kraken/ws_api.inl (template).
//
// Namespace: exchange::kraken::ws

#include "exchange/common/ws.hpp"
#include "exchange/kraken/types.hpp"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace exchange::kraken::ws {

using json = nlohmann::json;

// ── Re-export common scaffold types ──────────────────────────────────────────

using exchange::ws::WsRequestBase;
using exchange::ws::WsResponse;
using exchange::ws::SubscriptionHandle;
using exchange::ws::FrameDescriptor;
using exchange::ws::FrameKind;
using exchange::ws::BaseWsResponse;

// Method-call typed request base (re-export from common scaffold).
template<typename R>
using TypedWsRequest = exchange::ws::TypedWsRequest<R>;

// ── Forward declarations ──────────────────────────────────────────────────────

struct AddOrderResponse;
struct AmendOrderResponse;
struct CancelOrderResponse;
struct CancelAllResponse;
struct CancelOnDisconnectResponse;
struct BatchAddResponse;
struct BatchCancelResponse;
struct EditOrderResponse;
struct PongMessage;

// ── Response base ─────────────────────────────────────────────────────────────

struct BaseResponse : exchange::ws::BaseWsResponse {
    std::string              method;
    std::optional<int64_t>   req_id;
    std::optional<std::string> time_in;
    std::optional<std::string> time_out;

    static void parse_base(const json& j, BaseResponse& r);
};

// ── Authentication credentials ────────────────────────────────────────────────

struct KrakenWsCredentials {
    std::string token;

    json to_json() const;
};

// ── 1. ADD ORDER ──────────────────────────────────────────────────────────────

struct AddOrderRequest : TypedWsRequest<AddOrderResponse> {
    OrderType   order_type;
    Side        side;
    double      order_qty{0.0};
    std::string symbol;
    std::string token;

    std::optional<TickPrice> limit_price;
    std::optional<PriceType> limit_price_type;
    std::optional<Triggers>   triggers;
    std::optional<Conditional> conditional;
    std::optional<TimeInForce>   time_in_force;
    std::optional<bool>          margin;
    std::optional<bool>          post_only;
    std::optional<bool>          reduce_only;
    std::optional<std::string>   effective_time;
    std::optional<std::string>   expire_time;
    std::optional<std::string>   deadline;
    std::optional<std::string>   cl_ord_id;
    std::optional<int64_t>       order_userref;
    std::optional<double>        display_qty;
    std::optional<FeePreference> fee_preference;
    std::optional<StpType>       stp_type;
    std::optional<double>        cash_order_qty;
    std::optional<bool>          validate;
    std::optional<std::string>   sender_sub_id;

    json to_json() const;
};

struct AddOrderResponse : BaseResponse {
    std::optional<std::string> order_id;
    std::optional<std::string> cl_ord_id;
    std::optional<int64_t>     order_userref;
    std::optional<std::vector<std::string>> warnings;

    static AddOrderResponse from_json(const json& j);
};

// ── 2. AMEND ORDER ───────────────────────────────────────────────────────────

struct AmendOrderRequest : TypedWsRequest<AmendOrderResponse> {
    std::string token;
    std::optional<std::string> order_id;
    std::optional<std::string> cl_ord_id;
    std::optional<double>    order_qty;
    std::optional<double>    display_qty;
    std::optional<TickPrice> limit_price;
    std::optional<PriceType> limit_price_type;
    std::optional<Triggers>  triggers;
    std::optional<bool>      post_only;
    std::optional<std::string> deadline;

    json to_json() const;
};

struct AmendOrderResponse : BaseResponse {
    std::optional<std::string> order_id;
    std::optional<std::string> cl_ord_id;
    std::optional<std::vector<std::string>> warnings;

    static AmendOrderResponse from_json(const json& j);
};

// ── 3. CANCEL ORDER ───────────────────────────────────────────────────────────

struct CancelOrderRequest : TypedWsRequest<CancelOrderResponse> {
    std::string token;
    std::optional<std::vector<std::string>> order_ids;
    std::optional<std::vector<std::string>> cl_ord_ids;

    json to_json() const;
};

struct CancelOrderResult {
    std::string order_id;
    bool        success{false};
    std::optional<std::string> error;
};

struct CancelOrderResponse : BaseResponse {
    std::optional<std::vector<CancelOrderResult>> orders_cancelled;

    static CancelOrderResponse from_json(const json& j);
};

// ── 4. CANCEL ALL ─────────────────────────────────────────────────────────────

struct CancelAllRequest : TypedWsRequest<CancelAllResponse> {
    std::string token;

    json to_json() const;
};

struct CancelAllResponse : BaseResponse {
    std::optional<int32_t> count;

    static CancelAllResponse from_json(const json& j);
};

// ── 5. CANCEL ON DISCONNECT ───────────────────────────────────────────────────

struct CancelOnDisconnectRequest : TypedWsRequest<CancelOnDisconnectResponse> {
    std::string token;
    int32_t     timeout{60};

    json to_json() const;
};

struct CancelOnDisconnectResponse : BaseResponse {
    std::optional<std::string> current_time;
    std::optional<std::string> trigger_time;

    static CancelOnDisconnectResponse from_json(const json& j);
};

// ── 6. BATCH ADD ──────────────────────────────────────────────────────────────

struct BatchAddRequest : TypedWsRequest<BatchAddResponse> {
    std::string token;
    std::string symbol;
    std::optional<std::string> deadline;
    std::optional<bool>        validate;
    std::vector<OrderParams> orders;

    json to_json() const;
};

struct BatchAddResult {
    std::string              order_id;
    bool                     success{false};
    std::optional<std::string> cl_ord_id;
    std::optional<int64_t>   order_userref;
    std::optional<std::string> error;
    std::optional<std::vector<std::string>> warnings;
};

struct BatchAddResponse : BaseResponse {
    std::optional<std::vector<BatchAddResult>> orders;

    static BatchAddResponse from_json(const json& j);
};

// ── 7. BATCH CANCEL ───────────────────────────────────────────────────────────

struct BatchCancelRequest : TypedWsRequest<BatchCancelResponse> {
    std::string token;
    std::optional<std::vector<std::string>> order_ids;
    std::optional<std::vector<std::string>> cl_ord_ids;

    json to_json() const;
};

struct BatchCancelResponse : BaseResponse {
    std::optional<int32_t> orders_cancelled;

    static BatchCancelResponse from_json(const json& j);
};

// ── 8. EDIT ORDER ─────────────────────────────────────────────────────────────

struct EditOrderRequest : TypedWsRequest<EditOrderResponse> {
    std::string token;
    std::optional<std::string> order_id;
    std::optional<std::string> cl_ord_id;
    std::optional<double>    order_qty;
    std::optional<double>    display_qty;
    std::optional<TickPrice> limit_price;
    std::optional<Triggers>  triggers;
    std::optional<bool>      post_only;
    std::optional<std::string> deadline;
    std::optional<std::string> new_cl_ord_id;

    json to_json() const;
};

struct EditOrderResponse : BaseResponse {
    std::optional<std::string> order_id;
    std::optional<std::string> original_order_id;
    std::optional<std::string> cl_ord_id;
    std::optional<std::vector<std::string>> warnings;

    static EditOrderResponse from_json(const json& j);
};

// ── 9. SUBSCRIPTIONS ─────────────────────────────────────────────────────────

enum class SubscribeChannel {
    Ticker,
    Book,
    Level3,
    OHLC,
    Trade,
    Instrument,
    Executions,
    Balances
};

std::string to_string(SubscribeChannel ch);

struct SubscribeRequest : WsRequestBase {
    SubscribeChannel channel;
    std::optional<std::vector<std::string>> symbols;
    std::optional<std::string> token;
    std::optional<int32_t>     depth;
    std::optional<int32_t>     interval;
    std::optional<bool>        snapshot;
    std::optional<bool>        snapshot_trades;

    json to_json() const;
};

struct UnsubscribeRequest {
    SubscribeChannel channel;
    std::optional<std::vector<std::string>> symbols;
    std::optional<std::string> token;
    std::optional<int64_t> req_id;

    json to_json() const;
};

struct SubscribeResponse : BaseResponse {
    std::optional<std::string> channel;
    std::optional<std::string> symbol;

    static SubscribeResponse from_json(const json& j);
};

template<typename PushMsg, SubscribeChannel Ch>
struct TypedSubscribeRequest : SubscribeRequest {
    using push_type     = PushMsg;
    using response_type = SubscribeResponse;
    static constexpr SubscribeChannel channel_value = Ch;

    TypedSubscribeRequest();                 // sets channel = Ch; defined in ws_api.inl
    std::string route_key() const;           // defined in ws_api.inl
    json        unsubscribe_json() const;    // defined in ws_api.inl
};

// ── 10. MARKET DATA - Ticker ──────────────────────────────────────────────────

struct TickerData {
    std::string symbol;
    double      bid{0.0};
    double      bid_qty{0.0};
    double      ask{0.0};
    double      ask_qty{0.0};
    double      last{0.0};
    double      volume{0.0};
    double      vwap{0.0};
    double      low{0.0};
    double      high{0.0};
    double      change{0.0};
    double      change_pct{0.0};

    static TickerData from_json(const json& j);
};

struct TickerMessage {
    std::string channel;
    std::string type;
    std::vector<TickerData> data;

    static TickerMessage from_json(const json& j);
};

// ── 11. MARKET DATA - Book ────────────────────────────────────────────────────

struct BookEntry {
    double price{0.0};
    double qty{0.0};
};

struct BookData {
    std::string             symbol;
    std::vector<BookEntry>  bids;
    std::vector<BookEntry>  asks;
    std::optional<unsigned int> checksum;

    static BookData from_json(const json& j);
};

struct BookMessage {
    std::string channel;
    std::string type;
    std::vector<BookData> data;

    static BookMessage from_json(const json& j);
};

// ── 12. MARKET DATA - Trades ──────────────────────────────────────────────────

struct TradeData {
    std::string symbol;
    double      price{0.0};
    double      qty{0.0};
    std::string side;
    std::string ord_type;
    int64_t     trade_id{0};
    std::string timestamp;

    static TradeData from_json(const json& j);
};

struct TradeMessage {
    std::string channel;
    std::string type;
    std::vector<TradeData> data;

    static TradeMessage from_json(const json& j);
};

// ── 13. MARKET DATA - OHLC ────────────────────────────────────────────────────

struct OHLCData {
    std::string symbol;
    std::string timestamp;
    double      open{0.0};
    double      high{0.0};
    double      low{0.0};
    double      close{0.0};
    double      vwap{0.0};
    double      volume{0.0};
    int64_t     trades{0};
    std::string interval_begin;
    std::optional<int32_t> interval;

    static OHLCData from_json(const json& j);
};

struct OHLCMessage {
    std::string channel;
    std::string type;
    std::vector<OHLCData> data;

    static OHLCMessage from_json(const json& j);
};

// ── 14. MARKET DATA - Instrument ─────────────────────────────────────────────

struct AssetInfo {
    std::string id;
    std::string status;
    std::optional<int32_t> precision;
    std::optional<int32_t> precision_display;
    std::optional<bool>    borrowable;
    std::optional<double>  collateral_value;
    std::optional<double>  margin_rate;

    static AssetInfo from_json(const json& j);
};

struct InstrumentInfo {
    std::string symbol;
    std::string base;
    std::string quote;
    std::string status;
    double      qty_increment{0.0};
    double      qty_min{0.0};
    double      price_increment{0.0};
    double      cost_min{0.0};
    int32_t     margin_initial{0};
    std::optional<int32_t> position_limit_long;
    std::optional<int32_t> position_limit_short;
    std::optional<bool>    has_index;
    std::optional<int32_t> cost_precision;
    std::optional<int32_t> qty_precision;

    static InstrumentInfo from_json(const json& j);
};

struct InstrumentMessage {
    std::string channel;
    std::string type;
    std::vector<AssetInfo>      assets;
    std::vector<InstrumentInfo> pairs;

    static InstrumentMessage from_json(const json& j);
};

// ── 15. USER DATA - Executions ────────────────────────────────────────────────

struct ExecutionData {
    std::string exec_id;
    std::string exec_type;
    std::string order_id;
    std::string symbol;
    std::string side;
    std::string order_type;
    double      order_qty{0.0};
    double      cum_qty{0.0};
    double      leaves_qty{0.0};
    double      last_qty{0.0};
    double      last_price{0.0};
    double      avg_price{0.0};
    double      cost{0.0};
    std::string order_status;
    std::string timestamp;
    std::optional<std::string> cl_ord_id;
    std::optional<int64_t>     order_userref;
    std::optional<double>      fee;            // total fee (summed from `fees[]`, quote ccy)
    std::optional<std::string> fee_currency;   // asset of the first `fees[]` element
    std::optional<std::string> liquidity_ind;  // "m" (maker) / "t" (taker)
    std::optional<std::string> fee_ccy_pref;   // "fciq" (quote) / "fcib" (base)
    std::optional<double>      limit_price;
    std::optional<std::string> time_in_force;
    std::optional<bool>        post_only;
    std::optional<bool>        margin;
    std::optional<std::string> reason;

    static ExecutionData from_json(const json& j);
};

struct ExecutionsMessage {
    std::string channel;
    std::string type;
    std::vector<ExecutionData> data;

    static ExecutionsMessage from_json(const json& j);
};

// ── 16. USER DATA - Balances ──────────────────────────────────────────────────

struct BalanceData {
    std::string asset;
    double      balance{0.0};
    double      hold_trade{0.0};

    static BalanceData from_json(const json& j);
};

struct BalancesMessage {
    std::string channel;
    std::string type;
    std::vector<BalanceData> data;

    static BalancesMessage from_json(const json& j);
};

// ── 17. ADMIN - Status / Heartbeat / Ping ────────────────────────────────────

struct StatusMessage {
    std::string channel;
    std::string type;
    std::string system;
    std::string version;

    static StatusMessage from_json(const json& j);
};

struct PingRequest : TypedWsRequest<PongMessage> {
    json to_json() const;
};

struct PongMessage {
    std::string method;
    std::optional<int64_t> req_id;

    static PongMessage from_json(const json& j);
};

// ── Message Dispatcher ────────────────────────────────────────────────────────

enum class MessageKind {
    AddOrderResponse,
    AmendOrderResponse,
    CancelOrderResponse,
    CancelAllResponse,
    CancelOnDisconnectResponse,
    BatchAddResponse,
    BatchCancelResponse,
    EditOrderResponse,
    SubscribeResponse,
    UnsubscribeResponse,
    Pong,
    Ticker,
    Book,
    Level3,
    OHLC,
    Trade,
    Instrument,
    Executions,
    Balances,
    Status,
    Heartbeat,
    Unknown
};

MessageKind identify_message(const json& j);

// ── Per-channel typed subscribe request aliases ───────────────────────────────

using TickerSubscribeRequest     = TypedSubscribeRequest<TickerMessage,     SubscribeChannel::Ticker>;
using BookSubscribeRequest       = TypedSubscribeRequest<BookMessage,       SubscribeChannel::Book>;
using TradeSubscribeRequest      = TypedSubscribeRequest<TradeMessage,      SubscribeChannel::Trade>;
using OHLCSubscribeRequest       = TypedSubscribeRequest<OHLCMessage,       SubscribeChannel::OHLC>;
using InstrumentSubscribeRequest = TypedSubscribeRequest<InstrumentMessage, SubscribeChannel::Instrument>;
using ExecutionsSubscribeRequest = TypedSubscribeRequest<ExecutionsMessage,  SubscribeChannel::Executions>;
using BalancesSubscribeRequest   = TypedSubscribeRequest<BalancesMessage,   SubscribeChannel::Balances>;

// ── Kraken frame descriptor ───────────────────────────────────────────────────

exchange::ws::FrameDescriptor kraken_frame_descriptor(const json& j);

} // namespace exchange::kraken::ws

#include "exchange/kraken/ws_api.inl"
