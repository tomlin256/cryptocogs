# Coinbase Exchange adapter (REST + WebSocket)

**Status:** Done (implemented on `feature/coinbase-adapter`; WS model resolved as
§2 Option A — bespoke `CoinbaseStreamClient`, `exchange_common` untouched)
**Depends on:** the multi-exchange scaffold (plan multi-exchange-abstraction, complete) and the
[add-an-exchange playbook](../agent-add-exchange.md), which this plan follows
row-for-row using **Binance as the worked reference**.
**Sibling:** [plan coinbase-fix-order-entry — Coinbase FIX order entry](coinbase-fix-order-entry.md)
(deferred; order placement in *this* plan is over REST).

---

## 1 — Goal

Add a fourth peer adapter, `exchange::coinbase::*` (`libcoinbase.a`), behind a
`CRYPTOCOGS_BUILD_COINBASE` flag (default `ON`), covering the **Coinbase Exchange
API** (`api.exchange.coinbase.com`, formerly Coinbase Pro):

- **REST** — public market data **and** private account/trading, *including order
  placement and cancellation* (`POST`/`DELETE /orders`). Coinbase has no
  WebSocket order entry, so REST is the order path for this plan.
- **WebSocket feed** (`ws-feed.exchange.coinbase.com`) — market-data channels and
  the authenticated **user channel** (order-lifecycle updates).

Out of scope (recorded, not built here):
- **FIX order entry** → [plan coinbase-fix-order-entry](coinbase-fix-order-entry.md) (deferred).
- **FIX market data** → not planned; WS covers market data.
- **Cursor pagination** of history endpoints (see Risk R4).

### Target API facts (verified against docs.cdp.coinbase.com, 2026-06)

| Aspect | Value |
|---|---|
| REST base (prod) | `https://api.exchange.coinbase.com` |
| REST base (sandbox) | `https://api-public.sandbox.exchange.coinbase.com` |
| WS feed (prod) | `wss://ws-feed.exchange.coinbase.com` |
| WS feed (sandbox) | `wss://ws-feed-public.sandbox.exchange.coinbase.com` |
| REST auth | HMAC-SHA256; headers `CB-ACCESS-KEY` / `CB-ACCESS-SIGN` / `CB-ACCESS-TIMESTAMP` / `CB-ACCESS-PASSPHRASE` |
| Signed prehash | `timestamp + method + requestPath + body` (requestPath **includes** the query string) |
| Signature | `base64( HMAC-SHA256( base64_decode(secret), prehash ) )` |
| Timestamp | Unix epoch **seconds**; must be within 30 s of server time |
| Credentials | **three** fields: `api_key`, `api_secret` (base64), `passphrase` |
| WS subscribe | `{"type":"subscribe","product_ids":[...],"channels":[...]}`; no per-request id |
| WS ack | `{"type":"subscriptions","channels":[...]}` — full-state echo, **no correlation id** |
| WS error | `{"type":"error","message":"..."}` |
| User-channel auth | subscribe carries `key`/`passphrase`/`timestamp`/`signature`; signature = `base64(HMAC-SHA256(base64_decode(secret), timestamp+"GET"+"/users/self/verify"))` |

---

## 2 — Key design decision: the WebSocket subscription model

This is the one place Coinbase **does not** fit the existing adapters, and it
must be settled before Step 6.

`exchange::ws::ExchangeWsClient::subscribe_async` (ws_client.inl:93) keys its
pending-ack handler by `std::to_string(req_id)` and only installs the push
callback when an inbound frame's `FrameDescriptor` returns
`MethodResponse{correlation_id == that key}`. Binance and Kraken both echo a
per-request id, so their acks correlate cleanly.

**Coinbase has no per-request id.** Its post-subscribe `subscriptions` frame is a
*full-state broadcast* of every active channel — it carries nothing that maps
back to a specific `req_id`, and a single coalesced `subscriptions` frame may
acknowledge several subscribes at once. There is therefore **no adapter-only way**
to make Coinbase's ack satisfy the id-correlated contract (the pending key is
fixed to `std::to_string(req_id)` inside the generic client, which an adapter
cannot influence). Two real options:

| | **Option A — bespoke `CoinbaseStreamClient`** *(recommended)* | **Option B — extend `exchange_common`** |
|---|---|---|
| Shape | A small Coinbase-owned WS client over `IWsConnection` with type/channel-keyed dispatch and **optimistic subscribe** (install callback + resolve on send; surface `error` frames via the error handler; treat `subscriptions` as a confirmation push) | Add an opt-in *ack-less / route-key-correlated* subscribe path to `ExchangeWsClient`; Coinbase then reuses it as a bare alias like Binance/Kraken |
| Reuses | `IWsConnection`, `IxWsConnection`, `MockWsConnection`, `WsResponse<T>`, `RateLimitedWsErrorHandler`, `WsReconnectSession` | the entire generic client (adapter stays thin) |
| Touches `exchange_common`? | **No** — honors the playbook's hard rule | **Yes** — additive, but changes the shared engine both shipping adapters depend on |
| Cost | ~200–300 lines of new (but simple — no id correlation) dispatch + its own `CoinbaseSubscriptionHandle` + tests | smaller adapter, but engine change needs tests proving Binance/Kraken are byte-for-byte unaffected |
| Risk to Binance/Kraken | none | non-zero (shared code) |

**Recommendation: Option A.** It respects "never modify `exchange_common`,"
keeps all blast radius inside the new adapter, and is honest about Coinbase's
genuinely different (correlation-free) WS model. If a *second* type-keyed pub/sub
exchange later appears, that is the right moment to generalize Option A's
dispatch into the common layer (YAGNI until then). **Plan steps below assume
Option A; switching to B changes only Step 6.**

> Decision owner: Rob, at plan approval. The rest of the plan is independent of
> this choice except Step 6.

---

## 3 — Files to create

```
include/exchange/coinbase/
  auth.hpp          # CoinbaseCredentials{key,secret,passphrase}, CoinbaseAuth:IRestAuth, detail:: HMAC/base64
  types.hpp         # re-export canonical enums + coinbase_* converters; parse_coinbase_response<T>(status,j)
  rest_api.hpp      # public + private request/response structs (+ .inl if any templates)
  rest_client.hpp   # CoinbaseRestClient + make_test_client
  ws_streams.hpp    # (+ .inl) Option A: CoinbaseStreamClient, channel events, subscribe scaffold, user channel
src/coinbase/
  auth.cpp  types.cpp  rest_api.cpp  rest_client.cpp  ws_streams.cpp
tests/unit/
  coinbase_rest_example_json.hpp      # public-endpoint fixtures (live-captured)
  coinbase_account_example_json.hpp   # private fixtures (synthetic — marked)
  coinbase_ws_example_json.hpp        # WS push + ack fixtures
  test_coinbase_auth.cpp  test_coinbase_types.cpp
  test_coinbase_rest_requests.cpp  test_coinbase_rest_responses.cpp
  test_coinbase_client.cpp  test_coinbase_ws_client.cpp
tests/examples/coinbase/
  coinbase_rest_client_example.cpp    # CLI11 — every public REST endpoint
  coinbase_ws_client_example.cpp      # market-data channels + connection reuse
```

Every `.hpp`/`.inl`/`.cpp` opens with the standard MIT banner (markdown exempt).
**No edits** to `exchange_common`, `exchange_http`, `kraken`, or `binance`.

---

## 4 — Steps

Each step is one checkpoint commit. **Done = `cmake --build build` clean AND
`cd build && ctest --output-on-failure` fully green** (the whole suite, not just
the new tests), then commit `feat: coinbase — <what>`. Do not start a step until
the previous one is committed green. CMake wiring grows incrementally so every
checkpoint builds and runs.

### Step 1 — CMake skeleton + Auth
- **CMake:** add `option(CRYPTOCOGS_BUILD_COINBASE "Build the Coinbase exchange adapter" ON)`; extend the "nothing will be built" warning; add the `coinbase` static lib in `src/CMakeLists.txt` (mirror the Binance block — link `PUBLIC exchange_common exchange_http OpenSSL::SSL OpenSSL::Crypto`, `target_compile_features … cxx_std_17`), starting with `coinbase/auth.cpp`; add the install component (`include/exchange/coinbase`) and the install-targets list entry; wire `test_coinbase_auth` behind the flag in `tests/unit/CMakeLists.txt`.
- **Code:** `auth.hpp` — `CoinbaseCredentials{api_key, api_secret, passphrase}` (plain struct, no file loader — mirrors `BinanceCredentials`); `detail::hmac_sha256`, `detail::base64_encode`, `detail::base64_decode`; `CoinbaseAuth : exchange::rest::IRestAuth` with an injectable `ClockFn`. `sign()` builds `requestPath = path + (query.empty()? "" : "?"+query)`, computes the prehash and signature, and sets the four `CB-ACCESS-*` headers. Bodies for `POST` are JSON; signature is over the exact body string.
- **Tests** (`test_coinbase_auth.cpp`): with a fixed clock and a known key, assert the four headers are present and `CB-ACCESS-SIGN` equals an independently-recomputed `base64(HMAC-SHA256(base64_decode(secret), prehash))` for a GET (no body) **and** a POST (JSON body + query). No official vector exists → pin the *construction* and say so in a comment (the `test_binance_client` precedent). Round-trip `base64_decode(base64_encode(x)) == x`.

### Step 2 — Types + enum converters + REST envelope
- **Code:** `types.hpp` — re-export the canonical four (`Side`, `OrderType`, `TimeInForce`, `OrderStatus`); add `coinbase_order_type_{to,from}_string` (lowercase `"limit"`/`"market"`/`"stop"`), `coinbase_side_*`, `coinbase_time_in_force_*` (`GTC`/`GTT`/`IOC`/`FOK`), `coinbase_order_status_*` (`open`/`pending`/`active`/`done`/…); all throw `std::invalid_argument` on unknown input. Add `parse_coinbase_response<T>(int status, const json& j) -> exchange::rest::RestResponse<T>`: `ok = status < 400`, else `errors = { j.value("message", …) }` (Coinbase returns the result body directly on 2xx and `{"message":…}` on error — no `{error[],result}` wrapper).
- **CMake:** append `coinbase/types.cpp`; wire `test_coinbase_types`.
- **Tests** (`test_coinbase_types.cpp`): every converter round-trips and rejects bad input; `parse_coinbase_response` maps a 2xx body to `ok+result` and a 4xx `{"message":…}` to `!ok` with the message in `errors`.

### Step 3 — REST **public** requests/responses
- **Endpoints:** `GET /time`, `/products`, `/products/{id}`, `/products/{id}/book?level=`, `/products/{id}/ticker`, `/products/{id}/trades`, `/products/{id}/candles?granularity=&start=&end=`, `/products/{id}/stats`. Each request derives the adapter's `TypedPublicRequest<R>` (over `exchange::rest::TypedPublicRequest`), defines `using response_type`, implements `build() -> HttpRequest`; each result has `static R from_json(const json&)`. Monetary/size fields arrive as **JSON strings** → `std::stod(j.value("field","0"))`; candles are positional arrays `[time,low,high,open,close,volume]`.
- **CMake:** append `coinbase/rest_api.cpp`; wire `test_coinbase_rest_requests` + `test_coinbase_rest_responses`.
- **Tests:** `test_coinbase_rest_requests.cpp` asserts method/path/query for each; `test_coinbase_rest_responses.cpp` asserts `from_json` fields against live-captured fixtures in `coinbase_rest_example_json.hpp`.

### Step 4 — REST **private** requests/responses
- **Endpoints:** `GET /accounts`, `/accounts/{id}`; `POST /orders` (limit + market; JSON body, `time_in_force`, `client_oid`); `DELETE /orders/{id}`; `DELETE /orders`; `GET /orders?status=&product_id=`; `GET /orders/{id}`; `GET /fills?order_id=&product_id=`. Private requests derive `TypedPrivateRequest<R>` and implement `build(const CoinbaseCredentials&)` *or* `build()` + sign via `CoinbaseAuth` in the client (match whichever the client expects — see Step 5). Use `TickPrice` for order prices that must round-trip exactly.
- **CMake:** no new files (same `rest_api.cpp`); extend the two REST test targets with private cases.
- **Tests:** extend `test_coinbase_rest_requests.cpp` (place-order JSON body shape, cancel path, list-orders query) and `test_coinbase_rest_responses.cpp` (account, order, fill, cancel-id) against **synthetic** fixtures in `coinbase_account_example_json.hpp` (marked `synthetic`, since no live creds — Risk R3).

### Step 5 — REST client
- **Code:** `rest_client.hpp` + `rest_client.cpp` — `CoinbaseRestClient` (default base `https://api.exchange.coinbase.com`) templated like `BinanceRestClient`: `execute(req)` (public) and `execute(req, const CoinbaseCredentials&)` (private, applies `CoinbaseAuth`), both returning `exchange::rest::RestResponse<Req::response_type>` via `parse_coinbase_response`. Transport is the shared `exchange::rest::CurlHttpClient`. `make_test_client(fn)` injects a mock performer (friends the private ctor).
- **CMake:** append `coinbase/rest_client.cpp`; wire `test_coinbase_client`.
- **Tests** (`test_coinbase_client.cpp`): mock performer asserts the outgoing path/method/body **and** the four `CB-ACCESS-*` headers on a private call, feeds canned JSON, and checks the typed result; a 4xx `{"message":…}` body maps to `!ok` with the message surfaced.

### Step 6 — WebSocket layer (Option A)
- **Code:** `ws_streams.hpp` (+ `.inl` if needed) + `ws_streams.cpp` — `STREAM_URL`; push-event structs with `from_json` for `ticker`, `level2` (`snapshot` + `l2update`), `matches` (`match`), `heartbeat`, `status`, and the `full`/user lifecycle types (`received`/`open`/`done`/`change`/`activate`); a `CoinbaseStreamClient` (per §2 Option A) over `IWsConnection` with: pre-open send queue flushed on `on_open`; `subscribe(channel, product_ids, callback) -> CoinbaseSubscriptionHandle` (optimistic — install callback keyed by channel/route, send subscribe, surface `error` frames via the `IWsErrorHandler`); type/channel-keyed inbound dispatch; idempotent `cancel()` sending the `unsubscribe` frame. **User channel:** `subscribe_user(creds, product_ids, callback)` emits the signed subscribe frame (`key`/`passphrase`/`timestamp`/`signature` over `timestamp+"GET"+"/users/self/verify"`), reusing the `auth.hpp` `detail::` signer. `make_coinbase_stream_client(conn, error_handler=nullptr)` factory + the `IxWsConnection` URL path.
- **CMake:** append `coinbase/ws_streams.cpp`; wire `test_coinbase_ws_client`; the WS test/example link only `coinbase` (+ `ixwebsocket spdlog CLI11 example_backward` for the example), never `kraken`/`binance`.
- **Tests** (`test_coinbase_ws_client.cpp`, driving shared `MockWsConnection`): `fire_open()` flushes the queued subscribe; outbound subscribe frame shape (incl. signed user-channel frame); injected `ticker`/`l2update`/`match`/lifecycle frames reach the right typed callback; `subscriptions` confirmation observed; injected `error` frame routed to the error handler; `cancel()` removes the callback + sends `unsubscribe` and is idempotent. No sockets, no sleeps.

### Step 7 — Build-matrix validation (the decoupling proof)
- `cmake -B build && cmake --build build` (all flags `ON`) clean; `ctest --output-on-failure` fully green.
- `cmake -B build-off -DCRYPTOCOGS_BUILD_COINBASE=OFF && cmake --build build-off` — **no `coinbase` target, header, test, or example** exists (matches plan step-9-cmake-build-validation's matrix cell for Binance).
- `cmake -B build-cb -DCRYPTOCOGS_BUILD_KRAKEN=OFF -DCRYPTOCOGS_BUILD_BINANCE=OFF` — a **Coinbase-only** tree builds and its tests pass.
- Confirm `cmake --install` ships `include/exchange/coinbase` only when the flag is `ON`. Commit (validation + any install-gating fix only).

### Step 8 — CLI examples (live public verification)
- `coinbase_rest_client_example.cpp` (CLI11, mirrors `binance_rest_client_example`): a subcommand per public endpoint; run live against prod (no creds) and confirm parsed output.
- `coinbase_ws_client_example.cpp`: subscribe to `ticker`/`level2`/`matches` for a product, print pushes, demonstrate connection reuse + `cancel()`; run live.
- Both build only when the flag is `ON` and vanish when `OFF`. Commit.

### Step 9 — Docs
- Update top-level `CLAUDE.md`: add a **Coinbase adapter reference** section (auth's three-credential shape; REST endpoint tables; the §2 WS model and why it differs), plus the build-outputs table, the test-binary table (+ new test count), and the namespace-layout table. Update the project-structure tree. Mark this plan **Done** in `docs/plans.md`. Commit.

---

## 5 — Test inventory (per global policy — tests at every step)

| Test file | Verifies | Step |
|---|---|---|
| `test_coinbase_auth.cpp` | `CB-ACCESS-*` headers + signature construction (GET + POST) | 1 |
| `test_coinbase_types.cpp` | enum converters round-trip/reject; `parse_coinbase_response` status mapping | 2 |
| `test_coinbase_rest_requests.cpp` | each request builds correct method/path/query/body (public + private) | 3,4 |
| `test_coinbase_rest_responses.cpp` | `from_json` field assertions vs. fixtures (public live, private synthetic) | 3,4 |
| `test_coinbase_client.cpp` | signed `execute()` round-trip via mock performer; status→error mapping | 5 |
| `test_coinbase_ws_client.cpp` | optimistic subscribe, type-keyed dispatch, user-channel signing, `error` routing, `cancel()` idempotency | 6 |

All tests are network-free (mock HTTP performer / `MockWsConnection`) and
deterministic (no `sleep_for`, no wall-clock polling).

---

## 6 — Self-review: risks & assumptions

**Risks**
- **R1 — WS ack model (the big one).** Coinbase's id-less, full-state
  `subscriptions` broadcast cannot satisfy the generic id-correlated
  `subscribe_async`. Resolved by §2 Option A. *Residual:* optimistic subscribe
  could mask a server-side subscribe rejection → mitigated by routing `error`
  frames to the `IWsErrorHandler` and (optionally) reconciling against the
  `subscriptions` echo before marking a handle active.
- **R2 — Three-credential auth.** `CoinbaseCredentials` carries a `passphrase`
  sent as `CB-ACCESS-PASSPHRASE` — a shape neither existing adapter has. Keep it
  a distinct struct; do **not** shoehorn into `BinanceCredentials`.
- **R3 — No live private creds** (verification = "synthetic fixtures only").
  Private REST, the user channel, and order placement are tested against
  **synthetic** fixtures marked as such; signing tests pin *construction*, not an
  official vector. Live verification covers only public endpoints (Step 8).
- **R4 — Cursor pagination unsupported in v1.** `exchange::rest::HttpResponse`
  exposes `{status, body}` but **no headers**, and Coinbase paginates history via
  `CB-AFTER`/`CB-BEFORE` response headers. `GET /orders`/`/fills` therefore return
  the **first page only** in v1. Full pagination would require extending
  `exchange_http` to surface headers — **explicitly out of scope** here (would
  touch a shared lib) and noted as a follow-up.
- **R5 — Sandbox flakiness.** Coinbase's sandbox is intermittently unavailable;
  live checks (Step 8) target **public prod** endpoints, which need no creds.
- **R6 — FIX excluded.** Low-latency order entry is deliberately deferred to
  [plan coinbase-fix-order-entry](coinbase-fix-order-entry.md); REST order placement is the v1
  path. No FIX scaffolding is introduced here.

**Assumptions**
- Target is the **Coinbase Exchange** API (`api.exchange.coinbase.com`), *not*
  Advanced Trade (`api.coinbase.com/api/v3/brokerage`) — per the docs URL given.
- Exchange auth remains **HMAC + passphrase**, not CDP JWT/Ed25519 (confirmed for
  this product as of 2026-06).
- REST order placement is sufficient for v1; FIX is a separate, deferred effort.
- The adapter is **purely additive** — `exchange_common`, `exchange_http`, and the
  Kraken/Binance adapters are untouched (Option A guarantees this).

**Done criteria** (mirror the playbook §5): all-flags-ON build + full green
ctest; `-DCRYPTOCOGS_BUILD_COINBASE=OFF` leaves no trace; a Coinbase-only build
passes; both CLI examples print live public output.
