// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/common/rest.hpp"
#include "exchange/common/ws.hpp"
#include "exchange/kraken/types.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

using exchange::OrderType;

// plan 037 D1: the underlying type of the public enums is pinned; widening one changes the layout of the order-entry structs (#38).
static_assert(std::is_same_v<std::underlying_type_t<exchange::Side>,                      std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<exchange::OrderType>,                 std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<exchange::TimeInForce>,               std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<exchange::OrderStatus>,               std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<exchange::rest::HttpRequest::Method>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<exchange::ws::FrameKind>,             std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<exchange::kraken::PriceType>,         std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<exchange::kraken::TriggerReference>,  std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<exchange::kraken::StpType>,           std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<exchange::kraken::FeePreference>,     std::uint8_t>);

namespace {

// All ten OrderType values, paired with the wire string each converter must
// produce. The generic converter (shared across exchanges) uses underscores;
// Kraken's wire format uses hyphens for the same multi-word values — these
// are genuinely different strings, not a naming-convention nuance.
struct Case {
    OrderType   value;
    const char* generic;
    const char* kraken;
};

constexpr Case kCases[] = {
    {OrderType::Limit,             "limit",              "limit"},
    {OrderType::Market,            "market",             "market"},
    {OrderType::Iceberg,           "iceberg",            "iceberg"},
    {OrderType::StopLoss,          "stop_loss",          "stop-loss"},
    {OrderType::StopLossLimit,     "stop_loss_limit",    "stop-loss-limit"},
    {OrderType::TakeProfit,        "take_profit",        "take-profit"},
    {OrderType::TakeProfitLimit,   "take_profit_limit",  "take-profit-limit"},
    {OrderType::TrailingStop,      "trailing_stop",      "trailing-stop"},
    {OrderType::TrailingStopLimit, "trailing_stop_limit","trailing-stop-limit"},
    {OrderType::SettlePosition,    "settle_position",    "settle-position"},
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// exchange::to_string(OrderType) — generic, underscore-separated wire format
// ─────────────────────────────────────────────────────────────────────────────

TEST(OrderTypeToString, GenericUsesUnderscores) {
    for (const auto& c : kCases)
        EXPECT_EQ(exchange::to_string(c.value), c.generic);
}

// ─────────────────────────────────────────────────────────────────────────────
// kraken_order_type_to_string / kraken_order_type_from_string — Kraken's
// hyphen-separated wire format (byte-identical to the pre-refactor kraken::
// converters; this is what the compat shim must forward to).
// ─────────────────────────────────────────────────────────────────────────────

TEST(OrderTypeToString, KrakenUsesHyphens) {
    for (const auto& c : kCases)
        EXPECT_EQ(exchange::kraken::kraken_order_type_to_string(c.value), c.kraken);
}

TEST(OrderTypeToString, GenericAndKrakenDivergeForMultiWordValues) {
    // The whole reason kraken_order_type_to_string exists as a distinct
    // function: for any multi-word value, the generic and Kraken strings
    // are NOT the same. A shim that re-exported the generic overload for
    // OrderType would silently corrupt Kraken's wire format.
    for (const auto& c : kCases) {
        const std::string generic = exchange::to_string(c.value);
        const std::string kraken  = exchange::kraken::kraken_order_type_to_string(c.value);
        if (generic.find('_') != std::string::npos)
            EXPECT_NE(generic, kraken) << "expected divergence for " << generic;
        else
            EXPECT_EQ(generic, kraken) << "single-word values should match";
    }
}

TEST(OrderTypeFromString, KrakenRoundTrip) {
    for (const auto& c : kCases)
        EXPECT_EQ(exchange::kraken::kraken_order_type_from_string(c.kraken), c.value);
}

TEST(OrderTypeFromString, ThrowsOnUnknown) {
    EXPECT_THROW(exchange::kraken::kraken_order_type_from_string("not-a-real-type"),
                 std::invalid_argument);
}

// ─────────────────────────────────────────────────────────────────────────────
// TimeInForce — canonical FOK (added for Binance) has no Kraken wire format
// ─────────────────────────────────────────────────────────────────────────────

TEST(TimeInForceToString, CanonicalIncludesFok) {
    using exchange::TimeInForce;
    EXPECT_EQ(exchange::to_string(TimeInForce::GTC), "gtc");
    EXPECT_EQ(exchange::to_string(TimeInForce::GTD), "gtd");
    EXPECT_EQ(exchange::to_string(TimeInForce::IOC), "ioc");
    EXPECT_EQ(exchange::to_string(TimeInForce::FOK), "fok");
}

TEST(TimeInForceToString, KrakenRejectsFok) {
    using exchange::TimeInForce;
    // Kraken's Spot API has no fill-or-kill time-in-force; the canonical
    // value exists for Binance. The Kraken converter must throw rather than
    // emit a string Kraken would reject (or worse, misinterpret).
    EXPECT_EQ(exchange::kraken::kraken_tif_to_string(TimeInForce::GTC), "gtc");
    EXPECT_THROW(exchange::kraken::kraken_tif_to_string(TimeInForce::FOK),
                 std::invalid_argument);
    EXPECT_THROW(exchange::kraken::kraken_tif_from_string("fok"),
                 std::invalid_argument);
}
