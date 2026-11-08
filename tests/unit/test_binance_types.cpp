// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/binance/auth.hpp"
#include "exchange/binance/types.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

using exchange::OrderStatus;
using exchange::OrderType;
using exchange::Side;
using exchange::TimeInForce;
using namespace exchange::binance;

// plan clear-clang-tidy-findings D1: the underlying type of the public enums is pinned; widening one changes the layout of the
// order-entry structs (#38).
static_assert(std::is_same_v<std::underlying_type_t<exchange::binance::BinanceOrderRespType>,       std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<exchange::binance::rest::BinanceSignAlgorithm>, std::uint8_t>);

// ─────────────────────────────────────────────────────────────────────────────
// Side — uppercase wire format, distinct from the canonical lowercase
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceSide, RoundTrip) {
    EXPECT_EQ(binance_side_to_string(Side::Buy), "BUY");
    EXPECT_EQ(binance_side_to_string(Side::Sell), "SELL");
    EXPECT_EQ(binance_side_from_string("BUY"), Side::Buy);
    EXPECT_EQ(binance_side_from_string("SELL"), Side::Sell);
}

TEST(BinanceSide, DivergesFromCanonical) {
    // The canonical converter is lowercase; reusing it for Binance would
    // silently corrupt the wire format (same rationale as the Kraken
    // OrderType divergence test).
    EXPECT_NE(binance_side_to_string(Side::Buy), exchange::to_string(Side::Buy));
    EXPECT_EQ(exchange::to_string(Side::Buy), "buy");
}

TEST(BinanceSide, FromStringThrowsOnUnknown) {
    EXPECT_THROW(binance_side_from_string("buy"), std::invalid_argument);
    EXPECT_THROW(binance_side_from_string("HOLD"), std::invalid_argument);
}

// ─────────────────────────────────────────────────────────────────────────────
// OrderType — 6 mappable values; throws on the rest (no Unknown to absorb)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct OrderTypeCase {
    OrderType   value;
    const char* wire;
};

constexpr OrderTypeCase kSupportedOrderTypes[] = {
    {OrderType::Limit,           "LIMIT"},
    {OrderType::Market,          "MARKET"},
    {OrderType::StopLoss,        "STOP_LOSS"},
    {OrderType::StopLossLimit,   "STOP_LOSS_LIMIT"},
    {OrderType::TakeProfit,      "TAKE_PROFIT"},
    {OrderType::TakeProfitLimit, "TAKE_PROFIT_LIMIT"},
};

} // namespace

TEST(BinanceOrderType, SupportedValuesRoundTrip) {
    for (const auto& c : kSupportedOrderTypes) {
        EXPECT_EQ(binance_order_type_to_string(c.value), c.wire);
        EXPECT_EQ(binance_order_type_from_string(c.wire), c.value);
    }
}

TEST(BinanceOrderType, DivergesFromCanonical) {
    // Canonical is lowercase-underscore ("stop_loss_limit"); Binance is
    // uppercase-underscore ("STOP_LOSS_LIMIT") — genuinely different strings.
    for (const auto& c : kSupportedOrderTypes)
        EXPECT_NE(binance_order_type_to_string(c.value), exchange::to_string(c.value));
}

TEST(BinanceOrderType, ToStringThrowsOnUnsupported) {
    EXPECT_THROW(binance_order_type_to_string(OrderType::Iceberg),
                 std::invalid_argument);
    EXPECT_THROW(binance_order_type_to_string(OrderType::TrailingStop),
                 std::invalid_argument);
    EXPECT_THROW(binance_order_type_to_string(OrderType::TrailingStopLimit),
                 std::invalid_argument);
    EXPECT_THROW(binance_order_type_to_string(OrderType::SettlePosition),
                 std::invalid_argument);
}

TEST(BinanceOrderType, FromStringThrowsOnUnmappable) {
    // LIMIT_MAKER is a real Binance order type with no canonical OrderType
    // value — response structs keep `type` as a raw string for exactly this
    // reason; the converter is for callers who explicitly want the enum.
    EXPECT_THROW(binance_order_type_from_string("LIMIT_MAKER"),
                 std::invalid_argument);
    EXPECT_THROW(binance_order_type_from_string("limit"), std::invalid_argument);
    EXPECT_THROW(binance_order_type_from_string("not-a-type"),
                 std::invalid_argument);
}

// ─────────────────────────────────────────────────────────────────────────────
// TimeInForce — {GTC, IOC, FOK} total after the canonical FOK addition
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceTimeInForce, RoundTrip) {
    EXPECT_EQ(binance_tif_to_string(TimeInForce::GTC), "GTC");
    EXPECT_EQ(binance_tif_to_string(TimeInForce::IOC), "IOC");
    EXPECT_EQ(binance_tif_to_string(TimeInForce::FOK), "FOK");
    EXPECT_EQ(binance_tif_from_string("GTC"), TimeInForce::GTC);
    EXPECT_EQ(binance_tif_from_string("IOC"), TimeInForce::IOC);
    EXPECT_EQ(binance_tif_from_string("FOK"), TimeInForce::FOK);
}

TEST(BinanceTimeInForce, GtdThrows) {
    // Binance has no GTD time-in-force.
    EXPECT_THROW(binance_tif_to_string(TimeInForce::GTD), std::invalid_argument);
    EXPECT_THROW(binance_tif_from_string("GTD"), std::invalid_argument);
}

TEST(BinanceTimeInForce, CanonicalFokIsLowercase) {
    // The canonical converter gained FOK alongside this plan — assert its
    // wire string here too (the Kraken-side behaviour is asserted in
    // test_order_type.cpp).
    EXPECT_EQ(exchange::to_string(TimeInForce::FOK), "fok");
}

// ─────────────────────────────────────────────────────────────────────────────
// OrderStatus — total via the Unknown fallback; never throws
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceOrderStatus, MappedValues) {
    EXPECT_EQ(binance_order_status_from_string("NEW"), OrderStatus::New);
    EXPECT_EQ(binance_order_status_from_string("PARTIALLY_FILLED"),
              OrderStatus::PartiallyFilled);
    EXPECT_EQ(binance_order_status_from_string("FILLED"), OrderStatus::Filled);
    EXPECT_EQ(binance_order_status_from_string("CANCELED"), OrderStatus::Canceled);
    EXPECT_EQ(binance_order_status_from_string("EXPIRED"), OrderStatus::Expired);
    EXPECT_EQ(binance_order_status_from_string("EXPIRED_IN_MATCH"),
              OrderStatus::Expired);
}

TEST(BinanceOrderStatus, UnmappedValuesFoldToUnknown) {
    // Deliberately lossy (plan step-6-binance-rest-private decision 3): a strict parser here would
    // make one odd row blow up an entire openOrders parse.
    EXPECT_EQ(binance_order_status_from_string("PENDING_CANCEL"),
              OrderStatus::Unknown);
    EXPECT_EQ(binance_order_status_from_string("REJECTED"), OrderStatus::Unknown);
    EXPECT_EQ(binance_order_status_from_string("SOME_FUTURE_STATUS"),
              OrderStatus::Unknown);
    EXPECT_EQ(binance_order_status_from_string(""), OrderStatus::Unknown);
}

// ─────────────────────────────────────────────────────────────────────────────
// BinanceOrderRespType — newOrderRespType wire values
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceOrderRespTypeTest, ToString) {
    EXPECT_EQ(binance_order_resp_type_to_string(BinanceOrderRespType::Ack), "ACK");
    EXPECT_EQ(binance_order_resp_type_to_string(BinanceOrderRespType::Result),
              "RESULT");
    EXPECT_EQ(binance_order_resp_type_to_string(BinanceOrderRespType::Full),
              "FULL");
}
