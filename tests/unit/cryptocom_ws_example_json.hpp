// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Captured Crypto.com Exchange v1 WebSocket frames (plan cryptocom-exchange-adapter step 6). The MARKET
// frames (ticker/trade/book/candlestick pushes, the subscribe acks, and the
// heartbeat) are LIVE from wss://stream.crypto.com on 2026-06 — note data frames
// reuse method "subscribe" with id -1 for updates. Arrays are trimmed to a couple
// of rows. The USER frames (user.order / user.balance) and the public/auth reply
// are SYNTHETIC (no live credentials), hand-authored from the documented fields.

#pragma once

namespace cryptocom_ws_fixtures {

// ── Market pushes (live) — full frames; updates carry id -1 ──────────────────

inline constexpr const char* TICKER_PUSH = R"json(
{"id":-1,"method":"subscribe","code":0,"result":{"instrument_name":"BTC_USD","subscription":"ticker.BTC_USD","channel":"ticker","data":[{"h":"64531.99","l":"63120.85","a":"63958.47","c":"0.0092","b":"63962.64","bs":"0.27217","k":"63962.65","ks":"0.23532","i":"BTC_USD","v":"2657.5053","vv":"169819487.92","oi":"0","t":1782032455141}]}}
)json";

inline constexpr const char* TRADE_PUSH = R"json(
{"id":-1,"method":"subscribe","code":0,"result":{"instrument_name":"BTC_USD","subscription":"trade.BTC_USD","channel":"trade","data":[{"d":"1782032437671850129","t":1782032437671,"p":"63958.47","q":"0.04333","s":"SELL","i":"BTC_USD","m":"4611686018731788404"}]}}
)json";

inline constexpr const char* BOOK_PUSH = R"json(
{"id":-1,"method":"subscribe","code":0,"result":{"instrument_name":"BTC_USD","subscription":"book.BTC_USD.10","channel":"book","depth":10,"data":[{"asks":[["63962.65","0.23532","4"],["63962.67","0.08488","1"]],"bids":[["63962.64","0.27217","8"],["63960.59","0.03139","1"]],"t":1782032455315,"tt":1782032455261,"u":338686661310464,"cs":-1486958660}]}}
)json";

inline constexpr const char* CANDLE_PUSH = R"json(
{"id":-1,"method":"subscribe","code":0,"result":{"instrument_name":"BTC_USD","subscription":"candlestick.1m.BTC_USD","channel":"candlestick","interval":"1m","data":[{"o":"64349.71","h":"64356.28","l":"64349.71","c":"64356.28","v":"0.22838","t":1782014460000,"ut":1782014515996}]}}
)json";

// ── Subscribe acks (live) — echo the request id ───────────────────────────────

// Ticker ack carries the snapshot in result (id echoes the request, here 1).
inline constexpr const char* SUBSCRIBE_ACK = R"json(
{"id":1,"method":"subscribe","code":0,"result":{"instrument_name":"BTC_USD","subscription":"ticker.BTC_USD","channel":"ticker"}}
)json";

// Book's initial ack is bare — channel at top level, no result.
inline constexpr const char* SUBSCRIBE_ACK_BARE = R"json(
{"id":1,"method":"subscribe","code":0,"channel":"book.BTC_USD.10"}
)json";

// ── Heartbeat (live) ──────────────────────────────────────────────────────────

inline constexpr const char* HEARTBEAT = R"json(
{"id":1782032467342,"method":"public/heartbeat","code":0}
)json";

// ── User feed (synthetic) ─────────────────────────────────────────────────────

inline constexpr const char* AUTH_RESPONSE = R"json(
{"id":1,"method":"public/auth","code":0}
)json";

inline constexpr const char* USER_ORDER_PUSH = R"json(
{"id":-1,"method":"subscribe","code":0,"result":{"subscription":"user.order.BTC_USD","channel":"user.order","data":[{"order_id":"18342311","client_oid":"c5f682ed","instrument_name":"BTC_USD","status":"ACTIVE","side":"BUY","order_type":"LIMIT","quantity":"0.00100","limit_price":"63000.00","cumulative_quantity":"0","avg_price":"0","create_time":1781980000000,"update_time":1781980000000}]}}
)json";

inline constexpr const char* USER_BALANCE_PUSH = R"json(
{"id":-1,"method":"subscribe","code":0,"result":{"subscription":"user.balance","channel":"user.balance","data":[{"instrument_name":"USD","total_available_balance":"1000.00","total_cash_balance":"1000.00"}]}}
)json";

} // namespace cryptocom_ws_fixtures
