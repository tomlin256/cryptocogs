// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/kraken/types.hpp
// Kraken-specific types: enumerations, order parameter structures, and
// order/trade/ledger info structs shared between the REST and WebSocket layers.
//
// The canonical exchange-agnostic enums (Side, OrderType, TimeInForce,
// OrderStatus) live in exchange/common/types.hpp and are pulled in here with
// using declarations so callers need only include this header.
//
// Member/function bodies are defined in src/kraken/types.cpp (non-template) and
// kraken/types.inl (template).
//
// Namespace: exchange::kraken

#include "exchange/common/types.hpp"
#include "exchange/common/tick_price.hpp"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace exchange::kraken {

using json = nlohmann::json;

// ── Re-export canonical exchange enums ───────────────────────────────────────

using exchange::Side;
using exchange::OrderType;
using exchange::TimeInForce;
using exchange::OrderStatus;

// Pull in the to_string / from_string helpers for re-exported types.
using exchange::to_string;
using exchange::side_from_string;
using exchange::order_status_from_string;

// ── Kraken-specific enumerations ─────────────────────────────────────────────

enum class PriceType : std::uint8_t { Static, Pct, Quote };

enum class TriggerReference : std::uint8_t { Index, Last };

enum class StpType : std::uint8_t { CancelNewest, CancelOldest, CancelBoth };

enum class FeePreference : std::uint8_t { Base, Quote };

// ── to_string / from_string for Kraken-specific enums (src/kraken/types.cpp) ──

std::string      to_string(PriceType v);
PriceType        price_type_from_string(const std::string& s);

std::string      to_string(TriggerReference v);
TriggerReference trigger_ref_from_string(const std::string& s);

std::string      to_string(StpType v);
StpType          stp_type_from_string(const std::string& s);

std::string      to_string(FeePreference v);
FeePreference    fee_preference_from_string(const std::string& s);

// Kraken wire format uses different strings for OrderType than common/types.hpp.
std::string      kraken_order_type_to_string(OrderType v);
OrderType        kraken_order_type_from_string(const std::string& s);

std::string      kraken_tif_to_string(TimeInForce v);
TimeInForce      kraken_tif_from_string(const std::string& s);

// ── TickPrice — exact decimal price representation ───────────────────────────
//
// TickPrice is exchange-agnostic and now lives in exchange:: (see
// exchange/common/tick_price.hpp). Re-exported here so existing call sites that
// name exchange::kraken::TickPrice — and the kraken::TickPrice compat alias —
// keep resolving unchanged.

using exchange::TickPrice;

// ── Sub-objects shared between REST and WebSocket ─────────────────────────────

struct Triggers {
    TickPrice price{};
    std::optional<TriggerReference> reference;
    std::optional<PriceType>        price_type;

    json            to_json() const;
    static Triggers from_json(const json& j);
};

struct Conditional {
    std::optional<OrderType>  order_type;
    std::optional<TickPrice>  limit_price;
    std::optional<PriceType>  limit_price_type;
    std::optional<TickPrice>  trigger_price;
    std::optional<PriceType>  trigger_price_type;

    json               to_json() const;
    static Conditional from_json(const json& j);
};

// ── Core order parameter block ────────────────────────────────────────────────

struct OrderParams {
    // Required
    OrderType   order_type{OrderType::Market};
    Side        side{Side::Buy};
    double      order_qty{0.0};
    std::string symbol;

    // Optional pricing
    std::optional<TickPrice> limit_price;
    std::optional<PriceType> limit_price_type;

    // Trigger section
    std::optional<Triggers>   triggers;

    // OTO conditional close
    std::optional<Conditional> conditional;

    // Execution controls
    std::optional<TimeInForce>   time_in_force;
    std::optional<bool>          margin;
    std::optional<bool>          post_only;
    std::optional<bool>          reduce_only;
    std::optional<std::string>   effective_time;
    std::optional<std::string>   expire_time;
    std::optional<std::string>   deadline;
    std::optional<std::string>   cl_ord_id;
    std::optional<int64_t>       order_userref;
    std::optional<double>        display_qty;
    std::optional<FeePreference> fee_preference;
    std::optional<StpType>       stp_type;
    std::optional<double>        cash_order_qty;
    std::optional<bool>          validate;
    std::optional<std::string>   sender_sub_id;

    json               to_json() const;
    static OrderParams from_json(const json& j);
};

// ── Order description ─────────────────────────────────────────────────────────

struct OrderDescription {
    std::string pair;
    Side        side{Side::Buy};
    OrderType   order_type{OrderType::Market};
    std::string price;
    std::string price2;
    std::string leverage;
    std::string order;
    std::string close;

    static OrderDescription from_json(const json& j);
};

// ── Full order info ───────────────────────────────────────────────────────────

struct OrderInfo {
    std::string       txid;
    OrderStatus       status{OrderStatus::Unknown};
    OrderDescription  descr;
    double            vol{0.0};
    double            vol_exec{0.0};
    double            cost{0.0};
    double            fee{0.0};
    double            price{0.0};
    double            stopprice{0.0};
    double            limitprice{0.0};
    std::string       misc;
    std::string       oflags;
    std::optional<int64_t>     userref;
    std::optional<double>      opentm;
    std::optional<double>      closetm;
    std::optional<double>      starttm;
    std::optional<double>      expiretm;
    std::optional<std::string> reason;
    std::optional<std::vector<std::string>> trades;

    static OrderInfo from_json(const json& j, const std::string& id = "");
};

// ── Trade info ────────────────────────────────────────────────────────────────

struct TradeInfo {
    std::string txid;
    std::string ordertxid;
    std::string pair;
    double      time{0.0};
    Side        type{Side::Buy};
    OrderType   ordertype{OrderType::Market};
    double      price{0.0};
    double      cost{0.0};
    double      fee{0.0};
    double      vol{0.0};
    double      margin{0.0};
    std::string misc;
    std::optional<std::string> posstatus;
    std::optional<double>      cprice;
    std::optional<double>      ccost;
    std::optional<double>      cfee;
    std::optional<double>      cvol;
    std::optional<double>      cmargin;
    std::optional<double>      net;

    static TradeInfo from_json(const json& j, const std::string& id = "");
};

// ── Ledger entry ──────────────────────────────────────────────────────────────

struct LedgerEntry {
    std::string txid;
    std::string refid;
    double      time{0.0};
    std::string type;
    std::string subtype;
    std::string aclass;
    std::string asset;
    double      amount{0.0};
    double      fee{0.0};
    double      balance{0.0};

    static LedgerEntry from_json(const json& j, const std::string& id = "");
};

// ── Generic REST response envelope ───────────────────────────────────────────

template<typename T>
struct RestResponse {
    std::vector<std::string> errors;
    bool                     ok{false};
    std::optional<T>         result;

    bool               has_error() const;    // defined in kraken/types.inl
    const std::string& first_error() const;  // defined in kraken/types.inl
};

// Helper: parse the outer envelope and call T::from_json(result_node).
// Defined in kraken/types.inl.
template<typename T>
RestResponse<T> parse_rest_response(const json& j);

} // namespace exchange::kraken

#include "exchange/kraken/types.inl"
