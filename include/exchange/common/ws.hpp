// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/common/ws.hpp
// Exchange-agnostic WebSocket layer types.
//
// IWsConnection     — abstract transport interface (no ixwebsocket dependency).
// IWsErrorHandler   — strategy interface for connection/parse errors.
// WsResponse<T>     — uniform response envelope (ok, error, result).
// SubscriptionHandle— returned by subscribe(); call cancel() to unsubscribe.
// BaseWsResponse    — minimal base for method-call responses with success/error.
// WsRequestBase     — carries the client-assigned correlation id (req_id).
// TypedWsRequest<R> — links a method-call request type to its response type.
// FrameDescriptor   — result of MessageIdentifier: classifies an inbound frame.
// MessageIdentifier — per-exchange function: json → FrameDescriptor.
//
// Subscribe-request concept (structural contract, not a base class):
//   using push_type     = <push message type>;
//   using response_type = <ack type>;
//   int64_t req_id;                    // inherited from WsRequestBase
//   std::string route_key() const;     // routing key for push callbacks
//   json to_json() const;              // SUBSCRIBE wire frame
//   json unsubscribe_json() const;     // matching UNSUBSCRIBE frame

#include "exchange/common/cxx_standard.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

namespace exchange::ws {

using json = nlohmann::json;

// ── Forward declarations ─────────────────────────────────────────────────────

class ExchangeWsClient;

// ── Transport interface ──────────────────────────────────────────────────────

class IWsConnection {
public:
    using MessageCb = std::function<void(const std::string&)>;
    using OpenCb    = std::function<void()>;
    using CloseCb   = std::function<void(std::string reason)>;
    using ErrorCb   = std::function<void(const std::string& reason)>;

    virtual ~IWsConnection() = default;

    virtual void connect()                    = 0;
    virtual void disconnect()                 = 0;
    virtual bool is_connected() const         = 0;
    virtual void send(const std::string& msg) = 0;

    virtual void set_on_message(MessageCb cb) = 0;
    virtual void set_on_open(OpenCb cb)       = 0;
    virtual void set_on_close(CloseCb cb)     = 0;
    virtual void set_on_error(ErrorCb cb)     = 0;
};

// ── Error handler interface ──────────────────────────────────────────────────

class IWsErrorHandler {
public:
    virtual ~IWsErrorHandler() = default;
    virtual void on_malformed_frame(const std::string& raw,
                                    const std::exception& e) = 0;
    virtual void on_connection_error(const std::string& reason) = 0;
};

class RateLimitedWsErrorHandler : public IWsErrorHandler {
public:
    explicit RateLimitedWsErrorHandler(
        std::chrono::milliseconds interval = std::chrono::seconds{60});

    void on_malformed_frame(const std::string& raw,
                            const std::exception& e) override;
    void on_connection_error(const std::string& reason) override;

private:
    int64_t               interval_us_;
    std::atomic<uint64_t> count_{0};
    std::atomic<int64_t>  last_logged_us_{0};
};

// ── Response types ───────────────────────────────────────────────────────────

template<typename T>
struct WsResponse {
    bool ok{false};
    std::optional<std::string> error;
    std::optional<T>           result;
};

// Minimal base for response types that carry a success/error flag.
// ExchangeWsClient::detail::make_ws_response checks std::is_base_of_v<BaseWsResponse, T>
// to determine how to derive ok: from success/error for subtypes, or always-true otherwise.
struct BaseWsResponse {
    bool                       success{false};
    std::optional<std::string> error;
};

// ── SubscriptionHandle ───────────────────────────────────────────────────────

class SubscriptionHandle {
public:
    SubscriptionHandle() = default;

    bool is_active() const;  // defined in src/exchange/common/ws.cpp

    // Remove the push callback and send the pre-built unsubscribe frame.
    // Idempotent — safe to call from any thread, including after the client
    // is destroyed (the weak_ptr guards against use-after-free).
    void cancel();

private:
    std::shared_ptr<std::atomic<bool>> active_;
    std::weak_ptr<ExchangeWsClient>    client_;
    std::string                        route_key_;
    std::string                        unsub_json_;

    friend class ExchangeWsClient;

    SubscriptionHandle(std::shared_ptr<std::atomic<bool>> active,
                       std::weak_ptr<ExchangeWsClient>    client,
                       std::string                        route_key,
                       std::string                        unsub_json)
        : active_(std::move(active))
        , client_(std::move(client))
        , route_key_(std::move(route_key))
        , unsub_json_(std::move(unsub_json))
    {}
};

// ── Frame classification ─────────────────────────────────────────────────────

enum class FrameKind : std::uint8_t { MethodResponse, PushMessage, Unknown };

struct FrameDescriptor {
    FrameKind kind{FrameKind::Unknown};
    // MethodResponse: string correlation id (Kraken stringifies its int req_id;
    // Binance uses its UUID/int id directly).
    std::optional<std::string> correlation_id;
    // PushMessage: the routing key the push callback was registered under.
    std::string route_key;
};

// Per-exchange function that classifies an inbound JSON frame and extracts the
// ids needed for dispatch.  Passed to ExchangeWsClient at construction.
using MessageIdentifier = std::function<FrameDescriptor(const json&)>;

// ── Request scaffold ─────────────────────────────────────────────────────────

// Carries the client-assigned correlation slot.  ExchangeWsClient assigns
// req_id before calling to_json(); each exchange's to_json() renders it in
// the wire position/format that exchange uses.
struct WsRequestBase {
    int64_t req_id{0};
};

// Links a method-call request to its single response type.
// All method-call request structs inherit TypedWsRequest<Resp> and provide
// a to_json() that emits req_id in the exchange-specific wire location.
template<typename R>
struct TypedWsRequest : WsRequestBase {
    using response_type = R;
};

} // namespace exchange::ws
