// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/binance/ws_api.hpp
// Binance WebSocket API (bidirectional trading) — request/response types,
// frame descriptor, and client factory for the ws-api endpoint.
// All member/function bodies are defined in src/binance/ws_api.cpp.
//
// Namespace: exchange::binance::ws (sibling of ws_streams.hpp — the two
// surfaces are independently includable and co-includable).
//
// This header does not include ix_ws_connection.hpp; for the real transport
// use the URL overload of exchange::ws::make_exchange_ws_client there.

#include "exchange/binance/auth.hpp"
#include "exchange/binance/rest_api.hpp"
#include "exchange/common/ws_client.hpp"

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace exchange::binance::ws {

using json = nlohmann::json;

// ── Re-export common client types ────────────────────────────────────────────

using exchange::ws::IWsConnection;
using exchange::ws::IWsErrorHandler;
using exchange::ws::RateLimitedWsErrorHandler;
using exchange::ws::WsResponse;
using exchange::ws::ExchangeWsClient;
using exchange::ws::MessageIdentifier;
using exchange::ws::WsRequestBase;
using exchange::ws::TypedWsRequest;
using exchange::ws::FrameDescriptor;
using exchange::ws::FrameKind;
using exchange::ws::BaseWsResponse;

// ── Endpoint URL constant ─────────────────────────────────────────────────────

inline constexpr std::string_view WS_API_URL = "wss://ws-api.binance.com/ws-api/v3";

// ── Reply envelope ────────────────────────────────────────────────────────────
//
// Every WS API reply shares the shell
//   {"id":…,"status":N,"result":{…}|"error":{"code":C,"msg":M},"rateLimits":[…]}
// (appendix §4). BinanceWsApiResponse carries the shell; each concrete
// response type derives it and adds its typed result.

// One entry of the "rateLimits" array — the API's flow-control feedback.
struct BinanceWsRateLimit {
    std::string rate_limit_type;   // "ORDERS", "REQUEST_WEIGHT", …
    std::string interval;          // "SECOND", "MINUTE", "DAY"
    int         interval_num{0};
    int64_t     limit{0};
    int64_t     count{0};

    static BinanceWsRateLimit from_json(const json& j);
};

struct BinanceWsApiResponse : exchange::ws::BaseWsResponse {
    int                             status{0};       // mirrors HTTP status codes
    std::optional<int64_t>          id;              // echoed request id (int form)
    std::optional<int>              error_code;      // error.code, e.g. -2010
    std::vector<BinanceWsRateLimit> rate_limits;
};

namespace detail {

// Parses the shared reply shell into the envelope base. Defined in
// src/binance/ws_api.cpp.
void parse_ws_api_envelope(BinanceWsApiResponse& r, const json& j);

} // namespace detail

// ── Frame descriptor (MessageIdentifier) ──────────────────────────────────────
//
// Classifies ws-api frames per appendix §5: any frame with a non-null "id" is
// a MethodResponse; there is no push branch on this endpoint. Defined in
// src/binance/ws_api.cpp.
exchange::ws::FrameDescriptor binance_ws_api_frame_descriptor(const json& j);

// ── Signing ───────────────────────────────────────────────────────────────────
//
// The WS API uses the same key material, algorithm (HMAC-SHA256 → lowercase
// hex), and recvWindow semantics as REST — only the payload framing differs
// (alphabetically-sorted "k=v&k=v" over the params object). The credential
// bundle is the REST one by alias.

using BinanceWsCredentials = exchange::binance::rest::BinanceCredentials;

namespace detail {

// Renders one param value as it appears in the signed payload: strings raw,
// everything else via dump(). Floats are rejected. Defined in ws_api.cpp.
std::string ws_param_value(const json& v);

// Injects apiKey, timestamp, and recvWindow into params, then appends
// "signature" — HMAC-SHA256 hex over the alphabetically-sorted payload.
// HMAC-SHA256 only. Defined in src/binance/ws_api.cpp.
void ws_sign_params(json& params, const BinanceWsCredentials& creds,
                    int64_t timestamp_ms);

// Current wall-clock ms — the default when a request's timestamp is unset.
int64_t ws_now_ms();

} // namespace detail

// ── ping ──────────────────────────────────────────────────────────────────────

// Reply is envelope-only: {"id":N,"status":200,"result":{}}.
struct BinanceWsPongMessage : BinanceWsApiResponse {
    static BinanceWsPongMessage from_json(const json& j);
};

// Unsigned heartbeat / connectivity probe — the endpoint's only
// credential-free method in scope.
struct BinanceWsPingRequest : TypedWsRequest<BinanceWsPongMessage> {
    json to_json() const;
};

// ── order.place / order.cancel ────────────────────────────────────────────────
//
// Field sets mirror the REST request structs; result payloads are the REST
// response shapes verbatim, so the WS responses embed them (plan step-8-binance-ws-api decision
// 5). Signed requests carry creds plus an explicit timestamp: 0 means "system
// clock at to_json() time"; tests set a fixed value, making the whole frame —
// signature included — exactly assertable.

struct BinanceWsNewOrderResponse : BinanceWsApiResponse {
    // Set on success; parses like the REST RESULT/FULL order shape.
    std::optional<exchange::binance::rest::BinanceNewOrderResponse> order;

    static BinanceWsNewOrderResponse from_json(const json& j);
};

struct BinanceWsNewOrderRequest : TypedWsRequest<BinanceWsNewOrderResponse> {
    BinanceWsCredentials creds;
    int64_t              timestamp{0};  // ms; 0 → now at to_json() time

    std::string symbol;                  // required
    Side        side{Side::Buy};         // required
    OrderType   type{OrderType::Limit};  // required
    std::optional<TimeInForce>          time_in_force;
    std::optional<std::string>          quantity;          // caller-formatted exact
    std::optional<std::string>          quote_order_qty;   //   decimals (plan step-6-binance-rest-private
    std::optional<std::string>          price;             //   decision 6)
    std::optional<std::string>          new_client_order_id;
    std::optional<std::string>          stop_price;
    std::optional<std::string>          iceberg_qty;
    std::optional<BinanceOrderRespType> new_order_resp_type;

    json to_json() const;
};

struct BinanceWsCancelOrderResponse : BinanceWsApiResponse {
    // Set on success; the DELETE /api/v3/order shape inside the §4 envelope.
    std::optional<exchange::binance::rest::BinanceCancelOrderResponse> order;

    static BinanceWsCancelOrderResponse from_json(const json& j);
};

struct BinanceWsCancelOrderRequest : TypedWsRequest<BinanceWsCancelOrderResponse> {
    BinanceWsCredentials creds;
    int64_t              timestamp{0};  // ms; 0 → now at to_json() time

    std::string                symbol;                // required
    std::optional<int64_t>     order_id;              // one of order_id /
    std::optional<std::string> orig_client_order_id;  //   orig_client_order_id
    std::optional<std::string> new_client_order_id;

    json to_json() const;
};

// ── Client alias and connection-based factory ─────────────────────────────────
//
// BinanceWsApiClient is ExchangeWsClient parameterised with the WS API frame
// descriptor — the same runtime type as BinanceStreamClient / KrakenWsClient.
// For the real transport use the URL overload in
// exchange/common/ix_ws_connection.hpp:
//   exchange::ws::make_exchange_ws_client(std::string(WS_API_URL),
//                                         binance_ws_api_frame_descriptor);

using BinanceWsApiClient = exchange::ws::ExchangeWsClient;

// Defined in src/binance/ws_api.cpp.
std::shared_ptr<BinanceWsApiClient>
make_binance_ws_api_client(std::shared_ptr<IWsConnection>  conn,
                           std::shared_ptr<IWsErrorHandler> error_handler = nullptr);

} // namespace exchange::binance::ws
