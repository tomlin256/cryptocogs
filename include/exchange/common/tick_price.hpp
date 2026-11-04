// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/common/tick_price.hpp
// TickPrice — exact-decimal price representation, exchange-agnostic.
//
// Stores a price as an integer tick count plus a decimal-point position, so the
// decimal string is built by integer point-insertion (no floating-point
// formatting, no locale). Generic: any adapter that needs exact-decimal price
// round-tripping can use it. Re-exported into adapter namespaces (e.g.
// exchange::kraken::TickPrice) with a using declaration.
//
// Namespace: exchange

#include "exchange/common/cxx_standard.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace exchange {

using json = nlohmann::json;

// All member functions are defined in src/exchange/common/tick_price.cpp.
struct TickPrice {
    int64_t ticks{0};
    int     decimals{0};

    // Snap an absolute price onto the tick grid for a given decimal precision.
    static TickPrice from(double price, int decimals);

    // Exact decimal string via integer point-insertion — no FP, no locale.
    std::string str() const;

    json to_json() const;

    static TickPrice from_json(const json& j);
};

} // namespace exchange
