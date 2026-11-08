# Step 5: Binance REST Public (Market Data) Endpoints

> **MANDATORY — Branch and commit discipline** (inherited from plan multi-exchange-abstraction)
>
> All work continues on `feature/multi-exchange-abstraction`. Commit at the
> completion of every sub-step below. Every checkpoint must include a full
> build and full `ctest --output-on-failure` run — both green — before moving
> on.

**Goal**: Implement [Step 5](multi-exchange-abstraction.md#step-5--binance-rest-public-endpoints)
of plan multi-exchange-abstraction — the 8 Binance Spot public market-data endpoints, with mock-based
unit tests and a CLI example program. Step 5's "done when": *"Market data
endpoints are implemented and unit-tested with mock HTTP performers."*

---

## Design decisions confirmed before writing code

These were verified by reading the as-built Step-4 scaffold and resolve
drift between plan multi-exchange-abstraction's prose and the actual code:

1. **Base class**: every new request type inherits the **local**
   `exchange::binance::rest::TypedPublicRequest<R>` defined in
   `include/exchange/binance/rest_api.hpp` (not
   `exchange::rest::TypedPublicRequest<R>` from the common scaffold — plan
   multi-exchange-abstraction's text names the common one, but
   `BinanceRestClient::execute`'s SFINAE is gated on the local `PublicRequest`,
   exactly mirroring Kraken's pattern).
   Concretely:
   ```cpp
   struct BinanceFooRequest : TypedPublicRequest<BinanceFoo> {
       HttpRequest build() const override;
   };
   ```

2. **Naming**: top-level types keep the `Binance` prefix exactly as plan
   multi-exchange-abstraction's Step-5 table names them (`BinancePing`, `BinanceServerTime`, …,
   `BinanceTradesResult`). Per-row/nested helper structs (price levels,
   klines, symbol info, trade rows, ticker entries) are unprefixed plain
   names living in the same `rest_api.hpp`, matching Kraken's
   `RestBookEntry`/`OHLCCandle`/`PublicTrade` precedent. No new `types.hpp`.

3. **Single-object-vs-array ticker responses**: `BinanceTickerPrice` and
   `BinanceTicker24hr` each wrap a `std::vector<...Entry>` called `entries`.
   `from_json` checks `j.is_array()`: if true, parses each element into
   `entries`; if false, parses `j` itself as the sole element. Callers always
   see a uniform vector (size 1 for a single-`symbol` request, size N for a
   `symbols=[...]` request).

4. **`symbols=[...]` query encoding**: Binance's documented format is
   `symbols=%5B%22BTCUSDT%22%2C%22ETHBTC%22%5D` (the JSON array
   `["BTCUSDT","ETHBTC"]`, percent-encoded). A small local helper —
   `detail::symbols_query_value(const std::vector<std::string>&)` — builds
   exactly this string by hand-escaping only `[`, `]`, `"`, `,` (real Binance
   symbols are uppercase alphanumeric and need no other escaping). This is
   *not* a general-purpose URL encoder and is not shared with Kraken's
   form-body `detail::url_encode`.

5. **CMake**: two new standalone test executables —
   `test_binance_rest_requests` and `test_binance_rest_responses` — following
   the existing per-file `test_binance_auth` pattern. Plan multi-exchange-abstraction Step 9
   explicitly owns consolidating all `test_binance_*` files into one
   `binance_unit_tests` binary; doing that here would be scope creep into
   Step 9.

6. **Out of scope for this plan** (confirmed against the message-format
   appendix and Step 5's endpoint table):
   - `GET /api/v3/ticker/bookTicker` — appears in the appendix and the
     testing-strategy fixture list, but is **not** one of the 8 request types
     in Step 5's table. Left for a future small addition.
   - `BinanceExchangeInfo.symbols[]` omits `filters`, `permissions`,
     `permissionSets`, `rateLimits`, `exchangeFilters`,
     `defaultSelfTradePreventionMode`, `allowedSelfTradePreventionModes` — the
     appendix explicitly calls these "out of scope for the first cut."

---

## Sub-step 5.1 — Foundation: fixtures, Ping, ServerTime, TickerPrice

**Done when**: `BinancePingRequest`, `BinanceServerTimeRequest`,
`BinanceTickerPriceRequest` build correct `HttpRequest`s; their response
types parse the appendix fixtures (including the single-object-vs-array
ticker case and the `symbols=[...]` encoder); `parse_binance_response<T>`'s
success and error paths are covered.

### New file: `tests/unit/binance_rest_example_json.hpp`
`namespace exchange::binance::rest::test`, `inline constexpr const char*`
fixtures (verbatim from `appendix-binance-message-formats.md` unless
marked synthetic):
- `kPingJson` = `{}`
- `kServerTimeJson` = `{"serverTime": 1499827319559}`
- `kTickerPriceSingleJson` = `{"symbol":"LTCBTC","price":"4.00000200"}`
- `kTickerPriceArrayJson` *(synthetic, 2 elements)* — array form of the above
  plus a second symbol
- `kErrorJson` = `{"code":-1121,"msg":"Invalid symbol."}`

### `include/exchange/binance/rest_api.hpp` additions
```cpp
struct BinancePing {
    static BinancePing from_json(const json&) { return {}; }
};
struct BinancePingRequest : TypedPublicRequest<BinancePing> {
    HttpRequest build() const override;
};

struct BinanceServerTime {
    int64_t server_time{0};
    static BinanceServerTime from_json(const json& j);
};
struct BinanceServerTimeRequest : TypedPublicRequest<BinanceServerTime> {
    HttpRequest build() const override;
};

struct BinanceTickerPriceEntry {
    std::string symbol;
    double price{0.0};
    static BinanceTickerPriceEntry from_json(const json& j);
};
struct BinanceTickerPrice {
    std::vector<BinanceTickerPriceEntry> entries;
    static BinanceTickerPrice from_json(const json& j);
};
struct BinanceTickerPriceRequest : TypedPublicRequest<BinanceTickerPrice> {
    std::optional<std::string> symbol;
    std::optional<std::vector<std::string>> symbols;
    HttpRequest build() const override;  // prefers `symbol` if set, else `symbols`, else neither
};

namespace detail {
std::string symbols_query_value(const std::vector<std::string>& symbols);
}
```

### New file: `tests/unit/test_binance_rest_requests.cpp`
- `BinancePingRequest` → GET `/api/v3/ping`, empty query
- `BinanceServerTimeRequest` → GET `/api/v3/time`, empty query
- `BinanceTickerPriceRequest{}` → empty query
- `BinanceTickerPriceRequest{.symbol="BTCUSDT"}` → `query == "symbol=BTCUSDT"`
- `BinanceTickerPriceRequest{.symbols={"BTCUSDT","ETHBTC"}}` →
  `query == "symbols=%5B%22BTCUSDT%22%2C%22ETHBTC%22%5D"`

### New file: `tests/unit/test_binance_rest_responses.cpp`
- `BinancePing::from_json(kPingJson)` does not throw
- `BinanceServerTime::from_json(kServerTimeJson).server_time == 1499827319559`
- `BinanceTickerPrice::from_json(kTickerPriceSingleJson)` → `entries.size()==1`,
  fields match
- `BinanceTickerPrice::from_json(kTickerPriceArrayJson)` → `entries.size()==2`
- `parse_binance_response<BinancePing>(200, kPingJson_parsed)` → `ok==true`
- `parse_binance_response<BinancePing>(400, kErrorJson_parsed)` → `ok==false`,
  `errors[0]=="Invalid symbol."`

### CMake — `tests/unit/CMakeLists.txt`
```cmake
add_executable(test_binance_rest_requests test_binance_rest_requests.cpp)
target_link_libraries(test_binance_rest_requests binance GTest::gtest_main)
gtest_discover_tests(test_binance_rest_requests)

add_executable(test_binance_rest_responses test_binance_rest_responses.cpp)
target_link_libraries(test_binance_rest_responses binance GTest::gtest_main)
gtest_discover_tests(test_binance_rest_responses)
```

### Checkpoint commit
`feat: step 5.1 — Binance Ping/ServerTime/TickerPrice + fixtures + parse_binance_response tests`
Full build + `ctest --output-on-failure` green.

---

## Sub-step 5.2 — OrderBook (depth) + RecentTrades

**Done when**: `BinanceOrderBookRequest`/`BinanceRecentTradesRequest` build
correct requests (required `symbol`, optional `limit`); their response types
correctly parse positional 2-element price-level rows and the 7-field trade
object array.

### `rest_api.hpp` additions
```cpp
struct BinanceBookLevel {
    double price{0.0};
    double quantity{0.0};
    static BinanceBookLevel from_json(const json& row);  // ["price","qty"]
};
struct BinanceOrderBook {
    int64_t last_update_id{0};
    std::vector<BinanceBookLevel> bids;
    std::vector<BinanceBookLevel> asks;
    static BinanceOrderBook from_json(const json& j);
};
struct BinanceOrderBookRequest : TypedPublicRequest<BinanceOrderBook> {
    std::string symbol;
    std::optional<int> limit;
    HttpRequest build() const override;
};

struct BinanceTrade {
    int64_t id{0};
    double price{0.0};
    double qty{0.0};
    double quote_qty{0.0};
    int64_t time{0};
    bool is_buyer_maker{false};
    bool is_best_match{false};
    static BinanceTrade from_json(const json& j);
};
struct BinanceTradesResult {
    std::vector<BinanceTrade> trades;
    static BinanceTradesResult from_json(const json& j);  // top-level array
};
struct BinanceRecentTradesRequest : TypedPublicRequest<BinanceTradesResult> {
    std::string symbol;
    std::optional<int> limit;
    HttpRequest build() const override;
};
```

### Fixtures (add to `binance_rest_example_json.hpp`)
- `kDepthJson` — verbatim from appendix (`lastUpdateId`, 1 bid, 1 ask)
- `kTradesJson` — verbatim from appendix (1-element array)

### `test_binance_rest_requests.cpp` additions
- `BinanceOrderBookRequest{.symbol="BTCUSDT"}` →
  `path=="/api/v3/depth"`, `query=="symbol=BTCUSDT"`
- `BinanceOrderBookRequest{.symbol="BTCUSDT", .limit=50}` →
  `query=="symbol=BTCUSDT&limit=50"`
- `BinanceRecentTradesRequest{.symbol="BTCUSDT"}` →
  `path=="/api/v3/trades"`, `query=="symbol=BTCUSDT"`
- `BinanceRecentTradesRequest{.symbol="BTCUSDT", .limit=10}` → query includes
  `&limit=10`

### `test_binance_rest_responses.cpp` additions
- `BinanceOrderBook::from_json(kDepthJson)` → `last_update_id==1027024`,
  `bids[0]=={4.0, 431.0}`, `asks[0]=={4.000002, 12.0}`
- `BinanceTradesResult::from_json(kTradesJson)` → `trades.size()==1`, all 7
  fields match (`id==28457`, `is_buyer_maker==true`, etc.)
- Empty-array edge case: `BinanceTradesResult::from_json(json::array())` →
  `trades.empty()`

### Checkpoint commit
`feat: step 5.2 — Binance OrderBook and RecentTrades`
Full build + `ctest --output-on-failure` green.

---

## Sub-step 5.3 — Klines

**Done when**: `BinanceKlinesRequest` builds correct requests (required
`symbol`+`interval`, optional `startTime`/`endTime`/`limit`); `BinanceKline`
correctly parses the 12-element positional array, dropping the documented
"ignore" field 11.

### `rest_api.hpp` additions
```cpp
struct BinanceKline {
    int64_t open_time{0};
    double  open{0.0};
    double  high{0.0};
    double  low{0.0};
    double  close{0.0};
    double  volume{0.0};
    int64_t close_time{0};
    double  quote_asset_volume{0.0};
    int64_t num_trades{0};
    double  taker_buy_base_volume{0.0};
    double  taker_buy_quote_volume{0.0};
    static BinanceKline from_json(const json& row);  // 12-element array
};
struct BinanceKlinesResult {
    std::vector<BinanceKline> klines;
    static BinanceKlinesResult from_json(const json& j);  // top-level array
};
struct BinanceKlinesRequest : TypedPublicRequest<BinanceKlinesResult> {
    std::string symbol;
    std::string interval;
    std::optional<int64_t> start_time;
    std::optional<int64_t> end_time;
    std::optional<int> limit;
    HttpRequest build() const override;
};
```

### Fixtures
- `kKlinesJson` — verbatim from appendix (1-element array of the 12-field row)

### `test_binance_rest_requests.cpp` additions
- `BinanceKlinesRequest{.symbol="BTCUSDT", .interval="1m"}` →
  `path=="/api/v3/klines"`, `query=="symbol=BTCUSDT&interval=1m"`
- With `start_time`/`end_time`/`limit` all set → query contains
  `&startTime=...&endTime=...&limit=...` in that order
- Each optional independently appended when only one is set

### `test_binance_rest_responses.cpp` additions
- `BinanceKlinesResult::from_json(kKlinesJson)` → `klines.size()==1`; assert
  all 11 stored fields against the appendix row
  (`open_time==1499040000000`, `open==0.01634790`, …,
  `taker_buy_quote_volume==28.46694368`) — field 11 (`"0"`, "ignore") has no
  corresponding member

### Checkpoint commit
`feat: step 5.3 — Binance Klines`
Full build + `ctest --output-on-failure` green.

---

## Sub-step 5.4 — ExchangeInfo + Ticker24hr

**Done when**: `BinanceExchangeInfoRequest` builds with no params;
`BinanceExchangeInfo` parses `timezone`/`serverTime`/`symbols[]` (12 fields
per symbol per the "first cut" scope); `BinanceTicker24hrRequest` supports
`symbol`/`symbols`/neither; `BinanceTicker24hr` parses both single-object and
array responses with all 21 numeric/integer fields.

### `rest_api.hpp` additions
```cpp
struct BinanceSymbolInfo {
    std::string symbol;
    std::string status;
    std::string base_asset;
    int base_asset_precision{0};
    std::string quote_asset;
    int quote_precision{0};
    int quote_asset_precision{0};
    std::vector<std::string> order_types;
    bool iceberg_allowed{false};
    bool oco_allowed{false};
    bool is_spot_trading_allowed{false};
    bool is_margin_trading_allowed{false};
    static BinanceSymbolInfo from_json(const json& j);
};
struct BinanceExchangeInfo {
    std::string timezone;
    int64_t server_time{0};
    std::vector<BinanceSymbolInfo> symbols;
    static BinanceExchangeInfo from_json(const json& j);
};
struct BinanceExchangeInfoRequest : TypedPublicRequest<BinanceExchangeInfo> {
    HttpRequest build() const override;  // no query params
};

struct BinanceTicker24hrEntry {
    std::string symbol;
    double price_change{0.0};
    double price_change_percent{0.0};
    double weighted_avg_price{0.0};
    double prev_close_price{0.0};
    double last_price{0.0};
    double last_qty{0.0};
    double bid_price{0.0};
    double bid_qty{0.0};
    double ask_price{0.0};
    double ask_qty{0.0};
    double open_price{0.0};
    double high_price{0.0};
    double low_price{0.0};
    double volume{0.0};
    double quote_volume{0.0};
    int64_t open_time{0};
    int64_t close_time{0};
    int64_t first_id{0};
    int64_t last_id{0};
    int64_t count{0};
    static BinanceTicker24hrEntry from_json(const json& j);
};
struct BinanceTicker24hr {
    std::vector<BinanceTicker24hrEntry> entries;
    static BinanceTicker24hr from_json(const json& j);  // single object or array
};
struct BinanceTicker24hrRequest : TypedPublicRequest<BinanceTicker24hr> {
    std::optional<std::string> symbol;
    std::optional<std::vector<std::string>> symbols;
    HttpRequest build() const override;  // prefers `symbol` if set, else `symbols`, else neither
};
```

### Fixtures
- `kExchangeInfoJson` — abridged from appendix: `timezone`, `serverTime`, one
  `symbols[]` entry (ETHBTC) with the 12 in-scope fields
- `kTicker24hrSingleJson` — verbatim from appendix (BNBBTC, all 21 fields)
- `kTicker24hrArrayJson` *(synthetic, 2 elements)* — array form wrapping
  `kTicker24hrSingleJson`'s object plus a second symbol

### `test_binance_rest_requests.cpp` additions
- `BinanceExchangeInfoRequest{}` → `path=="/api/v3/exchangeInfo"`, empty query
- `BinanceTicker24hrRequest{}` → empty query
- `BinanceTicker24hrRequest{.symbol="BNBBTC"}` → `query=="symbol=BNBBTC"`
- `BinanceTicker24hrRequest{.symbols={"BTCUSDT","ETHBTC"}}` → same
  `symbols_query_value` encoding as 5.1

### `test_binance_rest_responses.cpp` additions
- `BinanceExchangeInfo::from_json(kExchangeInfoJson)` →
  `timezone=="UTC"`, `server_time==1565246363776`, `symbols.size()==1`,
  `symbols[0].symbol=="ETHBTC"`, `order_types` contains `"LIMIT"` and
  `"STOP_LOSS_LIMIT"`, all 4 bools `true`
- `BinanceTicker24hr::from_json(kTicker24hrSingleJson)` → `entries.size()==1`;
  spot-check all 21 fields on `entries[0]` (symbol, price_change,
  weighted_avg_price, …, count)
- `BinanceTicker24hr::from_json(kTicker24hrArrayJson)` → `entries.size()==2`

### Checkpoint commit
`feat: step 5.4 — Binance ExchangeInfo and Ticker24hr`
Full build + `ctest --output-on-failure` green.

---

## Sub-step 5.5 — CLI example program

**Done when**: `tests/examples/binance/binance_rest_client_example.cpp`
builds and links; running each subcommand against live `api.binance.com`
(no credentials) returns a 2xx and logs parsed fields via spdlog.

### New file: `tests/examples/binance/binance_rest_client_example.cpp`
CLI11 app, one subcommand per endpoint, mirroring
`tests/examples/rest_client_example.cpp`'s `run_*()` / spdlog pattern:

| Subcommand | Maps to | Options |
|---|---|---|
| `ping` | `BinancePingRequest` | — |
| `time` | `BinanceServerTimeRequest` | — |
| `exchangeinfo` | `BinanceExchangeInfoRequest` | — |
| `ticker` | `BinanceTickerPriceRequest` | `--symbol <S>` \| `--symbols <S,S,...>` (mutually exclusive; comma-list split into the request's `symbols` vector) |
| `book <symbol>` | `BinanceOrderBookRequest` | `--limit N` |
| `klines <symbol>` | `BinanceKlinesRequest` | `--interval <I>` (required), `--start-time`, `--end-time`, `--limit` |
| `trades <symbol>` | `BinanceRecentTradesRequest` | `--limit N` |

`main()`: `curl_global_init(CURL_GLOBAL_ALL)` → construct
`BinanceRestClient{"https://api.binance.com"}` → dispatch by subcommand →
`curl_global_cleanup()`. Each `run_*` checks `resp.ok` and logs
`spdlog::error` with `resp.errors` on failure.

### CMake — `tests/CMakeLists.txt`
```cmake
add_executable(binance_rest_client_example examples/binance/binance_rest_client_example.cpp)
target_link_libraries(binance_rest_client_example
    binance spdlog::spdlog CLI11::CLI11 example_backward
)
```

### Verification
- Build succeeds.
- `./build/bin/binance_rest_client_example ping` → no error, logs success.
- `./build/bin/binance_rest_client_example time` → logs a server time close to
  current wall-clock.
- `./build/bin/binance_rest_client_example ticker --symbol BTCUSDT` → logs a
  price.
- (Examples are not part of `ctest`; this is a manual run, same as existing
  Kraken examples.)

### Checkpoint commit
`feat: step 5.5 — Binance REST public CLI example`
Full build + `ctest --output-on-failure` green (unchanged from 5.4 — this
step adds no new unit tests, only the example binary).

---

## Self-review

### Risks / assumptions

- **`symbols_query_value` is a minimal hand-escaper**, not RFC 3986-general.
  It only escapes `[`, `]`, `"`, `,` and assumes symbol strings themselves
  (e.g. `"BTCUSDT"`) need no escaping — true for every real Binance symbol.
  If a caller ever passed a symbol containing a special character, the output
  would be malformed. Documented as a code comment at the helper's
  definition.

- **`BinanceExchangeInfo` deliberately omits `filters`/`permissions`/etc.**
  per the appendix's explicit "out of scope for first cut." If Step 6
  (account/trading) later needs `LOT_SIZE`/`PRICE_FILTER` for client-side
  order validation, that's a follow-up addition to `BinanceSymbolInfo`, not a
  rework of this plan.

- **No fixture exercises the "no `symbol`/`symbols` param → all-symbols
  array" case** (Binance returns 1000+ entries for `ticker/24hr` with no
  params). The `is_array()` branch in `from_json` is exercised by the
  2-element synthetic fixture instead — array size doesn't affect the parsing
  logic, so this is considered sufficient coverage without an unwieldy
  fixture.

- **`BookTicker` (`/api/v3/ticker/bookTicker`) is out of scope** — it's
  mentioned in the message-format appendix and the testing-strategy fixture
  list, but is not one of Step 5's 8 request types. Flagged for a future
  small addition (likely alongside Step 6 or as its own micro-step) rather
  than silently expanding this plan's scope.

- **5.5's live-API verification needs network access to `api.binance.com`.**
  If the build sandbox has no outbound network, `ctest` (mock-based, 5.1–5.4)
  is unaffected and stays green; only the manual CLI smoke-check in 5.5 would
  be skipped/deferred. This will be called out explicitly at that checkpoint
  if it occurs, per the "stop and tell Rob" rule for anything that can't be
  verified as specified.

- **Two new standalone GTest binaries** (`test_binance_rest_requests`,
  `test_binance_rest_responses`) add to the growing list of separate
  `test_binance_*` executables that Step 9 will consolidate. This is known,
  intentional, and explicitly Step 9's job — not addressed here.

### What "done" looks like across all of 5.1–5.5

- 8 new request types + their response types in `rest_api.hpp`, each
  inheriting the local `TypedPublicRequest<R>`.
- `tests/unit/binance_rest_example_json.hpp` with fixtures for all 8
  endpoints + the error envelope.
- `tests/unit/test_binance_rest_requests.cpp` and
  `tests/unit/test_binance_rest_responses.cpp`, each registered as its own
  CTest binary.
- `tests/examples/binance/binance_rest_client_example.cpp`, building and
  runnable against live Binance.
- 5 checkpoint commits on `feature/multi-exchange-abstraction`, each with a
  green full build + `ctest --output-on-failure`.
