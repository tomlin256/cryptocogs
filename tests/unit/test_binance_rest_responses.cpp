// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/binance/rest_api.hpp"
#include "binance_account_example_json.hpp"
#include "binance_rest_example_json.hpp"
#include "assert_has_value.hpp"

#include <algorithm>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace exchange::binance::rest;
using json = nlohmann::json;
namespace fixtures = exchange::binance::rest::test;

// ---------------------------------------------------------------------------
// from_json — response parsing
// ---------------------------------------------------------------------------

TEST(BinanceRestResponses, Ping_FromJson_DoesNotThrow) {
    auto j = json::parse(fixtures::kPingJson);
    EXPECT_NO_THROW(BinancePing::from_json(j));
}

TEST(BinanceRestResponses, ServerTime_FromJson) {
    auto j = json::parse(fixtures::kServerTimeJson);
    auto t = BinanceServerTime::from_json(j);
    EXPECT_EQ(t.server_time, 1499827319559LL);
}

TEST(BinanceRestResponses, TickerPrice_SingleObject) {
    auto j = json::parse(fixtures::kTickerPriceSingleJson);
    auto t = BinanceTickerPrice::from_json(j);
    ASSERT_EQ(t.entries.size(), 1u);
    EXPECT_EQ(t.entries[0].symbol, "LTCBTC");
    EXPECT_DOUBLE_EQ(t.entries[0].price, 4.00000200);
}

TEST(BinanceRestResponses, TickerPrice_Array) {
    auto j = json::parse(fixtures::kTickerPriceArrayJson);
    auto t = BinanceTickerPrice::from_json(j);
    ASSERT_EQ(t.entries.size(), 2u);
    EXPECT_EQ(t.entries[0].symbol, "LTCBTC");
    EXPECT_DOUBLE_EQ(t.entries[0].price, 4.00000200);
    EXPECT_EQ(t.entries[1].symbol, "ETHBTC");
    EXPECT_DOUBLE_EQ(t.entries[1].price, 0.07734100);
}

TEST(BinanceRestResponses, OrderBook_FromJson) {
    auto j = json::parse(fixtures::kDepthJson);
    auto b = BinanceOrderBook::from_json(j);
    EXPECT_EQ(b.last_update_id, 1027024LL);
    ASSERT_EQ(b.bids.size(), 1u);
    EXPECT_DOUBLE_EQ(b.bids[0].price, 4.0);
    EXPECT_DOUBLE_EQ(b.bids[0].quantity, 431.0);
    ASSERT_EQ(b.asks.size(), 1u);
    EXPECT_DOUBLE_EQ(b.asks[0].price, 4.000002);
    EXPECT_DOUBLE_EQ(b.asks[0].quantity, 12.0);
}

TEST(BinanceRestResponses, Trades_FromJson) {
    auto j = json::parse(fixtures::kTradesJson);
    auto r = BinanceTradesResult::from_json(j);
    ASSERT_EQ(r.trades.size(), 1u);
    const auto& t = r.trades[0];
    EXPECT_EQ(t.id, 28457LL);
    EXPECT_DOUBLE_EQ(t.price, 4.00000100);
    EXPECT_DOUBLE_EQ(t.qty, 12.0);
    EXPECT_DOUBLE_EQ(t.quote_qty, 48.000012);
    EXPECT_EQ(t.time, 1499865549590LL);
    EXPECT_TRUE(t.is_buyer_maker);
    EXPECT_TRUE(t.is_best_match);
}

TEST(BinanceRestResponses, Trades_EmptyArray) {
    auto r = BinanceTradesResult::from_json(json::array());
    EXPECT_TRUE(r.trades.empty());
}

TEST(BinanceRestResponses, Klines_FromJson) {
    auto j = json::parse(fixtures::kKlinesJson);
    auto r = BinanceKlinesResult::from_json(j);
    ASSERT_EQ(r.klines.size(), 1u);
    const auto& k = r.klines[0];
    EXPECT_EQ(k.open_time, 1499040000000LL);
    EXPECT_DOUBLE_EQ(k.open, 0.01634790);
    EXPECT_DOUBLE_EQ(k.high, 0.80000000);
    EXPECT_DOUBLE_EQ(k.low, 0.01575800);
    EXPECT_DOUBLE_EQ(k.close, 0.01577100);
    EXPECT_DOUBLE_EQ(k.volume, 148976.11427815);
    EXPECT_EQ(k.close_time, 1499644799999LL);
    EXPECT_DOUBLE_EQ(k.quote_asset_volume, 2434.19055334);
    EXPECT_EQ(k.num_trades, 308LL);
    EXPECT_DOUBLE_EQ(k.taker_buy_base_volume, 1756.87402397);
    EXPECT_DOUBLE_EQ(k.taker_buy_quote_volume, 28.46694368);
}

TEST(BinanceRestResponses, ExchangeInfo_FromJson) {
    auto j = json::parse(fixtures::kExchangeInfoJson);
    auto e = BinanceExchangeInfo::from_json(j);
    EXPECT_EQ(e.timezone, "UTC");
    EXPECT_EQ(e.server_time, 1565246363776LL);
    ASSERT_EQ(e.symbols.size(), 1u);
    const auto& s = e.symbols[0];
    EXPECT_EQ(s.symbol, "ETHBTC");
    EXPECT_EQ(s.status, "TRADING");
    EXPECT_EQ(s.base_asset, "ETH");
    EXPECT_EQ(s.base_asset_precision, 8);
    EXPECT_EQ(s.quote_asset, "BTC");
    EXPECT_EQ(s.quote_precision, 8);
    EXPECT_EQ(s.quote_asset_precision, 8);
    ASSERT_EQ(s.order_types.size(), 7u);
    EXPECT_NE(std::find(s.order_types.begin(), s.order_types.end(), "LIMIT"),
              s.order_types.end());
    EXPECT_NE(std::find(s.order_types.begin(), s.order_types.end(), "STOP_LOSS_LIMIT"),
              s.order_types.end());
    EXPECT_TRUE(s.iceberg_allowed);
    EXPECT_TRUE(s.oco_allowed);
    EXPECT_TRUE(s.is_spot_trading_allowed);
    EXPECT_TRUE(s.is_margin_trading_allowed);
}

TEST(BinanceRestResponses, Ticker24hr_SingleObject) {
    auto j = json::parse(fixtures::kTicker24hrSingleJson);
    auto t = BinanceTicker24hr::from_json(j);
    ASSERT_EQ(t.entries.size(), 1u);
    const auto& e = t.entries[0];
    EXPECT_EQ(e.symbol, "BNBBTC");
    EXPECT_DOUBLE_EQ(e.price_change, -94.99999800);
    EXPECT_DOUBLE_EQ(e.price_change_percent, -95.960);
    EXPECT_DOUBLE_EQ(e.weighted_avg_price, 0.29628482);
    EXPECT_DOUBLE_EQ(e.prev_close_price, 0.10002000);
    EXPECT_DOUBLE_EQ(e.last_price, 4.00000200);
    EXPECT_DOUBLE_EQ(e.last_qty, 200.0);
    EXPECT_DOUBLE_EQ(e.bid_price, 4.0);
    EXPECT_DOUBLE_EQ(e.bid_qty, 100.0);
    EXPECT_DOUBLE_EQ(e.ask_price, 4.00000200);
    EXPECT_DOUBLE_EQ(e.ask_qty, 100.0);
    EXPECT_DOUBLE_EQ(e.open_price, 99.0);
    EXPECT_DOUBLE_EQ(e.high_price, 100.0);
    EXPECT_DOUBLE_EQ(e.low_price, 0.1);
    EXPECT_DOUBLE_EQ(e.volume, 8913.3);
    EXPECT_DOUBLE_EQ(e.quote_volume, 15.3);
    EXPECT_EQ(e.open_time, 1499783499040LL);
    EXPECT_EQ(e.close_time, 1499869899040LL);
    EXPECT_EQ(e.first_id, 28385LL);
    EXPECT_EQ(e.last_id, 28460LL);
    EXPECT_EQ(e.count, 76LL);
}

TEST(BinanceRestResponses, Ticker24hr_Array) {
    auto j = json::parse(fixtures::kTicker24hrArrayJson);
    auto t = BinanceTicker24hr::from_json(j);
    ASSERT_EQ(t.entries.size(), 2u);
    EXPECT_EQ(t.entries[0].symbol, "BNBBTC");
    EXPECT_EQ(t.entries[1].symbol, "ETHBTC");
    EXPECT_DOUBLE_EQ(t.entries[1].last_price, 0.07734100);
}

// ---------------------------------------------------------------------------
// from_json — signed (private) endpoint responses
// ---------------------------------------------------------------------------

TEST(BinanceRestResponses, Account_FromJson) {
    auto j = json::parse(fixtures::kAccountJson);
    auto a = BinanceAccount::from_json(j);
    EXPECT_EQ(a.maker_commission, 15);
    EXPECT_EQ(a.taker_commission, 15);
    EXPECT_EQ(a.buyer_commission, 0);
    EXPECT_EQ(a.seller_commission, 0);
    EXPECT_DOUBLE_EQ(a.commission_rates.maker, 0.0015);
    EXPECT_DOUBLE_EQ(a.commission_rates.taker, 0.0015);
    EXPECT_DOUBLE_EQ(a.commission_rates.buyer, 0.0);
    EXPECT_DOUBLE_EQ(a.commission_rates.seller, 0.0);
    EXPECT_TRUE(a.can_trade);
    EXPECT_TRUE(a.can_withdraw);
    EXPECT_TRUE(a.can_deposit);
    EXPECT_FALSE(a.brokered);
    EXPECT_FALSE(a.require_self_trade_prevention);
    EXPECT_FALSE(a.prevent_sor);
    EXPECT_EQ(a.update_time, 123456789LL);
    EXPECT_EQ(a.account_type, "SPOT");
    ASSERT_EQ(a.balances.size(), 2u);
    EXPECT_EQ(a.balances[0].asset, "BTC");
    EXPECT_DOUBLE_EQ(a.balances[0].free, 4723846.89208129);
    EXPECT_DOUBLE_EQ(a.balances[0].locked, 0.0);
    EXPECT_EQ(a.balances[1].asset, "LTC");
    EXPECT_DOUBLE_EQ(a.balances[1].free, 4763368.68006011);
    ASSERT_EQ(a.permissions.size(), 1u);
    EXPECT_EQ(a.permissions[0], "SPOT");
    EXPECT_EQ(a.uid, 354937868LL);
}

TEST(BinanceRestResponses, OpenOrders_FromJson) {
    auto j = json::parse(fixtures::kOpenOrdersJson);
    auto r = BinanceOpenOrdersResult::from_json(j);
    ASSERT_EQ(r.orders.size(), 1u);
    const auto& o = r.orders[0];
    EXPECT_EQ(o.symbol, "LTCBTC");
    EXPECT_EQ(o.order_id, 1LL);
    EXPECT_EQ(o.order_list_id, -1LL);
    EXPECT_EQ(o.client_order_id, "myOrder1");
    EXPECT_DOUBLE_EQ(o.price, 0.1);
    EXPECT_DOUBLE_EQ(o.orig_qty, 1.0);
    EXPECT_DOUBLE_EQ(o.executed_qty, 0.0);
    EXPECT_DOUBLE_EQ(o.cummulative_quote_qty, 0.0);
    EXPECT_DOUBLE_EQ(o.orig_quote_order_qty, 0.0);
    EXPECT_EQ(o.status, exchange::OrderStatus::New);
    EXPECT_EQ(o.time_in_force, exchange::TimeInForce::GTC);
    EXPECT_EQ(o.type, "LIMIT");  // raw wire string — see plan 004 decision 3
    EXPECT_EQ(o.side, exchange::Side::Buy);
    EXPECT_DOUBLE_EQ(o.stop_price, 0.0);
    EXPECT_DOUBLE_EQ(o.iceberg_qty, 0.0);
    EXPECT_EQ(o.time, 1499827319559LL);
    EXPECT_EQ(o.update_time, 1499827319559LL);
    EXPECT_TRUE(o.is_working);
    EXPECT_EQ(o.working_time, 1499827319559LL);
    EXPECT_EQ(o.self_trade_prevention_mode, "NONE");
}

TEST(BinanceRestResponses, AllOrders_AliasParsesSameShape) {
    // BinanceAllOrdersResult is an alias of BinanceOpenOrdersResult — the
    // two endpoints share one row shape; sanity-check the alias compiles and
    // parses identically.
    auto j = json::parse(fixtures::kOpenOrdersJson);
    auto r = BinanceAllOrdersResult::from_json(j);
    ASSERT_EQ(r.orders.size(), 1u);
    EXPECT_EQ(r.orders[0].order_id, 1LL);
}

TEST(BinanceRestResponses, OpenOrders_EmptyArray) {
    auto r = BinanceOpenOrdersResult::from_json(json::array());
    EXPECT_TRUE(r.orders.empty());
}

TEST(BinanceRestResponses, MyTrades_FromJson) {
    auto j = json::parse(fixtures::kMyTradesJson);
    auto r = BinanceMyTradesResult::from_json(j);
    ASSERT_EQ(r.trades.size(), 1u);
    const auto& t = r.trades[0];
    EXPECT_EQ(t.symbol, "BNBBTC");
    EXPECT_EQ(t.id, 28457LL);
    EXPECT_EQ(t.order_id, 100234LL);
    EXPECT_EQ(t.order_list_id, -1LL);
    EXPECT_DOUBLE_EQ(t.price, 4.00000100);
    EXPECT_DOUBLE_EQ(t.qty, 12.0);
    EXPECT_DOUBLE_EQ(t.quote_qty, 48.000012);
    EXPECT_DOUBLE_EQ(t.commission, 10.1);
    EXPECT_EQ(t.commission_asset, "BNB");
    EXPECT_EQ(t.time, 1499865549590LL);
    EXPECT_TRUE(t.is_buyer);
    EXPECT_FALSE(t.is_maker);
    EXPECT_TRUE(t.is_best_match);
}

// ---------------------------------------------------------------------------
// from_json — trading responses (Step 6.4)
// ---------------------------------------------------------------------------

TEST(BinanceRestResponses, NewOrder_AckShape) {
    auto j = json::parse(fixtures::kNewOrderAckJson);
    auto r = BinanceNewOrderResponse::from_json(j);
    EXPECT_EQ(r.symbol, "BTCUSDT");
    EXPECT_EQ(r.order_id, 28LL);
    EXPECT_EQ(r.order_list_id, -1LL);
    EXPECT_EQ(r.client_order_id, "6gCrw2kRUAF9CvJDGP16IP");
    EXPECT_EQ(r.transact_time, 1507725176595LL);
    // ACK carries nothing beyond the five always-fields — every optional
    // must stay unset, distinguishing "absent" from "sent as zero".
    EXPECT_FALSE(r.price.has_value());
    EXPECT_FALSE(r.orig_qty.has_value());
    EXPECT_FALSE(r.executed_qty.has_value());
    EXPECT_FALSE(r.orig_quote_order_qty.has_value());
    EXPECT_FALSE(r.cummulative_quote_qty.has_value());
    EXPECT_FALSE(r.status.has_value());
    EXPECT_FALSE(r.time_in_force.has_value());
    EXPECT_FALSE(r.type.has_value());
    EXPECT_FALSE(r.side.has_value());
    EXPECT_FALSE(r.working_time.has_value());
    EXPECT_FALSE(r.self_trade_prevention_mode.has_value());
    EXPECT_TRUE(r.fills.empty());
}

TEST(BinanceRestResponses, NewOrder_ResultShape) {
    auto j = json::parse(fixtures::kNewOrderResultJson);
    auto r = BinanceNewOrderResponse::from_json(j);
    EXPECT_EQ(r.symbol, "BTCUSDT");
    EXPECT_EQ(r.order_id, 28LL);
    ASSERT_HAS_VALUE(r.price);
    EXPECT_DOUBLE_EQ(*r.price, 0.0);
    ASSERT_HAS_VALUE(r.orig_qty);
    EXPECT_DOUBLE_EQ(*r.orig_qty, 10.0);
    ASSERT_HAS_VALUE(r.executed_qty);
    EXPECT_DOUBLE_EQ(*r.executed_qty, 10.0);
    ASSERT_HAS_VALUE(r.orig_quote_order_qty);
    EXPECT_DOUBLE_EQ(*r.orig_quote_order_qty, 0.0);
    ASSERT_HAS_VALUE(r.cummulative_quote_qty);
    EXPECT_DOUBLE_EQ(*r.cummulative_quote_qty, 10.0);
    EXPECT_EQ(r.status, exchange::OrderStatus::Filled);
    EXPECT_EQ(r.time_in_force, exchange::TimeInForce::GTC);
    EXPECT_EQ(r.type, "MARKET");  // raw wire string — see plan 004 decision 3
    EXPECT_EQ(r.side, exchange::Side::Sell);
    EXPECT_EQ(r.working_time, 1507725176595LL);
    EXPECT_EQ(r.self_trade_prevention_mode, "NONE");
    EXPECT_TRUE(r.fills.empty());
}

TEST(BinanceRestResponses, NewOrder_FullShapeWithFills) {
    auto j = json::parse(fixtures::kNewOrderFullJson);
    auto r = BinanceNewOrderResponse::from_json(j);
    EXPECT_EQ(r.status, exchange::OrderStatus::Filled);
    EXPECT_EQ(r.side, exchange::Side::Sell);
    ASSERT_EQ(r.fills.size(), 2u);
    EXPECT_DOUBLE_EQ(r.fills[0].price, 4000.0);
    EXPECT_DOUBLE_EQ(r.fills[0].qty, 1.0);
    EXPECT_DOUBLE_EQ(r.fills[0].commission, 4.0);
    EXPECT_EQ(r.fills[0].commission_asset, "USDT");
    EXPECT_EQ(r.fills[0].trade_id, 56LL);
    EXPECT_DOUBLE_EQ(r.fills[1].price, 3999.0);
    EXPECT_DOUBLE_EQ(r.fills[1].qty, 5.0);
    EXPECT_DOUBLE_EQ(r.fills[1].commission, 19.995);
    EXPECT_EQ(r.fills[1].commission_asset, "USDT");
    EXPECT_EQ(r.fills[1].trade_id, 57LL);
}

TEST(BinanceRestResponses, CancelOrder_FromJson) {
    auto j = json::parse(fixtures::kCancelOrderJson);
    auto r = BinanceCancelOrderResponse::from_json(j);
    EXPECT_EQ(r.symbol, "LTCBTC");
    EXPECT_EQ(r.orig_client_order_id, "myOrder1");
    EXPECT_EQ(r.order_id, 4LL);
    EXPECT_EQ(r.order_list_id, -1LL);
    EXPECT_EQ(r.client_order_id, "cancelMyOrder1");
    EXPECT_EQ(r.transact_time, 1684804350068LL);
    EXPECT_DOUBLE_EQ(r.price, 2.0);
    EXPECT_DOUBLE_EQ(r.orig_qty, 1.0);
    EXPECT_DOUBLE_EQ(r.executed_qty, 0.0);
    EXPECT_DOUBLE_EQ(r.orig_quote_order_qty, 0.0);
    EXPECT_DOUBLE_EQ(r.cummulative_quote_qty, 0.0);
    EXPECT_EQ(r.status, exchange::OrderStatus::Canceled);
    EXPECT_EQ(r.time_in_force, exchange::TimeInForce::GTC);
    EXPECT_EQ(r.type, "LIMIT");
    EXPECT_EQ(r.side, exchange::Side::Buy);
    EXPECT_EQ(r.self_trade_prevention_mode, "NONE");
}

TEST(BinanceRestResponses, CancelAllOpenOrders_FromJson) {
    auto j = json::parse(fixtures::kCancelAllOpenOrdersJson);
    auto r = BinanceCancelAllResponse::from_json(j);
    ASSERT_EQ(r.orders.size(), 1u);
    const auto& o = r.orders[0];
    EXPECT_EQ(o.symbol, "BTCUSDT");
    EXPECT_EQ(o.orig_client_order_id, "E6APeyTJvkMvLMYMqu1KQ4");
    EXPECT_EQ(o.order_id, 11LL);
    EXPECT_EQ(o.status, exchange::OrderStatus::Canceled);
    EXPECT_EQ(o.side, exchange::Side::Buy);
}

TEST(BinanceRestResponses, CancelAllOpenOrders_EmptyArray) {
    auto r = BinanceCancelAllResponse::from_json(json::array());
    EXPECT_TRUE(r.orders.empty());
}

// ---------------------------------------------------------------------------
// parse_binance_response envelope
// ---------------------------------------------------------------------------

TEST(ParseBinanceResponse, OkResponseSetsOkTrue) {
    auto j = json::parse(fixtures::kPingJson);
    auto resp = parse_binance_response<BinancePing>(200, j);
    EXPECT_TRUE(resp.ok);
    EXPECT_TRUE(resp.errors.empty());
    ASSERT_TRUE(resp.result.has_value());
}

TEST(ParseBinanceResponse, ErrorResponseSetsOkFalse) {
    auto j = json::parse(fixtures::kErrorJson);
    auto resp = parse_binance_response<BinancePing>(400, j);
    EXPECT_FALSE(resp.ok);
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_EQ(resp.errors[0], "Invalid symbol.");
    EXPECT_FALSE(resp.result.has_value());
}
