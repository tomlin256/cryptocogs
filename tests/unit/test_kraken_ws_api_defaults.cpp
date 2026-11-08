// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// What the Kraken WebSocket requests that hold an enum are before any field is set (plan clear-clang-tidy-findings, #46).
// AddOrderRequest, SubscribeRequest and UnsubscribeRequest give each enum member a default initialiser, so a bare request
// is defined (the first enumerator) and serialises to a definite frame. Those members used to have none, and a bare
// request's to_json() read an indeterminate value. Also pins the underlying type of the two enums that ws_api.hpp declares
// (plan clear-clang-tidy-findings D1).

#include "exchange/kraken/ws_api.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

using namespace exchange::kraken::ws;
using exchange::OrderType;
using exchange::Side;

// plan clear-clang-tidy-findings D1: the underlying type of the public enums is pinned; widening one changes the layout of the
// order-entry structs (#38).
static_assert(std::is_same_v<std::underlying_type_t<SubscribeChannel>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<MessageKind>, std::uint8_t>);

// Each request below is declared const, which is the compile-time half of the check: a const object of class type cannot be
// default-initialised unless every one of its members has an initialiser or a constructor of its own, so these tests stop
// compiling if a `{}` goes from an enum member. The values asserted are the other half.

TEST(KrakenWsApiDefaults, BareAddOrderRequestIsDefined) {
    const AddOrderRequest req;

    EXPECT_EQ(req.order_type, OrderType::Limit);
    EXPECT_EQ(req.side, Side::Buy);
    EXPECT_DOUBLE_EQ(req.order_qty, 0.0);
    EXPECT_TRUE(req.symbol.empty());
    EXPECT_TRUE(req.token.empty());
    EXPECT_EQ(req.req_id, 0);
    EXPECT_FALSE(req.limit_price.has_value());
    EXPECT_FALSE(req.time_in_force.has_value());
    EXPECT_FALSE(req.stp_type.has_value());
    EXPECT_FALSE(req.cl_ord_id.has_value());

    // The frame it serialises to: the defaults, and nothing optional.
    EXPECT_EQ(req.to_json(),
              json::parse(R"({"method":"add_order","req_id":0,
                              "params":{"order_type":"limit","side":"buy","order_qty":0.0,"symbol":"","token":""}})"));
}

TEST(KrakenWsApiDefaults, BareSubscribeRequestIsDefined) {
    const SubscribeRequest req;

    EXPECT_EQ(req.channel, SubscribeChannel::Ticker);
    EXPECT_EQ(req.req_id, 0);
    EXPECT_FALSE(req.symbols.has_value());
    EXPECT_FALSE(req.token.has_value());
    EXPECT_FALSE(req.depth.has_value());

    EXPECT_EQ(req.to_json(),
              json::parse(R"({"method":"subscribe","req_id":0,"params":{"channel":"ticker"}})"));
}

TEST(KrakenWsApiDefaults, BareUnsubscribeRequestIsDefined) {
    const UnsubscribeRequest req;

    EXPECT_EQ(req.channel, SubscribeChannel::Ticker);
    EXPECT_FALSE(req.symbols.has_value());
    EXPECT_FALSE(req.token.has_value());
    EXPECT_FALSE(req.req_id.has_value());

    // No req_id in the frame: it is optional here, and unset.
    EXPECT_EQ(req.to_json(),
              json::parse(R"({"method":"unsubscribe","params":{"channel":"ticker"}})"));
}

// A typed request sets its channel in its constructor, after the member's own initialiser has run: the channel it names
// wins. Balances is not the first enumerator, so it cannot be the default by accident.
TEST(KrakenWsApiDefaults, TypedSubscribeRequestKeepsItsOwnChannel) {
    const BalancesSubscribeRequest req;

    EXPECT_EQ(req.channel, SubscribeChannel::Balances);
    EXPECT_EQ(req.route_key(), "balances");
    EXPECT_EQ(req.unsubscribe_json(),
              json::parse(R"({"method":"unsubscribe","params":{"channel":"balances"}})"));
}
