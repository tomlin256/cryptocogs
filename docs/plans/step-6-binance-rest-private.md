# Step 6: Binance REST Private (Account + Trading) Endpoints

> **MANDATORY — Branch and commit discipline** (inherited from plan multi-exchange-abstraction)
>
> All work continues on `feature/multi-exchange-abstraction`. Commit at the
> completion of every sub-step below. Every checkpoint must include a full
> build and full `ctest --output-on-failure` run — both green — before moving
> on.

**Goal**: Implement [Step 6](multi-exchange-abstraction.md#step-6--binance-rest-private-account--trading-endpoints)
of plan multi-exchange-abstraction — the 7 Binance Spot signed (private) endpoints, with mock-based
unit tests including end-to-end signed `execute()` round-trips. Step 6's
"done when": *"Account and order endpoints implemented and unit-tested."*

Per the [testing-strategy appendix](appendix-testing-strategy.md), this
step ships **no example program** ("examples are public-only") — signed
endpoints need real API keys and `POST /api/v3/order` would place real
orders. All verification is mock-based.

---

## Design decisions confirmed before writing code

Verified against the as-built Step 4/5 code; these resolve drift between plan
multi-exchange-abstraction's prose and the actual scaffold:

1. **Base class**: every new request type inherits the **local**
   `exchange::binance::rest::TypedPrivateRequest<R>` from
   `include/exchange/binance/rest_api.hpp` (plan multi-exchange-abstraction names the common-scaffold
   one, but `BinanceRestClient::execute`'s SFINAE gates on the local
   `PrivateRequest` — same resolution as plan step-5-binance-rest-public decision 1). Note the local
   `PrivateRequest::build()` takes **no credentials** (unlike Kraken's
   `build(const Credentials&)`): `execute(req, auth)` calls `req.build()`
   then `auth.sign(http)`.

2. **POST params go in `body`; GET/DELETE params go in `query` — strictly.**
   `BinanceAuth::sign` appends `timestamp`/`recvWindow`/`signature` to the
   *body* for POST and to the *query* for everything else, and signs the raw
   concatenation `query + body` (no separator). A POST that put params in
   `query` would produce `"symbol=...side=..." + "timestamp=..."` — a corrupt
   signed payload. So `BinanceNewOrderRequest::build()` writes all params to
   `req.body` and leaves `query` empty; all GET/DELETE requests write only
   `query`. The 6.5 client tests enforce this (asserting the *other* field is
   empty).

3. **Enum strategy — "canonical enum where the wire↔enum mapping is total,
   raw `std::string` otherwise."** Binance wire strings are uppercase
   (`"BUY"`, `"STOP_LOSS_LIMIT"`, `"GTC"`), diverging from the canonical
   lowercase forms, so Binance gets its own converter pair per enum (the
   `kraken_order_type_to_string` precedent). Per field:
   - `side` → `exchange::Side`. `"BUY"`/`"SELL"` ↔ `Buy`/`Sell` is total
     both ways.
   - `timeInForce` → `exchange::TimeInForce`, **after adding `FOK` to the
     canonical enum** (decision 4). Binance's TIF set is exactly
     {GTC, IOC, FOK} — total once FOK exists. `binance_tif_to_string(GTD)`
     throws (Binance has no GTD).
   - `status` → `exchange::OrderStatus`. `binance_order_status_from_string`
     is **total via the `Unknown` fallback**: `NEW`→`New`,
     `PARTIALLY_FILLED`→`PartiallyFilled`, `FILLED`→`Filled`,
     `CANCELED`→`Canceled`, `EXPIRED`→`Expired`,
     `EXPIRED_IN_MATCH`→`Expired`, anything else (`PENDING_CANCEL`,
     `REJECTED`, future values) → `Unknown`. Never throws — a strict parser
     here would make one odd row blow up an entire `openOrders` parse.
   - `type` (order type) → **raw `std::string` in response structs**.
     Binance has `LIMIT_MAKER` (and adds types over time); canonical
     `OrderType` has no `Unknown` value to absorb them, so an enum field
     cannot be total. Callers wanting the enum use
     `binance_order_type_from_string` explicitly (supports the 6 mappable
     types, throws otherwise — including on `"LIMIT_MAKER"`).
   - **Requests stay fully typed**: `BinanceNewOrderRequest` takes
     `exchange::Side`, `exchange::OrderType`,
     `std::optional<exchange::TimeInForce>`; `build()` converts via the
     Binance converters, which throw `std::invalid_argument` for canonical
     values Binance doesn't accept (`Iceberg`, `TrailingStop`,
     `TrailingStopLimit`, `SettlePosition`). Placing `LIMIT_MAKER` orders is
     **out of scope** (no canonical value; revisit if/when canonical gains a
     post-only concept).

3a. **Drift note**: plan multi-exchange-abstraction's line *"Binance-only types like
   `STOP_LOSS_LIMIT` live in `exchange::binance::`"* is stale —
   `StopLossLimit` (and `StopLoss`/`TakeProfit`/`TakeProfitLimit`) are
   canonical values; only `LIMIT_MAKER` is genuinely unmappable. No
   Binance-local order-type enum is needed.

4. **Canonical `TimeInForce` gains `FOK`** (`exchange/common/types.hpp`).
   Fill-or-kill is an industry-standard TIF (FIX 59=4), not a Binance quirk —
   the canonical enum already takes the union-of-adapters view (it carries
   Kraken's `Iceberg`/`SettlePosition`). Ripple, handled explicitly:
   - `exchange::to_string(TimeInForce)` gains `case FOK: return "fok";`.
   - `kraken_tif_to_string` gains an explicit
     `case TimeInForce::FOK: throw std::invalid_argument(...)` (Kraken does
     not support FOK; the explicit case keeps the switch exhaustive so
     `-Wswitch` stays quiet). `kraken_tif_from_string` is unchanged — `"fok"`
     never appears on Kraken's wire and already falls through to throw.
   - The compat shim forwards and is unaffected.
   - Kraken-side coverage added in `test_order_type.cpp` (the existing
     converter-test home): canonical `to_string(FOK) == "fok"`,
     `kraken_tif_to_string(FOK)` throws.
   The alternative — a Binance-local TIF enum — was rejected: two TIF types
   for three shared values is worse than one extra canonical value.

5. **New file `include/exchange/binance/types.hpp`** (namespace
   `exchange::binance`), mirroring `exchange/kraken/types.hpp`'s role: the
   canonical-enum re-exports (`using exchange::Side;` etc.), the Binance
   converter pairs, and `BinanceOrderRespType`. The WS steps (7/8) will need
   the same converters for `order.place`, so they don't belong inside
   `rest_api.hpp`. `rest_api.hpp` includes it; names resolve unprefixed in
   `exchange::binance::rest` via enclosing-namespace lookup.

6. **Quantities and prices in requests are caller-formatted
   `std::optional<std::string>`**, not `double`. Binance enforces exact
   decimals (LOT_SIZE/PRICE_FILTER); formatting a `double` reintroduces the
   FP-noise bug `TickPrice` exists to prevent — but `TickPrice` lives in
   `exchange::kraken::` and `binance`/`cryptocogs` are peer libraries that
   must not cross-include. Moving `TickPrice` to `exchange/common/` is a
   worthwhile **future** refactor, explicitly out of scope here. Response
   monetary fields stay `double` via `std::stod` (established Step 5
   convention — they're read, not echoed back to the wire).

7. **Response struct naming and reuse** (all `Binance`-prefixed, matching
   as-built Step 5 — `BinanceBookLevel`/`BinanceTrade` precedent):
   - `GET openOrders` and `GET allOrders` share one row shape (per the
     message-format appendix) → one row struct `BinanceOrderInfo`, one
     wrapper `BinanceOpenOrdersResult`, and
     `using BinanceAllOrdersResult = BinanceOpenOrdersResult;` — plan multi-exchange-abstraction's
     two names, zero duplication.
   - `DELETE /api/v3/order`'s object and `DELETE /api/v3/openOrders`'s array
     rows are the same shape → `BinanceCancelOrderResponse` doubles as the
     row type of `BinanceCancelAllResponse`.
   - Member names mirror the wire keys mechanically (camelCase →
     snake_case), **including Binance's `"cummulativeQuoteQty"` typo** →
     `cummulative_quote_qty` (the mechanical mapping keeps `from_json`
     self-evident and grep-able; noted in a comment).

8. **Two new standalone test binaries** — `test_binance_types` (6.1) and
   `test_binance_client` (6.5) — extending the per-file pattern. Plan multi-exchange-abstraction
   Step 9 owns consolidating all `test_binance_*` into one
   `binance_unit_tests` binary; not done here (same as plan step-5-binance-rest-public decision 5).

9. **Out of scope** (documented, not silently dropped):
   - Optional request params: `omitZeroBalances` (account),
     `strategyId`/`strategyType`/`trailingDelta`/`selfTradePreventionMode`
     (new order), `cancelRestrictions` (cancel).
   - OCO order lists: `DELETE /api/v3/openOrders` can interleave OCO
     cancellation objects (`contingencyType`, `orderReports`) among plain
     rows; first cut parses every row as `BinanceCancelOrderResponse`
     (`j.value` defaults mean OCO rows part-populate rather than throw).
   - The order-list endpoints (`/api/v3/orderList`, OCO placement) — not in
     plan multi-exchange-abstraction's Step 6 table at all.

---

## Sub-step 6.1 — `exchange/binance/types.hpp` + canonical `FOK`

**Done when**: the Binance converter pairs round-trip all supported values
and throw on unsupported ones; `binance_order_status_from_string` is total;
canonical `TimeInForce::FOK` exists with `to_string` coverage and the Kraken
converter explicitly rejects it; full suite green.

### `include/exchange/common/types.hpp` change
```cpp
enum class TimeInForce { GTC, GTD, IOC, FOK };
// to_string gains: case TimeInForce::FOK: return "fok";
```

### `include/exchange/kraken/types.hpp` change
```cpp
// kraken_tif_to_string gains:
case TimeInForce::FOK:
    throw std::invalid_argument("Kraken does not support FOK time-in-force");
```

### New file: `include/exchange/binance/types.hpp`
Namespace `exchange::binance`. Standard banner. Contents:
```cpp
using exchange::Side;
using exchange::OrderType;
using exchange::TimeInForce;
using exchange::OrderStatus;

// Wire format: uppercase ("BUY"/"SELL") — canonical is lowercase.
std::string binance_side_to_string(Side v);
Side        binance_side_from_string(const std::string& s);

// Supports the 6 canonical types Binance accepts (LIMIT, MARKET, STOP_LOSS,
// STOP_LOSS_LIMIT, TAKE_PROFIT, TAKE_PROFIT_LIMIT); throws std::invalid_argument
// for Iceberg/TrailingStop/TrailingStopLimit/SettlePosition and, in
// from_string, for "LIMIT_MAKER"/unknown input. Response structs therefore
// keep `type` as a raw string (see plan step-6-binance-rest-private decision 3).
std::string binance_order_type_to_string(OrderType v);
OrderType   binance_order_type_from_string(const std::string& s);

// {GTC, IOC, FOK} — total after the canonical FOK addition; GTD throws.
std::string binance_tif_to_string(TimeInForce v);
TimeInForce binance_tif_from_string(const std::string& s);

// Total — unmapped statuses (PENDING_CANCEL, REJECTED, future values)
// fold to OrderStatus::Unknown rather than throwing.
OrderStatus binance_order_status_from_string(const std::string& s);

// newOrderRespType param values for POST /api/v3/order.
enum class BinanceOrderRespType { Ack, Result, Full };
std::string binance_order_resp_type_to_string(BinanceOrderRespType v);
```
All inline free functions, matching `kraken/types.hpp` style.
`rest_api.hpp` adds `#include "exchange/binance/types.hpp"`.

### New file: `tests/unit/test_binance_types.cpp` (+ CMake executable)
- Side: both values round-trip; `binance_side_to_string(Buy)=="BUY"` while
  canonical `exchange::to_string(Side::Buy)=="buy"` — genuinely distinct
  (the `test_order_type.cpp` distinctness pattern).
- OrderType: 6 supported values round-trip
  (`StopLossLimit ↔ "STOP_LOSS_LIMIT"` vs canonical `"stop_loss_limit"`);
  `to_string` throws for `Iceberg`, `TrailingStop`, `TrailingStopLimit`,
  `SettlePosition`; `from_string` throws for `"LIMIT_MAKER"` and garbage.
- TimeInForce: GTC/IOC/FOK round-trip uppercase; `to_string(GTD)` throws;
  `from_string("GTD")` throws.
- OrderStatus: each documented wire value maps as specified;
  `EXPIRED_IN_MATCH`→`Expired`; `PENDING_CANCEL`/`REJECTED`/garbage →
  `Unknown` (no throw).
- RespType: 3 values → `"ACK"`/`"RESULT"`/`"FULL"`.

### `tests/unit/test_order_type.cpp` additions (Kraken-side FOK ripple)
- `exchange::to_string(TimeInForce::FOK) == "fok"`.
- `kraken_tif_to_string(TimeInForce::FOK)` throws `std::invalid_argument`.
- Existing GTC/GTD/IOC assertions untouched.

### CMake — `tests/unit/CMakeLists.txt`
```cmake
add_executable(test_binance_types test_binance_types.cpp)
target_link_libraries(test_binance_types binance GTest::gtest_main)
gtest_discover_tests(test_binance_types)
```

### Checkpoint commit
`feat: step 6.1 — Binance enum converters + canonical FOK time-in-force`
Full build + `ctest --output-on-failure` green.

---

## Sub-step 6.2 — Fixtures + Account endpoint

**Done when**: `BinanceAccountRequest` builds GET `/api/v3/account` with an
empty query (auth adds the signed params later); `BinanceAccount` parses the
appendix fixture field-by-field, including the nested `commissionRates` and
the `balances` array.

### New file: `tests/unit/binance_account_example_json.hpp`
Namespace `exchange::binance::rest::test`, `inline constexpr const char*`
fixtures, banner + per-fixture one-line comment (all **synthetic** — shapes
verbatim from `appendix-binance-message-formats.md` §2; credential-gated
endpoints can't be captured live, per the testing-strategy convention):
- `kAccountJson` — the §2 account object (2 balances, commissionRates, uid)
- (6.3 and 6.4 extend this same file; listed in their sub-steps)

### `include/exchange/binance/rest_api.hpp` additions
```cpp
// ── GET /api/v3/account (signed) ─────────────────────────────────────────────

struct BinanceCommissionRates {
    double maker{0.0}, taker{0.0}, buyer{0.0}, seller{0.0};  // string→double
    static BinanceCommissionRates from_json(const json& j);
};

struct BinanceBalance {
    std::string asset;
    double free{0.0};
    double locked{0.0};
    static BinanceBalance from_json(const json& j);
};

struct BinanceAccount {
    int maker_commission{0}, taker_commission{0};
    int buyer_commission{0}, seller_commission{0};
    BinanceCommissionRates commission_rates;
    bool can_trade{false}, can_withdraw{false}, can_deposit{false};
    bool brokered{false}, require_self_trade_prevention{false}, prevent_sor{false};
    int64_t update_time{0};
    std::string account_type;
    std::vector<BinanceBalance> balances;
    std::vector<std::string> permissions;
    int64_t uid{0};
    static BinanceAccount from_json(const json& j);
};

struct BinanceAccountRequest : TypedPrivateRequest<BinanceAccount> {
    HttpRequest build() const override;  // GET /api/v3/account, empty query
};
```

### `test_binance_rest_requests.cpp` additions
- `BinanceAccountRequest{}` → method GET, `path=="/api/v3/account"`, empty
  query, empty body.

### `test_binance_rest_responses.cpp` additions
- `BinanceAccount::from_json(kAccountJson)` — assert every field:
  commissions `15/15/0/0`; `commission_rates.maker==0.0015` (etc.); all six
  bools; `update_time==123456789`; `account_type=="SPOT"`;
  `balances.size()==2` with BTC `free==4723846.89208129`,
  `locked==0.0`; `permissions=={"SPOT"}`; `uid==354937868`.

### Checkpoint commit
`feat: step 6.2 — Binance account endpoint + private fixtures`
Full build + `ctest --output-on-failure` green.

---

## Sub-step 6.3 — Order/trade queries: openOrders, allOrders, myTrades

**Done when**: the three GET requests build the exact documented query
strings (required params first, optionals in declaration order);
`BinanceOrderInfo` parses the shared row shape with the decision-3 enum/string
field policy; `BinanceMyTrade` parses all 13 fields.

### `rest_api.hpp` additions
```cpp
// Shared row of GET /api/v3/openOrders and /api/v3/allOrders.
struct BinanceOrderInfo {
    std::string symbol;
    int64_t order_id{0};
    int64_t order_list_id{-1};
    std::string client_order_id;
    double price{0.0};
    double orig_qty{0.0};
    double executed_qty{0.0};
    double cummulative_quote_qty{0.0};   // wire: "cummulativeQuoteQty" (Binance's typo)
    double orig_quote_order_qty{0.0};
    OrderStatus status{OrderStatus::Unknown};
    TimeInForce time_in_force{TimeInForce::GTC};
    std::string type;                    // raw wire string — see decision 3
    Side side{Side::Buy};
    double stop_price{0.0};
    double iceberg_qty{0.0};
    int64_t time{0};
    int64_t update_time{0};
    bool is_working{false};
    int64_t working_time{0};
    std::string self_trade_prevention_mode;
    static BinanceOrderInfo from_json(const json& j);
};

struct BinanceOpenOrdersResult {
    std::vector<BinanceOrderInfo> orders;
    static BinanceOpenOrdersResult from_json(const json& j);  // top-level array
};
using BinanceAllOrdersResult = BinanceOpenOrdersResult;       // same shape

struct BinanceOpenOrdersRequest : TypedPrivateRequest<BinanceOpenOrdersResult> {
    std::optional<std::string> symbol;          // omit → all symbols
    HttpRequest build() const override;         // GET /api/v3/openOrders
};

struct BinanceAllOrdersRequest : TypedPrivateRequest<BinanceAllOrdersResult> {
    std::string symbol;                         // required
    std::optional<int64_t> order_id;
    std::optional<int64_t> start_time;
    std::optional<int64_t> end_time;
    std::optional<int> limit;
    HttpRequest build() const override;         // GET /api/v3/allOrders
};

struct BinanceMyTrade {
    std::string symbol;
    int64_t id{0};
    int64_t order_id{0};
    int64_t order_list_id{-1};
    double price{0.0};
    double qty{0.0};
    double quote_qty{0.0};
    double commission{0.0};
    std::string commission_asset;
    int64_t time{0};
    bool is_buyer{false};
    bool is_maker{false};
    bool is_best_match{false};
    static BinanceMyTrade from_json(const json& j);
};

struct BinanceMyTradesResult {
    std::vector<BinanceMyTrade> trades;
    static BinanceMyTradesResult from_json(const json& j);    // top-level array
};

struct BinanceMyTradesRequest : TypedPrivateRequest<BinanceMyTradesResult> {
    std::string symbol;                         // required
    std::optional<int64_t> order_id;
    std::optional<int64_t> start_time;
    std::optional<int64_t> end_time;
    std::optional<int64_t> from_id;
    std::optional<int> limit;
    HttpRequest build() const override;         // GET /api/v3/myTrades
};
```

### Fixtures (extend `binance_account_example_json.hpp`)
- `kOpenOrdersJson` — §2's one-row array (LTCBTC, status NEW). Reused for
  the `BinanceAllOrdersResult` alias test — same type, same shape, one
  fixture.
- `kMyTradesJson` — §2's one-row array (BNBBTC, id 28457).

### `test_binance_rest_requests.cpp` additions
- `BinanceOpenOrdersRequest{}` → GET `/api/v3/openOrders`, empty query.
- `BinanceOpenOrdersRequest{.symbol="LTCBTC"}` → `query=="symbol=LTCBTC"`.
- `BinanceAllOrdersRequest{.symbol="LTCBTC"}` → GET `/api/v3/allOrders`,
  `query=="symbol=LTCBTC"`.
- All optionals set → `symbol=LTCBTC&orderId=1&startTime=...&endTime=...&limit=10`
  in exactly that order.
- `BinanceMyTradesRequest{.symbol="BNBBTC"}` → GET `/api/v3/myTrades`,
  `query=="symbol=BNBBTC"`; with `from_id`+`limit` →
  `symbol=BNBBTC&fromId=28000&limit=5`.

### `test_binance_rest_responses.cpp` additions
- `BinanceOpenOrdersResult::from_json(kOpenOrdersJson)` → `orders.size()==1`;
  assert every field of the row, including
  `status==OrderStatus::New`, `time_in_force==TimeInForce::GTC`,
  `type=="LIMIT"` (raw string), `side==Side::Buy`, `is_working==true`,
  `working_time==1499827319559`, `self_trade_prevention_mode=="NONE"`.
- `BinanceAllOrdersResult::from_json(kOpenOrdersJson)` parses identically
  (alias sanity check).
- Empty-array edge case → `orders.empty()`.
- `BinanceMyTradesResult::from_json(kMyTradesJson)` → `trades.size()==1`;
  all 13 fields (`commission==10.1`, `commission_asset=="BNB"`,
  `is_buyer==true`, `is_maker==false`, `is_best_match==true`, …).

### Checkpoint commit
`feat: step 6.3 — Binance openOrders/allOrders/myTrades`
Full build + `ctest --output-on-failure` green.

---

## Sub-step 6.4 — Trading: new order, cancel, cancel-all

**Done when**: `BinanceNewOrderRequest::build()` produces a correctly ordered
form body (query empty) with enum conversion via the 6.1 converters;
`BinanceNewOrderResponse` parses ACK/RESULT/FULL shapes with optionals unset
where absent and `fills` populated only for FULL; the two DELETE requests
build query-side params; cancel responses parse.

### `rest_api.hpp` additions
```cpp
// ── POST /api/v3/order (signed) ──────────────────────────────────────────────

struct BinanceFill {
    double price{0.0};
    double qty{0.0};
    double commission{0.0};
    std::string commission_asset;
    int64_t trade_id{0};
    static BinanceFill from_json(const json& j);
};

struct BinanceNewOrderResponse {
    // Present in all three shapes (ACK and up):
    std::string symbol;
    int64_t order_id{0};
    int64_t order_list_id{-1};
    std::string client_order_id;
    int64_t transact_time{0};
    // RESULT/FULL only:
    std::optional<double> price;
    std::optional<double> orig_qty;
    std::optional<double> executed_qty;
    std::optional<double> orig_quote_order_qty;
    std::optional<double> cummulative_quote_qty;
    std::optional<OrderStatus> status;
    std::optional<TimeInForce> time_in_force;
    std::optional<std::string> type;            // raw wire string
    std::optional<Side> side;
    std::optional<int64_t> working_time;
    std::optional<std::string> self_trade_prevention_mode;
    // FULL only (empty otherwise):
    std::vector<BinanceFill> fills;
    static BinanceNewOrderResponse from_json(const json& j);
};

struct BinanceNewOrderRequest : TypedPrivateRequest<BinanceNewOrderResponse> {
    std::string symbol;                          // required
    Side side{Side::Buy};                        // required
    OrderType type{OrderType::Limit};            // required
    std::optional<TimeInForce> time_in_force;
    std::optional<std::string> quantity;         // caller-formatted exact decimal
    std::optional<std::string> quote_order_qty;  //   (see decision 6)
    std::optional<std::string> price;
    std::optional<std::string> new_client_order_id;
    std::optional<std::string> stop_price;
    std::optional<std::string> iceberg_qty;
    std::optional<BinanceOrderRespType> new_order_resp_type;
    HttpRequest build() const override;          // params → req.body; query empty
};

// ── DELETE /api/v3/order and /api/v3/openOrders (signed) ────────────────────

// Shape shared by DELETE /api/v3/order and each row of DELETE /api/v3/openOrders.
struct BinanceCancelOrderResponse {
    std::string symbol;
    std::string orig_client_order_id;
    int64_t order_id{0};
    int64_t order_list_id{-1};
    std::string client_order_id;
    int64_t transact_time{0};
    double price{0.0};
    double orig_qty{0.0};
    double executed_qty{0.0};
    double orig_quote_order_qty{0.0};
    double cummulative_quote_qty{0.0};
    OrderStatus status{OrderStatus::Unknown};
    TimeInForce time_in_force{TimeInForce::GTC};
    std::string type;                            // raw wire string
    Side side{Side::Buy};
    std::string self_trade_prevention_mode;
    static BinanceCancelOrderResponse from_json(const json& j);
};

struct BinanceCancelAllResponse {
    std::vector<BinanceCancelOrderResponse> orders;
    static BinanceCancelAllResponse from_json(const json& j);  // top-level array
};

struct BinanceCancelOrderRequest : TypedPrivateRequest<BinanceCancelOrderResponse> {
    std::string symbol;                          // required
    std::optional<int64_t> order_id;             // one of order_id /
    std::optional<std::string> orig_client_order_id;  //   orig_client_order_id
    std::optional<std::string> new_client_order_id;
    HttpRequest build() const override;          // DELETE /api/v3/order, query-side
};

struct BinanceCancelAllOpenOrdersRequest
    : TypedPrivateRequest<BinanceCancelAllResponse> {
    std::string symbol;                          // required
    HttpRequest build() const override;          // DELETE /api/v3/openOrders
};
```
`BinanceNewOrderRequest::build()` body ordering: `symbol`, `side`, `type`,
then set optionals in declaration order (`timeInForce`, `quantity`,
`quoteOrderQty`, `price`, `newClientOrderId`, `stopPrice`, `icebergQty`,
`newOrderRespType`). Values are appended verbatim (valid Binance symbols /
client-order-ids contain no characters needing form-encoding — same
minimal-escaping stance as `detail::symbols_query_value`, noted in a
comment). Which params Binance *requires* per order type (e.g. LIMIT needs
`timeInForce`+`quantity`+`price`) is enforced server-side, not by `build()`
— the request struct stays a faithful wire mapping.

### Fixtures (extend `binance_account_example_json.hpp`)
- `kNewOrderAckJson`, `kNewOrderResultJson`, `kNewOrderFullJson` — §2
  verbatim (FULL has 2 fills).
- `kCancelOrderJson` — §2 verbatim (LTCBTC, status CANCELED).
- `kCancelAllOpenOrdersJson` — §2's one-row array.

### `test_binance_rest_requests.cpp` additions
- Market order, minimal: `BinanceNewOrderRequest{.symbol="BTCUSDT",
  .side=Side::Sell, .type=OrderType::Market, .quantity="10.00000000"}` →
  method POST, `path=="/api/v3/order"`, **`query` empty**,
  `body=="symbol=BTCUSDT&side=SELL&type=MARKET&quantity=10.00000000"`.
- Limit order, all options set → full body string in the documented order,
  including `timeInForce=GTC`, `newOrderRespType=FULL`.
- `type=OrderType::TrailingStop` → `build()` throws `std::invalid_argument`
  (propagated from `binance_order_type_to_string`).
- `BinanceCancelOrderRequest{.symbol="LTCBTC", .order_id=4}` → method
  DELETE, `path=="/api/v3/order"`, `query=="symbol=LTCBTC&orderId=4"`,
  **`body` empty**.
- Cancel by `orig_client_order_id` →
  `query=="symbol=LTCBTC&origClientOrderId=myOrder1"`.
- `BinanceCancelAllOpenOrdersRequest{.symbol="BTCUSDT"}` → method DELETE,
  `path=="/api/v3/openOrders"`, `query=="symbol=BTCUSDT"`.

### `test_binance_rest_responses.cpp` additions
- ACK: 5 always-fields set; **every** optional `!has_value()`;
  `fills.empty()`.
- RESULT: optionals set (`status==OrderStatus::Filled`,
  `time_in_force==TimeInForce::GTC`, `type=="MARKET"`, `side==Side::Sell`,
  `executed_qty==10.0`, …); `fills.empty()`.
- FULL: RESULT assertions plus `fills.size()==2`, both fills field-asserted
  (`fills[0].price==4000.0`, `fills[1].commission==19.995`,
  `trade_id` 56/57).
- Cancel: field-by-field (`orig_client_order_id=="myOrder1"`,
  `status==OrderStatus::Canceled`, `type=="LIMIT"`, `side==Side::Buy`).
- CancelAll: `orders.size()==1`, row spot-checked; empty-array edge case.

### Checkpoint commit
`feat: step 6.4 — Binance new-order/cancel/cancel-all`
Full build + `ctest --output-on-failure` green.

---

## Sub-step 6.5 — Signed client round-trips (`test_binance_client.cpp`)

**Done when**: end-to-end `execute(req, auth)` through
`make_binance_test_client` is covered for GET-, POST-, and DELETE-signed
requests — asserting the exact signed wire request (deterministic via
`BinanceAuth`'s injectable `ClockFn`) and the parsed `RestResponse<T>` —
plus the error and non-2xx paths. This is the Binance analog of Kraken's
`test_client.cpp`, per the testing-strategy appendix's category 4.

### New file: `tests/unit/test_binance_client.cpp` (+ CMake executable)
Shared setup: `FIXED_TS = 1499827319559LL`;
`BinanceAuth auth{BinanceCredentials{"test-key", "test-secret"}, [] { return FIXED_TS; }}`
(default `recv_window_ms=5000`). Expected signatures are **recomputed in the
test** with `detail::hmac_sha256`/`detail::to_hex` over the exact expected
payload — deterministic, no hard-coded magic hex.

Tests:
- **GET signed round-trip** (`BinanceAccountRequest`): performer asserts
  method GET, path `/api/v3/account`,
  `query == "timestamp=1499827319559&recvWindow=5000&signature=" + expected_sig`,
  body empty, header `X-MBX-APIKEY=="test-key"`; returns
  `{200, kAccountJson}` → `resp.ok`, `result->uid==354937868`.
- **POST signed round-trip** (`BinanceNewOrderRequest`, the 6.4 market
  order): performer asserts **query empty** and
  `body == "symbol=BTCUSDT&side=SELL&type=MARKET&quantity=10.00000000"
  "&timestamp=...&recvWindow=5000&signature=" + expected_sig`, plus
  `Content-Type=="application/x-www-form-urlencoded"`; returns
  `{200, kNewOrderFullJson}` → `resp.ok`, `fills.size()==2`.
- **DELETE signed round-trip** (`BinanceCancelOrderRequest`): performer
  asserts method DELETE and that the signed params landed in
  `query` (`"symbol=LTCBTC&orderId=4&timestamp=...&signature=..."`) with
  **body empty**; returns `{200, kCancelOrderJson}` →
  `result->status==OrderStatus::Canceled`.
- **API error envelope**: performer returns `{400, kErrorJson}` →
  `ok==false`, `errors[0]=="Invalid symbol."`, `!result`.
- **Non-2xx without Binance error body**: `{500, "{}"}` → `ok==false`,
  `errors[0]=="HTTP 500"`.

### CMake — `tests/unit/CMakeLists.txt`
```cmake
add_executable(test_binance_client test_binance_client.cpp)
target_link_libraries(test_binance_client binance GTest::gtest_main)
gtest_discover_tests(test_binance_client)
```

### Checkpoint commit
`feat: step 6.5 — Binance signed client round-trip tests`
Full build + `ctest --output-on-failure` green.

### Wrap-up (after 6.5 is green)
Mark plan multi-exchange-abstraction Step 6 with a "**Done**:" paragraph (commit-trail, decisions,
test count) and flip this plan to Done in `docs/plans.md` — separate
`docs: mark plan 004 done` commit, per the precedent of plan step-5-binance-rest-public.

---

## Self-review

### Risks / assumptions

- **The canonical `FOK` addition touches shared and Kraken code** — the one
  part of this plan that isn't purely additive. Mitigation: both affected
  switches gain explicit cases (no `-Wswitch` regressions), Kraken-side
  behaviour for FOK is *throw* (it has no wire format), and both sides get
  new test assertions in 6.1. Existing tests are untouched — nothing is
  weakened. If the full suite shows any unexpected Kraken fallout at the 6.1
  checkpoint, stop and re-plan per the global error rule.

- **`binance_order_status_from_string` is deliberately lossy**:
  `REJECTED`/`PENDING_CANCEL` → `Unknown`, `EXPIRED_IN_MATCH` → `Expired`.
  Spot REST rarely surfaces `REJECTED` (rejections arrive as HTTP-level
  errors), but if rejection detection ever matters, the fix is a raw-string
  field alongside the enum or a canonical `Rejected` value — flagged as a
  follow-up, not handled here.

- **Response `type` as a raw string trades type-safety for totality.** A
  `LIMIT_MAKER` open order (or any future Binance order type) parses cleanly
  instead of throwing mid-array. Callers get `binance_order_type_from_string`
  for explicit conversion. Revisit if canonical `OrderType` ever gains an
  `Unknown`/post-only value.

- **`build()` does no per-order-type required-param validation** (e.g. LIMIT
  requiring `timeInForce`+`quantity`+`price`). Binance enforces this
  server-side and returns the `{code, msg}` envelope our error path already
  surfaces. Client-side validation would duplicate a moving server-side rule
  table — out of scope.

- **Form-encoding is minimal by design**: param values are appended verbatim.
  Valid Binance symbols (`[A-Z0-9]`) and client order IDs
  (`[\.A-Z:/a-z0-9_-]{1,36}`) contain no `&`/`=`/`%` characters, so this is
  safe for all valid inputs — same stance as `detail::symbols_query_value`,
  documented at the build sites. Garbage in → malformed request out (and a
  server-side error back), not memory unsafety.

- **No live verification.** All seven endpoints require real API keys, and
  the order endpoints would mutate a real account. Verification is
  exclusively mock-based (testing-strategy appendix: examples are
  public-only; signing correctness is already pinned to Binance's published
  HMAC vector in `test_binance_auth.cpp`). A Spot-testnet smoke run is a
  possible future addition, not part of this plan.

- **Fixtures are synthetic** (documented shapes, not captured frames) — the
  same trade-off the testing-strategy appendix prescribes for anything
  credential-gated. If Binance's real responses drift from their docs, mocks
  won't catch it; the 6.5 round-trip tests at least guarantee internal
  consistency end-to-end.

- **OCO rows in `DELETE /api/v3/openOrders`** part-populate as plain
  `BinanceCancelOrderResponse` rows (`j.value` defaults) rather than parse
  their full shape. Acceptable first cut; full OCO support is its own future
  step.

- **Two more standalone test binaries** (`test_binance_types`,
  `test_binance_client`) — known, intentional, Step 9 consolidates.

### What "done" looks like across 6.1–6.5

- `include/exchange/binance/types.hpp` (converters + `BinanceOrderRespType`),
  canonical `TimeInForce::FOK`, and the Kraken-side explicit-throw case.
- 7 new request types + 6 response/row structs in `rest_api.hpp`, all on the
  local `TypedPrivateRequest<R>`.
- `tests/unit/binance_account_example_json.hpp` with 8 fixtures.
- New `test_binance_types.cpp` + `test_binance_client.cpp` binaries;
  extended `test_binance_rest_requests.cpp`, `test_binance_rest_responses.cpp`,
  and `test_order_type.cpp` — roughly 35 new tests, suite green throughout.
- 5 checkpoint commits + 1 docs wrap-up commit on
  `feature/multi-exchange-abstraction`.
