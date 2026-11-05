// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Verifies the Crypto.com enum converters and the parse_cryptocom_response REST
// envelope helper.

#include "exchange/cryptocom/types.hpp"
#include "assert_has_value.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

using namespace exchange::cryptocom;
using json = nlohmann::json;

// ── Side ─────────────────────────────────────────────────────────────────────

TEST(CryptoComTypes, Side_RoundTrip) {
    EXPECT_EQ(cryptocom_side_to_string(Side::Buy),  "BUY");
    EXPECT_EQ(cryptocom_side_to_string(Side::Sell), "SELL");
    EXPECT_EQ(cryptocom_side_from_string("BUY"),  Side::Buy);
    EXPECT_EQ(cryptocom_side_from_string("SELL"), Side::Sell);
}

TEST(CryptoComTypes, Side_RejectsUnknown) {
    EXPECT_THROW(cryptocom_side_from_string("buy"), std::invalid_argument);  // wire is UPPERCASE
    EXPECT_THROW(cryptocom_side_from_string(""),    std::invalid_argument);
}

// ── OrderType ────────────────────────────────────────────────────────────────

TEST(CryptoComTypes, OrderType_RoundTripSupported) {
    EXPECT_EQ(cryptocom_order_type_to_string(OrderType::Limit),           "LIMIT");
    EXPECT_EQ(cryptocom_order_type_to_string(OrderType::Market),          "MARKET");
    EXPECT_EQ(cryptocom_order_type_to_string(OrderType::StopLoss),        "STOP_LOSS");
    EXPECT_EQ(cryptocom_order_type_to_string(OrderType::StopLossLimit),   "STOP_LIMIT");
    EXPECT_EQ(cryptocom_order_type_to_string(OrderType::TakeProfit),      "TAKE_PROFIT");
    EXPECT_EQ(cryptocom_order_type_to_string(OrderType::TakeProfitLimit), "TAKE_PROFIT_LIMIT");

    EXPECT_EQ(cryptocom_order_type_from_string("LIMIT"),             OrderType::Limit);
    EXPECT_EQ(cryptocom_order_type_from_string("MARKET"),            OrderType::Market);
    EXPECT_EQ(cryptocom_order_type_from_string("STOP_LOSS"),         OrderType::StopLoss);
    EXPECT_EQ(cryptocom_order_type_from_string("STOP_LIMIT"),        OrderType::StopLossLimit);
    EXPECT_EQ(cryptocom_order_type_from_string("TAKE_PROFIT"),       OrderType::TakeProfit);
    EXPECT_EQ(cryptocom_order_type_from_string("TAKE_PROFIT_LIMIT"), OrderType::TakeProfitLimit);
}

TEST(CryptoComTypes, OrderType_ThrowsForUnsupported) {
    EXPECT_THROW(cryptocom_order_type_to_string(OrderType::Iceberg),      std::invalid_argument);
    EXPECT_THROW(cryptocom_order_type_to_string(OrderType::TrailingStop), std::invalid_argument);
    EXPECT_THROW(cryptocom_order_type_from_string("limit"),   std::invalid_argument);  // wrong case
    EXPECT_THROW(cryptocom_order_type_from_string("unknown"), std::invalid_argument);
}

// ── TimeInForce ──────────────────────────────────────────────────────────────

TEST(CryptoComTypes, TimeInForce_RoundTripThree) {
    EXPECT_EQ(cryptocom_tif_to_string(TimeInForce::GTC), "GOOD_TILL_CANCEL");
    EXPECT_EQ(cryptocom_tif_to_string(TimeInForce::IOC), "IMMEDIATE_OR_CANCEL");
    EXPECT_EQ(cryptocom_tif_to_string(TimeInForce::FOK), "FILL_OR_KILL");

    EXPECT_EQ(cryptocom_tif_from_string("GOOD_TILL_CANCEL"),    TimeInForce::GTC);
    EXPECT_EQ(cryptocom_tif_from_string("IMMEDIATE_OR_CANCEL"), TimeInForce::IOC);
    EXPECT_EQ(cryptocom_tif_from_string("FILL_OR_KILL"),        TimeInForce::FOK);
}

TEST(CryptoComTypes, TimeInForce_GtdUnsupported_AndRejectsUnknown) {
    EXPECT_THROW(cryptocom_tif_to_string(TimeInForce::GTD), std::invalid_argument);
    EXPECT_THROW(cryptocom_tif_from_string("GTC"), std::invalid_argument);  // short form, not wire
    EXPECT_THROW(cryptocom_tif_from_string(""),    std::invalid_argument);
}

// ── OrderStatus (total — Unknown fallback) ───────────────────────────────────

TEST(CryptoComTypes, OrderStatus_MapsKnownValues) {
    EXPECT_EQ(cryptocom_order_status_from_string("ACTIVE"),           OrderStatus::New);
    EXPECT_EQ(cryptocom_order_status_from_string("PENDING"),          OrderStatus::PendingNew);
    EXPECT_EQ(cryptocom_order_status_from_string("PARTIALLY_FILLED"), OrderStatus::PartiallyFilled);
    EXPECT_EQ(cryptocom_order_status_from_string("FILLED"),           OrderStatus::Filled);
    EXPECT_EQ(cryptocom_order_status_from_string("CANCELED"),         OrderStatus::Canceled);
    EXPECT_EQ(cryptocom_order_status_from_string("EXPIRED"),          OrderStatus::Expired);
}

TEST(CryptoComTypes, OrderStatus_UnknownFallback) {
    EXPECT_EQ(cryptocom_order_status_from_string("REJECTED"), OrderStatus::Unknown);
    EXPECT_EQ(cryptocom_order_status_from_string("weird"),    OrderStatus::Unknown);
    EXPECT_EQ(cryptocom_order_status_from_string(""),         OrderStatus::Unknown);
}

// ── parse_cryptocom_response ───────────────────────────────────────────────────

namespace {
struct FakeResult {
    int value{0};
    static FakeResult from_json(const json& j) {
        return FakeResult{j.at("value").get<int>()};
    }
};
} // namespace

TEST(CryptoComEnvelope, Success_Code0_ParsesInnerResult) {
    auto resp = parse_cryptocom_response<FakeResult>(
        200, json{{"id", 1}, {"method", "public/get-tickers"}, {"code", 0},
                  {"result", {{"value", 42}}}});
    EXPECT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->value, 42);
    EXPECT_TRUE(resp.errors.empty());
}

TEST(CryptoComEnvelope, AppError_NonZeroCode_SurfacesCodeAndMessage) {
    // Crypto.com app-level errors can arrive with HTTP 200 but a non-zero code.
    auto resp = parse_cryptocom_response<FakeResult>(
        200, json{{"id", 1}, {"code", 10004}, {"message", "Bad request"}});
    EXPECT_FALSE(resp.ok);
    EXPECT_FALSE(resp.result.has_value());
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_NE(resp.errors[0].find("10004"), std::string::npos);
    EXPECT_NE(resp.errors[0].find("Bad request"), std::string::npos);
}

TEST(CryptoComEnvelope, HttpError_NonZeroCode_IsError) {
    auto resp = parse_cryptocom_response<FakeResult>(
        401, json{{"code", 10002}, {"message", "Unauthorized"}});
    EXPECT_FALSE(resp.ok);
    EXPECT_FALSE(resp.result.has_value());
}

TEST(CryptoComEnvelope, HttpError_NoBody_FallsBackToStatus) {
    auto resp = parse_cryptocom_response<FakeResult>(503, json::object());
    EXPECT_FALSE(resp.ok);
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_EQ(resp.errors[0], "HTTP 503");
}
