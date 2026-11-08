// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/coinbase/types.hpp
// Coinbase-specific enum converters, the REST response envelope helper, and
// small shared types.
//
// Namespace: exchange::coinbase
//
// The canonical enums (Side, OrderType, TimeInForce, OrderStatus) live in
// exchange/common/types.hpp and are re-exported here. Coinbase's wire formats
// are lowercase ("buy", "limit", "GTC"), so — per the binance_*/kraken_*
// precedent — Coinbase keeps its own converter pair per enum.
//
// Conversion policy (mirrors plan step-6-binance-rest-private decision 3 — "enum where the wire↔enum
// mapping is total, raw string otherwise"):
//   - Side          : total both directions ("buy"/"sell").
//   - OrderType     : Coinbase's wire `type` is only "limit"/"market" (a stop
//                     is a separate overlay field, not an order type), so the
//                     converters cover exactly those two and throw otherwise.
//   - TimeInForce   : Coinbase's set is {GTC, GTT, IOC, FOK}. Canonical GTD maps
//                     to Coinbase "GTT" (good-til-time); GTT additionally needs
//                     a cancel_after field on the request.
//   - OrderStatus   : from_string is total via the Unknown fallback.

#include "exchange/common/rest.hpp"
#include "exchange/common/types.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace exchange::coinbase {

using json = nlohmann::json;

// Re-export the canonical enums so exchange::coinbase code refers to them
// unprefixed (mirrors exchange/binance/types.hpp).
using exchange::Side;
using exchange::OrderType;
using exchange::TimeInForce;
using exchange::OrderStatus;

// Converters below are defined in src/coinbase/types.cpp.

// ── Side ─────────────────────────────────────────────────────────────────────

std::string coinbase_side_to_string(Side v);
Side        coinbase_side_from_string(const std::string& s);

// ── OrderType ────────────────────────────────────────────────────────────────
//
// Coinbase's order `type` is "limit" or "market". Throws std::invalid_argument
// for any other canonical value (and for unknown input in from_string) —
// canonical OrderType has no Unknown value to absorb them.

std::string coinbase_order_type_to_string(OrderType v);
OrderType   coinbase_order_type_from_string(const std::string& s);

// ── TimeInForce ──────────────────────────────────────────────────────────────
//
// Total. Canonical GTD ↔ Coinbase "GTT".

std::string coinbase_tif_to_string(TimeInForce v);
TimeInForce coinbase_tif_from_string(const std::string& s);

// ── OrderStatus ──────────────────────────────────────────────────────────────
//
// Total — never throws. Unmapped statuses fold to OrderStatus::Unknown so one
// odd row cannot blow up a whole order-list parse. Note: "done"/"settled" map
// to Filled as a best effort; read the raw `status` + `done_reason` on the
// order struct to distinguish a filled order from a canceled one.

OrderStatus coinbase_order_status_from_string(const std::string& s);

// ── REST response envelope ────────────────────────────────────────────────────
//
// Coinbase has no {error[],result} wrapper: a 2xx response body *is* the result
// (object or array), and an error body is {"message":"..."} with a non-2xx
// status. parse_coinbase_response maps that onto the common RestResponse<T>.
// Defined inline (template); T::from_json(j) is resolved at the call site.

template<typename T>
exchange::rest::RestResponse<T> parse_coinbase_response(int http_status, const json& j) {
    exchange::rest::RestResponse<T> resp;
    if (http_status >= 200 && http_status < 300) {
        resp.ok     = true;
        resp.result = T::from_json(j);
    } else {
        resp.ok = false;
        if (j.is_object() && j.contains("message") && j.at("message").is_string())
            resp.errors.push_back(j.at("message").get<std::string>());
        else
            resp.errors.push_back("HTTP " + std::to_string(http_status));
    }
    return resp;
}

} // namespace exchange::coinbase
