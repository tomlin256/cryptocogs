// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/kraken/rest_api.hpp"
#include "exchange/kraken/types.hpp"
#include "assert_has_value.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>

using namespace exchange::kraken::rest;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// parse_rest_response envelope
// ---------------------------------------------------------------------------

TEST(RestEnvelope, OkResponseSetsOkTrue) {
    auto j = json::parse(R"({"error":[],"result":{"unixtime":1700000000,"rfc1123":"Mon, 14 Nov 2023 00:00:00 +0000"}})");
    auto resp = exchange::kraken::parse_rest_response<ServerTime>(j);
    EXPECT_TRUE(resp.ok);
    EXPECT_TRUE(resp.errors.empty());
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->unixtime, 1700000000);
}

TEST(RestEnvelope, ErrorResponseSetsOkFalse) {
    auto j = json::parse(R"({"error":["EGeneral:Invalid arguments"],"result":{}})");
    auto resp = exchange::kraken::parse_rest_response<ServerTime>(j);
    EXPECT_FALSE(resp.ok);
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_EQ(resp.errors[0], "EGeneral:Invalid arguments");
    EXPECT_FALSE(resp.result.has_value());
}

TEST(RestEnvelope, MultipleErrorsCollected) {
    auto j = json::parse(R"({"error":["EOrder:Insufficient funds","EOrder:Invalid price"]})");
    auto resp = exchange::kraken::parse_rest_response<ServerTime>(j);
    EXPECT_FALSE(resp.ok);
    EXPECT_EQ(resp.errors.size(), 2u);
}

// ---------------------------------------------------------------------------
// ServerTime
// ---------------------------------------------------------------------------

TEST(ServerTime, ParsesFieldsCorrectly) {
    auto j = json::parse(R"({"unixtime":1700000000,"rfc1123":"Mon, 14 Nov 2023 00:00:00 +0000"})");
    auto t = ServerTime::from_json(j);
    EXPECT_EQ(t.unixtime, 1700000000);
    EXPECT_EQ(t.rfc1123, "Mon, 14 Nov 2023 00:00:00 +0000");
}

TEST(ServerTime, DefaultsToZeroWhenMissing) {
    auto t = ServerTime::from_json(json::object());
    EXPECT_EQ(t.unixtime, 0);
    EXPECT_TRUE(t.rfc1123.empty());
}

// ---------------------------------------------------------------------------
// TickerResult
// ---------------------------------------------------------------------------

TEST(TickerResult, ParsesTickerInfo) {
    auto j = json::parse(R"({
        "XXBTZUSD": {
            "a": ["30000.00","1","1.000"],
            "b": ["29999.00","1","1.000"],
            "c": ["30000.50","0.001"],
            "v": ["100.0","200.0"],
            "p": ["30001.0","30002.0"],
            "t": [500, 1000],
            "l": ["29500.0","29000.0"],
            "h": ["30500.0","31000.0"],
            "o": "29800.0"
        }
    })");
    auto r = TickerResult::from_json(j);
    ASSERT_EQ(r.tickers.count("XXBTZUSD"), 1u);
    const auto& t = r.tickers.at("XXBTZUSD");
    EXPECT_DOUBLE_EQ(t.ask,          30000.00);
    EXPECT_DOUBLE_EQ(t.bid,          29999.00);
    EXPECT_DOUBLE_EQ(t.last,         30000.50);
    EXPECT_DOUBLE_EQ(t.volume_today, 100.0);
    EXPECT_DOUBLE_EQ(t.volume_24h,   200.0);
    EXPECT_EQ(t.trades_today,        500);
    EXPECT_EQ(t.trades_24h,          1000);
    EXPECT_DOUBLE_EQ(t.low_today,    29500.0);
    EXPECT_DOUBLE_EQ(t.high_24h,     31000.0);
    EXPECT_DOUBLE_EQ(t.open,         29800.0);
}

TEST(TickerResult, EmptyResultParsesOk) {
    auto r = TickerResult::from_json(json::object());
    EXPECT_TRUE(r.tickers.empty());
}

// ---------------------------------------------------------------------------
// AccountBalanceResult
// ---------------------------------------------------------------------------

TEST(AccountBalanceResult, ParsesBalanceMap) {
    auto j = json::parse(R"({"XXBT":"1.5000000000","ZUSD":"10000.00","ETH":"5.0"})");
    auto r = AccountBalanceResult::from_json(j);
    ASSERT_EQ(r.balances.count("XXBT"), 1u);
    EXPECT_DOUBLE_EQ(r.balances.at("XXBT"), 1.5);
    EXPECT_DOUBLE_EQ(r.balances.at("ZUSD"), 10000.0);
    EXPECT_DOUBLE_EQ(r.balances.at("ETH"),  5.0);
}

TEST(AccountBalanceResult, EmptyBalance) {
    auto r = AccountBalanceResult::from_json(json::object());
    EXPECT_TRUE(r.balances.empty());
}

// ---------------------------------------------------------------------------
// AddOrderResult
// ---------------------------------------------------------------------------

TEST(AddOrderResult, ParsesTxidAndDescription) {
    auto j = json::parse(R"({
        "descr": {"order": "buy 0.001 XBTUSD @ limit 30000"},
        "txid": ["OABC12-DEFG34-HIJKL5"]
    })");
    auto r = AddOrderResult::from_json(j);
    EXPECT_EQ(r.descr_order, "buy 0.001 XBTUSD @ limit 30000");
    ASSERT_EQ(r.txids.size(), 1u);
    EXPECT_EQ(r.txids[0], "OABC12-DEFG34-HIJKL5");
    EXPECT_FALSE(r.descr_close.has_value());
}

TEST(AddOrderResult, ParsesOptionalClose) {
    auto j = json::parse(R"({
        "descr": {"order": "buy 0.001 XBTUSD @ limit 30000", "close": "close position"},
        "txid": ["OABC12-DEFG34-HIJKL5"]
    })");
    auto r = AddOrderResult::from_json(j);
    ASSERT_HAS_VALUE(r.descr_close);
    EXPECT_EQ(*r.descr_close, "close position");
}

// ---------------------------------------------------------------------------
// WebSocketsTokenResult
// ---------------------------------------------------------------------------

TEST(WebSocketsTokenResult, ParsesToken) {
    auto j = json::parse(R"({"token":"abc123","expires":900})");
    auto r = WebSocketsTokenResult::from_json(j);
    EXPECT_EQ(r.token, "abc123");
    EXPECT_EQ(r.expires, 900);
}

// ---------------------------------------------------------------------------
// EarnBoolResult
// ---------------------------------------------------------------------------

TEST(EarnBoolResult, ParsesTrue) {
    auto r = EarnBoolResult::from_json(json(true));
    EXPECT_TRUE(r.result);
}

TEST(EarnBoolResult, ParsesFalse) {
    auto r = EarnBoolResult::from_json(json(false));
    EXPECT_FALSE(r.result);
}

// ---------------------------------------------------------------------------
// CancelOrderResult
// ---------------------------------------------------------------------------

TEST(CancelOrderResult, ParsesCount) {
    auto j = json::parse(R"({"count":1})");
    auto r = CancelOrderResult::from_json(j);
    EXPECT_EQ(r.count, 1);
}

// ---------------------------------------------------------------------------
// stp_type parsing
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// fee_preference parsing
// ---------------------------------------------------------------------------

TEST(FeePreferenceParsing, Base) {
    auto j = json::parse(R"({"fee_preference":"base"})");
    auto p = exchange::kraken::OrderParams::from_json(j);
    ASSERT_HAS_VALUE(p.fee_preference);
    EXPECT_EQ(*p.fee_preference, exchange::kraken::FeePreference::Base);
}

TEST(FeePreferenceParsing, Quote) {
    auto j = json::parse(R"({"fee_preference":"quote"})");
    auto p = exchange::kraken::OrderParams::from_json(j);
    ASSERT_HAS_VALUE(p.fee_preference);
    EXPECT_EQ(*p.fee_preference, exchange::kraken::FeePreference::Quote);
}

TEST(FeePreferenceParsing, AbsentFieldLeavesOptionalEmpty) {
    auto j = json::parse(R"({})");
    auto p = exchange::kraken::OrderParams::from_json(j);
    EXPECT_FALSE(p.fee_preference.has_value());
}

TEST(FeePreferenceParsing, InvalidValueThrows) {
    auto j = json::parse(R"({"fee_preference":"bogus"})");
    EXPECT_THROW(exchange::kraken::OrderParams::from_json(j), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// stp_type parsing
// ---------------------------------------------------------------------------

TEST(StpTypeParsing, CancelNewest) {
    auto j = json::parse(R"({"order_type":"limit","side":"buy","symbol":"BTC/USD","stp_type":"cancel_newest"})");
    auto p = exchange::kraken::OrderParams::from_json(j);
    ASSERT_HAS_VALUE(p.stp_type);
    EXPECT_EQ(*p.stp_type, exchange::kraken::StpType::CancelNewest);
}

TEST(StpTypeParsing, CancelOldest) {
    auto j = json::parse(R"({"order_type":"limit","side":"buy","symbol":"BTC/USD","stp_type":"cancel_oldest"})");
    auto p = exchange::kraken::OrderParams::from_json(j);
    ASSERT_HAS_VALUE(p.stp_type);
    EXPECT_EQ(*p.stp_type, exchange::kraken::StpType::CancelOldest);
}

TEST(StpTypeParsing, CancelBoth) {
    auto j = json::parse(R"({"order_type":"limit","side":"buy","symbol":"BTC/USD","stp_type":"cancel_both"})");
    auto p = exchange::kraken::OrderParams::from_json(j);
    ASSERT_HAS_VALUE(p.stp_type);
    EXPECT_EQ(*p.stp_type, exchange::kraken::StpType::CancelBoth);
}

TEST(StpTypeParsing, AbsentFieldLeavesOptionalEmpty) {
    auto j = json::parse(R"({"order_type":"limit","side":"buy","symbol":"BTC/USD"})");
    auto p = exchange::kraken::OrderParams::from_json(j);
    EXPECT_FALSE(p.stp_type.has_value());
}

TEST(StpTypeParsing, InvalidValueThrows) {
    auto j = json::parse(R"({"order_type":"limit","side":"buy","symbol":"BTC/USD","stp_type":"bogus"})");
    EXPECT_THROW(exchange::kraken::OrderParams::from_json(j), std::invalid_argument);
}
