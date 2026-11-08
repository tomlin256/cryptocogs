// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Sample JSON frames from the Binance WebSocket market-stream docs (see
// docs/plans/appendix-binance-message-formats.md §3, fetched 2026-06-06).
// Used as test fixtures in test_binance_ws_client.cpp.
//
// Fixtures are verbatim from the appendix unless marked synthetic.

#pragma once

namespace exchange::binance::ws::test {

// ─────────────────────────────────────────────────────────────────────────────
// SUBSCRIBE / UNSUBSCRIBE acks
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kSubscribeAckJson = R"({"result":null,"id":1})";

inline constexpr const char* kErrorAckJson =
    R"({"error":{"code":2,"msg":"Invalid request: subscription id not provided"},"id":7})";

// ─────────────────────────────────────────────────────────────────────────────
// Combined-stream wrapper — what dispatch actually delivers to push callbacks
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kWrappedAggTradeJson = R"({
  "stream": "bnbbtc@aggTrade",
  "data": {"e":"aggTrade","E":1672515782136,"s":"BNBBTC","a":12345,"p":"0.001","q":"100","f":100,"l":105,"T":1672515782136,"m":true,"M":true}
})";

// ─────────────────────────────────────────────────────────────────────────────
// Bare event payloads (appendix §3 — what "data" carries)
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kAggTradeJson =
    R"({"e":"aggTrade","E":1672515782136,"s":"BNBBTC","a":12345,"p":"0.001","q":"100","f":100,"l":105,"T":1672515782136,"m":true,"M":true})";

inline constexpr const char* kTradeJson =
    R"({"e":"trade","E":1672515782136,"s":"BNBBTC","t":12345,"p":"0.001","q":"100","T":1672515782136,"m":true,"M":true})";

inline constexpr const char* kTickerJson =
    R"({"e":"24hrTicker","E":1672515782136,"s":"BNBBTC","p":"0.0015","P":"250.00","w":"0.0018","x":"0.0009","c":"0.0025","Q":"10","b":"0.0024","B":"10","a":"0.0026","A":"100","o":"0.0010","h":"0.0025","l":"0.0010","v":"10000","q":"18","O":0,"C":86400000,"F":0,"L":18150,"n":18151})";

inline constexpr const char* kMiniTickerJson =
    R"({"e":"24hrMiniTicker","E":1672515782136,"s":"BNBBTC","c":"0.0025","o":"0.0010","h":"0.0025","l":"0.0010","v":"10000","q":"18"})";

// No "e"/"E" event fields — identity comes from the wrapper's stream name.
inline constexpr const char* kBookTickerJson =
    R"({"u":400900217,"s":"BNBUSDT","b":"25.35190000","B":"31.21000000","a":"25.36520000","A":"40.66000000"})";

inline constexpr const char* kKlineJson =
    R"({"e":"kline","E":1672515782136,"s":"BNBBTC","k":{"t":1672515780000,"T":1672515839999,"s":"BNBBTC","i":"1m","f":100,"L":200,"o":"0.0010","c":"0.0020","h":"0.0025","l":"0.0015","v":"1000","n":100,"x":false,"q":"1.0000","V":"500","Q":"0.500","B":"123456"}})";

inline constexpr const char* kDepthUpdateJson =
    R"({"e":"depthUpdate","E":1672515782136,"s":"BNBBTC","U":157,"u":160,"b":[["0.0024","10"]],"a":[["0.0026","100"]]})";

inline constexpr const char* kPartialDepthJson =
    R"({"lastUpdateId":160,"bids":[["0.0024","10"]],"asks":[["0.0026","100"]]})";

} // namespace exchange::binance::ws::test
