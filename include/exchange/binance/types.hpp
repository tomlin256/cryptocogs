// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/binance/types.hpp
// Binance-specific enum converters and small shared types.
//
// Namespace: exchange::binance
//
// The canonical exchange-agnostic enums (Side, OrderType, TimeInForce,
// OrderStatus) live in exchange/common/types.hpp and are re-exported here.
// Binance's wire formats are UPPERCASE ("BUY", "STOP_LOSS_LIMIT", "GTC") —
// diverging from the canonical lowercase strings — so, per the
// kraken_order_type_to_string precedent, Binance keeps its own converter
// pair per enum alongside (not instead of) the canonical converters.
//
// Conversion policy (plan 004 decision 3 — "enum where the wire↔enum
// mapping is total, raw string otherwise"):
//   - Side          : total both directions.
//   - TimeInForce   : total — Binance's set is exactly {GTC, IOC, FOK};
//                     GTD throws (no Binance wire format).
//   - OrderStatus   : from_string is total via the Unknown fallback.
//   - OrderType     : NOT total — Binance's LIMIT_MAKER (and any future
//                     type) has no canonical value, so response structs
//                     keep `type` as a raw string and these converters
//                     throw on unmappable input.

#include "exchange/common/types.hpp"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>

namespace exchange::binance {

using json = nlohmann::json;

// Re-export the canonical enums so exchange::binance code refers to them
// unprefixed (mirrors exchange/kraken/types.hpp).
using exchange::Side;
using exchange::OrderType;
using exchange::TimeInForce;
using exchange::OrderStatus;

// Converters below are defined in src/binance/types.cpp.

// ── Side ─────────────────────────────────────────────────────────────────────

std::string binance_side_to_string(Side v);
Side        binance_side_from_string(const std::string& s);

// ── OrderType ────────────────────────────────────────────────────────────────
//
// Supports the 6 canonical types Binance accepts. Throws std::invalid_argument
// for canonical values with no Binance wire format (Iceberg, TrailingStop,
// TrailingStopLimit, SettlePosition) and, in from_string, for "LIMIT_MAKER"
// or unknown input — canonical OrderType has no Unknown value to absorb them.

std::string binance_order_type_to_string(OrderType v);
OrderType   binance_order_type_from_string(const std::string& s);

// ── TimeInForce ──────────────────────────────────────────────────────────────

std::string binance_tif_to_string(TimeInForce v);
TimeInForce binance_tif_from_string(const std::string& s);

// ── OrderStatus ──────────────────────────────────────────────────────────────
//
// Total — never throws. Unmapped wire statuses (PENDING_CANCEL, REJECTED,
// and anything Binance adds later) fold to OrderStatus::Unknown so a single
// odd row cannot blow up an entire openOrders/allOrders parse.

OrderStatus binance_order_status_from_string(const std::string& s);

// ── newOrderRespType (POST /api/v3/order) ────────────────────────────────────

enum class BinanceOrderRespType : std::uint8_t { Ack, Result, Full };

std::string binance_order_resp_type_to_string(BinanceOrderRespType v);

// ── BinanceBookLevel ─────────────────────────────────────────────────────────
//
// One order-book price level. Shared by the REST order book
// (GET /api/v3/depth) and the WS depth streams (@depth diff and
// @depth<levels> partial), which all use the same positional row format.

struct BinanceBookLevel {
    double price{0.0};
    double quantity{0.0};
    // Parses a positional 2-element row: ["price","qty"]. Defined in types.cpp.
    static BinanceBookLevel from_json(const json& row);
};

} // namespace exchange::binance
