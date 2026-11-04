// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Crypto.com WebSocket unit tests (no network — MockWsConnection only):
//   - HeartbeatResponder decorator (auto-reply + swallow, forward otherwise);
//   - cryptocom_frame_descriptor classification (ack/auth by id, push by id -1);
//   - subscribe ack / auth parsing + auth signing;
//   - push event from_json field assertions (live market + synthetic user);
//   - the full subscribe lifecycle through make_cryptocom_market_client.

#include "exchange/cryptocom/heartbeat_connection.hpp"
#include "exchange/cryptocom/ws.hpp"
#include "cryptocom_ws_example_json.hpp"
#include "mock_ws_connection.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace exchange::cryptocom::ws;
using json = nlohmann::json;
namespace fx = cryptocom_ws_fixtures;

namespace {

int64_t sent_id(const std::string& raw) { return json::parse(raw).at("id").get<int64_t>(); }

// A push update frame (id -1) for a subscription, wrapping the captured payload's
// result (so route_key + data come straight from the live fixture).
std::string push_from(const char* fixture) { return std::string(fixture); }

// A success ack echoing the client-assigned id, for a given channel/subscription.
std::string make_ack(int64_t id, const std::string& subscription, const std::string& channel) {
    return json{{"id", id}, {"method", "subscribe"}, {"code", 0},
                {"result", {{"subscription", subscription}, {"channel", channel}}}}.dump();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Channel-name helpers (must match what the server echoes in result.subscription)
// ─────────────────────────────────────────────────────────────────────────────

TEST(CryptoComWsChannels, Helpers) {
    EXPECT_EQ(ticker_channel("BTC_USD"), "ticker.BTC_USD");
    EXPECT_EQ(trade_channel("BTC_USD"), "trade.BTC_USD");
    EXPECT_EQ(book_channel("BTC_USD", 10), "book.BTC_USD.10");
    EXPECT_EQ(candlestick_channel("1m", "BTC_USD"), "candlestick.1m.BTC_USD");
    EXPECT_EQ(user_order_channel("BTC_USD"), "user.order.BTC_USD");
}

// ─────────────────────────────────────────────────────────────────────────────
// cryptocom_frame_descriptor
// ─────────────────────────────────────────────────────────────────────────────

TEST(CryptoComWsDescriptor, SubscribeAck_IsMethodResponseById) {
    auto d = cryptocom_frame_descriptor(json::parse(fx::SUBSCRIBE_ACK));
    EXPECT_EQ(d.kind, FrameKind::MethodResponse);
    ASSERT_TRUE(d.correlation_id.has_value());
    EXPECT_EQ(*d.correlation_id, "1");
    EXPECT_TRUE(d.route_key.empty());
}

TEST(CryptoComWsDescriptor, Update_IsPushRoutedBySubscription) {
    auto d = cryptocom_frame_descriptor(json::parse(fx::TICKER_PUSH));
    EXPECT_EQ(d.kind, FrameKind::PushMessage);
    EXPECT_EQ(d.route_key, "ticker.BTC_USD");
    EXPECT_FALSE(d.correlation_id.has_value());
}

TEST(CryptoComWsDescriptor, AuthReply_IsMethodResponseById) {
    auto d = cryptocom_frame_descriptor(json::parse(fx::AUTH_RESPONSE));
    EXPECT_EQ(d.kind, FrameKind::MethodResponse);
    ASSERT_TRUE(d.correlation_id.has_value());
    EXPECT_EQ(*d.correlation_id, "1");
}

TEST(CryptoComWsDescriptor, Heartbeat_IsUnknown) {
    // Heartbeats are answered by HeartbeatResponder; if one reaches the
    // descriptor it must not be mistaken for a method response.
    auto d = cryptocom_frame_descriptor(json::parse(fx::HEARTBEAT));
    EXPECT_EQ(d.kind, FrameKind::Unknown);
    EXPECT_FALSE(d.correlation_id.has_value());
}

TEST(CryptoComWsDescriptor, EmptyObject_IsUnknown) {
    EXPECT_EQ(cryptocom_frame_descriptor(json::parse("{}")).kind, FrameKind::Unknown);
}

// ─────────────────────────────────────────────────────────────────────────────
// Subscribe ack / auth parsing + signing
// ─────────────────────────────────────────────────────────────────────────────

TEST(CryptoComWsAck, SuccessFromResult) {
    auto a = CryptoComSubscribeAck::from_json(json::parse(fx::SUBSCRIBE_ACK));
    EXPECT_TRUE(a.success);
    EXPECT_FALSE(a.error.has_value());
    ASSERT_TRUE(a.id.has_value());
    EXPECT_EQ(*a.id, 1);
    EXPECT_EQ(a.channel, "ticker");
    EXPECT_EQ(a.subscription, "ticker.BTC_USD");
}

TEST(CryptoComWsAck, BareAck_ChannelFromTopLevel) {
    auto a = CryptoComSubscribeAck::from_json(json::parse(fx::SUBSCRIBE_ACK_BARE));
    EXPECT_TRUE(a.success);
    EXPECT_EQ(a.channel, "book.BTC_USD.10");
    EXPECT_TRUE(a.subscription.empty());
}

TEST(CryptoComWsAck, ErrorCode_NotSuccess) {
    auto a = CryptoComSubscribeAck::from_json(
        json::parse(R"({"id":3,"method":"subscribe","code":40003})"));
    EXPECT_FALSE(a.success);
    ASSERT_TRUE(a.error.has_value());
    EXPECT_NE(a.error->find("40003"), std::string::npos);
}

TEST(CryptoComWsAuth, RequestSignsAndParses) {
    CryptoComAuthRequest req;
    req.creds  = {"my-key", "my-secret"};
    req.req_id = 5;

    auto frame = req.to_json();
    EXPECT_EQ(frame.at("method"), "public/auth");
    EXPECT_EQ(frame.at("id").get<int64_t>(), 5);
    EXPECT_EQ(frame.at("api_key"), "my-key");
    ASSERT_TRUE(frame.contains("sig"));
    ASSERT_TRUE(frame.contains("nonce"));
    // sig signs method+id+api_key+nonce (empty params); recompute independently.
    const std::string sig = req.creds.sign(
        "public/auth", 5, json::object(), frame.at("nonce").get<int64_t>());
    EXPECT_EQ(frame.at("sig"), sig);

    auto resp = CryptoComAuthResponse::from_json(json::parse(fx::AUTH_RESPONSE));
    EXPECT_TRUE(resp.success);
}

// ─────────────────────────────────────────────────────────────────────────────
// Push event from_json — field assertions
// ─────────────────────────────────────────────────────────────────────────────

TEST(CryptoComWsEvents, Ticker) {
    auto e = CryptoComTickerEvent::from_json(json::parse(fx::TICKER_PUSH));
    EXPECT_EQ(e.subscription, "ticker.BTC_USD");
    EXPECT_EQ(e.instrument_name, "BTC_USD");
    EXPECT_DOUBLE_EQ(e.high, 64531.99);
    EXPECT_DOUBLE_EQ(e.last, 63958.47);
    EXPECT_DOUBLE_EQ(e.bid, 63962.64);
    EXPECT_DOUBLE_EQ(e.bid_size, 0.27217);
    EXPECT_DOUBLE_EQ(e.ask, 63962.65);
    EXPECT_DOUBLE_EQ(e.ask_size, 0.23532);
    EXPECT_DOUBLE_EQ(e.volume, 2657.5053);
    EXPECT_EQ(e.timestamp, 1782032455141);
}

TEST(CryptoComWsEvents, Trade) {
    auto e = CryptoComTradeEvent::from_json(json::parse(fx::TRADE_PUSH));
    EXPECT_EQ(e.subscription, "trade.BTC_USD");
    ASSERT_EQ(e.trades.size(), 1u);
    EXPECT_EQ(e.trades[0].trade_id, "1782032437671850129");
    EXPECT_DOUBLE_EQ(e.trades[0].price, 63958.47);
    EXPECT_DOUBLE_EQ(e.trades[0].quantity, 0.04333);
    EXPECT_EQ(e.trades[0].side, "SELL");  // WS feed side is UPPERCASE
    EXPECT_EQ(e.trades[0].match_id, "4611686018731788404");
}

TEST(CryptoComWsEvents, Book) {
    auto e = CryptoComBookEvent::from_json(json::parse(fx::BOOK_PUSH));
    EXPECT_EQ(e.subscription, "book.BTC_USD.10");
    EXPECT_EQ(e.depth, 10);
    EXPECT_EQ(e.t, 1782032455315);
    EXPECT_EQ(e.u, 338686661310464);
    ASSERT_EQ(e.bids.size(), 2u);
    ASSERT_EQ(e.asks.size(), 2u);
    EXPECT_DOUBLE_EQ(e.bids[0].price, 63962.64);
    EXPECT_DOUBLE_EQ(e.bids[0].size, 0.27217);
    EXPECT_EQ(e.bids[0].num_orders, 8);
    EXPECT_DOUBLE_EQ(e.asks[0].price, 63962.65);
    EXPECT_EQ(e.asks[0].num_orders, 4);
}

TEST(CryptoComWsEvents, Candlestick) {
    auto e = CryptoComCandlestickEvent::from_json(json::parse(fx::CANDLE_PUSH));
    EXPECT_EQ(e.subscription, "candlestick.1m.BTC_USD");
    EXPECT_EQ(e.interval, "1m");
    ASSERT_EQ(e.candles.size(), 1u);
    EXPECT_EQ(e.candles[0].t, 1782014460000);
    EXPECT_EQ(e.candles[0].ut, 1782014515996);
    EXPECT_DOUBLE_EQ(e.candles[0].o, 64349.71);
    EXPECT_DOUBLE_EQ(e.candles[0].c, 64356.28);
    EXPECT_DOUBLE_EQ(e.candles[0].v, 0.22838);
}

TEST(CryptoComWsEvents, UserOrder_Synthetic) {
    auto e = CryptoComUserOrderEvent::from_json(json::parse(fx::USER_ORDER_PUSH));
    EXPECT_EQ(e.subscription, "user.order.BTC_USD");
    ASSERT_EQ(e.orders.size(), 1u);
    EXPECT_EQ(e.orders[0].order_id, "18342311");
    EXPECT_EQ(e.orders[0].status, "ACTIVE");
    EXPECT_EQ(e.orders[0].side, "BUY");
    EXPECT_DOUBLE_EQ(e.orders[0].quantity, 0.001);
    EXPECT_DOUBLE_EQ(e.orders[0].limit_price, 63000.00);
}

TEST(CryptoComWsEvents, UserBalance_Synthetic) {
    auto e = CryptoComUserBalanceEvent::from_json(json::parse(fx::USER_BALANCE_PUSH));
    EXPECT_EQ(e.subscription, "user.balance");
    ASSERT_EQ(e.balances.size(), 1u);
    EXPECT_EQ(e.balances[0].instrument_name, "USD");
    EXPECT_DOUBLE_EQ(e.balances[0].total_available_balance, 1000.00);
}

// ─────────────────────────────────────────────────────────────────────────────
// HeartbeatResponder — auto-reply + swallow, forward otherwise
// ─────────────────────────────────────────────────────────────────────────────

TEST(CryptoComHeartbeat, AnswersAndSwallows) {
    auto inner = std::make_shared<MockWsConnection>();
    HeartbeatResponder hb{inner};

    std::vector<std::string> forwarded;
    hb.set_on_message([&](const std::string& raw) { forwarded.push_back(raw); });

    // A heartbeat is answered via the inner connection and NOT forwarded.
    inner->inject_message(std::string(fx::HEARTBEAT));
    ASSERT_EQ(inner->sent().size(), 1u);
    auto reply = json::parse(inner->sent()[0]);
    EXPECT_EQ(reply.at("method"), "public/respond-heartbeat");
    EXPECT_EQ(reply.at("id").get<int64_t>(), 1782032467342);
    EXPECT_TRUE(forwarded.empty());

    // A non-heartbeat frame is forwarded untouched and not answered.
    inner->inject_message(std::string(fx::TICKER_PUSH));
    ASSERT_EQ(forwarded.size(), 1u);
    EXPECT_EQ(forwarded[0], std::string(fx::TICKER_PUSH));
    EXPECT_EQ(inner->sent().size(), 1u);  // no extra reply
}

TEST(CryptoComHeartbeat, ForwardsTransportMethods) {
    auto inner = std::make_shared<MockWsConnection>();
    HeartbeatResponder hb{inner};
    EXPECT_FALSE(hb.is_connected());
    hb.connect();
    EXPECT_TRUE(hb.is_connected());
    hb.send("hello");
    ASSERT_EQ(inner->sent().size(), 1u);
    EXPECT_EQ(inner->sent()[0], "hello");
    hb.disconnect();
    EXPECT_FALSE(hb.is_connected());
}

// ─────────────────────────────────────────────────────────────────────────────
// Subscribe lifecycle through make_cryptocom_market_client (heartbeat-wrapped)
// ─────────────────────────────────────────────────────────────────────────────

TEST(CryptoComWsLifecycle, Subscribe_Ack_Push_Cancel) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_cryptocom_market_client(conn);
    conn->fire_open();

    CryptoComTickerSubscribe req;
    req.channel = ticker_channel("BTC_USD");

    std::vector<CryptoComTickerEvent> received;
    auto fut = client->subscribe_async(
        req, [&](CryptoComTickerEvent e) { received.push_back(std::move(e)); });

    // Phase 2 — SUBSCRIBE frame sent with the auto-assigned id.
    ASSERT_EQ(conn->sent().size(), 1u);
    auto sub = json::parse(conn->sent()[0]);
    EXPECT_EQ(sub.at("method"), "subscribe");
    EXPECT_EQ(sub.at("params").at("channels"), json::array({"ticker.BTC_USD"}));
    const auto id = sub.at("id").get<int64_t>();

    // Phase 3 — ack echoing the id; callback installed, handle active.
    conn->inject_message(make_ack(id, "ticker.BTC_USD", "ticker"));
    auto [ack, handle] = fut.get();
    EXPECT_TRUE(ack.ok);
    EXPECT_TRUE(handle.is_active());

    // Update (id -1) routes by subscription to the typed callback.
    conn->inject_message(push_from(fx::TICKER_PUSH));
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].instrument_name, "BTC_USD");
    EXPECT_DOUBLE_EQ(received[0].last, 63958.47);

    // A heartbeat arriving mid-stream is answered, not delivered as a push.
    conn->inject_message(std::string(fx::HEARTBEAT));
    EXPECT_EQ(received.size(), 1u);
    ASSERT_EQ(conn->sent().size(), 2u);
    EXPECT_EQ(json::parse(conn->sent()[1]).at("method"), "public/respond-heartbeat");

    // Cancel — UNSUBSCRIBE sent once; idempotent; callback removed.
    handle.cancel();
    ASSERT_EQ(conn->sent().size(), 3u);
    auto unsub = json::parse(conn->sent()[2]);
    EXPECT_EQ(unsub.at("method"), "unsubscribe");
    EXPECT_EQ(unsub.at("params").at("channels"), json::array({"ticker.BTC_USD"}));
    EXPECT_FALSE(handle.is_active());

    handle.cancel();
    EXPECT_EQ(conn->sent().size(), 3u);

    conn->inject_message(push_from(fx::TICKER_PUSH));
    EXPECT_EQ(received.size(), 1u);  // no callback after cancel
}

TEST(CryptoComWsLifecycle, Subscribe_BeforeOpen_QueuedAndFlushed) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_cryptocom_market_client(conn);
    // No fire_open() — the SUBSCRIBE must queue.

    CryptoComTradeSubscribe req;
    req.channel = trade_channel("BTC_USD");
    auto fut = client->subscribe_async(req, [](const CryptoComTradeEvent&) {});
    EXPECT_TRUE(conn->sent().empty());

    conn->fire_open();
    ASSERT_EQ(conn->sent().size(), 1u);
    EXPECT_EQ(json::parse(conn->sent()[0]).at("method"), "subscribe");

    conn->inject_message(make_ack(sent_id(conn->sent()[0]), "trade.BTC_USD", "trade"));
    auto [ack, handle] = fut.get();
    EXPECT_TRUE(ack.ok);
    EXPECT_TRUE(handle.is_active());
}

TEST(CryptoComWsLifecycle, Subscribe_ErrorAck_NoCallback) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_cryptocom_market_client(conn);
    conn->fire_open();

    CryptoComTickerSubscribe req;
    req.channel = ticker_channel("BTC_USD");
    int calls = 0;
    auto fut = client->subscribe_async(req, [&](const CryptoComTickerEvent&) { ++calls; });

    const auto id = sent_id(conn->sent().at(0));
    conn->inject_message(
        json{{"id", id}, {"method", "subscribe"}, {"code", 40003}}.dump());

    auto [ack, handle] = fut.get();
    EXPECT_FALSE(ack.ok);
    EXPECT_FALSE(handle.is_active());

    conn->inject_message(push_from(fx::TICKER_PUSH));
    EXPECT_EQ(calls, 0);
}

TEST(CryptoComWsLifecycle, UserFeed_AuthThenSubscribe) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_cryptocom_user_client(conn);
    conn->fire_open();

    // public/auth via execute() — signs, resolves on the id-matched reply.
    CryptoComAuthRequest auth;
    auth.creds = {"my-key", "my-secret"};
    auto auth_fut = client->execute_async(auth);
    ASSERT_EQ(conn->sent().size(), 1u);
    auto auth_frame = json::parse(conn->sent()[0]);
    EXPECT_EQ(auth_frame.at("method"), "public/auth");
    const auto auth_id = auth_frame.at("id").get<int64_t>();
    conn->inject_message(
        json{{"id", auth_id}, {"method", "public/auth"}, {"code", 0}}.dump());
    EXPECT_TRUE(auth_fut.get().ok);

    // Then subscribe to user.order and receive an update.
    CryptoComUserOrderSubscribe req;
    req.channel = user_order_channel("BTC_USD");
    std::vector<CryptoComUserOrderEvent> got;
    auto fut = client->subscribe_async(req, [&](CryptoComUserOrderEvent e) { got.push_back(std::move(e)); });
    const auto sub_id = sent_id(conn->sent().at(1));
    conn->inject_message(make_ack(sub_id, "user.order.BTC_USD", "user.order"));
    auto [ack, handle] = fut.get();
    EXPECT_TRUE(ack.ok);

    conn->inject_message(push_from(fx::USER_ORDER_PUSH));
    ASSERT_EQ(got.size(), 1u);
    ASSERT_EQ(got[0].orders.size(), 1u);
    EXPECT_EQ(got[0].orders[0].order_id, "18342311");
}
