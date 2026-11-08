# Appendix to multi-exchange-abstraction — Binance Message Formats

Companion to [001-multi-exchange-abstraction.md](001-multi-exchange-abstraction.md).

Every message below is captured verbatim from the Binance Spot API docs (fetched 2026-06-06). These are the source frames for the test fixture headers — the Binance analog of `tests/unit/ws_client_example_json.hpp`. Each `from_json` implementation must parse the corresponding frame here, and `test_binance_*_responses.cpp` asserts the parsed fields against these exact payloads.

## Cross-cutting conventions

These hold across **all** Binance messages and differ from Kraken in specific ways — get them right once in the shared helpers:

| Aspect | Binance | Kraken (for contrast) |
|---|---|---|
| Monetary / qty values | JSON **strings** `"4.00000200"` | JSON strings (same) |
| Timestamps | integer **milliseconds** `1499865549590` | ISO-8601 strings |
| List-of-symbols query | response becomes a JSON **array** of the object | n/a |
| Order book / kline rows | positional **arrays**, not keyed objects | keyed objects |
| WS push keys | terse **single letters** (`e`,`E`,`s`,`p`,`q`) | verbose (`symbol`,`price`) |
| Success signal | HTTP 2xx + absence of `code` | empty `error` array |
| Error shape | `{"code":-1121,"msg":"…"}` | `{"error":["E…"],"result":{}}` |
| WS reply id | integer **or string** | integer `req_id` |

Parse helpers needed: `bn_str_to_double(j, key)` (string → double), times stay `int64_t` (`j.value(key, 0LL)`).

---

## 1. REST — public market data

### GET /api/v3/ping
```json
{}
```
Empty object. `BinancePing` is a trivial struct (presence = success).

### GET /api/v3/time
```json
{"serverTime": 1499827319559}
```
`server_time` → `int64_t`.

### GET /api/v3/exchangeInfo (abridged — symbols array is the part we parse)
```json
{
  "timezone": "UTC",
  "serverTime": 1565246363776,
  "rateLimits": [ { } ],
  "exchangeFilters": [],
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
      "isMarginTradingAllowed": true,
      "filters": [],
      "permissions": [],
      "permissionSets": [["SPOT","MARGIN"]],
      "defaultSelfTradePreventionMode": "NONE",
      "allowedSelfTradePreventionModes": ["NONE"]
    }
  ]
}
```
Parse `timezone`, `serverTime`, and a `vector<BinanceSymbolInfo>` from `symbols` (symbol, status, baseAsset, quoteAsset, precisions, orderTypes). The `filters` array (PRICE_FILTER, LOT_SIZE) can be parsed in a later pass — out of scope for the first cut; keep the raw `json` if needed.

### GET /api/v3/depth
```json
{
  "lastUpdateId": 1027024,
  "bids": [["4.00000000","431.00000000"]],
  "asks": [["4.00000200","12.00000000"]]
}
```
`last_update_id` → `int64_t`; `bids`/`asks` → `vector<PriceLevel{double price, qty}>` parsed positionally (`row[0]`, `row[1]`).

### GET /api/v3/trades
```json
[
  {"id":28457,"price":"4.00000100","qty":"12.00000000","quoteQty":"48.000012","time":1499865549590,"isBuyerMaker":true,"isBestMatch":true}
]
```
Top-level **array**. Per row: `id`→int64, `price`/`qty`/`quoteQty`→double, `time`→int64, two bools.

### GET /api/v3/klines
```json
[
  [1499040000000,"0.01634790","0.80000000","0.01575800","0.01577100","148976.11427815",1499644799999,"2434.19055334",308,"1756.87402397","28.46694368","0"]
]
```
Array of **12-element positional arrays**: `[0]`openTime(int64), `[1]`open, `[2]`high, `[3]`low, `[4]`close, `[5]`volume (all string→double), `[6]`closeTime(int64), `[7]`quoteAssetVolume(double), `[8]`numTrades(int), `[9]`takerBuyBaseVol(double), `[10]`takerBuyQuoteVol(double), `[11]`ignore.

### GET /api/v3/ticker/24hr (FULL)
```json
{
  "symbol":"BNBBTC","priceChange":"-94.99999800","priceChangePercent":"-95.960",
  "weightedAvgPrice":"0.29628482","prevClosePrice":"0.10002000","lastPrice":"4.00000200",
  "lastQty":"200.00000000","bidPrice":"4.00000000","bidQty":"100.00000000",
  "askPrice":"4.00000200","askQty":"100.00000000","openPrice":"99.00000000",
  "highPrice":"100.00000000","lowPrice":"0.10000000","volume":"8913.30000000",
  "quoteVolume":"15.30000000","openTime":1499783499040,"closeTime":1499869899040,
  "firstId":28385,"lastId":28460,"count":76
}
```
All price/qty fields string→double; `openTime`/`closeTime`→int64; `firstId`/`lastId`/`count`→int64. With a `symbols=[...]` query the response is an **array** of these.

### GET /api/v3/ticker/price
```json
{"symbol":"LTCBTC","price":"4.00000200"}
```

### GET /api/v3/ticker/bookTicker
```json
{"symbol":"LTCBTC","bidPrice":"4.00000000","bidQty":"431.00000000","askPrice":"4.00000200","askQty":"9.00000000"}
```

---

## 2. REST — account & trading (signed)

### GET /api/v3/account
```json
{
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
}
```
`balances` → `vector<Balance{asset, double free, locked}>`; `commissionRates` nested object (string→double); `updateTime`/`uid`→int64; commission ints; bools.

### POST /api/v3/order — ACK (newOrderRespType=ACK)
```json
{"symbol":"BTCUSDT","orderId":28,"orderListId":-1,"clientOrderId":"6gCrw2kRUAF9CvJDGP16IP","transactTime":1507725176595}
```

### POST /api/v3/order — RESULT
```json
{
  "symbol":"BTCUSDT","orderId":28,"orderListId":-1,"clientOrderId":"6gCrw2kRUAF9CvJDGP16IP",
  "transactTime":1507725176595,"price":"0.00000000","origQty":"10.00000000",
  "executedQty":"10.00000000","origQuoteOrderQty":"0.000000","cummulativeQuoteQty":"10.00000000",
  "status":"FILLED","timeInForce":"GTC","type":"MARKET","side":"SELL",
  "workingTime":1507725176595,"selfTradePreventionMode":"NONE"
}
```

### POST /api/v3/order — FULL (adds `fills`)
```json
{
  "symbol":"BTCUSDT","orderId":28,"orderListId":-1,"clientOrderId":"6gCrw2kRUAF9CvJDGP16IP",
  "transactTime":1507725176595,"price":"0.00000000","origQty":"10.00000000",
  "executedQty":"10.00000000","origQuoteOrderQty":"0.000000","cummulativeQuoteQty":"10.00000000",
  "status":"FILLED","timeInForce":"GTC","type":"MARKET","side":"SELL",
  "workingTime":1507725176595,"selfTradePreventionMode":"NONE",
  "fills":[
    {"price":"4000.00000000","qty":"1.00000000","commission":"4.00000000","commissionAsset":"USDT","tradeId":56},
    {"price":"3999.00000000","qty":"5.00000000","commission":"19.99500000","commissionAsset":"USDT","tradeId":57}
  ]
}
```
`BinanceNewOrderResponse`: ids/timestamps int64, price/qty fields string→double, `status`/`type`/`side`/`timeInForce` strings (map to `exchange::` enums where shared), `fills` → `vector<Fill>` (empty unless FULL).

### DELETE /api/v3/order
```json
{
  "symbol":"LTCBTC","origClientOrderId":"myOrder1","orderId":4,"orderListId":-1,
  "clientOrderId":"cancelMyOrder1","transactTime":1684804350068,"price":"2.00000000",
  "origQty":"1.00000000","executedQty":"0.00000000","origQuoteOrderQty":"0.000000",
  "cummulativeQuoteQty":"0.00000000","status":"CANCELED","timeInForce":"GTC",
  "type":"LIMIT","side":"BUY","selfTradePreventionMode":"NONE"
}
```

### DELETE /api/v3/openOrders (array)
```json
[
  {"symbol":"BTCUSDT","origClientOrderId":"E6APeyTJvkMvLMYMqu1KQ4","orderId":11,"orderListId":-1,"clientOrderId":"pXLV6Hz6mprAcVYpVMTGgx","transactTime":1684804350068,"price":"0.089853","origQty":"0.178622","executedQty":"0.000000","origQuoteOrderQty":"0.000000","cummulativeQuoteQty":"0.000000","status":"CANCELED","timeInForce":"GTC","type":"LIMIT","side":"BUY","selfTradePreventionMode":"NONE"}
]
```

### GET /api/v3/openOrders and /api/v3/allOrders (array; same row shape)
```json
[
  {"symbol":"LTCBTC","orderId":1,"orderListId":-1,"clientOrderId":"myOrder1","price":"0.1","origQty":"1.0","executedQty":"0.0","cummulativeQuoteQty":"0.0","status":"NEW","timeInForce":"GTC","type":"LIMIT","side":"BUY","stopPrice":"0.0","icebergQty":"0.0","time":1499827319559,"updateTime":1499827319559,"isWorking":true,"origQuoteOrderQty":"0.000000","workingTime":1499827319559,"selfTradePreventionMode":"NONE"}
]
```

### GET /api/v3/myTrades (array)
```json
[
  {"symbol":"BNBBTC","id":28457,"orderId":100234,"orderListId":-1,"price":"4.00000100","qty":"12.00000000","quoteQty":"48.000012","commission":"10.10000000","commissionAsset":"BNB","time":1499865549590,"isBuyer":true,"isMaker":false,"isBestMatch":true}
]
```

### Error response (any signed/public endpoint, with non-2xx status)
```json
{"code":-1121,"msg":"Invalid symbol."}
```
`parse_binance_response<T>` reads `code`/`msg` into `RestResponse::errors` and sets `ok=false`.

---

## 3. WebSocket — market streams (combined endpoint)

Endpoint: `wss://stream.binance.com/stream` (combined). Every push frame is wrapped; **`route_key` = the `stream` value**.

### Subscribe / unsubscribe
```json
{"method":"SUBSCRIBE","params":["btcusdt@aggTrade","btcusdt@depth"],"id":1}
```
ack:
```json
{"result":null,"id":1}
```
Unsubscribe is identical with `"method":"UNSUBSCRIBE"`. The ack is a `MethodResponse`, `correlation_id = "1"`. **No stream name is echoed** — the `SubscriptionHandle` keeps its own `route_key`.

### Combined-stream wrapper (all push frames)
```json
{"stream":"bnbbtc@aggTrade","data":{ …event payload… }}
```

### aggTrade — `<symbol>@aggTrade`
```json
{"e":"aggTrade","E":1672515782136,"s":"BNBBTC","a":12345,"p":"0.001","q":"100","f":100,"l":105,"T":1672515782136,"m":true,"M":true}
```
`e`event, `E`/`T` int64 ms, `s` symbol, `a`/`f`/`l` int64 ids, `p`/`q` string→double, `m`/`M` bool.

### trade — `<symbol>@trade`
```json
{"e":"trade","E":1672515782136,"s":"BNBBTC","t":12345,"p":"0.001","q":"100","T":1672515782136,"m":true,"M":true}
```

### kline — `<symbol>@kline_<interval>`
```json
{"e":"kline","E":1672515782136,"s":"BNBBTC","k":{"t":1672515780000,"T":1672515839999,"s":"BNBBTC","i":"1m","f":100,"L":200,"o":"0.0010","c":"0.0020","h":"0.0025","l":"0.0015","v":"1000","n":100,"x":false,"q":"1.0000","V":"500","Q":"0.500","B":"123456"}}
```
Candle nested under `k`: `t`/`T` int64, `i` interval string, `o/c/h/l/v/q/V/Q` string→double, `n` int, `x` bool (candle closed).

### 24hrTicker — `<symbol>@ticker`
```json
{"e":"24hrTicker","E":1672515782136,"s":"BNBBTC","p":"0.0015","P":"250.00","w":"0.0018","x":"0.0009","c":"0.0025","Q":"10","b":"0.0024","B":"10","a":"0.0026","A":"100","o":"0.0010","h":"0.0025","l":"0.0010","v":"10000","q":"18","O":0,"C":86400000,"F":0,"L":18150,"n":18151}
```

### 24hrMiniTicker — `<symbol>@miniTicker`
```json
{"e":"24hrMiniTicker","E":1672515782136,"s":"BNBBTC","c":"0.0025","o":"0.0010","h":"0.0025","l":"0.0010","v":"10000","q":"18"}
```

### bookTicker — `<symbol>@bookTicker` (no `e` field)
```json
{"u":400900217,"s":"BNBUSDT","b":"25.35190000","B":"31.21000000","a":"25.36520000","A":"40.66000000"}
```
Note: **no `e` event-type field** — `identify_message` must route this by the `route_key` (stream name from the wrapper) since the bare payload is ambiguous.

### Partial book depth — `<symbol>@depth<levels>`
```json
{"lastUpdateId":160,"bids":[["0.0024","10"]],"asks":[["0.0026","100"]]}
```

### Diff depth — `<symbol>@depth`
```json
{"e":"depthUpdate","E":1672515782136,"s":"BNBBTC","U":157,"u":160,"b":[["0.0024","10"]],"a":[["0.0026","100"]]}
```
`U`/`u` int64 update ids; `b`/`a` positional price-level arrays.

---

## 4. WebSocket API (bidirectional trading)

Endpoint: `wss://ws-api.binance.com/ws-api/v3`. Every frame is request/response correlated by `id` — there is no push/channel concept here.

### Request (signed `order.place`)
```json
{"id":"e2a85d9f-07a5-4f94-8d5f-789dc3deb097","method":"order.place","params":{"symbol":"BTCUSDT","side":"BUY","type":"LIMIT","price":"0.1","quantity":"10","timeInForce":"GTC","timestamp":1655716096498,"apiKey":"…","signature":"…"}}
```
`id` may be string or int. `signature` = HMAC-SHA256 hex over the sorted `params` (excluding `signature`).

### Success reply
```json
{"id":"e2a85d9f-07a5-4f94-8d5f-789dc3deb097","status":200,"result":{"symbol":"BTCUSDT","orderId":12510053279,"orderListId":-1,"clientOrderId":"a097fe6304b20a7e4fc436","transactTime":1655716096505,"price":"0.10000000","origQty":"10.00000000","executedQty":"0.00000000","origQuoteOrderQty":"0.000000","cummulativeQuoteQty":"0.00000000","status":"NEW","timeInForce":"GTC","type":"LIMIT","side":"BUY","workingTime":1655716096505,"selfTradePreventionMode":"NONE"},"rateLimits":[{"rateLimitType":"ORDERS","interval":"SECOND","intervalNum":10,"limit":50,"count":12}]}
```
`status`→int; `ok = status < 400`; `result` parses like the REST RESULT order shape.

### Error reply
```json
{"id":"e2a85d9f-07a5-4f94-8d5f-789dc3deb097","status":400,"error":{"code":-2010,"msg":"Account has insufficient balance for requested action."},"rateLimits":[{"rateLimitType":"ORDERS","interval":"SECOND","intervalNum":10,"limit":50,"count":13}]}
```
`ok=false`; `WsResponse::error` = `error.msg`.

---

## 5. Dispatch routing-key table (drives `BinanceStreamIdentifier` / `BinanceWsIdentifier`)

| Endpoint | Inbound frame | `FrameKind` | `correlation_id` | `route_key` |
|---|---|---|---|---|
| Streams | `{"result":null,"id":N}` | MethodResponse | `str(id)` | — |
| Streams | `{"stream":"X@s","data":{…}}` | PushMessage | — | `"X@s"` (the `stream`) |
| WS API | `{"id":…,"status":…,…}` | MethodResponse | `str(id)` | — |

Kraken, for contrast: replies route by `req_id` (`method` present); push frames route by `channel`. The generic `FrameDescriptor` covers both because `correlation_id` is a stringified id and `route_key` is whatever the subscription registered under.
