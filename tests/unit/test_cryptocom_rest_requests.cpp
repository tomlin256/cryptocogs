// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Verifies that each Crypto.com REST request builds the correct HTTP method,
// path, and query string. Response parsing is covered in
// test_cryptocom_rest_responses.cpp.

#include "exchange/cryptocom/rest_api.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace exchange::cryptocom;
using namespace exchange::cryptocom::rest;
using json = nlohmann::json;
using M = HttpRequest::Method;

namespace {
// A function-local static, built on first use: a namespace-scope const would allocate in its static initialiser, which can throw
// (bugprone-throwing-static-initialization).
const CryptoComCredentials& creds() {
    static const CryptoComCredentials c{"test-api-key", "test-secret"};
    return c;
}
}  // namespace

// ── Public endpoints ──────────────────────────────────────────────────────────

TEST(CryptoComRestRequests, Instruments) {
    auto r = CryptoComInstrumentsRequest{}.build();
    EXPECT_EQ(r.method, M::GET);
    EXPECT_EQ(r.path, "/exchange/v1/public/get-instruments");
    EXPECT_TRUE(r.query.empty());
    EXPECT_TRUE(r.body.empty());
}

TEST(CryptoComRestRequests, Tickers_AllInstruments_NoQuery) {
    auto r = CryptoComTickersRequest{}.build();
    EXPECT_EQ(r.method, M::GET);
    EXPECT_EQ(r.path, "/exchange/v1/public/get-tickers");
    EXPECT_TRUE(r.query.empty());
}

TEST(CryptoComRestRequests, Tickers_SingleInstrument) {
    CryptoComTickersRequest req;
    req.instrument_name = "BTC_USD";
    auto r = req.build();
    EXPECT_EQ(r.path, "/exchange/v1/public/get-tickers");
    EXPECT_EQ(r.query, "instrument_name=BTC_USD");
}

TEST(CryptoComRestRequests, Book_WithDepth) {
    CryptoComOrderBookRequest req;
    req.instrument_name = "BTC_USD";
    req.depth           = 10;
    auto r = req.build();
    EXPECT_EQ(r.path, "/exchange/v1/public/get-book");
    EXPECT_EQ(r.query, "instrument_name=BTC_USD&depth=10");
}

TEST(CryptoComRestRequests, Book_WithoutDepth) {
    CryptoComOrderBookRequest req;
    req.instrument_name = "ETH_USD";
    auto r = req.build();
    EXPECT_EQ(r.query, "instrument_name=ETH_USD");
}

TEST(CryptoComRestRequests, Candlestick_AllParams) {
    CryptoComCandlesRequest req;
    req.instrument_name = "BTC_USD";
    req.timeframe       = "5m";
    req.count           = 25;
    auto r = req.build();
    EXPECT_EQ(r.path, "/exchange/v1/public/get-candlestick");
    EXPECT_EQ(r.query, "instrument_name=BTC_USD&timeframe=5m&count=25");
}

TEST(CryptoComRestRequests, Candlestick_TimeframeOnly) {
    CryptoComCandlesRequest req;
    req.instrument_name = "BTC_USD";
    req.timeframe       = "1m";
    auto r = req.build();
    EXPECT_EQ(r.query, "instrument_name=BTC_USD&timeframe=1m");
}

TEST(CryptoComRestRequests, Trades_WithCount) {
    CryptoComTradesRequest req;
    req.instrument_name = "BTC_USD";
    req.count           = 50;
    auto r = req.build();
    EXPECT_EQ(r.path, "/exchange/v1/public/get-trades");
    EXPECT_EQ(r.query, "instrument_name=BTC_USD&count=50");
}

TEST(CryptoComRestRequests, Trades_InstrumentOnly) {
    CryptoComTradesRequest req;
    req.instrument_name = "BTC_USD";
    auto r = req.build();
    EXPECT_EQ(r.query, "instrument_name=BTC_USD");
}

// ── Private endpoints (signed envelope) ───────────────────────────────────────

namespace {
// Assert the common signed-envelope shape and that `sig` matches an independent
// recomputation from the body's own id/params/nonce (pins signing end-to-end).
void expect_signed_envelope(const HttpRequest& r, const std::string& method) {
    EXPECT_EQ(r.method, M::POST);
    EXPECT_EQ(r.path, "/exchange/v1/" + method);
    EXPECT_EQ(r.headers.at("Content-Type"), "application/json");
    auto body = json::parse(r.body);
    EXPECT_EQ(body.at("method"), method);
    EXPECT_EQ(body.at("api_key"), creds().api_key);
    ASSERT_TRUE(body.contains("id"));
    ASSERT_TRUE(body.contains("nonce"));
    ASSERT_TRUE(body.contains("params"));
    ASSERT_TRUE(body.contains("sig"));
    const std::string sig = creds().sign(method, body.at("id").get<int64_t>(),
                                         body.at("params"), body.at("nonce").get<int64_t>());
    EXPECT_EQ(body.at("sig"), sig);
}
} // namespace

TEST(CryptoComRestRequests, UserBalance_EmptyParams) {
    auto r = CryptoComUserBalanceRequest{}.build(creds());
    expect_signed_envelope(r, "private/user-balance");
    auto body = json::parse(r.body);
    EXPECT_TRUE(body.at("params").is_object());
    EXPECT_TRUE(body.at("params").empty());
}

TEST(CryptoComRestRequests, CreateLimitOrder_ParamsAndSig) {
    CryptoComCreateOrderRequest req;
    req.id              = 42;
    req.instrument_name = "BTC_USD";
    req.side            = Side::Buy;
    req.type            = OrderType::Limit;
    req.price           = "63000.00";
    req.quantity        = "0.001";
    req.time_in_force   = TimeInForce::GTC;
    req.client_oid      = "abc-123";

    auto r = req.build(creds());
    expect_signed_envelope(r, "private/create-order");
    auto body = json::parse(r.body);
    EXPECT_EQ(body.at("id").get<int64_t>(), 42);
    const auto& p = body.at("params");
    EXPECT_EQ(p.at("instrument_name"), "BTC_USD");
    EXPECT_EQ(p.at("side"),            "BUY");
    EXPECT_EQ(p.at("type"),            "LIMIT");
    EXPECT_EQ(p.at("price"),           "63000.00");
    EXPECT_EQ(p.at("quantity"),        "0.001");
    EXPECT_EQ(p.at("time_in_force"),   "GOOD_TILL_CANCEL");
    EXPECT_EQ(p.at("client_oid"),      "abc-123");
    EXPECT_FALSE(p.contains("notional"));
    EXPECT_FALSE(p.contains("exec_inst"));
}

TEST(CryptoComRestRequests, CreateLimitOrder_PostOnly_SetsExecInstArray) {
    CryptoComCreateOrderRequest req;
    req.instrument_name = "BTC_USD";
    req.side            = Side::Sell;
    req.type            = OrderType::Limit;
    req.price           = "63000.00";
    req.quantity        = "0.001";
    req.time_in_force   = TimeInForce::GTC;
    req.exec_inst       = {EXEC_INST_POST_ONLY};

    auto r = req.build(creds());
    expect_signed_envelope(r, "private/create-order");
    auto body = json::parse(r.body);
    const auto& p = body.at("params");
    EXPECT_EQ(p.at("exec_inst"), json::array({"POST_ONLY"}));
}

TEST(CryptoComRestRequests, CreateMarketBuy_NotionalNoPrice) {
    CryptoComCreateOrderRequest req;
    req.instrument_name = "BTC_USD";
    req.side            = Side::Buy;
    req.type            = OrderType::Market;
    req.notional        = "150.00";

    auto body = json::parse(req.build(creds()).body);
    const auto& p = body.at("params");
    EXPECT_EQ(p.at("type"),     "MARKET");
    EXPECT_EQ(p.at("notional"), "150.00");
    EXPECT_FALSE(p.contains("price"));
    EXPECT_FALSE(p.contains("quantity"));
}

TEST(CryptoComRestRequests, CancelOrder) {
    CryptoComCancelOrderRequest req;
    req.order_id = "18342311";
    auto r = req.build(creds());
    expect_signed_envelope(r, "private/cancel-order");
    EXPECT_EQ(json::parse(r.body).at("params").at("order_id"), "18342311");
}

TEST(CryptoComRestRequests, GetOrderDetail) {
    CryptoComGetOrderDetailRequest req;
    req.order_id = "18342311";
    auto body = json::parse(req.build(creds()).body);
    EXPECT_EQ(body.at("method"), "private/get-order-detail");
    EXPECT_EQ(body.at("params").at("order_id"), "18342311");
}

TEST(CryptoComRestRequests, GetOpenOrders_WithInstrument) {
    CryptoComGetOpenOrdersRequest req;
    req.instrument_name = "BTC_USD";
    auto body = json::parse(req.build(creds()).body);
    EXPECT_EQ(body.at("method"), "private/get-open-orders");
    EXPECT_EQ(body.at("params").at("instrument_name"), "BTC_USD");
}

TEST(CryptoComRestRequests, GetTrades_LimitSerialisedAsString) {
    CryptoComGetTradesRequest req;
    req.instrument_name = "BTC_USD";
    req.limit           = 20;
    auto body = json::parse(req.build(creds()).body);
    const auto& p = body.at("params");
    EXPECT_EQ(p.at("instrument_name"), "BTC_USD");
    EXPECT_TRUE(p.at("limit").is_string());  // numbers serialised as strings
    EXPECT_EQ(p.at("limit"), "20");
}
