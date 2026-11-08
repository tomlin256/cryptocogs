// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Sample JSON messages captured from the Binance Spot REST API docs (see
// docs/plans/appendix-binance-message-formats.md, fetched 2026-06-06).
// Used as test fixtures in test_binance_rest_responses.cpp.
//
// Fixtures are verbatim from the appendix unless marked synthetic.

#pragma once

namespace exchange::binance::rest::test {

// ─────────────────────────────────────────────────────────────────────────────
// GET /api/v3/ping
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kPingJson = R"({})";

// ─────────────────────────────────────────────────────────────────────────────
// GET /api/v3/time
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kServerTimeJson = R"({"serverTime": 1499827319559})";

// ─────────────────────────────────────────────────────────────────────────────
// GET /api/v3/ticker/price
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kTickerPriceSingleJson = R"({"symbol":"LTCBTC","price":"4.00000200"})";

// Synthetic — array form returned for a `symbols=[...]` query (2 elements).
inline constexpr const char* kTickerPriceArrayJson =
    R"([{"symbol":"LTCBTC","price":"4.00000200"},{"symbol":"ETHBTC","price":"0.07734100"}])";

// ─────────────────────────────────────────────────────────────────────────────
// GET /api/v3/depth
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kDepthJson = R"({
  "lastUpdateId": 1027024,
  "bids": [["4.00000000","431.00000000"]],
  "asks": [["4.00000200","12.00000000"]]
})";

// ─────────────────────────────────────────────────────────────────────────────
// GET /api/v3/trades
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kTradesJson = R"([
  {"id":28457,"price":"4.00000100","qty":"12.00000000","quoteQty":"48.000012","time":1499865549590,"isBuyerMaker":true,"isBestMatch":true}
])";

// ─────────────────────────────────────────────────────────────────────────────
// GET /api/v3/klines
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kKlinesJson = R"([
  [1499040000000,"0.01634790","0.80000000","0.01575800","0.01577100","148976.11427815",1499644799999,"2434.19055334",308,"1756.87402397","28.46694368","0"]
])";

// ─────────────────────────────────────────────────────────────────────────────
// GET /api/v3/exchangeInfo
// ─────────────────────────────────────────────────────────────────────────────

// Abridged from the appendix: timezone, serverTime, and one symbols[] entry
// with the 12 in-scope fields (filters/permissions/etc. omitted per the
// "first cut" scope).
inline constexpr const char* kExchangeInfoJson = R"({
  "timezone": "UTC",
  "serverTime": 1565246363776,
  "symbols": [
    {
      "symbol": "ETHBTC",
      "status": "TRADING",
      "baseAsset": "ETH",
      "baseAssetPrecision": 8,
      "quoteAsset": "BTC",
      "quotePrecision": 8,
      "quoteAssetPrecision": 8,
      "orderTypes": ["LIMIT","LIMIT_MAKER","MARKET","STOP_LOSS","STOP_LOSS_LIMIT","TAKE_PROFIT","TAKE_PROFIT_LIMIT"],
      "icebergAllowed": true,
      "ocoAllowed": true,
      "isSpotTradingAllowed": true,
      "isMarginTradingAllowed": true
    }
  ]
})";

// ─────────────────────────────────────────────────────────────────────────────
// GET /api/v3/ticker/24hr
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kTicker24hrSingleJson = R"({
  "symbol":"BNBBTC","priceChange":"-94.99999800","priceChangePercent":"-95.960",
  "weightedAvgPrice":"0.29628482","prevClosePrice":"0.10002000","lastPrice":"4.00000200",
  "lastQty":"200.00000000","bidPrice":"4.00000000","bidQty":"100.00000000",
  "askPrice":"4.00000200","askQty":"100.00000000","openPrice":"99.00000000",
  "highPrice":"100.00000000","lowPrice":"0.10000000","volume":"8913.30000000",
  "quoteVolume":"15.30000000","openTime":1499783499040,"closeTime":1499869899040,
  "firstId":28385,"lastId":28460,"count":76
})";

// Synthetic — array form returned for a `symbols=[...]` query (2 elements:
// the appendix BNBBTC object plus a second symbol).
inline constexpr const char* kTicker24hrArrayJson = R"([
  {
    "symbol":"BNBBTC","priceChange":"-94.99999800","priceChangePercent":"-95.960",
    "weightedAvgPrice":"0.29628482","prevClosePrice":"0.10002000","lastPrice":"4.00000200",
    "lastQty":"200.00000000","bidPrice":"4.00000000","bidQty":"100.00000000",
    "askPrice":"4.00000200","askQty":"100.00000000","openPrice":"99.00000000",
    "highPrice":"100.00000000","lowPrice":"0.10000000","volume":"8913.30000000",
    "quoteVolume":"15.30000000","openTime":1499783499040,"closeTime":1499869899040,
    "firstId":28385,"lastId":28460,"count":76
  },
  {
    "symbol":"ETHBTC","priceChange":"0.00100000","priceChangePercent":"1.310",
    "weightedAvgPrice":"0.07700000","prevClosePrice":"0.07634100","lastPrice":"0.07734100",
    "lastQty":"5.00000000","bidPrice":"0.07734000","bidQty":"12.00000000",
    "askPrice":"0.07734200","askQty":"9.00000000","openPrice":"0.07634100",
    "highPrice":"0.07800000","lowPrice":"0.07600000","volume":"12345.00000000",
    "quoteVolume":"950.00000000","openTime":1499783499040,"closeTime":1499869899040,
    "firstId":1000,"lastId":2000,"count":1001
  }
])";

// ─────────────────────────────────────────────────────────────────────────────
// Error envelope (any endpoint, non-2xx status)
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kErrorJson = R"({"code":-1121,"msg":"Invalid symbol."})";

} // namespace exchange::binance::rest::test
