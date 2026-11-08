// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/coinbase/ws_streams.hpp
// Coinbase Exchange WebSocket feed — push event types and CoinbaseStreamClient.
//
// Namespace: exchange::coinbase::ws
//
// WHY A BESPOKE CLIENT (plan coinbase-exchange-adapter §2, Option A):
// Coinbase's feed is pure pub/sub with NO per-request correlation id — its
// `subscriptions` ack is a full-state broadcast, so it does not fit the generic
// id-correlated exchange::ws::ExchangeWsClient::subscribe_async contract that
// Binance/Kraken use. Rather than bend the shared engine, Coinbase ships this
// small client over the SAME exchange::ws::IWsConnection transport (so it reuses
// IxWsConnection, MockWsConnection, the error-handler strategy, and the reconnect
// session). Subscriptions are OPTIMISTIC: the typed callback is installed and the
// handle returned on send; `error` frames surface via the error callback /
// IWsErrorHandler, and `subscriptions` frames via an optional callback.
//
// Inbound frames are routed by their "type" field. One active callback per
// message type: re-subscribing a channel replaces the previous callback (this
// matches Coinbase multiplexing every product of a channel over one feed).
//
// This header does not include ix_ws_connection.hpp; for the real transport
// construct an exchange::ws::IxWsConnection(STREAM_URL) and pass it to
// make_coinbase_stream_client.

#include "exchange/coinbase/auth.hpp"
#include "exchange/common/ws.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace exchange::coinbase::ws {

using json = nlohmann::json;

// ── Re-export common transport types ──────────────────────────────────────────

using exchange::ws::IWsConnection;
using exchange::ws::IWsErrorHandler;
using exchange::ws::RateLimitedWsErrorHandler;

// Per-request HMAC creds for the authenticated user channel (alias — same shape
// as the REST credentials; the WS user-channel signs timestamp+"GET"+
// "/users/self/verify").
using CoinbaseWsCredentials = exchange::coinbase::rest::CoinbaseCredentials;

// ── Endpoint URL ──────────────────────────────────────────────────────────────

inline constexpr std::string_view STREAM_URL = "wss://ws-feed.exchange.coinbase.com";

// ── Push event types (from_json defined in src/coinbase/ws_streams.cpp) ──────

// channel "ticker" — type "ticker"
struct CoinbaseTickerEvent {
    std::string product_id;
    int64_t     sequence{0};
    int64_t     trade_id{0};
    double      price{0.0};
    double      best_bid{0.0};
    double      best_ask{0.0};
    double      last_size{0.0};
    double      volume_24h{0.0};
    std::string side;  // taker side of the last trade
    std::string time;
    static CoinbaseTickerEvent from_json(const json& j);
};

// channel "matches" — type "match" / "last_match"
struct CoinbaseMatchEvent {
    int64_t     trade_id{0};
    int64_t     sequence{0};
    std::string product_id;
    double      size{0.0};
    double      price{0.0};
    std::string side;
    std::string time;
    std::string maker_order_id;
    std::string taker_order_id;
    static CoinbaseMatchEvent from_json(const json& j);
};

// channel "level2" — type "snapshot"
struct CoinbaseL2Level {
    double price{0.0};
    double size{0.0};
};

struct CoinbaseL2Snapshot {
    std::string                  product_id;
    std::vector<CoinbaseL2Level> bids;
    std::vector<CoinbaseL2Level> asks;
    static CoinbaseL2Snapshot from_json(const json& j);
};

// channel "level2" — type "l2update"
struct CoinbaseL2Change {
    std::string side;   // "buy" / "sell"
    double      price{0.0};
    double      size{0.0};
};

struct CoinbaseL2Update {
    std::string                   product_id;
    std::string                   time;
    std::vector<CoinbaseL2Change> changes;
    static CoinbaseL2Update from_json(const json& j);
};

// channel "heartbeat" — type "heartbeat"
struct CoinbaseHeartbeatEvent {
    int64_t     sequence{0};
    int64_t     last_trade_id{0};
    std::string product_id;
    std::string time;
    static CoinbaseHeartbeatEvent from_json(const json& j);
};

// channels "full" and "user" — order-lifecycle types
// (received/open/done/match/change/activate). Common fields are modelled;
// `raw` carries the full frame for anything type-specific not surfaced here.
struct CoinbaseLifecycleEvent {
    std::string                type;         // discriminator
    std::string                product_id;
    int64_t                    sequence{0};
    std::string                order_id;
    std::string                side;
    std::string                time;
    std::optional<double>      price;
    std::optional<double>      size;
    std::optional<double>      remaining_size;
    std::optional<std::string> reason;          // "done" reason
    std::optional<int64_t>     trade_id;         // "match"
    std::optional<std::string> maker_order_id;   // "match"
    std::optional<std::string> taker_order_id;   // "match"
    json                       raw;
    static CoinbaseLifecycleEvent from_json(const json& j);
};

// type "error"
struct CoinbaseErrorEvent {
    std::string                message;
    std::optional<std::string> reason;
    json                       raw;
    static CoinbaseErrorEvent from_json(const json& j);
};

// ── Forward declaration ───────────────────────────────────────────────────────

class CoinbaseStreamClient;

// ── Subscription handle ───────────────────────────────────────────────────────
//
// Returned by every subscribe_*; cancel() removes the channel's callbacks and
// sends the unsubscribe frame. Idempotent and safe to call from any thread,
// including after the client is destroyed (weak_ptr guard).

class CoinbaseSubscriptionHandle {
public:
    CoinbaseSubscriptionHandle() = default;

    bool is_active() const;  // defined in src/coinbase/ws_streams.cpp
    void cancel();

private:
    friend class CoinbaseStreamClient;

    std::shared_ptr<std::atomic<bool>>  active_;
    std::weak_ptr<CoinbaseStreamClient> client_;
    std::vector<std::string>            message_types_;  // dispatch keys to remove
    std::string                         unsub_json_;

    CoinbaseSubscriptionHandle(std::shared_ptr<std::atomic<bool>>  active,
                               std::weak_ptr<CoinbaseStreamClient> client,
                               std::vector<std::string>            message_types,
                               std::string                         unsub_json)
        : active_(std::move(active))
        , client_(std::move(client))
        , message_types_(std::move(message_types))
        , unsub_json_(std::move(unsub_json))
    {}
};

// ── CoinbaseStreamClient ──────────────────────────────────────────────────────

class CoinbaseStreamClient : public std::enable_shared_from_this<CoinbaseStreamClient> {
public:
    using ErrorCallback         = std::function<void(const CoinbaseErrorEvent&)>;
    using SubscriptionsCallback = std::function<void(const json&)>;

    // Prefer make_coinbase_stream_client (it calls init()).
    explicit CoinbaseStreamClient(std::shared_ptr<IWsConnection>   conn,
                                  std::shared_ptr<IWsErrorHandler> error_handler = nullptr);

    // Wires the IWsConnection callbacks. Called by the factory; do not call twice.
    void init();

    // Transport passthroughs (the real IxWsConnection needs an explicit connect).
    void connect();
    void disconnect();

    // Optional observers.
    void set_on_error(ErrorCallback cb);
    void set_on_subscriptions(SubscriptionsCallback cb);

    // Typed market-data subscriptions.
    CoinbaseSubscriptionHandle subscribe_ticker(
        const std::vector<std::string>& product_ids,
        std::function<void(CoinbaseTickerEvent)> cb);

    CoinbaseSubscriptionHandle subscribe_matches(
        const std::vector<std::string>& product_ids,
        std::function<void(CoinbaseMatchEvent)> cb);

    CoinbaseSubscriptionHandle subscribe_level2(
        const std::vector<std::string>& product_ids,
        std::function<void(CoinbaseL2Snapshot)> on_snapshot,
        std::function<void(CoinbaseL2Update)>   on_update);

    // Public unauthenticated order-book channel.  Delivers the same
    // "snapshot" and "l2update" frames as subscribe_level2, but does not
    // require authentication (unlike level2, which now requires a signed
    // user channel).  Use this for keyless market-data feeds.
    CoinbaseSubscriptionHandle subscribe_level2_batch(
        const std::vector<std::string>& product_ids,
        std::function<void(CoinbaseL2Snapshot)> on_snapshot,
        std::function<void(CoinbaseL2Update)>   on_update);

    CoinbaseSubscriptionHandle subscribe_heartbeat(
        const std::vector<std::string>& product_ids,
        std::function<void(CoinbaseHeartbeatEvent)> cb);

    CoinbaseSubscriptionHandle subscribe_full(
        const std::vector<std::string>& product_ids,
        std::function<void(CoinbaseLifecycleEvent)> cb);

    // Authenticated user channel — order-lifecycle events for the signing key.
    CoinbaseSubscriptionHandle subscribe_user(
        const CoinbaseWsCredentials&    creds,
        const std::vector<std::string>& product_ids,
        std::function<void(CoinbaseLifecycleEvent)> cb);

private:
    friend class CoinbaseSubscriptionHandle;

    // Core subscribe: register raw_handler under each message_type, send the
    // subscribe frame (optimistic), return a handle. extra_top_level merges
    // auth fields into the subscribe frame (user channel).
    CoinbaseSubscriptionHandle subscribe_raw(
        const std::string&              channel,
        const std::vector<std::string>& product_ids,
        const std::vector<std::string>& message_types,
        const std::function<void(const json&)>& raw_handler,
        const json&                     extra_top_level = json::object());

    void remove_handlers(const std::vector<std::string>& message_types,
                         const std::string& unsub_json);

    void on_open_handler();
    void on_raw_message(const std::string& raw);
    void enqueue_or_send(const std::string& payload);

    std::shared_ptr<IWsConnection>   conn_;
    std::shared_ptr<IWsErrorHandler> error_handler_;

    std::mutex               mu_;
    std::atomic<bool>        connected_{false};
    std::vector<std::string> send_queue_;
    std::unordered_map<std::string, std::function<void(const json&)>> handlers_;

    ErrorCallback         on_error_;
    SubscriptionsCallback on_subscriptions_;
};

// ── Factory ───────────────────────────────────────────────────────────────────
//
// Wraps an already-constructed connection and calls init(). For the real
// transport: make_coinbase_stream_client(std::make_shared<IxWsConnection>(
//   std::string(STREAM_URL))). Defined in src/coinbase/ws_streams.cpp.

std::shared_ptr<CoinbaseStreamClient>
make_coinbase_stream_client(std::shared_ptr<IWsConnection>   conn,
                            std::shared_ptr<IWsErrorHandler> error_handler = nullptr);

} // namespace exchange::coinbase::ws
