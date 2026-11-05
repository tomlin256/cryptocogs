// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Unit tests for WebSocket API JSON parsing.
// Every test parses a sample JSON constant from ws_client_example_json.hpp
// and verifies the resulting struct fields.  This ensures that from_json()
// implementations stay in sync with the actual wire format.

#include "exchange/kraken/ws_api.hpp"
#include "assert_has_value.hpp"
#include "ws_client_example_json.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace exchange::kraken::ws::test;

// ─────────────────────────────────────────────────────────────────────────────
// identify_message — one test per MessageKind covered by the sample constants
// ─────────────────────────────────────────────────────────────────────────────

TEST(IdentifyMessage, Ticker) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kTickerSnapshotJson)),
              exchange::kraken::ws::MessageKind::Ticker);
}

TEST(IdentifyMessage, Book) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kBookSnapshotJson)),
              exchange::kraken::ws::MessageKind::Book);
}

TEST(IdentifyMessage, Trade) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kTradeUpdateJson)),
              exchange::kraken::ws::MessageKind::Trade);
}

TEST(IdentifyMessage, OHLC) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kOhlcUpdateJson)),
              exchange::kraken::ws::MessageKind::OHLC);
}

TEST(IdentifyMessage, Instrument) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kInstrumentSnapshotJson)),
              exchange::kraken::ws::MessageKind::Instrument);
}

TEST(IdentifyMessage, Status) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kStatusUpdateJson)),
              exchange::kraken::ws::MessageKind::Status);
}

TEST(IdentifyMessage, Heartbeat) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kHeartbeatJson)),
              exchange::kraken::ws::MessageKind::Heartbeat);
}

TEST(IdentifyMessage, Executions) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kExecutionsSnapshotJson)),
              exchange::kraken::ws::MessageKind::Executions);
}

TEST(IdentifyMessage, Balances) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kBalancesSnapshotJson)),
              exchange::kraken::ws::MessageKind::Balances);
}

TEST(IdentifyMessage, Pong) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kPongJson)),
              exchange::kraken::ws::MessageKind::Pong);
}

TEST(IdentifyMessage, SubscribeResponse) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kSubscribeResponseJson)),
              exchange::kraken::ws::MessageKind::SubscribeResponse);
}

TEST(IdentifyMessage, AddOrderResponse) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kAddOrderResponseJson)),
              exchange::kraken::ws::MessageKind::AddOrderResponse);
}

TEST(IdentifyMessage, AmendOrderResponse) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kAmendOrderResponseJson)),
              exchange::kraken::ws::MessageKind::AmendOrderResponse);
}

TEST(IdentifyMessage, CancelOrderResponse) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kCancelOrderResponseJson)),
              exchange::kraken::ws::MessageKind::CancelOrderResponse);
}

TEST(IdentifyMessage, CancelAllResponse) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kCancelAllResponseJson)),
              exchange::kraken::ws::MessageKind::CancelAllResponse);
}

TEST(IdentifyMessage, CancelOnDisconnectResponse) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kCancelOnDisconnectResponseJson)),
              exchange::kraken::ws::MessageKind::CancelOnDisconnectResponse);
}

TEST(IdentifyMessage, BatchAddResponse) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kBatchAddResponseJson)),
              exchange::kraken::ws::MessageKind::BatchAddResponse);
}

TEST(IdentifyMessage, BatchCancelResponse) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kBatchCancelResponseJson)),
              exchange::kraken::ws::MessageKind::BatchCancelResponse);
}

TEST(IdentifyMessage, EditOrderResponse) {
    EXPECT_EQ(exchange::kraken::ws::identify_message(json::parse(kEditOrderResponseJson)),
              exchange::kraken::ws::MessageKind::EditOrderResponse);
}

// ─────────────────────────────────────────────────────────────────────────────
// StatusMessage
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseStatusMessage, FieldsFromSampleJson) {
    auto m = exchange::kraken::ws::StatusMessage::from_json(json::parse(kStatusUpdateJson));
    EXPECT_EQ(m.channel, "status");
    EXPECT_EQ(m.type, "update");
    EXPECT_EQ(m.system, "online");
    EXPECT_EQ(m.version, "2.0.10");
}

// ─────────────────────────────────────────────────────────────────────────────
// SubscribeResponse
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseSubscribeResponse, FieldsFromSampleJson) {
    auto r = exchange::kraken::ws::SubscribeResponse::from_json(json::parse(kSubscribeResponseJson));
    EXPECT_EQ(r.method, "subscribe");
    EXPECT_TRUE(r.success);
    // No req_id — the outbound subscribe request did not include one.
    EXPECT_FALSE(r.req_id.has_value());
    ASSERT_HAS_VALUE(r.channel);
    EXPECT_EQ(r.channel.value(), "ticker");
    ASSERT_HAS_VALUE(r.symbol);
    EXPECT_EQ(r.symbol.value(), "BTC/USD");
    ASSERT_HAS_VALUE(r.time_in);
    EXPECT_EQ(r.time_in.value(), "2026-03-15T11:25:20.538603Z");
}

// ─────────────────────────────────────────────────────────────────────────────
// TickerMessage
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseTickerMessage, FieldsFromSampleJson) {
    auto m = exchange::kraken::ws::TickerMessage::from_json(json::parse(kTickerSnapshotJson));
    EXPECT_EQ(m.channel, "ticker");
    EXPECT_EQ(m.type, "snapshot");
    ASSERT_EQ(m.data.size(), 1u);
    const auto& d = m.data[0];
    EXPECT_EQ(d.symbol, "BTC/USD");
    EXPECT_DOUBLE_EQ(d.bid, 71770.3);
    EXPECT_DOUBLE_EQ(d.bid_qty, 0.03155564);
    EXPECT_DOUBLE_EQ(d.ask, 71770.4);
    EXPECT_DOUBLE_EQ(d.ask_qty, 1.72978253);
    EXPECT_DOUBLE_EQ(d.last, 71770.4);
    EXPECT_DOUBLE_EQ(d.volume, 636.8977771);
    EXPECT_DOUBLE_EQ(d.vwap, 71318.4);
    EXPECT_DOUBLE_EQ(d.low, 70510.9);
    EXPECT_DOUBLE_EQ(d.high, 72000.0);
    EXPECT_DOUBLE_EQ(d.change, 1087.6);
    EXPECT_DOUBLE_EQ(d.change_pct, 1.54);
}

// ─────────────────────────────────────────────────────────────────────────────
// BookMessage
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseBookMessage, FieldsFromSampleJson) {
    auto m = exchange::kraken::ws::BookMessage::from_json(json::parse(kBookSnapshotJson));
    EXPECT_EQ(m.channel, "book");
    EXPECT_EQ(m.type, "snapshot");
    ASSERT_EQ(m.data.size(), 1u);
    const auto& d = m.data[0];
    EXPECT_EQ(d.symbol, "BTC/USD");
    ASSERT_EQ(d.asks.size(), 10u);
    EXPECT_DOUBLE_EQ(d.asks[0].price, 71770.4);
    EXPECT_DOUBLE_EQ(d.asks[0].qty, 1.80783344);
    ASSERT_EQ(d.bids.size(), 10u);
    EXPECT_DOUBLE_EQ(d.bids[0].price, 71770.3);
    EXPECT_DOUBLE_EQ(d.bids[0].qty, 0.03228444);
    ASSERT_HAS_VALUE(d.checksum);
    EXPECT_EQ(d.checksum.value(), 2552662837u);
}

// ─────────────────────────────────────────────────────────────────────────────
// TradeMessage
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseTradeMessage, FieldsFromSampleJson) {
    auto m = exchange::kraken::ws::TradeMessage::from_json(json::parse(kTradeUpdateJson));
    EXPECT_EQ(m.channel, "trade");
    EXPECT_EQ(m.type, "update");
    ASSERT_EQ(m.data.size(), 1u);
    const auto& d = m.data[0];
    EXPECT_EQ(d.symbol, "BTC/USD");
    EXPECT_DOUBLE_EQ(d.price, 71749.0);
    EXPECT_DOUBLE_EQ(d.qty, 0.00220616);
    EXPECT_EQ(d.side, "sell");
    EXPECT_EQ(d.ord_type, "limit");
    EXPECT_EQ(d.trade_id, 97184416);
    EXPECT_EQ(d.timestamp, "2026-03-15T11:26:23.023215Z");
}

// ─────────────────────────────────────────────────────────────────────────────
// OHLCMessage
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseOHLCMessage, FieldsFromSampleJson) {
    auto m = exchange::kraken::ws::OHLCMessage::from_json(json::parse(kOhlcUpdateJson));
    EXPECT_EQ(m.channel, "ohlc");
    EXPECT_EQ(m.type, "update");
    ASSERT_EQ(m.data.size(), 1u);
    const auto& d = m.data[0];
    EXPECT_EQ(d.symbol, "BTC/USD");
    EXPECT_EQ(d.timestamp, "2026-03-15T11:27:00.000000Z");
    EXPECT_DOUBLE_EQ(d.open, 71749.1);
    EXPECT_DOUBLE_EQ(d.high, 71749.1);
    EXPECT_DOUBLE_EQ(d.low, 71712.0);
    EXPECT_DOUBLE_EQ(d.close, 71712.0);
    EXPECT_DOUBLE_EQ(d.vwap, 71738.2);
    EXPECT_DOUBLE_EQ(d.volume, 0.06060006);
    EXPECT_EQ(d.trades, 14);
    EXPECT_EQ(d.interval_begin, "2026-03-15T11:26:00.000000000Z");
    ASSERT_HAS_VALUE(d.interval);
    EXPECT_EQ(d.interval.value(), 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// InstrumentMessage — data is {"assets":[…], "pairs":[…]}
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseInstrumentMessage, FieldsFromSampleJson) {
    auto m = exchange::kraken::ws::InstrumentMessage::from_json(json::parse(kInstrumentSnapshotJson));
    EXPECT_EQ(m.channel, "instrument");
    EXPECT_EQ(m.type, "snapshot");

    // assets
    ASSERT_EQ(m.assets.size(), 1u);
    const auto& a = m.assets[0];
    EXPECT_EQ(a.id, "BTC");
    EXPECT_EQ(a.status, "enabled");
    ASSERT_HAS_VALUE(a.precision);
    EXPECT_EQ(a.precision.value(), 10);
    ASSERT_HAS_VALUE(a.precision_display);
    EXPECT_EQ(a.precision_display.value(), 5);
    ASSERT_HAS_VALUE(a.borrowable);
    EXPECT_TRUE(a.borrowable.value());
    ASSERT_HAS_VALUE(a.collateral_value);
    EXPECT_DOUBLE_EQ(a.collateral_value.value(), 1.0);
    ASSERT_HAS_VALUE(a.margin_rate);
    EXPECT_DOUBLE_EQ(a.margin_rate.value(), 0.02);

    // pairs
    ASSERT_EQ(m.pairs.size(), 1u);
    const auto& p = m.pairs[0];
    EXPECT_EQ(p.symbol, "BTC/USD");
    EXPECT_EQ(p.base, "BTC");
    EXPECT_EQ(p.quote, "USD");
    EXPECT_EQ(p.status, "online");
    EXPECT_DOUBLE_EQ(p.qty_increment, 0.00000001);
    EXPECT_DOUBLE_EQ(p.qty_min, 0.0001);
    EXPECT_DOUBLE_EQ(p.price_increment, 0.1);
    EXPECT_DOUBLE_EQ(p.cost_min, 0.5);
    EXPECT_EQ(p.margin_initial, 20);
    ASSERT_HAS_VALUE(p.position_limit_long);
    EXPECT_EQ(p.position_limit_long.value(), 250);
    ASSERT_HAS_VALUE(p.position_limit_short);
    EXPECT_EQ(p.position_limit_short.value(), 250);
    ASSERT_HAS_VALUE(p.has_index);
    EXPECT_TRUE(p.has_index.value());
    ASSERT_HAS_VALUE(p.cost_precision);
    EXPECT_EQ(p.cost_precision.value(), 5);
    ASSERT_HAS_VALUE(p.qty_precision);
    EXPECT_EQ(p.qty_precision.value(), 10);
}

// ─────────────────────────────────────────────────────────────────────────────
// ExecutionsMessage
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseExecutionsMessage, FieldsFromSampleJson) {
    auto m = exchange::kraken::ws::ExecutionsMessage::from_json(json::parse(kExecutionsSnapshotJson));
    EXPECT_EQ(m.channel, "executions");
    EXPECT_EQ(m.type, "snapshot");
    ASSERT_EQ(m.data.size(), 1u);
    const auto& d = m.data[0];
    EXPECT_EQ(d.exec_id, "EXEC-001");
    EXPECT_EQ(d.exec_type, "trade");
    EXPECT_EQ(d.order_id, "ORDER-001");
    EXPECT_EQ(d.symbol, "BTC/USD");
    EXPECT_EQ(d.side, "buy");
    EXPECT_EQ(d.order_type, "limit");
    EXPECT_DOUBLE_EQ(d.order_qty, 0.1);
    EXPECT_DOUBLE_EQ(d.cum_qty, 0.1);
    EXPECT_DOUBLE_EQ(d.leaves_qty, 0.0);
    EXPECT_DOUBLE_EQ(d.last_qty, 0.1);
    EXPECT_DOUBLE_EQ(d.last_price, 50000.0);
    EXPECT_DOUBLE_EQ(d.avg_price, 50000.0);
    EXPECT_DOUBLE_EQ(d.cost, 5000.0);
    EXPECT_EQ(d.order_status, "filled");
    EXPECT_EQ(d.timestamp, "2026-03-15T12:00:00.000Z");
    ASSERT_HAS_VALUE(d.fee);
    EXPECT_DOUBLE_EQ(d.fee.value(), 2.5);
    ASSERT_HAS_VALUE(d.fee_currency);
    EXPECT_EQ(d.fee_currency.value(), "USD");
    ASSERT_HAS_VALUE(d.liquidity_ind);
    EXPECT_EQ(d.liquidity_ind.value(), "m");
    ASSERT_HAS_VALUE(d.fee_ccy_pref);
    EXPECT_EQ(d.fee_ccy_pref.value(), "fciq");
    ASSERT_HAS_VALUE(d.limit_price);
    EXPECT_DOUBLE_EQ(d.limit_price.value(), 50000.0);
    ASSERT_HAS_VALUE(d.time_in_force);
    EXPECT_EQ(d.time_in_force.value(), "GTC");
    ASSERT_HAS_VALUE(d.post_only);
    EXPECT_FALSE(d.post_only.value());
    ASSERT_HAS_VALUE(d.margin);
    EXPECT_FALSE(d.margin.value());
}

TEST(ParseExecutionsMessage, MultiElementFeesArraySums) {
    // Kraken reports fees as an array; the total is the sum of the element qtys,
    // and fee_currency is the first element's asset.
    const char* jsonStr = R"({"channel":"executions","type":"update","data":[{
        "exec_id":"E1","exec_type":"trade","order_id":"O1","symbol":"BTC/USD",
        "side":"sell","order_type":"limit","last_qty":0.01,"last_price":50000.0,
        "liquidity_ind":"t","fees":[{"asset":"USD","qty":1.5},{"asset":"USD","qty":1.0}]
    }]})";
    auto m = exchange::kraken::ws::ExecutionsMessage::from_json(json::parse(jsonStr));
    ASSERT_EQ(m.data.size(), 1u);
    const auto& d = m.data[0];
    ASSERT_HAS_VALUE(d.fee);
    EXPECT_DOUBLE_EQ(d.fee.value(), 2.5);
    ASSERT_HAS_VALUE(d.fee_currency);
    EXPECT_EQ(d.fee_currency.value(), "USD");
    ASSERT_HAS_VALUE(d.liquidity_ind);
    EXPECT_EQ(d.liquidity_ind.value(), "t");
}

TEST(ParseExecutionsMessage, LegacySingularFeeFallback) {
    // A payload without a `fees` array still parses via the singular `fee` /
    // `fee_currency` fallback (legacy / synthetic form).
    const char* jsonStr = R"({"channel":"executions","type":"update","data":[{
        "exec_id":"E1","exec_type":"trade","order_id":"O1","symbol":"BTC/USD",
        "side":"buy","order_type":"limit","last_qty":0.01,"last_price":50000.0,
        "fee":2.5,"fee_currency":"USD"
    }]})";
    auto m = exchange::kraken::ws::ExecutionsMessage::from_json(json::parse(jsonStr));
    ASSERT_EQ(m.data.size(), 1u);
    const auto& d = m.data[0];
    ASSERT_HAS_VALUE(d.fee);
    EXPECT_DOUBLE_EQ(d.fee.value(), 2.5);
    ASSERT_HAS_VALUE(d.fee_currency);
    EXPECT_EQ(d.fee_currency.value(), "USD");
    EXPECT_FALSE(d.liquidity_ind.has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// BalancesMessage
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseBalancesMessage, FieldsFromSampleJson) {
    auto m = exchange::kraken::ws::BalancesMessage::from_json(json::parse(kBalancesSnapshotJson));
    EXPECT_EQ(m.channel, "balances");
    EXPECT_EQ(m.type, "snapshot");
    ASSERT_EQ(m.data.size(), 2u);
    EXPECT_EQ(m.data[0].asset, "BTC");
    EXPECT_DOUBLE_EQ(m.data[0].balance, 1.5);
    EXPECT_DOUBLE_EQ(m.data[0].hold_trade, 0.1);
    EXPECT_EQ(m.data[1].asset, "USD");
    EXPECT_DOUBLE_EQ(m.data[1].balance, 25000.0);
    EXPECT_DOUBLE_EQ(m.data[1].hold_trade, 5000.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// PongMessage
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParsePongMessage, FieldsFromSampleJson) {
    auto m = exchange::kraken::ws::PongMessage::from_json(json::parse(kPongJson));
    EXPECT_EQ(m.method, "pong");
    ASSERT_HAS_VALUE(m.req_id);
    EXPECT_EQ(m.req_id.value(), 42);
}

// ─────────────────────────────────────────────────────────────────────────────
// AddOrderResponse
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseAddOrderResponse, FieldsFromSampleJson) {
    auto r = exchange::kraken::ws::AddOrderResponse::from_json(json::parse(kAddOrderResponseJson));
    EXPECT_EQ(r.method, "add_order");
    EXPECT_TRUE(r.success);
    ASSERT_HAS_VALUE(r.req_id);
    EXPECT_EQ(r.req_id.value(), 2);
    ASSERT_HAS_VALUE(r.order_id);
    EXPECT_EQ(r.order_id.value(), "ORDER-001");
    ASSERT_HAS_VALUE(r.cl_ord_id);
    EXPECT_EQ(r.cl_ord_id.value(), "my-order-1");
    ASSERT_HAS_VALUE(r.order_userref);
    EXPECT_EQ(r.order_userref.value(), 12345);
}

// ─────────────────────────────────────────────────────────────────────────────
// AmendOrderResponse
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseAmendOrderResponse, FieldsFromSampleJson) {
    auto r = exchange::kraken::ws::AmendOrderResponse::from_json(json::parse(kAmendOrderResponseJson));
    EXPECT_EQ(r.method, "amend_order");
    EXPECT_TRUE(r.success);
    ASSERT_HAS_VALUE(r.req_id);
    EXPECT_EQ(r.req_id.value(), 3);
    ASSERT_HAS_VALUE(r.order_id);
    EXPECT_EQ(r.order_id.value(), "ORDER-001");
    ASSERT_HAS_VALUE(r.cl_ord_id);
    EXPECT_EQ(r.cl_ord_id.value(), "my-order-1");
}

// ─────────────────────────────────────────────────────────────────────────────
// CancelOrderResponse
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseCancelOrderResponse, FieldsFromSampleJson) {
    auto r = exchange::kraken::ws::CancelOrderResponse::from_json(json::parse(kCancelOrderResponseJson));
    EXPECT_EQ(r.method, "cancel_order");
    EXPECT_TRUE(r.success);
    ASSERT_HAS_VALUE(r.req_id);
    EXPECT_EQ(r.req_id.value(), 4);
    ASSERT_HAS_VALUE(r.orders_cancelled);
    ASSERT_EQ(r.orders_cancelled->size(), 1u);
    EXPECT_EQ((*r.orders_cancelled)[0].order_id, "ORDER-001");
    EXPECT_TRUE((*r.orders_cancelled)[0].success);
}

// ─────────────────────────────────────────────────────────────────────────────
// CancelAllResponse
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseCancelAllResponse, FieldsFromSampleJson) {
    auto r = exchange::kraken::ws::CancelAllResponse::from_json(json::parse(kCancelAllResponseJson));
    EXPECT_EQ(r.method, "cancel_all");
    EXPECT_TRUE(r.success);
    ASSERT_HAS_VALUE(r.req_id);
    EXPECT_EQ(r.req_id.value(), 5);
    ASSERT_HAS_VALUE(r.count);
    EXPECT_EQ(r.count.value(), 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// CancelOnDisconnectResponse
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseCancelOnDisconnectResponse, FieldsFromSampleJson) {
    auto r = exchange::kraken::ws::CancelOnDisconnectResponse::from_json(
        json::parse(kCancelOnDisconnectResponseJson));
    EXPECT_EQ(r.method, "cancel_after");
    EXPECT_TRUE(r.success);
    ASSERT_HAS_VALUE(r.req_id);
    EXPECT_EQ(r.req_id.value(), 6);
    ASSERT_HAS_VALUE(r.current_time);
    EXPECT_EQ(r.current_time.value(), "2026-03-15T12:00:00.000Z");
    ASSERT_HAS_VALUE(r.trigger_time);
    EXPECT_EQ(r.trigger_time.value(), "2026-03-15T12:01:00.000Z");
}

// ─────────────────────────────────────────────────────────────────────────────
// BatchAddResponse
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseBatchAddResponse, FieldsFromSampleJson) {
    auto r = exchange::kraken::ws::BatchAddResponse::from_json(json::parse(kBatchAddResponseJson));
    EXPECT_EQ(r.method, "batch_add");
    EXPECT_TRUE(r.success);
    ASSERT_HAS_VALUE(r.req_id);
    EXPECT_EQ(r.req_id.value(), 7);
    ASSERT_HAS_VALUE(r.orders);
    ASSERT_EQ(r.orders->size(), 2u);
    const auto& first = (*r.orders)[0];
    EXPECT_EQ(first.order_id, "ORDER-A");
    EXPECT_TRUE(first.success);
    ASSERT_HAS_VALUE(first.cl_ord_id);
    EXPECT_EQ(first.cl_ord_id.value(), "batch-1");
    EXPECT_EQ((*r.orders)[1].order_id, "ORDER-B");
    EXPECT_TRUE((*r.orders)[1].success);
}

// ─────────────────────────────────────────────────────────────────────────────
// BatchCancelResponse
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseBatchCancelResponse, FieldsFromSampleJson) {
    auto r = exchange::kraken::ws::BatchCancelResponse::from_json(json::parse(kBatchCancelResponseJson));
    EXPECT_EQ(r.method, "batch_cancel");
    EXPECT_TRUE(r.success);
    ASSERT_HAS_VALUE(r.req_id);
    EXPECT_EQ(r.req_id.value(), 8);
    ASSERT_HAS_VALUE(r.orders_cancelled);
    EXPECT_EQ(r.orders_cancelled.value(), 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// EditOrderResponse
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseEditOrderResponse, FieldsFromSampleJson) {
    auto r = exchange::kraken::ws::EditOrderResponse::from_json(json::parse(kEditOrderResponseJson));
    EXPECT_EQ(r.method, "edit_order");
    EXPECT_TRUE(r.success);
    ASSERT_HAS_VALUE(r.req_id);
    EXPECT_EQ(r.req_id.value(), 9);
    ASSERT_HAS_VALUE(r.order_id);
    EXPECT_EQ(r.order_id.value(), "ORDER-002");
    ASSERT_HAS_VALUE(r.original_order_id);
    EXPECT_EQ(r.original_order_id.value(), "ORDER-001");
    ASSERT_HAS_VALUE(r.cl_ord_id);
    EXPECT_EQ(r.cl_ord_id.value(), "edit-1");
}
