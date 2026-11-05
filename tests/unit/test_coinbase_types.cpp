// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Verifies the Coinbase enum converters and the parse_coinbase_response REST
// envelope helper.

#include "exchange/coinbase/types.hpp"
#include "assert_has_value.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

using namespace exchange::coinbase;
using json = nlohmann::json;

// ── Side ─────────────────────────────────────────────────────────────────────

TEST(CoinbaseTypes, Side_RoundTrip) {
    EXPECT_EQ(coinbase_side_to_string(Side::Buy),  "buy");
    EXPECT_EQ(coinbase_side_to_string(Side::Sell), "sell");
    EXPECT_EQ(coinbase_side_from_string("buy"),  Side::Buy);
    EXPECT_EQ(coinbase_side_from_string("sell"), Side::Sell);
}

TEST(CoinbaseTypes, Side_RejectsUnknown) {
    EXPECT_THROW(coinbase_side_from_string("BUY"), std::invalid_argument);
    EXPECT_THROW(coinbase_side_from_string(""),    std::invalid_argument);
}

// ── OrderType ────────────────────────────────────────────────────────────────

TEST(CoinbaseTypes, OrderType_LimitAndMarketRoundTrip) {
    EXPECT_EQ(coinbase_order_type_to_string(OrderType::Limit),  "limit");
    EXPECT_EQ(coinbase_order_type_to_string(OrderType::Market), "market");
    EXPECT_EQ(coinbase_order_type_from_string("limit"),  OrderType::Limit);
    EXPECT_EQ(coinbase_order_type_from_string("market"), OrderType::Market);
}

TEST(CoinbaseTypes, OrderType_ThrowsForUnsupported) {
    EXPECT_THROW(coinbase_order_type_to_string(OrderType::StopLoss), std::invalid_argument);
    EXPECT_THROW(coinbase_order_type_to_string(OrderType::Iceberg),  std::invalid_argument);
    EXPECT_THROW(coinbase_order_type_from_string("stop"),    std::invalid_argument);
    EXPECT_THROW(coinbase_order_type_from_string("unknown"), std::invalid_argument);
}

// ── TimeInForce ──────────────────────────────────────────────────────────────

TEST(CoinbaseTypes, TimeInForce_RoundTripAllFour) {
    EXPECT_EQ(coinbase_tif_to_string(TimeInForce::GTC), "GTC");
    EXPECT_EQ(coinbase_tif_to_string(TimeInForce::GTD), "GTT");  // canonical GTD ↔ Coinbase GTT
    EXPECT_EQ(coinbase_tif_to_string(TimeInForce::IOC), "IOC");
    EXPECT_EQ(coinbase_tif_to_string(TimeInForce::FOK), "FOK");

    EXPECT_EQ(coinbase_tif_from_string("GTC"), TimeInForce::GTC);
    EXPECT_EQ(coinbase_tif_from_string("GTT"), TimeInForce::GTD);
    EXPECT_EQ(coinbase_tif_from_string("IOC"), TimeInForce::IOC);
    EXPECT_EQ(coinbase_tif_from_string("FOK"), TimeInForce::FOK);
}

TEST(CoinbaseTypes, TimeInForce_RejectsUnknown) {
    EXPECT_THROW(coinbase_tif_from_string("GTD"), std::invalid_argument);  // canonical name, not wire
    EXPECT_THROW(coinbase_tif_from_string(""),    std::invalid_argument);
}

// ── OrderStatus (total — Unknown fallback) ───────────────────────────────────

TEST(CoinbaseTypes, OrderStatus_MapsKnownValues) {
    EXPECT_EQ(coinbase_order_status_from_string("received"),  OrderStatus::PendingNew);
    EXPECT_EQ(coinbase_order_status_from_string("pending"),   OrderStatus::PendingNew);
    EXPECT_EQ(coinbase_order_status_from_string("open"),      OrderStatus::New);
    EXPECT_EQ(coinbase_order_status_from_string("active"),    OrderStatus::New);
    EXPECT_EQ(coinbase_order_status_from_string("done"),      OrderStatus::Filled);
    EXPECT_EQ(coinbase_order_status_from_string("settled"),   OrderStatus::Filled);
    EXPECT_EQ(coinbase_order_status_from_string("canceled"),  OrderStatus::Canceled);
    EXPECT_EQ(coinbase_order_status_from_string("cancelled"), OrderStatus::Canceled);
}

TEST(CoinbaseTypes, OrderStatus_UnknownFallback) {
    EXPECT_EQ(coinbase_order_status_from_string("rejected"), OrderStatus::Unknown);
    EXPECT_EQ(coinbase_order_status_from_string("weird"),    OrderStatus::Unknown);
    EXPECT_EQ(coinbase_order_status_from_string(""),         OrderStatus::Unknown);
}

// ── parse_coinbase_response ───────────────────────────────────────────────────

namespace {
struct FakeResult {
    int value{0};
    static FakeResult from_json(const json& j) {
        return FakeResult{j.at("value").get<int>()};
    }
};
} // namespace

TEST(CoinbaseEnvelope, SuccessStatus_ParsesResult) {
    auto resp = parse_coinbase_response<FakeResult>(200, json{{"value", 42}});
    EXPECT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->value, 42);
    EXPECT_TRUE(resp.errors.empty());
}

TEST(CoinbaseEnvelope, ErrorStatusWithMessage_SurfacesMessage) {
    auto resp = parse_coinbase_response<FakeResult>(
        400, json{{"message", "Invalid product_id"}});
    EXPECT_FALSE(resp.ok);
    EXPECT_FALSE(resp.result.has_value());
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_EQ(resp.errors[0], "Invalid product_id");
}

TEST(CoinbaseEnvelope, ErrorStatusWithoutMessage_FallsBackToStatus) {
    auto resp = parse_coinbase_response<FakeResult>(503, json::array());
    EXPECT_FALSE(resp.ok);
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_EQ(resp.errors[0], "HTTP 503");
}
