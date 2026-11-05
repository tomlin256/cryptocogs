// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/common/types.hpp
// Canonical exchange-agnostic enumerations shared across REST and WebSocket
// layers for all exchange adapters.

#include "exchange/common/cxx_standard.hpp"

#include <cstdint>
#include <string>

namespace exchange {

enum class Side : std::uint8_t { Buy, Sell };

enum class OrderType : std::uint8_t {
    Limit,
    Market,
    Iceberg,
    StopLoss,
    StopLossLimit,
    TakeProfit,
    TakeProfitLimit,
    TrailingStop,
    TrailingStopLimit,
    SettlePosition
};

enum class TimeInForce : std::uint8_t { GTC, GTD, IOC, FOK };

enum class OrderStatus : std::uint8_t {
    PendingNew,
    New,
    PartiallyFilled,
    Filled,
    Canceled,
    Expired,
    Unknown
};

// ── to_string / from_string (defined in src/exchange/common/types.cpp) ───────

std::string to_string(Side v);
Side        side_from_string(const std::string& s);

std::string to_string(OrderType v);

std::string to_string(TimeInForce v);

std::string to_string(OrderStatus v);
OrderStatus order_status_from_string(const std::string& s);

} // namespace exchange
