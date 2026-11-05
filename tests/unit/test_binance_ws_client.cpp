// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Binance WebSocket unit tests, both surfaces — market streams (frame
// descriptor classification, ack parsing, event from_json field assertions,
// subscribe lifecycle) and the WS API trading endpoint (descriptor, reply
// envelope, signed requests, execute lifecycle) — all through ExchangeWsClient
// with a mock connection. No network I/O.

#include "exchange/binance/ws_api.hpp"
#include "exchange/binance/ws_streams.hpp"
#include "assert_has_value.hpp"
#include "binance_ws_api_example_json.hpp"
#include "binance_ws_stream_example_json.hpp"
#include "mock_ws_connection.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace exchange::binance::ws;
using namespace exchange::binance;  // canonical enums (Side, OrderType, …)
namespace fixtures = exchange::binance::ws::test;

namespace {

// Client backed by a MockWsConnection. Tests fire_open() themselves for
// precise control over the pre-connection outbound queue.
std::pair<std::shared_ptr<BinanceStreamClient>, std::shared_ptr<MockWsConnection>>
make_mock_client() {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_binance_stream_client(conn);
    return {client, conn};
}

// Extracts the client-assigned id from a sent SUBSCRIBE frame.
int64_t sent_id(const std::string& raw) {
    return json::parse(raw).at("id").get<int64_t>();
}

// Success ack matching a sent request id.
std::string make_ack(int64_t id) {
    return json{{"result", nullptr}, {"id", id}}.dump();
}

// Wraps a bare payload in the combined-stream envelope dispatch delivers.
std::string wrap(const std::string& stream, const char* bare_payload) {
    return json{{"stream", stream}, {"data", json::parse(bare_payload)}}.dump();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// binance_stream_frame_descriptor — appendix §5 classification
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceStreamDescriptor, SuccessAck_IsMethodResponse) {
    auto d = binance_stream_frame_descriptor(json::parse(fixtures::kSubscribeAckJson));
    EXPECT_EQ(d.kind, FrameKind::MethodResponse);
    ASSERT_HAS_VALUE(d.correlation_id);
    EXPECT_EQ(*d.correlation_id, "1");
    EXPECT_TRUE(d.route_key.empty());
}

TEST(BinanceStreamDescriptor, ErrorAck_IsMethodResponse) {
    auto d = binance_stream_frame_descriptor(json::parse(fixtures::kErrorAckJson));
    EXPECT_EQ(d.kind, FrameKind::MethodResponse);
    ASSERT_HAS_VALUE(d.correlation_id);
    EXPECT_EQ(*d.correlation_id, "7");
}

TEST(BinanceStreamDescriptor, WrappedPush_RoutesByStreamName) {
    auto d = binance_stream_frame_descriptor(json::parse(fixtures::kWrappedAggTradeJson));
    EXPECT_EQ(d.kind, FrameKind::PushMessage);
    EXPECT_EQ(d.route_key, "bnbbtc@aggTrade");
    EXPECT_FALSE(d.correlation_id.has_value());
}

TEST(BinanceStreamDescriptor, EmptyObject_IsUnknown) {
    auto d = binance_stream_frame_descriptor(json::parse("{}"));
    EXPECT_EQ(d.kind, FrameKind::Unknown);
}

TEST(BinanceStreamDescriptor, NullIdErrorFrame_IsUnknown) {
    // Binance sends id:null when the request itself was malformed JSON —
    // uncorrelatable, so the descriptor must not invent a correlation id.
    auto d = binance_stream_frame_descriptor(
        json::parse(R"({"error":{"code":2,"msg":"Invalid request"},"id":null})"));
    EXPECT_EQ(d.kind, FrameKind::Unknown);
    EXPECT_FALSE(d.correlation_id.has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// BinanceStreamAck::from_json
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceStreamAck, SuccessAck_FromJson) {
    auto a = BinanceStreamAck::from_json(json::parse(fixtures::kSubscribeAckJson));
    EXPECT_TRUE(a.success);
    EXPECT_FALSE(a.error.has_value());
    EXPECT_EQ(a.id, 1);
}

TEST(BinanceStreamAck, ErrorAck_FromJson) {
    auto a = BinanceStreamAck::from_json(json::parse(fixtures::kErrorAckJson));
    EXPECT_FALSE(a.success);
    ASSERT_HAS_VALUE(a.error);
    EXPECT_EQ(*a.error, "Invalid request: subscription id not provided");
    EXPECT_EQ(a.id, 7);
}

// ─────────────────────────────────────────────────────────────────────────────
// Flat push event types — from_json field assertions (Step 7.2)
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceStreamEvents, AggTrade_FromJson) {
    auto e = BinanceAggTradeEvent::from_json(json::parse(fixtures::kAggTradeJson));
    EXPECT_EQ(e.event_time, 1672515782136LL);
    EXPECT_EQ(e.symbol, "BNBBTC");
    EXPECT_EQ(e.agg_trade_id, 12345);
    EXPECT_DOUBLE_EQ(e.price, 0.001);
    EXPECT_DOUBLE_EQ(e.qty, 100.0);
    EXPECT_EQ(e.first_trade_id, 100);
    EXPECT_EQ(e.last_trade_id, 105);
    EXPECT_EQ(e.trade_time, 1672515782136LL);
    EXPECT_TRUE(e.is_buyer_maker);
}

// Pins the decision-4 unwrap rule: dispatch hands push callbacks the whole
// {"stream","data"} frame, and from_json must parse it identically to the
// bare payload.
TEST(BinanceStreamEvents, AggTrade_ParsesWrappedFrame) {
    auto bare    = BinanceAggTradeEvent::from_json(json::parse(fixtures::kAggTradeJson));
    auto wrapped = BinanceAggTradeEvent::from_json(json::parse(fixtures::kWrappedAggTradeJson));
    EXPECT_EQ(wrapped.event_time, bare.event_time);
    EXPECT_EQ(wrapped.symbol, bare.symbol);
    EXPECT_EQ(wrapped.agg_trade_id, bare.agg_trade_id);
    EXPECT_DOUBLE_EQ(wrapped.price, bare.price);
    EXPECT_DOUBLE_EQ(wrapped.qty, bare.qty);
    EXPECT_EQ(wrapped.first_trade_id, bare.first_trade_id);
    EXPECT_EQ(wrapped.last_trade_id, bare.last_trade_id);
    EXPECT_EQ(wrapped.trade_time, bare.trade_time);
    EXPECT_EQ(wrapped.is_buyer_maker, bare.is_buyer_maker);
}

TEST(BinanceStreamEvents, Trade_FromJson) {
    auto e = BinanceTradeEvent::from_json(json::parse(fixtures::kTradeJson));
    EXPECT_EQ(e.event_time, 1672515782136LL);
    EXPECT_EQ(e.symbol, "BNBBTC");
    EXPECT_EQ(e.trade_id, 12345);
    EXPECT_DOUBLE_EQ(e.price, 0.001);
    EXPECT_DOUBLE_EQ(e.qty, 100.0);
    EXPECT_EQ(e.trade_time, 1672515782136LL);
    EXPECT_TRUE(e.is_buyer_maker);
}

TEST(BinanceStreamEvents, Ticker_FromJson) {
    auto e = BinanceTickerEvent::from_json(json::parse(fixtures::kTickerJson));
    EXPECT_EQ(e.event_time, 1672515782136LL);
    EXPECT_EQ(e.symbol, "BNBBTC");
    EXPECT_DOUBLE_EQ(e.price_change, 0.0015);
    EXPECT_DOUBLE_EQ(e.price_change_pct, 250.00);
    EXPECT_DOUBLE_EQ(e.weighted_avg_price, 0.0018);
    EXPECT_DOUBLE_EQ(e.prev_close, 0.0009);
    EXPECT_DOUBLE_EQ(e.last_price, 0.0025);
    EXPECT_DOUBLE_EQ(e.last_qty, 10.0);
    EXPECT_DOUBLE_EQ(e.bid_price, 0.0024);
    EXPECT_DOUBLE_EQ(e.bid_qty, 10.0);
    EXPECT_DOUBLE_EQ(e.ask_price, 0.0026);
    EXPECT_DOUBLE_EQ(e.ask_qty, 100.0);
    EXPECT_DOUBLE_EQ(e.open, 0.0010);
    EXPECT_DOUBLE_EQ(e.high, 0.0025);
    EXPECT_DOUBLE_EQ(e.low, 0.0010);
    EXPECT_DOUBLE_EQ(e.volume, 10000.0);
    EXPECT_DOUBLE_EQ(e.quote_volume, 18.0);
    EXPECT_EQ(e.stats_open_time, 0);
    EXPECT_EQ(e.stats_close_time, 86400000);
    EXPECT_EQ(e.first_trade_id, 0);
    EXPECT_EQ(e.last_trade_id, 18150);
    EXPECT_EQ(e.num_trades, 18151);
}

TEST(BinanceStreamEvents, MiniTicker_FromJson) {
    auto e = BinanceMiniTickerEvent::from_json(json::parse(fixtures::kMiniTickerJson));
    EXPECT_EQ(e.event_time, 1672515782136LL);
    EXPECT_EQ(e.symbol, "BNBBTC");
    EXPECT_DOUBLE_EQ(e.close, 0.0025);
    EXPECT_DOUBLE_EQ(e.open, 0.0010);
    EXPECT_DOUBLE_EQ(e.high, 0.0025);
    EXPECT_DOUBLE_EQ(e.low, 0.0010);
    EXPECT_DOUBLE_EQ(e.volume, 10000.0);
    EXPECT_DOUBLE_EQ(e.quote_volume, 18.0);
}

TEST(BinanceStreamEvents, BookTicker_FromJson) {
    auto e = BinanceBookTickerEvent::from_json(json::parse(fixtures::kBookTickerJson));
    EXPECT_EQ(e.update_id, 400900217);
    EXPECT_EQ(e.symbol, "BNBUSDT");
    EXPECT_DOUBLE_EQ(e.bid_price, 25.3519);
    EXPECT_DOUBLE_EQ(e.bid_qty, 31.21);
    EXPECT_DOUBLE_EQ(e.ask_price, 25.3652);
    EXPECT_DOUBLE_EQ(e.ask_qty, 40.66);
}

// ─────────────────────────────────────────────────────────────────────────────
// Structured push event types — kline + depth (Step 7.3)
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceStreamEvents, Kline_FromJson) {
    auto e = BinanceKlineEvent::from_json(json::parse(fixtures::kKlineJson));
    EXPECT_EQ(e.event_time, 1672515782136LL);
    EXPECT_EQ(e.symbol, "BNBBTC");

    const auto& k = e.kline;
    EXPECT_EQ(k.start_time, 1672515780000LL);
    EXPECT_EQ(k.close_time, 1672515839999LL);
    EXPECT_EQ(k.symbol, "BNBBTC");
    EXPECT_EQ(k.interval, "1m");
    EXPECT_EQ(k.first_trade_id, 100);
    EXPECT_EQ(k.last_trade_id, 200);
    EXPECT_DOUBLE_EQ(k.open, 0.0010);
    EXPECT_DOUBLE_EQ(k.close, 0.0020);
    EXPECT_DOUBLE_EQ(k.high, 0.0025);
    EXPECT_DOUBLE_EQ(k.low, 0.0015);
    EXPECT_DOUBLE_EQ(k.volume, 1000.0);
    EXPECT_DOUBLE_EQ(k.quote_volume, 1.0);
    EXPECT_DOUBLE_EQ(k.taker_buy_base_volume, 500.0);
    EXPECT_DOUBLE_EQ(k.taker_buy_quote_volume, 0.5);
    EXPECT_EQ(k.num_trades, 100);
    EXPECT_FALSE(k.is_closed);
}

TEST(BinanceStreamEvents, DepthUpdate_FromJson) {
    auto e = BinanceDepthUpdateEvent::from_json(json::parse(fixtures::kDepthUpdateJson));
    EXPECT_EQ(e.event_time, 1672515782136LL);
    EXPECT_EQ(e.symbol, "BNBBTC");
    EXPECT_EQ(e.first_update_id, 157);
    EXPECT_EQ(e.final_update_id, 160);
    ASSERT_EQ(e.bids.size(), 1u);
    EXPECT_DOUBLE_EQ(e.bids[0].price, 0.0024);
    EXPECT_DOUBLE_EQ(e.bids[0].quantity, 10.0);
    ASSERT_EQ(e.asks.size(), 1u);
    EXPECT_DOUBLE_EQ(e.asks[0].price, 0.0026);
    EXPECT_DOUBLE_EQ(e.asks[0].quantity, 100.0);
}

TEST(BinanceStreamEvents, PartialDepth_FromJson) {
    auto d = BinancePartialDepth::from_json(json::parse(fixtures::kPartialDepthJson));
    EXPECT_EQ(d.last_update_id, 160);
    ASSERT_EQ(d.bids.size(), 1u);
    EXPECT_DOUBLE_EQ(d.bids[0].price, 0.0024);
    EXPECT_DOUBLE_EQ(d.bids[0].quantity, 10.0);
    ASSERT_EQ(d.asks.size(), 1u);
    EXPECT_DOUBLE_EQ(d.asks[0].price, 0.0026);
    EXPECT_DOUBLE_EQ(d.asks[0].quantity, 100.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Subscribe request scaffold + stream-name helpers (Step 7.4)
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceStreamSubscribe, SubscribeRequest_ToJson) {
    BinanceAggTradeSubscribe req;
    req.stream = agg_trade_stream("BNBBTC");
    req.req_id = 42;

    EXPECT_EQ(req.route_key(), "bnbbtc@aggTrade");

    auto expected_sub = json::parse(
        R"({"method":"SUBSCRIBE","params":["bnbbtc@aggTrade"],"id":42})");
    EXPECT_EQ(req.to_json(), expected_sub);

    auto expected_unsub = json::parse(
        R"({"method":"UNSUBSCRIBE","params":["bnbbtc@aggTrade"],"id":42})");
    EXPECT_EQ(req.unsubscribe_json(), expected_unsub);
}

TEST(BinanceStreamSubscribe, StreamHelpers_LowercaseSymbol) {
    EXPECT_EQ(agg_trade_stream("BNBBTC"), "bnbbtc@aggTrade");
    EXPECT_EQ(trade_stream("BNBBTC"), "bnbbtc@trade");
    EXPECT_EQ(kline_stream("BTCUSDT", "1m"), "btcusdt@kline_1m");
    EXPECT_EQ(ticker_stream("BNBBTC"), "bnbbtc@ticker");
    EXPECT_EQ(mini_ticker_stream("BNBBTC"), "bnbbtc@miniTicker");
    EXPECT_EQ(book_ticker_stream("BNBUSDT"), "bnbusdt@bookTicker");
    EXPECT_EQ(depth_stream("BNBBTC"), "bnbbtc@depth");
    EXPECT_EQ(partial_depth_stream("BNBBTC", 5), "bnbbtc@depth5");
}

// ─────────────────────────────────────────────────────────────────────────────
// Subscribe lifecycle through ExchangeWsClient (mock connection, no network)
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceStreamLifecycle, Subscribe_Lifecycle) {
    auto [client, conn] = make_mock_client();
    conn->fire_open();

    BinanceAggTradeSubscribe req;
    req.stream = agg_trade_stream("BNBBTC");

    std::vector<BinanceAggTradeEvent> received;
    auto fut = client->subscribe_async(
        req, [&](BinanceAggTradeEvent e) { received.push_back(std::move(e)); });

    // Phase 2 — SUBSCRIBE frame sent with the auto-assigned id.
    ASSERT_EQ(conn->sent().size(), 1u);
    auto sub_frame = json::parse(conn->sent()[0]);
    EXPECT_EQ(sub_frame.at("method"), "SUBSCRIBE");
    EXPECT_EQ(sub_frame.at("params"), json::array({"bnbbtc@aggTrade"}));
    const auto id = sub_frame.at("id").get<int64_t>();

    // Phase 3 — ack matched by id; callback installed, handle active.
    conn->inject_message(make_ack(id));
    auto [ack, handle] = fut.get();
    EXPECT_TRUE(ack.ok);
    ASSERT_HAS_VALUE(ack.result);
    EXPECT_EQ(ack.result->id, id);
    EXPECT_TRUE(handle.is_active());

    // Push — wrapped frame routes by stream name to the typed callback.
    conn->inject_message(wrap("bnbbtc@aggTrade", fixtures::kAggTradeJson));
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].symbol, "BNBBTC");
    EXPECT_EQ(received[0].agg_trade_id, 12345);
    EXPECT_DOUBLE_EQ(received[0].price, 0.001);

    // Cancel — UNSUBSCRIBE sent once; idempotent on second call.
    handle.cancel();
    ASSERT_EQ(conn->sent().size(), 2u);
    auto unsub_frame = json::parse(conn->sent()[1]);
    EXPECT_EQ(unsub_frame.at("method"), "UNSUBSCRIBE");
    EXPECT_EQ(unsub_frame.at("params"), json::array({"bnbbtc@aggTrade"}));
    EXPECT_EQ(unsub_frame.at("id").get<int64_t>(), id);
    EXPECT_FALSE(handle.is_active());

    handle.cancel();
    EXPECT_EQ(conn->sent().size(), 2u);

    // After cancel the push callback is gone.
    conn->inject_message(wrap("bnbbtc@aggTrade", fixtures::kAggTradeJson));
    EXPECT_EQ(received.size(), 1u);
}

TEST(BinanceStreamLifecycle, Subscribe_ErrorAck_NoCallbackInstalled) {
    auto [client, conn] = make_mock_client();
    conn->fire_open();

    BinanceAggTradeSubscribe req;
    req.stream = agg_trade_stream("BNBBTC");

    int callback_count = 0;
    auto fut = client->subscribe_async(
        req, [&](const BinanceAggTradeEvent&) { ++callback_count; });

    const auto id = sent_id(conn->sent().at(0));
    conn->inject_message(
        json{{"error", {{"code", 2}, {"msg", "Invalid request"}}}, {"id", id}}.dump());

    auto [ack, handle] = fut.get();
    EXPECT_FALSE(ack.ok);
    ASSERT_HAS_VALUE(ack.error);
    EXPECT_EQ(*ack.error, "Invalid request");
    EXPECT_FALSE(handle.is_active());

    // No callback was installed — a push frame must not fire it.
    conn->inject_message(wrap("bnbbtc@aggTrade", fixtures::kAggTradeJson));
    EXPECT_EQ(callback_count, 0);
}

TEST(BinanceStreamLifecycle, Subscribe_BeforeOpen_QueuedAndFlushed) {
    auto [client, conn] = make_mock_client();
    // No fire_open() yet — the SUBSCRIBE must queue, not send.

    BinanceTradeSubscribe req;
    req.stream = trade_stream("BNBBTC");

    auto fut = client->subscribe_async(req, [](const BinanceTradeEvent&) {});
    EXPECT_TRUE(conn->sent().empty());

    conn->fire_open();
    ASSERT_EQ(conn->sent().size(), 1u);
    EXPECT_EQ(json::parse(conn->sent()[0]).at("method"), "SUBSCRIBE");

    // Complete the handshake so the pending future doesn't dangle.
    conn->inject_message(make_ack(sent_id(conn->sent()[0])));
    auto [ack, handle] = fut.get();
    EXPECT_TRUE(ack.ok);
    EXPECT_TRUE(handle.is_active());
}

TEST(BinanceStreamLifecycle, TwoStreamsOneConnection) {
    auto [client, conn] = make_mock_client();
    conn->fire_open();

    BinanceAggTradeSubscribe agg_req;
    agg_req.stream = agg_trade_stream("BNBBTC");
    BinanceBookTickerSubscribe bt_req;
    bt_req.stream = book_ticker_stream("BNBUSDT");

    int agg_count = 0;
    int bt_count  = 0;
    auto agg_fut = client->subscribe_async(
        agg_req, [&](const BinanceAggTradeEvent& e) {
            ++agg_count;
            EXPECT_EQ(e.symbol, "BNBBTC");
        });
    auto bt_fut = client->subscribe_async(
        bt_req, [&](const BinanceBookTickerEvent& e) {
            ++bt_count;
            EXPECT_EQ(e.symbol, "BNBUSDT");
        });

    ASSERT_EQ(conn->sent().size(), 2u);
    conn->inject_message(make_ack(sent_id(conn->sent()[0])));
    conn->inject_message(make_ack(sent_id(conn->sent()[1])));

    auto [agg_ack, agg_handle] = agg_fut.get();
    auto [bt_ack, bt_handle]   = bt_fut.get();
    EXPECT_TRUE(agg_ack.ok);
    EXPECT_TRUE(bt_ack.ok);
    EXPECT_TRUE(agg_handle.is_active());
    EXPECT_TRUE(bt_handle.is_active());

    // Pushes route to the right callbacks by route_key — no cross-fire.
    conn->inject_message(wrap("bnbbtc@aggTrade", fixtures::kAggTradeJson));
    conn->inject_message(wrap("bnbusdt@bookTicker", fixtures::kBookTickerJson));
    conn->inject_message(wrap("bnbbtc@aggTrade", fixtures::kAggTradeJson));

    EXPECT_EQ(agg_count, 2);
    EXPECT_EQ(bt_count, 1);

    // Cancelling one stream leaves the other live.
    agg_handle.cancel();
    conn->inject_message(wrap("bnbbtc@aggTrade", fixtures::kAggTradeJson));
    conn->inject_message(wrap("bnbusdt@bookTicker", fixtures::kBookTickerJson));
    EXPECT_EQ(agg_count, 2);
    EXPECT_EQ(bt_count, 2);
}

// ═════════════════════════════════════════════════════════════════════════════
// WS API (trading endpoint) — appendix §4/§5
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// binance_ws_api_frame_descriptor — every reply is a MethodResponse by id
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceWsApiDescriptor, SuccessReply_IsMethodResponse) {
    auto d = binance_ws_api_frame_descriptor(
        json::parse(fixtures::kWsApiOrderPlaceSuccessJson));
    EXPECT_EQ(d.kind, FrameKind::MethodResponse);
    ASSERT_HAS_VALUE(d.correlation_id);
    EXPECT_EQ(*d.correlation_id, "e2a85d9f-07a5-4f94-8d5f-789dc3deb097");
    EXPECT_TRUE(d.route_key.empty());
}

TEST(BinanceWsApiDescriptor, ErrorReply_IsMethodResponse) {
    auto d = binance_ws_api_frame_descriptor(
        json::parse(fixtures::kWsApiOrderPlaceErrorJson));
    EXPECT_EQ(d.kind, FrameKind::MethodResponse);
    ASSERT_HAS_VALUE(d.correlation_id);
    EXPECT_EQ(*d.correlation_id, "e2a85d9f-07a5-4f94-8d5f-789dc3deb097");
}

TEST(BinanceWsApiDescriptor, IntId_StringifiesLikePendingKey) {
    // The client keys pending handlers by std::to_string(req_id) — an int id
    // must stringify identically or replies never match their futures.
    auto d = binance_ws_api_frame_descriptor(
        json::parse(R"({"id":7,"status":200,"result":{}})"));
    EXPECT_EQ(d.kind, FrameKind::MethodResponse);
    ASSERT_HAS_VALUE(d.correlation_id);
    EXPECT_EQ(*d.correlation_id, "7");
}

TEST(BinanceWsApiDescriptor, NoUsableId_IsUnknown) {
    EXPECT_EQ(binance_ws_api_frame_descriptor(json::parse("{}")).kind,
              FrameKind::Unknown);

    // id:null — Binance's reply to malformed request JSON; uncorrelatable.
    auto d = binance_ws_api_frame_descriptor(json::parse(
        R"({"id":null,"status":400,"error":{"code":-32700,"msg":"JSON parse error"}})"));
    EXPECT_EQ(d.kind, FrameKind::Unknown);
    EXPECT_FALSE(d.correlation_id.has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// BinanceWsApiResponse envelope — parsed here via BinanceWsPongMessage, the
// envelope-only response type (the typed order results land in step 8.3)
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceWsApiEnvelope, Pong_Success) {
    auto m = BinanceWsPongMessage::from_json(json::parse(fixtures::kWsApiPongJson));
    EXPECT_TRUE(m.success);
    EXPECT_EQ(m.status, 200);
    ASSERT_HAS_VALUE(m.id);
    EXPECT_EQ(*m.id, 1);
    EXPECT_FALSE(m.error.has_value());
    EXPECT_FALSE(m.error_code.has_value());
    EXPECT_TRUE(m.rate_limits.empty());
}

TEST(BinanceWsApiEnvelope, Error_PopulatesCodeAndMsg) {
    auto m = BinanceWsPongMessage::from_json(
        json::parse(fixtures::kWsApiOrderPlaceErrorJson));
    EXPECT_FALSE(m.success);
    EXPECT_EQ(m.status, 400);
    ASSERT_HAS_VALUE(m.error);
    EXPECT_EQ(*m.error, "Account has insufficient balance for requested action.");
    ASSERT_HAS_VALUE(m.error_code);
    EXPECT_EQ(*m.error_code, -2010);
    // String id is tolerated but not stored in the int slot.
    EXPECT_FALSE(m.id.has_value());
}

TEST(BinanceWsApiEnvelope, RateLimits_Parsed) {
    auto m = BinanceWsPongMessage::from_json(
        json::parse(fixtures::kWsApiOrderPlaceSuccessJson));
    EXPECT_TRUE(m.success);
    ASSERT_EQ(m.rate_limits.size(), 1u);
    EXPECT_EQ(m.rate_limits[0].rate_limit_type, "ORDERS");
    EXPECT_EQ(m.rate_limits[0].interval, "SECOND");
    EXPECT_EQ(m.rate_limits[0].interval_num, 10);
    EXPECT_EQ(m.rate_limits[0].limit, 50);
    EXPECT_EQ(m.rate_limits[0].count, 12);
}

// ─────────────────────────────────────────────────────────────────────────────
// BinanceWsPingRequest
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceWsApiPing, ToJson) {
    BinanceWsPingRequest req;
    req.req_id = 42;
    EXPECT_EQ(req.to_json(), json::parse(R"({"id":42,"method":"ping"})"));
}

// ─────────────────────────────────────────────────────────────────────────────
// detail::ws_sign_params — sorted-payload HMAC-SHA256 signing
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Expected signature recomputed over the exact expected sorted payload with
// the same primitives (the test_binance_client precedent).
std::string ws_sig(const std::string& secret, const std::string& payload) {
    namespace rd = exchange::binance::rest::detail;
    return rd::to_hex(rd::hmac_sha256(secret, payload));
}

BinanceWsCredentials test_ws_creds(int recv_window_ms = 0) {
    BinanceWsCredentials creds;
    creds.api_key        = "testApiKey";
    creds.secret_key     = "testSecret";
    creds.recv_window_ms = recv_window_ms;
    return creds;
}

} // namespace

TEST(BinanceWsSignParams, DeterministicSignature) {
    json params{{"symbol", "BTCUSDT"}, {"side", "BUY"}};
    detail::ws_sign_params(params, test_ws_creds(), 1655716096498);

    EXPECT_EQ(params.at("apiKey"), "testApiKey");
    EXPECT_EQ(params.at("timestamp"), 1655716096498);
    EXPECT_EQ(params.at("signature"),
              ws_sig("testSecret",
                     "apiKey=testApiKey&side=BUY&symbol=BTCUSDT"
                     "&timestamp=1655716096498"));
}

TEST(BinanceWsSignParams, PayloadIsAlphabeticallySorted) {
    // Keys inserted in reverse-alphabetical order must still sign in sorted
    // order — pins the std::map-backed-object assumption against a future
    // switch to ordered_json.
    json params;
    params["zzz"] = "last";
    params["aaa"] = "first";
    detail::ws_sign_params(params, test_ws_creds(), 1000);

    EXPECT_EQ(params.at("signature"),
              ws_sig("testSecret",
                     "aaa=first&apiKey=testApiKey&timestamp=1000&zzz=last"));
}

TEST(BinanceWsSignParams, RecvWindowIncludedWhenSet) {
    json with_window{{"symbol", "BTCUSDT"}};
    detail::ws_sign_params(with_window, test_ws_creds(5000), 1000);
    EXPECT_EQ(with_window.at("recvWindow"), 5000);
    EXPECT_EQ(with_window.at("signature"),
              ws_sig("testSecret",
                     "apiKey=testApiKey&recvWindow=5000&symbol=BTCUSDT"
                     "&timestamp=1000"));

    json without_window{{"symbol", "BTCUSDT"}};
    detail::ws_sign_params(without_window, test_ws_creds(0), 1000);
    EXPECT_FALSE(without_window.contains("recvWindow"));
}

TEST(BinanceWsSignParams, ValueRendering_StringsRawIntsAsDigits) {
    // String values render unquoted; non-strings via dump(). A quoted string
    // or a stray decimal point would produce a different signature.
    json params{{"price", "0.10"}, {"orderId", 12510053279}};
    detail::ws_sign_params(params, test_ws_creds(), 1000);

    EXPECT_EQ(params.at("signature"),
              ws_sig("testSecret",
                     "apiKey=testApiKey&orderId=12510053279&price=0.10"
                     "&timestamp=1000"));
}

TEST(BinanceWsSignParams, RejectsNonHmacAlgorithm) {
    // Review M1: a non-HMAC algorithm must be rejected, not silently HMAC-signed.
    json params{{"symbol", "BTCUSDT"}};
    auto creds      = test_ws_creds();
    creds.algorithm = exchange::binance::rest::BinanceSignAlgorithm::Rsa;
    EXPECT_THROW(detail::ws_sign_params(params, creds, 1000), std::invalid_argument);
}

TEST(BinanceWsSignParams, RejectsFloatParam) {
    // Review L3: a floating-point param would sign non-canonically (dump() may
    // use scientific notation), so it must be rejected — pass decimal strings.
    json params{{"price", 0.1}};  // float, not a caller-formatted "0.1" string
    EXPECT_THROW(detail::ws_sign_params(params, test_ws_creds(), 1000), std::invalid_argument);
}

// ─────────────────────────────────────────────────────────────────────────────
// order.place / order.cancel — signed frames + reply parsing (appendix §4)
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceWsApiNewOrder, ToJson) {
    BinanceWsNewOrderRequest req;
    req.creds         = test_ws_creds();
    req.timestamp     = 1655716096498;
    req.req_id        = 7;
    req.symbol        = "BTCUSDT";
    req.side          = Side::Buy;
    req.type          = OrderType::Limit;
    req.time_in_force = TimeInForce::GTC;
    req.quantity      = "10";
    req.price         = "0.10";

    const json expected_sig = ws_sig(
        "testSecret",
        "apiKey=testApiKey&price=0.10&quantity=10&side=BUY&symbol=BTCUSDT"
        "&timeInForce=GTC&timestamp=1655716096498&type=LIMIT");

    const json expected{
        {"id", 7},
        {"method", "order.place"},
        {"params",
         {{"symbol", "BTCUSDT"},
          {"side", "BUY"},
          {"type", "LIMIT"},
          {"timeInForce", "GTC"},
          {"quantity", "10"},
          {"price", "0.10"},
          {"apiKey", "testApiKey"},
          {"timestamp", 1655716096498},
          {"signature", expected_sig}}}};

    const json frame = req.to_json();
    EXPECT_EQ(frame, expected);

    // Enum wire strings and absence of every unset optional.
    const json& p = frame.at("params");
    EXPECT_EQ(p.at("side"), "BUY");
    EXPECT_EQ(p.at("type"), "LIMIT");
    EXPECT_EQ(p.at("timeInForce"), "GTC");
    EXPECT_FALSE(p.contains("quoteOrderQty"));
    EXPECT_FALSE(p.contains("newClientOrderId"));
    EXPECT_FALSE(p.contains("stopPrice"));
    EXPECT_FALSE(p.contains("icebergQty"));
    EXPECT_FALSE(p.contains("newOrderRespType"));
    EXPECT_FALSE(p.contains("recvWindow"));
}

TEST(BinanceWsApiNewOrder, ParsesSuccess) {
    const auto resp =
        BinanceWsNewOrderResponse::from_json(json::parse(fixtures::kWsApiOrderPlaceSuccessJson));

    EXPECT_TRUE(resp.success);
    EXPECT_EQ(resp.status, 200);
    ASSERT_HAS_VALUE(resp.order);
    EXPECT_EQ(resp.order->order_id, 12510053279);
    EXPECT_EQ(resp.order->symbol, "BTCUSDT");
    ASSERT_HAS_VALUE(resp.order->status);
    EXPECT_EQ(*resp.order->status, OrderStatus::New);
    EXPECT_EQ(resp.order->transact_time, 1655716096505);
    ASSERT_HAS_VALUE(resp.order->price);
    EXPECT_DOUBLE_EQ(*resp.order->price, 0.10);

    ASSERT_EQ(resp.rate_limits.size(), 1u);
    EXPECT_EQ(resp.rate_limits[0].count, 12);
}

TEST(BinanceWsApiNewOrder, ParsesError) {
    const auto resp =
        BinanceWsNewOrderResponse::from_json(json::parse(fixtures::kWsApiOrderPlaceErrorJson));

    EXPECT_FALSE(resp.success);
    EXPECT_EQ(resp.status, 400);
    ASSERT_HAS_VALUE(resp.error);
    EXPECT_EQ(*resp.error, "Account has insufficient balance for requested action.");
    ASSERT_HAS_VALUE(resp.error_code);
    EXPECT_EQ(*resp.error_code, -2010);
    EXPECT_FALSE(resp.order.has_value());
}

TEST(BinanceWsApiCancel, ToJson_ByOrderId) {
    BinanceWsCancelOrderRequest req;
    req.creds     = test_ws_creds();
    req.timestamp = 1000;
    req.req_id    = 8;
    req.symbol    = "BTCUSDT";
    req.order_id  = 12510053279;

    const json expected{
        {"id", 8},
        {"method", "order.cancel"},
        {"params",
         {{"symbol", "BTCUSDT"},
          {"orderId", 12510053279},
          {"apiKey", "testApiKey"},
          {"timestamp", 1000},
          {"signature",
           ws_sig("testSecret",
                  "apiKey=testApiKey&orderId=12510053279&symbol=BTCUSDT"
                  "&timestamp=1000")}}}};

    const json frame = req.to_json();
    EXPECT_EQ(frame, expected);
    EXPECT_TRUE(frame.at("params").contains("orderId"));
    EXPECT_FALSE(frame.at("params").contains("origClientOrderId"));
}

TEST(BinanceWsApiCancel, ToJson_ByClientOrderId) {
    BinanceWsCancelOrderRequest req;
    req.creds                = test_ws_creds();
    req.timestamp            = 1000;
    req.req_id               = 9;
    req.symbol               = "BTCUSDT";
    req.orig_client_order_id = "myOrder1";

    const json expected{
        {"id", 9},
        {"method", "order.cancel"},
        {"params",
         {{"symbol", "BTCUSDT"},
          {"origClientOrderId", "myOrder1"},
          {"apiKey", "testApiKey"},
          {"timestamp", 1000},
          {"signature",
           ws_sig("testSecret",
                  "apiKey=testApiKey&origClientOrderId=myOrder1&symbol=BTCUSDT"
                  "&timestamp=1000")}}}};

    const json frame = req.to_json();
    EXPECT_EQ(frame, expected);
    EXPECT_TRUE(frame.at("params").contains("origClientOrderId"));
    EXPECT_FALSE(frame.at("params").contains("orderId"));
}

TEST(BinanceWsApiCancel, ParsesSuccess) {
    const auto resp =
        BinanceWsCancelOrderResponse::from_json(json::parse(fixtures::kWsApiCancelSuccessJson));

    EXPECT_TRUE(resp.success);
    EXPECT_EQ(resp.status, 200);
    ASSERT_HAS_VALUE(resp.order);
    EXPECT_EQ(resp.order->symbol, "BTCUSDT");
    EXPECT_EQ(resp.order->orig_client_order_id, "a097fe6304b20a7e4fc436");
    EXPECT_EQ(resp.order->order_id, 12510053279);
    EXPECT_EQ(resp.order->status, OrderStatus::Canceled);

    ASSERT_EQ(resp.rate_limits.size(), 1u);
    EXPECT_EQ(resp.rate_limits[0].count, 14);
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute lifecycle through ExchangeWsClient + MockWsConnection (no network)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// WS API client backed by a MockWsConnection (binance_ws_api_frame_descriptor).
std::pair<std::shared_ptr<BinanceWsApiClient>, std::shared_ptr<MockWsConnection>>
make_mock_api_client() {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_binance_ws_api_client(conn);
    return {client, conn};
}

} // namespace

TEST(BinanceWsApiLifecycle, PingRoundTrip) {
    auto [client, conn] = make_mock_api_client();
    conn->fire_open();

    auto fut = client->execute_async(BinanceWsPingRequest{});
    ASSERT_EQ(conn->sent().size(), 1u);
    const auto sent = json::parse(conn->sent()[0]);
    EXPECT_EQ(sent.at("method"), "ping");
    const auto id = sent.at("id").get<int64_t>();

    conn->inject_message(
        json{{"id", id}, {"status", 200}, {"result", json::object()}}.dump());

    const auto resp = fut.get();
    EXPECT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->status, 200);
}

TEST(BinanceWsApiLifecycle, OrderPlaceRoundTrip) {
    auto [client, conn] = make_mock_api_client();
    conn->fire_open();

    BinanceWsNewOrderRequest req;
    req.creds    = test_ws_creds();
    req.symbol   = "BTCUSDT";
    req.side     = Side::Buy;
    req.type     = OrderType::Limit;
    req.quantity = "10";
    req.price    = "0.10";

    auto fut = client->execute_async(req);
    ASSERT_EQ(conn->sent().size(), 1u);
    const auto sent = json::parse(conn->sent()[0]);
    EXPECT_EQ(sent.at("method"), "order.place");
    const auto id = sent.at("id").get<int64_t>();

    // Replay the §4 success fixture under the client-assigned id.
    auto reply = json::parse(fixtures::kWsApiOrderPlaceSuccessJson);
    reply["id"] = id;
    conn->inject_message(reply.dump());

    const auto resp = fut.get();
    EXPECT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    ASSERT_HAS_VALUE(resp.result->order);
    EXPECT_EQ(resp.result->order->order_id, 12510053279);
}

TEST(BinanceWsApiLifecycle, OrderPlaceError) {
    auto [client, conn] = make_mock_api_client();
    conn->fire_open();

    BinanceWsNewOrderRequest req;
    req.creds    = test_ws_creds();
    req.symbol   = "BTCUSDT";
    req.quantity = "10";
    req.price    = "0.10";

    auto fut = client->execute_async(req);
    const auto id = json::parse(conn->sent().at(0)).at("id").get<int64_t>();

    auto reply = json::parse(fixtures::kWsApiOrderPlaceErrorJson);
    reply["id"] = id;
    conn->inject_message(reply.dump());

    const auto resp = fut.get();  // resolves — must not hang
    EXPECT_FALSE(resp.ok);
    ASSERT_HAS_VALUE(resp.error);
    EXPECT_EQ(*resp.error, "Account has insufficient balance for requested action.");
}

TEST(BinanceWsApiLifecycle, BeforeOpen_QueuedAndFlushed) {
    auto [client, conn] = make_mock_api_client();
    // No fire_open() — the ping must queue, not send.

    auto fut = client->execute_async(BinanceWsPingRequest{});
    EXPECT_TRUE(conn->sent().empty());

    conn->fire_open();
    ASSERT_EQ(conn->sent().size(), 1u);
    const auto sent = json::parse(conn->sent()[0]);
    EXPECT_EQ(sent.at("method"), "ping");

    // Complete the round-trip so the pending future doesn't dangle.
    conn->inject_message(
        json{{"id", sent.at("id").get<int64_t>()}, {"status", 200}, {"result", json::object()}}
            .dump());
    EXPECT_TRUE(fut.get().ok);
}

TEST(BinanceWsApiLifecycle, Timeout) {
    auto [client, conn] = make_mock_api_client();
    conn->fire_open();

    // No reply is ever injected — the blocking execute must time out.
    const auto resp = client->execute(BinanceWsPingRequest{}, std::chrono::milliseconds{10});
    EXPECT_FALSE(resp.ok);
    ASSERT_HAS_VALUE(resp.error);
    EXPECT_EQ(*resp.error, "request timed out");
}

TEST(BinanceWsApiLifecycle, BeforeOpen_FlushesInFifoOrder) {
    // Review L1: requests queued before the socket opens must flush in enqueue
    // order. req_ids increase per execute_async, so the sent frames' ids must be
    // strictly increasing in send order.
    auto [client, conn] = make_mock_api_client();

    client->execute_async(BinanceWsPingRequest{});  // futures discarded; the
    client->execute_async(BinanceWsPingRequest{});  // promises live in pending_,
    client->execute_async(BinanceWsPingRequest{});  // the frames queue in order.
    EXPECT_TRUE(conn->sent().empty());

    conn->fire_open();
    ASSERT_EQ(conn->sent().size(), 3u);
    const auto id0 = json::parse(conn->sent()[0]).at("id").get<int64_t>();
    const auto id1 = json::parse(conn->sent()[1]).at("id").get<int64_t>();
    const auto id2 = json::parse(conn->sent()[2]).at("id").get<int64_t>();
    EXPECT_LT(id0, id1);
    EXPECT_LT(id1, id2);
}
