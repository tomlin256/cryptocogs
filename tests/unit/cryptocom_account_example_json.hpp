// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// SYNTHETIC Crypto.com Exchange v1 PRIVATE REST response fixtures (plan cryptocom-exchange-adapter
// step 4). No live credentials are available, so these are hand-authored from
// the documented field names/types (full {id,method,code,result} envelopes) and
// are NOT captured from the wire. Field names match the v1 docs; values are
// illustrative. Public fixtures (live-captured) live in
// cryptocom_rest_example_json.hpp.

#pragma once

namespace cryptocom_fixtures {

// private/user-balance — result.data[0] with one position_balances row.
inline constexpr const char* USER_BALANCE = R"json(
{"id":1,"method":"private/user-balance","code":0,"result":{"data":[
  {"total_available_balance":"1000.00","total_margin_balance":"1000.00","total_initial_margin":"0","total_cash_balance":"1000.00","total_collateral_value":"1000.00","total_session_unrealized_pnl":"0","total_session_realized_pnl":"0","instrument_name":"USD","position_balances":[
    {"instrument_name":"BTC","quantity":"0.5","market_value":"31889.34","collateral_amount":"31000.00","max_withdrawal_balance":"0.5","reserved_qty":"0"}
  ]}
]}}
)json";

// private/create-order — result {order_id, client_oid}.
inline constexpr const char* CREATE_ORDER = R"json(
{"id":1,"method":"private/create-order","code":0,"result":{"client_oid":"c5f682ed-7108-4f1c-b755-972fcdca0f02","order_id":"18342311"}}
)json";

// private/cancel-order — success carries no result body.
inline constexpr const char* CANCEL_ORDER = R"json(
{"id":1,"method":"private/cancel-order","code":0}
)json";

// private/get-order-detail — the full order object.
inline constexpr const char* ORDER_DETAIL = R"json(
{"id":1,"method":"private/get-order-detail","code":0,"result":{"account_id":"acc-1","order_id":"18342311","client_oid":"c5f682ed","order_type":"LIMIT","time_in_force":"GOOD_TILL_CANCEL","side":"BUY","quantity":"0.00100","limit_price":"63000.00","order_value":"63.00","avg_price":"0","cumulative_quantity":"0","cumulative_value":"0","cumulative_fee":"0","status":"ACTIVE","create_time":1781980000000,"update_time":1781980000000,"instrument_name":"BTC_USD","fee_instrument_name":"USD"}}
)json";

// private/get-open-orders — result.data[] of order objects.
inline constexpr const char* OPEN_ORDERS = R"json(
{"id":1,"method":"private/get-open-orders","code":0,"result":{"data":[
  {"account_id":"acc-1","order_id":"18342311","client_oid":"c5f682ed","order_type":"LIMIT","time_in_force":"GOOD_TILL_CANCEL","side":"BUY","quantity":"0.00100","limit_price":"63000.00","order_value":"63.00","avg_price":"0","cumulative_quantity":"0","cumulative_value":"0","cumulative_fee":"0","status":"ACTIVE","create_time":1781980000000,"update_time":1781980000000,"instrument_name":"BTC_USD","fee_instrument_name":"USD"}
]}}
)json";

// private/get-trades — result.data[] of user trades.
inline constexpr const char* USER_TRADES = R"json(
{"id":1,"method":"private/get-trades","code":0,"result":{"data":[
  {"account_id":"acc-1","event_date":"2026-06-20","traded_quantity":"0.00100","traded_price":"63000.00","fees":"0.0315","order_id":"18342311","trade_id":"99999","trade_match_id":"12345","create_time":1781980050000,"create_time_ns":"1781980050000000000","side":"BUY","instrument_name":"BTC_USD","fee_instrument_name":"USD","client_oid":"c5f682ed","taker_side":"BUY","liquidity_indicator":"TAKER"}
]}}
)json";

} // namespace cryptocom_fixtures
