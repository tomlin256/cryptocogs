// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Sample JSON messages for the Binance Spot signed (private) REST endpoints
// (see docs/plans/appendix-binance-message-formats.md §2, fetched
// 2026-06-06). Used as test fixtures in test_binance_rest_responses.cpp and
// test_binance_client.cpp.
//
// All fixtures here are synthetic: these endpoints are credential-gated, so
// frames cannot be captured live — shapes are verbatim from the documented
// examples in the appendix.

#pragma once

namespace exchange::binance::rest::test {

// ─────────────────────────────────────────────────────────────────────────────
// GET /api/v3/account
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kAccountJson = R"({
  "makerCommission":15,"takerCommission":15,"buyerCommission":0,"sellerCommission":0,
  "commissionRates":{"maker":"0.00150000","taker":"0.00150000","buyer":"0.00000000","seller":"0.00000000"},
  "canTrade":true,"canWithdraw":true,"canDeposit":true,"brokered":false,
  "requireSelfTradePrevention":false,"preventSor":false,"updateTime":123456789,
  "accountType":"SPOT",
  "balances":[
    {"asset":"BTC","free":"4723846.89208129","locked":"0.00000000"},
    {"asset":"LTC","free":"4763368.68006011","locked":"0.00000000"}
  ],
  "permissions":["SPOT"],"uid":354937868
})";

// ─────────────────────────────────────────────────────────────────────────────
// GET /api/v3/openOrders and /api/v3/allOrders (same row shape; this fixture
// is reused for both BinanceOpenOrdersResult and the BinanceAllOrdersResult
// alias)
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kOpenOrdersJson = R"([
  {"symbol":"LTCBTC","orderId":1,"orderListId":-1,"clientOrderId":"myOrder1","price":"0.1","origQty":"1.0","executedQty":"0.0","cummulativeQuoteQty":"0.0","status":"NEW","timeInForce":"GTC","type":"LIMIT","side":"BUY","stopPrice":"0.0","icebergQty":"0.0","time":1499827319559,"updateTime":1499827319559,"isWorking":true,"origQuoteOrderQty":"0.000000","workingTime":1499827319559,"selfTradePreventionMode":"NONE"}
])";

// ─────────────────────────────────────────────────────────────────────────────
// GET /api/v3/myTrades
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kMyTradesJson = R"([
  {"symbol":"BNBBTC","id":28457,"orderId":100234,"orderListId":-1,"price":"4.00000100","qty":"12.00000000","quoteQty":"48.000012","commission":"10.10000000","commissionAsset":"BNB","time":1499865549590,"isBuyer":true,"isMaker":false,"isBestMatch":true}
])";

// ─────────────────────────────────────────────────────────────────────────────
// POST /api/v3/order — the three newOrderRespType shapes (ACK ⊂ RESULT ⊂ FULL)
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kNewOrderAckJson = R"({
  "symbol":"BTCUSDT","orderId":28,"orderListId":-1,
  "clientOrderId":"6gCrw2kRUAF9CvJDGP16IP","transactTime":1507725176595
})";

inline constexpr const char* kNewOrderResultJson = R"({
  "symbol":"BTCUSDT","orderId":28,"orderListId":-1,"clientOrderId":"6gCrw2kRUAF9CvJDGP16IP",
  "transactTime":1507725176595,"price":"0.00000000","origQty":"10.00000000",
  "executedQty":"10.00000000","origQuoteOrderQty":"0.000000","cummulativeQuoteQty":"10.00000000",
  "status":"FILLED","timeInForce":"GTC","type":"MARKET","side":"SELL",
  "workingTime":1507725176595,"selfTradePreventionMode":"NONE"
})";

inline constexpr const char* kNewOrderFullJson = R"({
  "symbol":"BTCUSDT","orderId":28,"orderListId":-1,"clientOrderId":"6gCrw2kRUAF9CvJDGP16IP",
  "transactTime":1507725176595,"price":"0.00000000","origQty":"10.00000000",
  "executedQty":"10.00000000","origQuoteOrderQty":"0.000000","cummulativeQuoteQty":"10.00000000",
  "status":"FILLED","timeInForce":"GTC","type":"MARKET","side":"SELL",
  "workingTime":1507725176595,"selfTradePreventionMode":"NONE",
  "fills":[
    {"price":"4000.00000000","qty":"1.00000000","commission":"4.00000000","commissionAsset":"USDT","tradeId":56},
    {"price":"3999.00000000","qty":"5.00000000","commission":"19.99500000","commissionAsset":"USDT","tradeId":57}
  ]
})";

// ─────────────────────────────────────────────────────────────────────────────
// DELETE /api/v3/order
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kCancelOrderJson = R"({
  "symbol":"LTCBTC","origClientOrderId":"myOrder1","orderId":4,"orderListId":-1,
  "clientOrderId":"cancelMyOrder1","transactTime":1684804350068,"price":"2.00000000",
  "origQty":"1.00000000","executedQty":"0.00000000","origQuoteOrderQty":"0.000000",
  "cummulativeQuoteQty":"0.00000000","status":"CANCELED","timeInForce":"GTC",
  "type":"LIMIT","side":"BUY","selfTradePreventionMode":"NONE"
})";

// ─────────────────────────────────────────────────────────────────────────────
// DELETE /api/v3/openOrders (array of cancel-order rows)
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* kCancelAllOpenOrdersJson = R"([
  {"symbol":"BTCUSDT","origClientOrderId":"E6APeyTJvkMvLMYMqu1KQ4","orderId":11,"orderListId":-1,"clientOrderId":"pXLV6Hz6mprAcVYpVMTGgx","transactTime":1684804350068,"price":"0.089853","origQty":"0.178622","executedQty":"0.000000","origQuoteOrderQty":"0.000000","cummulativeQuoteQty":"0.000000","status":"CANCELED","timeInForce":"GTC","type":"LIMIT","side":"BUY","selfTradePreventionMode":"NONE"}
])";

} // namespace exchange::binance::rest::test
