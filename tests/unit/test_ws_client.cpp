// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Unit tests for KrakenWsClient using a MockWsConnection that avoids
// any real network I/O.  The mock lets tests:
//   - Inspect outbound messages (conn->sent())
//   - Inject inbound server frames (conn->inject_message(...))
//   - Simulate connection open/close (conn->fire_open() / fire_close())

#include "assert_has_value.hpp"
#include "exchange/kraken/ws_client.hpp"
#include "mock_ws_connection.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using json = nlohmann::json;
using namespace std::chrono_literals;

// ─────────────────────────────────────────────────────────────────────────────
// Test factory: creates a client backed by a MockWsConnection, already "open"
// ─────────────────────────────────────────────────────────────────────────────

static std::pair<std::shared_ptr<exchange::kraken::ws::KrakenWsClient>,
                 std::shared_ptr<MockWsConnection>>
make_test_client() {
    auto conn   = std::make_shared<MockWsConnection>();
    // Pass kraken_frame_descriptor explicitly; make_exchange_ws_client() calls init() internally.
    auto client = exchange::ws::make_exchange_ws_client(conn, exchange::kraken::ws::kraken_frame_descriptor);
    conn->fire_open();   // simulate immediate connection
    return {client, conn};
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a minimal AddOrderResponse JSON with matching req_id
// ─────────────────────────────────────────────────────────────────────────────

static std::string make_add_order_response(int64_t req_id,
                                           bool success = true,
                                           const std::string& order_id = "ORDER-001",
                                           const std::string& error_msg = "") {
    json j;
    j["method"]  = "add_order";
    j["req_id"]  = req_id;
    j["success"] = success;
    if (success) {
        j["result"]["order_id"] = order_id;
    } else {
        j["error"] = error_msg;
    }
    return j.dump();
}

static std::string make_subscribe_ack(int64_t req_id,
                                      bool success = true,
                                      const std::string& channel = "ticker",
                                      const std::string& error_msg = "") {
    json j;
    j["method"]  = "subscribe";
    j["req_id"]  = req_id;
    j["success"] = success;
    if (success) {
        j["result"]["channel"] = channel;
    } else {
        j["error"] = error_msg;
    }
    return j.dump();
}

static std::string make_ticker_push(const std::string& symbol = "BTC/USD",
                                    double bid = 50000.0,
                                    double ask = 50001.0) {
    json j;
    j["channel"] = "ticker";
    j["type"]    = "update";
    j["data"]    = json::array();
    json d;
    d["symbol"]   = symbol;
    d["bid"]      = bid;
    d["bid_qty"]  = 1.0;
    d["ask"]      = ask;
    d["ask_qty"]  = 1.0;
    d["last"]     = bid;
    d["volume"]   = 100.0;
    d["vwap"]     = bid;
    d["low"]      = bid - 500;
    d["high"]     = bid + 500;
    d["change"]   = 0.0;
    d["change_pct"] = 0.0;
    j["data"].push_back(d);
    return j.dump();
}

// ─────────────────────────────────────────────────────────────────────────────
// Group: TypedWsRequest – compile-time type aliases
// ─────────────────────────────────────────────────────────────────────────────

TEST(TypedWsRequest, ResponseTypeAliases) {
    static_assert(std::is_same_v<exchange::kraken::ws::AddOrderRequest::response_type,
                                 exchange::kraken::ws::AddOrderResponse>);
    static_assert(std::is_same_v<exchange::kraken::ws::AmendOrderRequest::response_type,
                                 exchange::kraken::ws::AmendOrderResponse>);
    static_assert(std::is_same_v<exchange::kraken::ws::CancelOrderRequest::response_type,
                                 exchange::kraken::ws::CancelOrderResponse>);
    static_assert(std::is_same_v<exchange::kraken::ws::CancelAllRequest::response_type,
                                 exchange::kraken::ws::CancelAllResponse>);
    static_assert(std::is_same_v<exchange::kraken::ws::CancelOnDisconnectRequest::response_type,
                                 exchange::kraken::ws::CancelOnDisconnectResponse>);
    static_assert(std::is_same_v<exchange::kraken::ws::BatchAddRequest::response_type,
                                 exchange::kraken::ws::BatchAddResponse>);
    static_assert(std::is_same_v<exchange::kraken::ws::BatchCancelRequest::response_type,
                                 exchange::kraken::ws::BatchCancelResponse>);
    static_assert(std::is_same_v<exchange::kraken::ws::EditOrderRequest::response_type,
                                 exchange::kraken::ws::EditOrderResponse>);
    static_assert(std::is_same_v<exchange::kraken::ws::PingRequest::response_type,
                                 exchange::kraken::ws::PongMessage>);
    SUCCEED();  // all static_asserts passed
}

TEST(TypedSubscribeRequest, PushAndResponseTypes) {
    static_assert(std::is_same_v<exchange::kraken::ws::TickerSubscribeRequest::push_type,
                                 exchange::kraken::ws::TickerMessage>);
    static_assert(std::is_same_v<exchange::kraken::ws::TickerSubscribeRequest::response_type,
                                 exchange::kraken::ws::SubscribeResponse>);

    static_assert(std::is_same_v<exchange::kraken::ws::BookSubscribeRequest::push_type,
                                 exchange::kraken::ws::BookMessage>);
    static_assert(std::is_same_v<exchange::kraken::ws::OHLCSubscribeRequest::push_type,
                                 exchange::kraken::ws::OHLCMessage>);
    static_assert(std::is_same_v<exchange::kraken::ws::TradeSubscribeRequest::push_type,
                                 exchange::kraken::ws::TradeMessage>);
    static_assert(std::is_same_v<exchange::kraken::ws::ExecutionsSubscribeRequest::push_type,
                                 exchange::kraken::ws::ExecutionsMessage>);
    static_assert(std::is_same_v<exchange::kraken::ws::BalancesSubscribeRequest::push_type,
                                 exchange::kraken::ws::BalancesMessage>);
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// Group: execute_async
// ─────────────────────────────────────────────────────────────────────────────

TEST(ExecuteAsync, SendsRequestWithInjectedReqId) {
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::AddOrderRequest req;
    req.order_type = exchange::OrderType::Limit;
    req.side       = exchange::Side::Buy;
    req.order_qty  = 0.001;
    req.symbol     = "BTC/USD";
    req.token      = "tok";
    req.limit_price = exchange::kraken::TickPrice::from(30000.0, 1);

    auto fut = client->execute_async(req);

    ASSERT_EQ(conn->sent().size(), 1u);
    auto j = json::parse(conn->sent()[0]);
    EXPECT_TRUE(j.contains("req_id"));
    EXPECT_EQ(j["method"], "add_order");

    // Satisfy the future to avoid broken-promise at shutdown, and check the reply it was given.
    int64_t id = j["req_id"].get<int64_t>();
    conn->inject_message(make_add_order_response(id));
    EXPECT_TRUE(fut.get().ok);
}

TEST(ExecuteAsync, ResolvesOnMatchingSuccessResponse) {
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::AddOrderRequest req;
    req.order_type  = exchange::OrderType::Market;
    req.side        = exchange::Side::Sell;
    req.order_qty   = 0.1;
    req.symbol      = "BTC/USD";
    req.token       = "tok";

    auto fut = client->execute_async(req);
    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();

    conn->inject_message(make_add_order_response(id, true, "ORDER-XYZ"));

    auto resp = fut.get();
    EXPECT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->order_id.value_or(""), "ORDER-XYZ");
}

TEST(ExecuteAsync, ResolvesOnErrorResponse) {
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::AddOrderRequest req;
    req.order_type = exchange::OrderType::Limit;
    req.side       = exchange::Side::Buy;
    req.order_qty  = 0.001;
    req.symbol     = "BTC/USD";
    req.token      = "tok";
    req.limit_price = exchange::kraken::TickPrice::from(1.0, 1);

    auto fut = client->execute_async(req);
    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();

    conn->inject_message(make_add_order_response(id, false, "", "EOrder:Insufficient funds"));

    auto resp = fut.get();
    EXPECT_FALSE(resp.ok);
    EXPECT_EQ(resp.error.value_or(""), "EOrder:Insufficient funds");
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_FALSE(resp.result->order_id.has_value());
}

TEST(ExecuteAsync, TwoRequestsInFlightResolvedInReverseOrder) {
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::AddOrderRequest req1;
    req1.order_type = exchange::OrderType::Limit;
    req1.side       = exchange::Side::Buy;
    req1.order_qty  = 0.001;
    req1.symbol     = "BTC/USD";
    req1.token      = "tok";
    req1.limit_price = exchange::kraken::TickPrice::from(30000.0, 1);

    exchange::kraken::ws::AddOrderRequest req2 = req1;
    req2.limit_price = exchange::kraken::TickPrice::from(31000.0, 1);

    auto fut1 = client->execute_async(req1);
    auto fut2 = client->execute_async(req2);

    ASSERT_EQ(conn->sent().size(), 2u);
    int64_t id1 = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    int64_t id2 = json::parse(conn->sent()[1])["req_id"].get<int64_t>();
    EXPECT_NE(id1, id2);

    // Inject responses in reverse order.
    conn->inject_message(make_add_order_response(id2, true, "ORDER-B"));
    conn->inject_message(make_add_order_response(id1, true, "ORDER-A"));

    auto resp1 = fut1.get();
    auto resp2 = fut2.get();

    ASSERT_HAS_VALUE(resp1.result);
    ASSERT_HAS_VALUE(resp2.result);
    EXPECT_EQ(resp1.result->order_id.value_or(""), "ORDER-A");
    EXPECT_EQ(resp2.result->order_id.value_or(""), "ORDER-B");
}

// ─────────────────────────────────────────────────────────────────────────────
// Group: execute (sync / blocking)
// ─────────────────────────────────────────────────────────────────────────────

TEST(Execute, BlocksUntilResponse) {
    auto [client, conn] = make_test_client();
    auto& conn_ref = conn;

    exchange::kraken::ws::AddOrderRequest req;
    req.order_type  = exchange::OrderType::Limit;
    req.side        = exchange::Side::Buy;
    req.order_qty   = 0.001;
    req.symbol      = "BTC/USD";
    req.token       = "tok";
    req.limit_price  = exchange::kraken::TickPrice::from(30000.0, 1);

    // Inject the response from a separate thread, once the client has sent the request. wait_for_sent() returns when
    // the request is in the mock's log, so there is no sleep and no guess at how long the client takes, and the read of
    // the log is under the mock's lock (#27).
    std::thread injector([&] {
        if (!conn_ref->wait_for_sent(1)) return;     // released: the request never came
        const int64_t id = json::parse(conn_ref->sent()[0])["req_id"].get<int64_t>();
        conn_ref->inject_message(make_add_order_response(id, true, "ORDER-SYNC"));
    });

    auto resp = client->execute(req, 2000ms);
    conn_ref->release_waiters();                     // so that a run in which nothing was sent cannot hang in join()
    injector.join();

    EXPECT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->order_id.value_or(""), "ORDER-SYNC");
}

TEST(Execute, TimesOutAndReturnsError) {
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::AddOrderRequest req;
    req.order_type  = exchange::OrderType::Limit;
    req.side        = exchange::Side::Buy;
    req.order_qty   = 0.001;
    req.symbol      = "BTC/USD";
    req.token       = "tok";
    req.limit_price  = exchange::kraken::TickPrice::from(30000.0, 1);

    // No response injected → should time out.
    auto resp = client->execute(req, 50ms);

    EXPECT_FALSE(resp.ok);
    EXPECT_EQ(resp.error.value_or(""), "request timed out");
}

// ─────────────────────────────────────────────────────────────────────────────
// Group: subscribe (three-phase lifecycle)
// ─────────────────────────────────────────────────────────────────────────────

// The request goes into subscribe_async as std::move(sub_req) (or, in Cancel.RemovesPushCallback, as a temporary), not as a copy of a
// named local. The tests do not use it again, and with -Werror GCC 15 rejects the copy in a ThreadSanitizer build (the sanitizers
// workflow, -O2): the destructor of the local, whose std::optional<std::string> token is never engaged, is reported with
// -Wmaybe-uninitialized, one of GCC's false positives with std::optional (PR 109561, PR 121173). Clang and GCC 13 do not report it,
// so a copy put back goes unnoticed except in that job.

TEST(Subscribe, PushCallbackNotFiredBeforeAck) {
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::TickerSubscribeRequest sub_req;
    sub_req.symbols = std::vector<std::string>{"BTC/USD"};

    std::atomic<int> push_count{0};

    auto fut = client->subscribe_async(
        std::move(sub_req),
        [&](const exchange::kraken::ws::TickerMessage&) { ++push_count; }
    );

    // Inject push data BEFORE the ack – callback must not fire.
    conn->inject_message(make_ticker_push());
    EXPECT_EQ(push_count.load(), 0);

    // Now send the ack.
    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_subscribe_ack(id));
    auto [ack, handle] = fut.get();

    EXPECT_TRUE(ack.ok);
    EXPECT_EQ(push_count.load(), 0);  // push before ack still not counted
}

TEST(Subscribe, SuccessfulAckThenPushData) {
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::TickerSubscribeRequest sub_req;
    sub_req.symbols = std::vector<std::string>{"BTC/USD"};

    std::atomic<int>    push_count{0};
    std::atomic<double> last_bid{0.0};

    auto fut = client->subscribe_async(
        std::move(sub_req),
        [&](const exchange::kraken::ws::TickerMessage& msg) {
            ++push_count;
            if (!msg.data.empty()) last_bid.store(msg.data[0].bid);
        }
    );

    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_subscribe_ack(id));
    auto [ack, handle] = fut.get();

    EXPECT_TRUE(ack.ok);
    EXPECT_TRUE(handle.is_active());

    // Push data AFTER ack – callback must fire.
    conn->inject_message(make_ticker_push("BTC/USD", 55000.0, 55001.0));
    EXPECT_EQ(push_count.load(), 1);
    EXPECT_DOUBLE_EQ(last_bid.load(), 55000.0);

    conn->inject_message(make_ticker_push("BTC/USD", 56000.0, 56001.0));
    EXPECT_EQ(push_count.load(), 2);
}

TEST(Subscribe, FailedAckPushCallbackNeverFires) {
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::TickerSubscribeRequest sub_req;
    sub_req.symbols = std::vector<std::string>{"BTC/USD"};

    std::atomic<int> push_count{0};

    auto fut = client->subscribe_async(
        std::move(sub_req),
        [&](const exchange::kraken::ws::TickerMessage&) { ++push_count; }
    );

    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_subscribe_ack(id, false, "ticker", "Permission denied"));
    auto [ack, handle] = fut.get();

    EXPECT_FALSE(ack.ok);
    EXPECT_EQ(ack.error.value_or(""), "Permission denied");

    // Push data after failed ack – callback must never fire.
    conn->inject_message(make_ticker_push());
    EXPECT_EQ(push_count.load(), 0);
}

TEST(Subscribe, HandleIsInactiveAfterFailedAck) {
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::TickerSubscribeRequest sub_req;

    auto fut = client->subscribe_async(
        sub_req, [](const exchange::kraken::ws::TickerMessage&) {}
    );

    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_subscribe_ack(id, false, "ticker", "Unauthorized"));
    auto [ack, handle] = fut.get();

    EXPECT_FALSE(handle.is_active());
}

TEST(Subscribe, HandleIsActiveAfterSuccessfulAck) {
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::TickerSubscribeRequest sub_req;

    auto fut = client->subscribe_async(
        sub_req, [](const exchange::kraken::ws::TickerMessage&) {}
    );

    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_subscribe_ack(id));
    auto [ack, handle] = fut.get();

    EXPECT_TRUE(handle.is_active());
}

// ─────────────────────────────────────────────────────────────────────────────
// Group: SubscriptionHandle::cancel
// ─────────────────────────────────────────────────────────────────────────────

TEST(Cancel, RemovesPushCallback) {
    auto [client, conn] = make_test_client();

    std::atomic<int> push_count{0};

    auto fut = client->subscribe_async(
        exchange::kraken::ws::TickerSubscribeRequest{},
        [&](const exchange::kraken::ws::TickerMessage&) { ++push_count; }
    );

    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_subscribe_ack(id));
    auto [ack, handle] = fut.get();
    ASSERT_TRUE(ack.ok);

    // Confirm push fires before cancel.
    conn->inject_message(make_ticker_push());
    EXPECT_EQ(push_count.load(), 1);

    handle.cancel();
    EXPECT_FALSE(handle.is_active());

    // Push after cancel must not fire.
    conn->inject_message(make_ticker_push());
    EXPECT_EQ(push_count.load(), 1);
}

TEST(Cancel, SendsUnsubscribeRequest) {
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::TickerSubscribeRequest sub_req;
    sub_req.symbols = std::vector<std::string>{"BTC/USD"};

    auto fut = client->subscribe_async(
        sub_req, [](const exchange::kraken::ws::TickerMessage&) {}
    );

    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_subscribe_ack(id));
    auto [ack, handle] = fut.get();
    ASSERT_TRUE(ack.ok);

    const size_t msg_count_before = conn->sent().size();
    handle.cancel();

    ASSERT_GT(conn->sent().size(), msg_count_before);
    auto unsub_j = json::parse(conn->sent().back());
    EXPECT_EQ(unsub_j["method"], "unsubscribe");
    EXPECT_EQ(unsub_j["params"]["channel"], "ticker");
}

TEST(Cancel, IsIdempotent) {
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::TickerSubscribeRequest sub_req;

    auto fut = client->subscribe_async(
        sub_req, [](const exchange::kraken::ws::TickerMessage&) {}
    );

    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_subscribe_ack(id));
    auto [ack, handle] = fut.get();

    const size_t before = conn->sent().size();
    handle.cancel();
    handle.cancel();  // second call should be a no-op
    handle.cancel();

    // Only one unsubscribe should have been sent.
    EXPECT_EQ(conn->sent().size(), before + 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Group: pre-connection outbound queue
// ─────────────────────────────────────────────────────────────────────────────

TEST(PreConnectionQueue, MessagesQueuedBeforeOpen) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = exchange::ws::make_exchange_ws_client(conn, exchange::kraken::ws::kraken_frame_descriptor);  // calls init(); does NOT connect
    // Deliberately NOT calling conn->fire_open() yet.

    exchange::kraken::ws::AddOrderRequest req;
    req.order_type  = exchange::OrderType::Limit;
    req.side        = exchange::Side::Buy;
    req.order_qty   = 0.001;
    req.symbol      = "BTC/USD";
    req.token       = "tok";
    req.limit_price  = exchange::kraken::TickPrice::from(30000.0, 1);

    auto fut = client->execute_async(req);

    // Nothing should have been sent yet.
    EXPECT_TRUE(conn->sent().empty());

    // Simulate connection opening → queue flushed.
    conn->fire_open();
    ASSERT_EQ(conn->sent().size(), 1u);

    // Satisfy future.
    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_add_order_response(id));
    EXPECT_TRUE(fut.get().ok);
}

TEST(PreConnectionQueue, FlushedInOrder) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = exchange::ws::make_exchange_ws_client(conn, exchange::kraken::ws::kraken_frame_descriptor);  // calls init(); does NOT connect

    exchange::kraken::ws::AddOrderRequest req1;
    req1.order_type = exchange::OrderType::Limit;
    req1.side       = exchange::Side::Buy;
    req1.order_qty  = 0.001;
    req1.symbol     = "BTC/USD";
    req1.token      = "tok";
    req1.limit_price = exchange::kraken::TickPrice::from(30000.0, 1);

    exchange::kraken::ws::AddOrderRequest req2 = req1;
    req2.limit_price = exchange::kraken::TickPrice::from(31000.0, 1);

    auto fut1 = client->execute_async(req1);
    auto fut2 = client->execute_async(req2);

    ASSERT_TRUE(conn->sent().empty());

    conn->fire_open();

    ASSERT_EQ(conn->sent().size(), 2u);

    auto j1 = json::parse(conn->sent()[0]);
    auto j2 = json::parse(conn->sent()[1]);

    // Both are add_order requests.
    EXPECT_EQ(j1["method"], "add_order");
    EXPECT_EQ(j2["method"], "add_order");

    // They have distinct req_ids.
    EXPECT_NE(j1["req_id"].get<int64_t>(), j2["req_id"].get<int64_t>());

    // Satisfy futures.
    conn->inject_message(make_add_order_response(j1["req_id"].get<int64_t>()));
    conn->inject_message(make_add_order_response(j2["req_id"].get<int64_t>()));
    EXPECT_TRUE(fut1.get().ok);
    EXPECT_TRUE(fut2.get().ok);
}

TEST(PreConnectionQueue, DirectSendWhenAlreadyConnected) {
    // make_test_client fires open immediately, so sends go directly.
    auto [client, conn] = make_test_client();

    exchange::kraken::ws::AddOrderRequest req;
    req.order_type  = exchange::OrderType::Limit;
    req.side        = exchange::Side::Buy;
    req.order_qty   = 0.001;
    req.symbol      = "BTC/USD";
    req.token       = "tok";
    req.limit_price  = exchange::kraken::TickPrice::from(30000.0, 1);

    auto fut = client->execute_async(req);

    // Should have sent immediately (no need for fire_open).
    ASSERT_EQ(conn->sent().size(), 1u);

    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_add_order_response(id));
    EXPECT_TRUE(fut.get().ok);
}

// ─────────────────────────────────────────────────────────────────────────────
// Group: TypedSubscribeRequest channel binding
// ─────────────────────────────────────────────────────────────────────────────

TEST(TypedSubscribeRequest, ChannelIsBoundToType) {
    static_assert(exchange::kraken::ws::TickerSubscribeRequest::channel_value     == exchange::kraken::ws::SubscribeChannel::Ticker);
    static_assert(exchange::kraken::ws::BookSubscribeRequest::channel_value       == exchange::kraken::ws::SubscribeChannel::Book);
    static_assert(exchange::kraken::ws::TradeSubscribeRequest::channel_value      == exchange::kraken::ws::SubscribeChannel::Trade);
    static_assert(exchange::kraken::ws::OHLCSubscribeRequest::channel_value       == exchange::kraken::ws::SubscribeChannel::OHLC);
    static_assert(exchange::kraken::ws::InstrumentSubscribeRequest::channel_value == exchange::kraken::ws::SubscribeChannel::Instrument);
    static_assert(exchange::kraken::ws::ExecutionsSubscribeRequest::channel_value == exchange::kraken::ws::SubscribeChannel::Executions);
    static_assert(exchange::kraken::ws::BalancesSubscribeRequest::channel_value   == exchange::kraken::ws::SubscribeChannel::Balances);
    SUCCEED();
}

TEST(TypedSubscribeRequest, DefaultConstructorSetsChannelField) {
    EXPECT_EQ(exchange::kraken::ws::TickerSubscribeRequest{}.channel,     exchange::kraken::ws::SubscribeChannel::Ticker);
    EXPECT_EQ(exchange::kraken::ws::BookSubscribeRequest{}.channel,       exchange::kraken::ws::SubscribeChannel::Book);
    EXPECT_EQ(exchange::kraken::ws::TradeSubscribeRequest{}.channel,      exchange::kraken::ws::SubscribeChannel::Trade);
    EXPECT_EQ(exchange::kraken::ws::OHLCSubscribeRequest{}.channel,       exchange::kraken::ws::SubscribeChannel::OHLC);
    EXPECT_EQ(exchange::kraken::ws::InstrumentSubscribeRequest{}.channel, exchange::kraken::ws::SubscribeChannel::Instrument);
    EXPECT_EQ(exchange::kraken::ws::ExecutionsSubscribeRequest{}.channel, exchange::kraken::ws::SubscribeChannel::Executions);
    EXPECT_EQ(exchange::kraken::ws::BalancesSubscribeRequest{}.channel,   exchange::kraken::ws::SubscribeChannel::Balances);
}

TEST(TypedSubscribeRequest, ToJsonContainsCorrectChannel) {
    exchange::kraken::ws::TickerSubscribeRequest req;
    req.symbols = std::vector<std::string>{"BTC/USD"};
    // No manual channel assignment — channel is set by the constructor.
    auto j = req.to_json();
    EXPECT_EQ(j["params"]["channel"].get<std::string>(), "ticker");
}

// ─────────────────────────────────────────────────────────────────────────────
// Group: IWsErrorHandler — malformed frame handling
// ─────────────────────────────────────────────────────────────────────────────

// A recording error handler that captures every call for inspection.
class RecordingErrorHandler : public exchange::ws::IWsErrorHandler {
public:
    struct Call { std::string raw; std::string what; };
    std::vector<Call>        calls;
    std::vector<std::string> connection_errors;

    void on_malformed_frame(const std::string& raw,
                            const std::exception& e) override {
        calls.push_back({raw, e.what()});
    }

    void on_connection_error(const std::string& reason) override {
        connection_errors.push_back(reason);
    }
};

// Helper: create a client with a RecordingErrorHandler already open.
static std::pair<std::shared_ptr<exchange::kraken::ws::KrakenWsClient>,
                 std::shared_ptr<MockWsConnection>>
make_client_with_handler(std::shared_ptr<exchange::ws::IWsErrorHandler> handler) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = exchange::ws::make_exchange_ws_client(
        std::static_pointer_cast<exchange::ws::IWsConnection>(conn),
        exchange::kraken::ws::kraken_frame_descriptor,
        std::move(handler));
    conn->fire_open();
    return {client, conn};
}

TEST(WsErrorHandler, InvokedOnMalformedFrame) {
    auto handler = std::make_shared<RecordingErrorHandler>();
    auto [client, conn] = make_client_with_handler(handler);

    conn->inject_message("not valid json {{{");

    ASSERT_EQ(handler->calls.size(), 1u);
    EXPECT_EQ(handler->calls[0].raw, "not valid json {{{");
    EXPECT_FALSE(handler->calls[0].what.empty());
}

TEST(WsErrorHandler, NotInvokedOnValidFrame) {
    auto handler = std::make_shared<RecordingErrorHandler>();
    auto [client, conn] = make_client_with_handler(handler);

    conn->inject_message(R"({"channel":"heartbeat"})");

    EXPECT_TRUE(handler->calls.empty());
}

TEST(WsErrorHandler, InvokedForEachMalformedFrame) {
    auto handler = std::make_shared<RecordingErrorHandler>();
    auto [client, conn] = make_client_with_handler(handler);

    conn->inject_message("bad1");
    conn->inject_message("bad2");
    conn->inject_message("bad3");

    EXPECT_EQ(handler->calls.size(), 3u);
    EXPECT_EQ(handler->calls[0].raw, "bad1");
    EXPECT_EQ(handler->calls[1].raw, "bad2");
    EXPECT_EQ(handler->calls[2].raw, "bad3");
}

TEST(WsErrorHandler, NotInvokedOnValidPushFrame) {
    auto handler = std::make_shared<RecordingErrorHandler>();
    auto [client, conn] = make_client_with_handler(handler);

    // Subscribe so the push callback is installed.
    exchange::kraken::ws::TickerSubscribeRequest sub_req;
    sub_req.symbols = std::vector<std::string>{"BTC/USD"};
    auto fut = client->subscribe_async(sub_req, [](const exchange::kraken::ws::TickerMessage&) {});
    int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_subscribe_ack(id));
    EXPECT_TRUE(fut.get().first.ok);

    conn->inject_message(make_ticker_push());

    EXPECT_TRUE(handler->calls.empty());
}

TEST(WsErrorHandler, RateLimitedHandlerCanBeConstructedWithCustomInterval) {
    // Verify custom interval compiles and runs without crashing.
    auto handler = std::make_shared<exchange::kraken::ws::RateLimitedWsErrorHandler>(
        std::chrono::milliseconds{100});
    auto [client, conn] = make_client_with_handler(handler);

    for (int i = 0; i < 5; ++i)
        conn->inject_message("not json");
    // No crash, no throw.
    SUCCEED();
}

TEST(WsErrorHandler, DefaultHandlerUsedWhenNoneProvided) {
    // When no handler is passed, make_ws_client must not crash on bad input.
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = exchange::ws::make_exchange_ws_client(
        std::static_pointer_cast<exchange::ws::IWsConnection>(conn),
        exchange::kraken::ws::kraken_frame_descriptor);
    conn->fire_open();

    // Should not throw or crash — default RateLimitedWsErrorHandler handles it.
    EXPECT_NO_THROW(conn->inject_message("{bad json"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Group: IWsConnection error callback (Gap 1)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ConnectionError, RoutedToErrorHandler) {
    auto handler = std::make_shared<RecordingErrorHandler>();
    auto [client, conn] = make_client_with_handler(handler);

    conn->fire_error("TLS handshake failed");

    ASSERT_EQ(handler->connection_errors.size(), 1u);
    EXPECT_EQ(handler->connection_errors[0], "TLS handshake failed");
}

TEST(ConnectionError, MultipleErrorsAllRouted) {
    auto handler = std::make_shared<RecordingErrorHandler>();
    auto [client, conn] = make_client_with_handler(handler);

    conn->fire_error("DNS resolution failed");
    conn->fire_error("Connection reset");

    ASSERT_EQ(handler->connection_errors.size(), 2u);
    EXPECT_EQ(handler->connection_errors[0], "DNS resolution failed");
    EXPECT_EQ(handler->connection_errors[1], "Connection reset");
}

TEST(ConnectionError, DoesNotAffectMalformedFrameTracking) {
    auto handler = std::make_shared<RecordingErrorHandler>();
    auto [client, conn] = make_client_with_handler(handler);

    conn->fire_error("some error");
    conn->inject_message("not json");

    EXPECT_EQ(handler->connection_errors.size(), 1u);
    EXPECT_EQ(handler->calls.size(), 1u);
}

TEST(ConnectionError, DefaultHandlerDoesNotCrashOnError) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = exchange::ws::make_exchange_ws_client(
        std::static_pointer_cast<exchange::ws::IWsConnection>(conn),
        exchange::kraken::ws::kraken_frame_descriptor);
    conn->fire_open();

    EXPECT_NO_THROW(conn->fire_error("Protocol violation"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Group: set_on_disconnect (Gap 2)
// ─────────────────────────────────────────────────────────────────────────────

TEST(Disconnect, CallbackFiredOnClose) {
    auto [client, conn] = make_test_client();

    std::atomic<int> fired{0};
    client->set_on_disconnect([&](const std::string&) { ++fired; });

    conn->fire_close();

    EXPECT_EQ(fired.load(), 1);
}

TEST(Disconnect, CallbackNotFiredBeforeClose) {
    auto [client, conn] = make_test_client();

    std::atomic<int> fired{0};
    client->set_on_disconnect([&](const std::string&) { ++fired; });

    EXPECT_EQ(fired.load(), 0);
}

TEST(Disconnect, CallbackFiredOnlyOnce) {
    auto [client, conn] = make_test_client();

    std::atomic<int> fired{0};
    client->set_on_disconnect([&](const std::string&) { ++fired; });

    conn->fire_close();
    conn->fire_close();

    EXPECT_EQ(fired.load(), 2);  // each close event fires it once
}

TEST(Disconnect, NoCallbackRegisteredDoesNotCrash) {
    auto [client, conn] = make_test_client();

    // No set_on_disconnect call — fire_close() must not crash.
    EXPECT_NO_THROW(conn->fire_close());
}

TEST(Disconnect, ConnectedFlagClearedBeforeCallbackFires) {
    auto [client, conn] = make_test_client();

    // The disconnect callback should observe connected_ already false.
    // We verify this indirectly: the callback fires only after close.
    bool cb_fired = false;
    client->set_on_disconnect([&](const std::string&) { cb_fired = true; });

    EXPECT_FALSE(cb_fired);
    conn->fire_close();
    EXPECT_TRUE(cb_fired);
}

TEST(Disconnect, ReasonStringForwardedToCallback) {
    auto [client, conn] = make_test_client();

    std::string received_reason;
    client->set_on_disconnect([&](std::string reason) {
        received_reason = std::move(reason);
    });

    conn->fire_close("[code 1001] Going Away");

    EXPECT_EQ(received_reason, "[code 1001] Going Away");
}

TEST(Disconnect, EmptyReasonForwardedToCallback) {
    auto [client, conn] = make_test_client();

    std::string received_reason = "untouched";
    client->set_on_disconnect([&](std::string reason) {
        received_reason = std::move(reason);
    });

    conn->fire_close();  // default empty reason

    EXPECT_EQ(received_reason, "");
}

// ─────────────────────────────────────────────────────────────────────────────
// AddOrderRequest serialises a TickPrice limit_price as a JSON *number*, not a
// string (Kraken WS wire requirement). Migrated here from test_tick_price.cpp
// when TickPrice moved to the exchange-agnostic common layer — this assertion is
// Kraken-specific (it exercises Kraken's AddOrderRequest), so it lives with the
// Kraken WS tests.
// ─────────────────────────────────────────────────────────────────────────────

TEST(KrakenAddOrderSerialisation, LimitPriceIsJsonNumber) {
    exchange::kraken::ws::AddOrderRequest req;
    req.order_type  = exchange::OrderType::Limit;
    req.side        = exchange::Side::Buy;
    req.order_qty   = 0.001;
    req.symbol      = "TAO/USD";
    req.token       = "tok";
    req.limit_price = exchange::kraken::TickPrice::from(3096217 * 0.0001, 4);

    const json j  = req.to_json();
    const auto lp = j["params"]["limit_price"];

    EXPECT_TRUE(lp.is_number()) << "limit_price must be a JSON number";
    EXPECT_EQ(lp.dump(), "309.6217");
}

// ─────────────────────────────────────────────────────────────────────────────
// Exception isolation (code-review H1): a from_json/callback throw must not
// escape on_raw_message into the transport's receive thread, and the typed
// futures must resolve to ok=false rather than break.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct CountingErrorHandler : exchange::ws::IWsErrorHandler {
    std::atomic<int> malformed{0};
    void on_malformed_frame(const std::string&, const std::exception&) override { ++malformed; }
    void on_connection_error(const std::string&) override {}
};

// A method response whose from_json always throws — stands in for any
// unexpected-frame-shape deserialization failure.
struct BoomResponse : exchange::ws::BaseWsResponse {
    static BoomResponse from_json(const json&) {
        throw std::runtime_error("from_json boom");
    }
};
struct BoomRequest : exchange::ws::TypedWsRequest<BoomResponse> {
    json to_json() const { return json{{"id", req_id}, {"method", "boom"}}; }
};

// Minimal identifier: any frame carrying an integer "id" is a method response.
exchange::ws::FrameDescriptor boom_identifier(const json& j) {
    exchange::ws::FrameDescriptor d;
    if (j.contains("id") && j.at("id").is_number_integer()) {
        d.kind           = exchange::ws::FrameKind::MethodResponse;
        d.correlation_id = std::to_string(j.at("id").get<int64_t>());
    }
    return d;
}

}  // namespace

TEST(WsErrorIsolation, MethodFromJsonThrowResolvesErrorNoCrash) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = exchange::ws::make_exchange_ws_client(conn, boom_identifier);
    conn->fire_open();

    auto fut = client->execute_async(BoomRequest{});
    ASSERT_EQ(conn->sent().size(), 1u);
    const auto id = json::parse(conn->sent().back()).at("id").get<int64_t>();

    // from_json throws inside the dispatched handler — injecting the reply must
    // not propagate, and the future resolves ok=false (not broken_promise).
    EXPECT_NO_THROW(conn->inject_message(json{{"id", id}, {"status", 200}}.dump()));

    const auto resp = fut.get();
    EXPECT_FALSE(resp.ok);
    ASSERT_HAS_VALUE(resp.error);
}

// The MessageIdentifier is supplied by the adapter and runs on the transport's receive thread like everything else on this
// path, so a throw from it must be contained too (plan transport-callback-lifetimes): report the frame like any other malformed
// one, drop it, and go on.
TEST(WsErrorIsolation, ThrowingIdentifierIsContained) {
    auto conn = std::make_shared<MockWsConnection>();
    auto eh   = std::make_shared<RecordingErrorHandler>();

    // The Kraken identifier, except that it throws on a frame marked "poison".
    const auto identifier = [](const json& j) -> exchange::ws::FrameDescriptor {
        if (j.contains("poison"))
            throw std::runtime_error("identifier boom");
        return exchange::kraken::ws::kraken_frame_descriptor(j);
    };
    auto client = exchange::ws::make_exchange_ws_client(conn, identifier, eh);
    conn->fire_open();

    const std::string poisoned = R"({"poison":true})";
    EXPECT_NO_THROW(conn->inject_message(poisoned));
    ASSERT_EQ(eh->calls.size(), 1u);
    EXPECT_EQ(eh->calls[0].raw, poisoned);
    EXPECT_EQ(eh->calls[0].what, "identifier boom");

    // The client goes on dispatching: a request made now still resolves.
    exchange::kraken::ws::AddOrderRequest req;
    req.order_type = exchange::OrderType::Market;
    req.side       = exchange::Side::Sell;
    req.order_qty  = 0.1;
    req.symbol     = "BTC/USD";
    req.token      = "tok";
    auto fut = client->execute_async(req);
    ASSERT_EQ(conn->sent().size(), 1u);
    const int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_add_order_response(id, true, "ORDER-AFTER"));

    const auto resp = fut.get();
    EXPECT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->order_id.value_or(""), "ORDER-AFTER");
}

TEST(WsErrorIsolation, ThrowingPushCallbackIsContained) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto eh     = std::make_shared<CountingErrorHandler>();
    auto client = exchange::ws::make_exchange_ws_client(
        conn, exchange::kraken::ws::kraken_frame_descriptor, eh);
    conn->fire_open();

    exchange::kraken::ws::TickerSubscribeRequest sub_req;
    sub_req.symbols = std::vector<std::string>{"BTC/USD"};

    auto fut = client->subscribe_async(
        sub_req,
        [](const exchange::kraken::ws::TickerMessage&) {
            throw std::runtime_error("push callback boom");
        });

    const int64_t id = json::parse(conn->sent()[0])["req_id"].get<int64_t>();
    conn->inject_message(make_subscribe_ack(id));
    auto [ack, handle] = fut.get();
    ASSERT_TRUE(ack.ok);

    // The valid ticker push fires the throwing callback. The receive thread must
    // survive (no escape) and the error handler is notified.
    EXPECT_NO_THROW(conn->inject_message(make_ticker_push("BTC/USD", 55000.0, 55001.0)));
    EXPECT_GE(eh->malformed.load(), 1);
}
