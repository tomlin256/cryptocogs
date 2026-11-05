// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Verifies the bespoke CoinbaseStreamClient (plan 018 §2 Option A) against the
// shared MockWsConnection — no sockets, no sleeps: pre-open queuing, typed
// type-keyed dispatch, the signed user-channel subscribe frame, subscriptions/
// error routing, malformed-frame handling, and idempotent cancel().

#include "exchange/coinbase/ws_streams.hpp"
#include "assert_has_value.hpp"
#include "mock_ws_connection.hpp"
#include "coinbase_ws_example_json.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace exchange::coinbase::ws;
namespace wf = coinbase_ws_fixtures;

namespace {

struct RecordingErrorHandler : exchange::ws::IWsErrorHandler {
    int         malformed{0};
    int         conn_errors{0};
    std::string last_conn_msg;
    void on_malformed_frame(const std::string&, const std::exception&) override { ++malformed; }
    void on_connection_error(const std::string& reason) override {
        ++conn_errors;
        last_conn_msg = reason;
    }
};

CoinbaseWsCredentials test_creds() {
    return CoinbaseWsCredentials{"my-key", "dGVzdHNlY3JldGtleQ==", "my-pass"};
}

} // namespace

// ── Pre-open queuing + subscribe frame shape ──────────────────────────────────

TEST(CoinbaseWsClient, PreOpenSubscribeIsQueuedThenFlushed) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_coinbase_stream_client(conn);

    auto h = client->subscribe_ticker({"BTC-USD"}, [](const CoinbaseTickerEvent&) {});
    EXPECT_TRUE(conn->sent().empty());  // queued until open
    EXPECT_TRUE(h.is_active());

    conn->fire_open();

    ASSERT_EQ(conn->sent().size(), 1u);
    auto sub = json::parse(conn->sent()[0]);
    EXPECT_EQ(sub.at("type"), "subscribe");
    EXPECT_EQ(sub.at("channels"), json::array({"ticker"}));
    EXPECT_EQ(sub.at("product_ids"), json::array({"BTC-USD"}));
}

// ── Typed dispatch by message "type" ──────────────────────────────────────────

TEST(CoinbaseWsClient, TickerFrameDispatchedToTypedCallback) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_coinbase_stream_client(conn);

    std::optional<CoinbaseTickerEvent> got;
    client->subscribe_ticker({"BTC-USD"}, [&](CoinbaseTickerEvent e) { got = std::move(e); });
    conn->fire_open();
    conn->inject_message(wf::TICKER_JSON);

    ASSERT_HAS_VALUE(got);
    EXPECT_EQ(got->product_id, "BTC-USD");
    EXPECT_EQ(got->trade_id, 1040406588);
    EXPECT_DOUBLE_EQ(got->price, 63212.97);
    EXPECT_DOUBLE_EQ(got->best_bid, 63212.97);
    EXPECT_DOUBLE_EQ(got->best_ask, 63212.98);
    EXPECT_EQ(got->side, "buy");
}

TEST(CoinbaseWsClient, Level2SnapshotAndUpdateRouteToTheirCallbacks) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_coinbase_stream_client(conn);

    std::optional<CoinbaseL2Snapshot> snap;
    std::optional<CoinbaseL2Update>   upd;
    client->subscribe_level2({"BTC-USD"},
                             [&](CoinbaseL2Snapshot s) { snap = std::move(s); },
                             [&](CoinbaseL2Update u)   { upd  = std::move(u); });
    conn->fire_open();

    conn->inject_message(wf::SNAPSHOT_JSON);
    ASSERT_HAS_VALUE(snap);
    ASSERT_EQ(snap->bids.size(), 2u);
    EXPECT_DOUBLE_EQ(snap->bids[0].price, 63210.11);
    EXPECT_DOUBLE_EQ(snap->asks[1].size, 2.1);

    conn->inject_message(wf::L2UPDATE_JSON);
    ASSERT_HAS_VALUE(upd);
    ASSERT_EQ(upd->changes.size(), 2u);
    EXPECT_EQ(upd->changes[0].side, "buy");
    EXPECT_DOUBLE_EQ(upd->changes[0].price, 63210.11);
    EXPECT_DOUBLE_EQ(upd->changes[0].size, 0.3);
}

TEST(CoinbaseWsClient, Level2BatchSubscribeFrameUsesLevel2BatchChannel) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_coinbase_stream_client(conn);

    client->subscribe_level2_batch({"BTC-USD", "ETH-USD"},
                                   [](const CoinbaseL2Snapshot&) {},
                                   [](const CoinbaseL2Update&) {});
    conn->fire_open();

    ASSERT_EQ(conn->sent().size(), 1u);
    auto sub = json::parse(conn->sent()[0]);
    EXPECT_EQ(sub.at("type"), "subscribe");
    EXPECT_EQ(sub.at("channels"), json::array({"level2_batch"}));
    EXPECT_EQ(sub.at("product_ids"), json::array({"BTC-USD", "ETH-USD"}));
}

TEST(CoinbaseWsClient, Level2BatchSnapshotAndUpdateRouteToTheirCallbacks) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_coinbase_stream_client(conn);

    std::optional<CoinbaseL2Snapshot> snap;
    std::optional<CoinbaseL2Update>   upd;
    client->subscribe_level2_batch({"BTC-USD"},
                                   [&](CoinbaseL2Snapshot s) { snap = std::move(s); },
                                   [&](CoinbaseL2Update u)   { upd  = std::move(u); });
    conn->fire_open();

    conn->inject_message(wf::SNAPSHOT_JSON);
    ASSERT_HAS_VALUE(snap);
    ASSERT_EQ(snap->bids.size(), 2u);
    EXPECT_DOUBLE_EQ(snap->bids[0].price, 63210.11);
    EXPECT_DOUBLE_EQ(snap->asks[1].size, 2.1);

    conn->inject_message(wf::L2UPDATE_JSON);
    ASSERT_HAS_VALUE(upd);
    ASSERT_EQ(upd->changes.size(), 2u);
    EXPECT_EQ(upd->changes[0].side, "buy");
    EXPECT_DOUBLE_EQ(upd->changes[0].price, 63210.11);
    EXPECT_DOUBLE_EQ(upd->changes[0].size, 0.3);
}

TEST(CoinbaseWsClient, MatchFrameDispatched) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_coinbase_stream_client(conn);

    std::optional<CoinbaseMatchEvent> got;
    client->subscribe_matches({"BTC-USD"}, [&](CoinbaseMatchEvent e) { got = std::move(e); });
    conn->fire_open();
    conn->inject_message(wf::MATCH_JSON);

    ASSERT_HAS_VALUE(got);
    EXPECT_EQ(got->trade_id, 1040406589);
    EXPECT_DOUBLE_EQ(got->price, 63212.98);
    EXPECT_EQ(got->side, "sell");
    EXPECT_EQ(got->maker_order_id, "m1b2c3d4-0000-4000-8000-000000000001");
}

TEST(CoinbaseWsClient, HeartbeatFrameDispatched) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_coinbase_stream_client(conn);

    std::optional<CoinbaseHeartbeatEvent> got;
    client->subscribe_heartbeat({"BTC-USD"}, [&](CoinbaseHeartbeatEvent e) { got = std::move(e); });
    conn->fire_open();
    conn->inject_message(wf::HEARTBEAT_JSON);

    ASSERT_HAS_VALUE(got);
    EXPECT_EQ(got->sequence, 7100000003);
    EXPECT_EQ(got->last_trade_id, 1040406590);
    EXPECT_EQ(got->product_id, "BTC-USD");
}

// ── subscriptions ack + error routing ─────────────────────────────────────────

TEST(CoinbaseWsClient, SubscriptionsConfirmationObserved) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_coinbase_stream_client(conn);

    std::optional<json> captured;
    client->set_on_subscriptions([&](const json& j) { captured = j; });
    conn->fire_open();
    conn->inject_message(wf::SUBSCRIPTIONS_JSON);

    ASSERT_HAS_VALUE(captured);
    EXPECT_EQ(captured->at("type"), "subscriptions");
}

TEST(CoinbaseWsClient, ErrorFrameRoutedToCallbackAndErrorHandler) {
    auto conn    = std::make_shared<MockWsConnection>();
    auto handler = std::make_shared<RecordingErrorHandler>();
    auto client  = make_coinbase_stream_client(conn, handler);

    std::optional<CoinbaseErrorEvent> err;
    client->set_on_error([&](const CoinbaseErrorEvent& e) { err = e; });
    conn->fire_open();
    conn->inject_message(wf::ERROR_JSON);

    ASSERT_HAS_VALUE(err);
    EXPECT_EQ(err->message, "Failed to subscribe");
    EXPECT_EQ(handler->conn_errors, 1);
    EXPECT_EQ(handler->last_conn_msg, "Failed to subscribe");
}

TEST(CoinbaseWsClient, MalformedFrameGoesToErrorHandler) {
    auto conn    = std::make_shared<MockWsConnection>();
    auto handler = std::make_shared<RecordingErrorHandler>();
    auto client  = make_coinbase_stream_client(conn, handler);

    conn->fire_open();
    conn->inject_message("{ this is not valid json");

    EXPECT_EQ(handler->malformed, 1);
}

// ── Authenticated user channel ────────────────────────────────────────────────

TEST(CoinbaseWsClient, UserChannelSubscribeFrameIsSigned) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_coinbase_stream_client(conn);
    const auto creds = test_creds();

    client->subscribe_user(creds, {"BTC-USD"}, [](const CoinbaseLifecycleEvent&) {});
    conn->fire_open();

    ASSERT_EQ(conn->sent().size(), 1u);
    auto frame = json::parse(conn->sent()[0]);
    EXPECT_EQ(frame.at("type"), "subscribe");
    EXPECT_EQ(frame.at("channels"), json::array({"user"}));
    EXPECT_EQ(frame.at("key"), "my-key");
    EXPECT_EQ(frame.at("passphrase"), "my-pass");

    const std::string ts = frame.at("timestamp");
    EXPECT_FALSE(ts.empty());
    EXPECT_EQ(frame.at("signature"),
              exchange::coinbase::rest::detail::coinbase_sign(
                  creds.api_secret, ts, "GET", "/users/self/verify", ""));
}

TEST(CoinbaseWsClient, UserLifecycleEventsDispatched) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_coinbase_stream_client(conn);

    std::optional<CoinbaseLifecycleEvent> ev;
    client->subscribe_user(test_creds(), {"BTC-USD"},
                           [&](CoinbaseLifecycleEvent e) { ev = std::move(e); });
    conn->fire_open();

    conn->inject_message(wf::USER_RECEIVED_JSON);
    ASSERT_HAS_VALUE(ev);
    EXPECT_EQ(ev->type, "received");
    EXPECT_EQ(ev->order_id, "d0c5a4f3-0000-4000-8000-000000000010");
    EXPECT_EQ(ev->side, "buy");
    ASSERT_HAS_VALUE(ev->price);
    EXPECT_DOUBLE_EQ(*ev->price, 30000.0);

    conn->inject_message(wf::USER_DONE_JSON);
    ASSERT_HAS_VALUE(ev);
    EXPECT_EQ(ev->type, "done");
    ASSERT_HAS_VALUE(ev->reason);
    EXPECT_EQ(*ev->reason, "filled");
    ASSERT_HAS_VALUE(ev->remaining_size);
    EXPECT_DOUBLE_EQ(*ev->remaining_size, 0.0);
}

// ── Cancellation ──────────────────────────────────────────────────────────────

TEST(CoinbaseWsClient, CancelRemovesCallbackSendsUnsubscribeAndIsIdempotent) {
    auto conn   = std::make_shared<MockWsConnection>();
    auto client = make_coinbase_stream_client(conn);

    int count = 0;
    auto h = client->subscribe_ticker({"BTC-USD"}, [&](const CoinbaseTickerEvent&) { ++count; });
    conn->fire_open();                       // flushes the subscribe (sent[0])
    conn->inject_message(wf::TICKER_JSON);
    EXPECT_EQ(count, 1);

    h.cancel();
    ASSERT_EQ(conn->sent().size(), 2u);  // subscribe + unsubscribe
    auto unsub = json::parse(conn->sent()[1]);
    EXPECT_EQ(unsub.at("type"), "unsubscribe");
    EXPECT_EQ(unsub.at("channels"), json::array({"ticker"}));
    EXPECT_FALSE(h.is_active());

    conn->inject_message(wf::TICKER_JSON);   // callback removed
    EXPECT_EQ(count, 1);

    h.cancel();                              // idempotent — no extra unsubscribe
    EXPECT_EQ(conn->sent().size(), 2u);
}
