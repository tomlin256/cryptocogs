// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Captured Crypto.com Exchange v1 PUBLIC REST responses (full {id,method,code,
// result} envelopes), live from api.crypto.com on 2026-06 (plan cryptocom-exchange-adapter step 3).
// Arrays trimmed to a couple of rows; field names/types are verbatim from the
// wire (terse single-letter keys, monetary fields as strings, ms timestamps as
// numbers). Private fixtures are synthetic and live in
// cryptocom_account_example_json.hpp.

#pragma once

namespace cryptocom_fixtures {

// public/get-instruments — result.data[] (one spot CCY_PAIR row).
inline constexpr const char* INSTRUMENTS = R"json(
{"id":1,"method":"public/get-instruments","code":0,"result":{"data":[
  {"symbol":"BTC_USD","inst_type":"CCY_PAIR","display_name":"BTC/USD","base_ccy":"BTC","quote_ccy":"USD","quote_decimals":2,"quantity_decimals":5,"price_tick_size":"0.01","qty_tick_size":"0.00001","max_leverage":"50","tradable":true,"expiry_timestamp_ms":0,"beta_product":false,"product_type":"DIGITAL_CURRENCIES","margin_buy_enabled":true,"margin_sell_enabled":true}
]}}
)json";

// public/get-tickers — result.data[].
inline constexpr const char* TICKERS = R"json(
{"id":1,"method":"public/get-tickers","code":0,"result":{"data":[
  {"i":"BTC_USD","h":"64330.11","l":"62896.85","a":"63778.90","v":"3025.0123","vv":"192262143.88","c":"0.0119","b":"63778.69","k":"63778.70","oi":"0","t":1781981527085}
]}}
)json";

// public/get-book — result.data[0] holds bids/asks/t; rows ["price","size","num"].
inline constexpr const char* BOOK = R"json(
{"id":1,"method":"public/get-book","code":0,"result":{"depth":5,"instrument_name":"BTC_USD","data":[
  {"bids":[["63778.69","0.32644","6"],["63776.21","0.00002","1"]],"asks":[["63778.70","0.29677","8"],["63778.90","0.01258","1"]],"t":1781981527396}
]}}
)json";

// public/get-candlestick — result has interval + data[] rows {o,h,l,c,v,t}.
inline constexpr const char* CANDLES = R"json(
{"id":1,"method":"public/get-candlestick","code":0,"result":{"interval":"1m","instrument_name":"BTC_USD","data":[
  {"o":"63802.28","h":"63810.00","l":"63800.00","c":"63805.10","v":"0.0001","t":1781980080000}
]}}
)json";

// public/get-trades — result.data[]; side `s` is lowercase, ids `d`/`m` strings.
inline constexpr const char* TRADES = R"json(
{"id":1,"method":"public/get-trades","code":0,"result":{"data":[
  {"d":"1781981518402983395","t":1781981518402,"tn":1781981518402983395,"q":"0.00109","p":"63778.90","s":"buy","i":"BTC_USD","m":"4611686018731745646"}
]}}
)json";

} // namespace cryptocom_fixtures
