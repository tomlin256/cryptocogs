# Crypto.com Exchange adapter (REST + WebSocket)

**Status:** Done (implemented on `feature/cryptocom-adapter`; WS resolved as §2
Option A — generic `ExchangeWsClient` + a `HeartbeatResponder` decorator).

> **Deviation from the "`exchange_common` untouched" guarantee (Rob-approved during
> Step 8):** live verification surfaced that ixwebsocket emits `Host: <host>:443`,
> which Crypto.com's WS gateway rejects with HTTP 400 (Binance/Coinbase tolerate
> it). ixwebsocket honors a caller-supplied `Host` header, so `IxWsConnection`
> gained an **additive** optional handshake-header override (default empty — the
> other adapters are byte-for-byte unaffected; all 472 tests still pass). The
> adapter sets `{{"Host", WS_HOST}}` when building its `IxWsConnection`. This is
> the only common-layer change; the WS dispatch engine itself is untouched.

**Depends on:** the multi-exchange scaffold (plan multi-exchange-abstraction, complete) and the
[add-an-exchange playbook](../agent-add-exchange.md), which this plan follows
row-for-row using **Binance and Coinbase as the worked references**.
**Source docs:** <https://exchange-docs.crypto.com/exchange/v1/rest-ws/index.html>
(Crypto.com Exchange **v1** unified REST + WebSocket API).

---

## 1 — Goal

Add a **fifth** peer adapter, `exchange::cryptocom::*` (`libcryptocom.a`), behind a
`CRYPTOCOGS_BUILD_CRYPTOCOM` flag (default `ON`), covering the **Crypto.com
Exchange v1 API**:

- **REST** — public market data **and** private account/trading, *including order
  placement and cancellation* (`private/create-order` / `private/cancel-order`).
- **WebSocket** — the **market** feed (`…/v1/market`, no auth) and the
  authenticated **user** feed (`…/v1/user`, order/trade/balance lifecycle), plus
  the protocol's **mandatory heartbeat** keepalive.

Out of scope (recorded, not built here — see Risk R7):
- Advanced order types — `private/advanced/*` (OCO / OTO / OTOCO, trigger/attach).
- Trading bots (`private/bot/*`), staking (`*/staking/*`), fiat (`private/fiat/*`),
  margin transfers/leverage, sub-account transfers, deposits/withdrawals.
- UAT/sandbox targeting (live checks use **public prod**, per Coinbase precedent).

### Target API facts (from the v1 docs, 2026-06)

| Aspect | Value |
|---|---|
| REST base (prod) | `https://api.crypto.com` — methods under `/exchange/v1/{method}` |
| WS market (prod) | `wss://stream.crypto.com/exchange/v1/market` |
| WS user (prod) | `wss://stream.crypto.com/exchange/v1/user` |
| **Unified envelope** | request `{id, method, api_key, params, nonce, sig}`; response `{id, method, code, result}`; **`code == 0` = success** (non-zero = error code) — the *same* shape for REST and WS |
| REST verbs | **public = GET** (params in query); **private = POST** (signed JSON envelope as the body) |
| Auth signature | `sig = hex( HMAC-SHA256( api_secret, digest ) )`, `digest = method + id + api_key + params_string + nonce` |
| **Secret usage** | `api_secret` is the HMAC key **as plaintext** — **not** base64-decoded (differs from Kraken **and** Coinbase) |
| `params_string` | keys sorted **ascending**, concatenated as `key+value`; lists iterate + recurse; `null`→ literal `"null"`; **`MAX_LEVEL = 3`** (deeper → `str(obj)`) |
| Credentials | **two** fields: `api_key`, `api_secret` (no passphrase — differs from Coinbase) |
| nonce | milliseconds since the Unix epoch (long int) |
| WS subscribe | `{"id":N,"method":"subscribe","params":{"channels":[…]}}` — **carries a per-request `id`** |
| WS ack | `{"id":N,"method":"subscribe","code":0,"result":{…}}` — **echoes `id`** → id-correlated ✓ |
| WS push | `{"method":"push","code":0,"result":{"subscription":"ticker.BTC_USD","channel":"ticker","data":[…]}}` — route by `result.subscription` |
| **WS heartbeat** | server→client `{"id":-1,"method":"public/heartbeat"}`; client **must** reply `{"id":-1,"method":"public/respond-heartbeat"}` (echo the id) or be disconnected |
| WS user auth | `{"id":N,"method":"public/auth","api_key":…,"sig":…,"nonce":…}`; signed over `method+id+api_key+nonce` (no `params`) before any `user.*` subscribe |

**Channel name formats** (market): `ticker.{instrument}`, `book.{instrument}.{depth}`,
`trade.{instrument}`, `candlestick.{period}.{instrument}`. **User:**
`user.order.{instrument}`, `user.trade.{instrument}`, `user.balance`. The exact
period token (e.g. `1m`/`M5`) and the precise string echoed back in
`result.subscription` are pinned from live captures in Step 6 (Risk R6).

---

## 2 — Key design decision: the WebSocket model (heartbeat + reuse vs. bespoke)

This is the one place Crypto.com needs a call before Step 6 — the analogue of
Coinbase plan coinbase-exchange-adapter §2. **Unlike Coinbase, Crypto.com's subscribe carries a
per-request `id` that the ack echoes**, so it *fits* the generic id-correlated
`exchange::ws::ExchangeWsClient` natively (subscribe ack → `MethodResponse` by
`id`; `method:"push"` → `PushMessage` by `result.subscription`; `public/auth` →
`MethodResponse` via `execute()`). The **only** misfit is the **mandatory
heartbeat**: the server sends `public/heartbeat` and the client must
auto-reply `public/respond-heartbeat`, and the generic client has no concept of
"reply to an inbound frame."

| | **Option A — reuse `ExchangeWsClient` + a `HeartbeatResponder` decorator** *(recommended)* | **Option B — bespoke `CryptoComStreamClient`** (Coinbase-style) |
|---|---|---|
| Shape | Keep the generic client; add a tiny adapter-local `HeartbeatResponder : IWsConnection` that wraps the real connection, auto-replies to `public/heartbeat`, and forwards every other frame untouched. Bind `cryptocom_frame_descriptor` via the normal factory. | A Crypto.com-owned WS client over `IWsConnection` that re-implements id-correlation, subscribe/handle/queue, **and** heartbeat. |
| Reuses | The entire tested generic engine (execute/subscribe/handles/pre-open queue/`WsReconnectSession`) **plus** `IxWsConnection`/`MockWsConnection`/`IWsErrorHandler`. New code = one ~100-line decorator + descriptor + request types. | `IWsConnection` & friends only; re-implements the dispatch the generic client already provides. |
| Touches `exchange_common`? | **No** — decorator is an `IWsConnection` impl in the adapter. | **No.** |
| Cost / risk | Smallest new surface; reuses proven id-correlation; heartbeat isolated + unit-tested. | ~250–300 lines re-implementing machinery that already fits; larger test surface. |

**Recommendation: Option A.** Crypto.com genuinely fits the id-correlated model
(the reason Coinbase needed a bespoke client does **not** apply here), so a small
`IWsConnection` decorator for the one true misfit (heartbeat) maximizes reuse,
keeps all blast radius in the adapter, and honors "never modify
`exchange_common`." **Plan steps below assume Option A; choosing B changes only
Step 6.**

> Decision owner: Rob, at plan approval.

---

## 3 — Files to create

```
include/exchange/cryptocom/
  auth.hpp                # CryptoComCredentials{api_key, api_secret}; detail:: hmac_sha256_hex,
                          #   params_to_str(json,level) [MAX_LEVEL=3]; sign(); make_nonce() (ms)
  types.hpp               # re-export canonical enums + cryptocom_* converters (UPPERCASE wire);
                          #   parse_cryptocom_response<T>(int status, const json&)  → exchange::rest::RestResponse<T>
  rest_api.hpp            # public + private request/response structs (+ .inl if any templates)
  rest_client.hpp         # CryptoComRestClient + make_cryptocom_test_client
  ws.hpp                  # (+ .inl) market+user channel events; CryptoComSubscribeRequest<PushMsg>;
                          #   CryptoComAuthRequest; cryptocom_frame_descriptor; client aliases + factories
  heartbeat_connection.hpp# HeartbeatResponder : exchange::ws::IWsConnection (Option A)
src/cryptocom/
  auth.cpp  types.cpp  rest_api.cpp  rest_client.cpp  ws.cpp  heartbeat_connection.cpp
tests/unit/
  cryptocom_rest_example_json.hpp      # public-endpoint fixtures (live-captured)
  cryptocom_account_example_json.hpp   # private fixtures (synthetic — marked)
  cryptocom_ws_example_json.hpp        # WS subscribe-ack + push + heartbeat fixtures
  test_cryptocom_auth.cpp  test_cryptocom_types.cpp
  test_cryptocom_rest_requests.cpp  test_cryptocom_rest_responses.cpp
  test_cryptocom_client.cpp  test_cryptocom_ws_client.cpp
tests/examples/cryptocom/
  cryptocom_rest_client_example.cpp    # CLI11 — every public REST endpoint
  cryptocom_ws_client_example.cpp      # market channels + heartbeat keepalive + connection reuse
```

Every `.hpp`/`.inl`/`.cpp` opens with the standard MIT banner (markdown exempt).
**No edits** to `exchange_common`, `exchange_http`, `kraken`, `binance`, or
`coinbase`.

> **Naming:** the namespace slug is `cryptocom` (`exchange::cryptocom::`,
> `src/cryptocom/`, `libcryptocom.a`, `CRYPTOCOGS_BUILD_CRYPTOCOM`). It is
> deliberately close to the project name `cryptocogs`; alternatives are `cdc`
> (Crypto.com's own abbreviation) or `crypto_com`. The slug is pervasive and
> hard to change later → **confirm at approval** (Risk R8).

---

## 4 — Steps

Each step is one checkpoint commit. **Done = `cmake --build build` clean AND
`cd build && ctest --output-on-failure` fully green** (the whole suite, not just
the new tests), then commit `feat: cryptocom — <what>`. Do not start a step until
the previous one is committed green. CMake wiring grows incrementally so every
checkpoint builds and runs.

### Step 1 — CMake skeleton + Auth (the signing core)
- **CMake:** add `option(CRYPTOCOGS_BUILD_CRYPTOCOM "Build the Crypto.com exchange adapter" ON)`; extend the "nothing will be built" warning; add the `cryptocom` static lib in `src/CMakeLists.txt` (mirror the Coinbase block — link `PUBLIC exchange_common exchange_http OpenSSL::SSL OpenSSL::Crypto`, `target_compile_features … cxx_std_17`), starting with `cryptocom/auth.cpp`; add the install component (`include/exchange/cryptocom`) and the install-targets list entry; wire `test_cryptocom_auth` behind the flag in `tests/unit/CMakeLists.txt`.
- **Code:** `auth.hpp` — `CryptoComCredentials{api_key, api_secret}` (plain struct, no file loader). `detail::hmac_sha256_hex(key, msg)`, `detail::params_to_str(const json& params, int level=0)` implementing the docs' recursive algorithm **exactly** (sorted keys, `key+value` concat, list iteration + recursion, `null`→`"null"`, `MAX_LEVEL=3`). `sign(method, id, api_key, params, nonce, secret)` → hex digest over `method + std::to_string(id) + api_key + params_to_str(params) + std::to_string(nonce)`. `make_nonce()` (ms since epoch).
  - **Key convention (Risk R1):** the adapter serializes **every** param value as a JSON **string** (numbers via `TickPrice::str()`/caller strings, bools as `"true"`/`"false"`) so `params_to_str`'s `str(value)` is deterministic and matches the server across the language boundary. Documented here and enforced when building request params in Step 4.
- **Tests** (`test_cryptocom_auth.cpp`): a dedicated **`params_to_str` matrix** — (a) flat object emits keys in ascending order as `key+value`; (b) nested object recurses; (c) a list of objects iterates + recurses; (d) `null` → `"null"`; (e) the **3-level cap** falls back to `str(obj)`; (f) string-valued numbers/bools pass through verbatim. Then `sign()` pins the *construction* = `hex(HMAC-SHA256(secret, method+id+api_key+params_string+nonce))` recomputed independently with the same primitives (no official vector exists → say so in a comment, per the `test_binance_client` precedent). Assert `make_nonce()` is ms-scale and monotonic. (No base64 — secret is used raw.)

### Step 2 — Types + enum converters + REST envelope
- **Code:** `types.hpp` — re-export the canonical four (`Side`, `OrderType`, `TimeInForce`, `OrderStatus`); add Crypto.com's **UPPERCASE** converters: `cryptocom_side_{to,from}_string` (`BUY`/`SELL`), `cryptocom_order_type_*` (`LIMIT`/`MARKET`/`STOP_LOSS`/`STOP_LIMIT`/`TAKE_PROFIT`/`TAKE_PROFIT_LIMIT`), `cryptocom_time_in_force_*` (`GOOD_TILL_CANCEL`/`IMMEDIATE_OR_CANCEL`/`FILL_OR_KILL`), `cryptocom_order_status_*` (`ACTIVE`/`CANCELED`/`FILLED`/`REJECTED`/`EXPIRED`/`PENDING`/…); all throw `std::invalid_argument` on unknown input. Add `parse_cryptocom_response<T>(int http_status, const json& j) -> exchange::rest::RestResponse<T>`: `ok = (http_status < 400 && j.value("code", -1) == 0)`; on failure `errors = { "code " + code (+ message if present) }`; on success `result = T::from_json(j.at("result"))`.
- **CMake:** append `cryptocom/types.cpp`; wire `test_cryptocom_types`.
- **Tests** (`test_cryptocom_types.cpp`): every converter round-trips and rejects bad input (note `OrderType` is the canonical enum's per-exchange divergence — `STOP_LOSS` vs. canonical `stop_loss`); `parse_cryptocom_response` maps `code:0` → `ok+result`, a non-zero `code` → `!ok` with the code in `errors`, and an HTTP-4xx body → `!ok`.

### Step 3 — REST **public** requests/responses
- **Endpoints:** `GET public/get-instruments`, `public/get-tickers` (all or `instrument_name`), `public/get-book` (`instrument_name`, `depth`), `public/get-candlestick` (`instrument_name`, `timeframe`), `public/get-trades` (`instrument_name`). Each derives the adapter's `TypedPublicRequest<R>` (over `exchange::rest::TypedPublicRequest`), defines `using response_type`, implements `build() -> HttpRequest` (path `/exchange/v1/public/get-…`, params in query). Each result has `static R from_json(const json&)`.
  - **Quirk (Risk R3):** Crypto.com result rows use **terse single-letter keys** (e.g. ticker `i`/`b`/`k`/`a`/`t`/`v`/`h`/`l`/`c`); pin the exact keys from live captures. Monetary/size fields arrive as **JSON strings** → `std::stod(j.value("k","0"))`; verify per-field whether candlestick rows are numbers vs. strings.
- **CMake:** append `cryptocom/rest_api.cpp`; wire `test_cryptocom_rest_requests` + `test_cryptocom_rest_responses`.
- **Tests:** `test_cryptocom_rest_requests.cpp` asserts method/path/query for each; `test_cryptocom_rest_responses.cpp` asserts `from_json` fields against **live-captured** fixtures in `cryptocom_rest_example_json.hpp`.

### Step 4 — REST **private** requests/responses
- **Endpoints:** `private/user-balance`; `private/create-order` (LIMIT + MARKET; `instrument_name`, `side`, `type`, `price`, `quantity`, `time_in_force`, `client_oid`); `private/cancel-order` (`order_id`); `private/cancel-all-orders` (`instrument_name`); `private/get-order-detail` (`order_id`); `private/get-open-orders`; `private/get-order-history`; `private/get-trades`.
- **Code:** private requests derive `TypedPrivateRequest<R>` and implement `build(const CryptoComCredentials&)` (**Kraken-style** — the signature lives *inside* the JSON body and depends on `params`, so `IRestAuth` header-injection does **not** fit; this matches the playbook's "either is fine, pick what matches your envelope"). `build()` constructs the full envelope `{id, method, api_key, params, nonce, sig}` as the POST body (path `/exchange/v1/private/…`), assigning `id` from a per-client atomic and `nonce` from `make_nonce()`, building `params` with **string values** (Step 1 convention; prices via `TickPrice::str()`), and signing via `auth.hpp`.
- **CMake:** no new files (same `rest_api.cpp`); extend the two REST test targets with private cases.
- **Tests:** extend `test_cryptocom_rest_requests.cpp` — assert the `create-order` body carries `method`/`api_key`/`params`/`nonce`/`sig`, that `sig` equals an independently-recomputed signature for that body, and that key order in `params` does **not** affect `sig` (server re-sorts); plus `cancel-order`/`get-open-orders` bodies. Extend `test_cryptocom_rest_responses.cpp` against **synthetic** fixtures in `cryptocom_account_example_json.hpp` (marked `synthetic` — no live creds, Risk R2).

### Step 5 — REST client
- **Code:** `rest_client.hpp` + `rest_client.cpp` — `CryptoComRestClient` (default base `https://api.crypto.com`) templated like `BinanceRestClient`: `execute(req)` (public) and `execute(req, const CryptoComCredentials&)` (private, calls `req.build(creds)`), both returning `exchange::rest::RestResponse<Req::response_type>` via `parse_cryptocom_response(status, body)`. Transport is the shared `exchange::rest::CurlHttpClient`. Holds the atomic request-`id` counter shared with private `build()`. `make_cryptocom_test_client(fn)` injects a mock performer (friends the private ctor).
- **CMake:** append `cryptocom/rest_client.cpp`; wire `test_cryptocom_client`.
- **Tests** (`test_cryptocom_client.cpp`): mock performer asserts the outgoing path/method and, for a private call, the signed envelope body (`api_key`/`sig`/`nonce`/`params` present and `sig` valid); feeds canned JSON and checks the typed result; a `code != 0` body maps to `!ok` with the code surfaced.

### Step 6 — WebSocket layer (Option A — reuse + `HeartbeatResponder`)
- **Code:**
  - `heartbeat_connection.hpp/.cpp` — `HeartbeatResponder : exchange::ws::IWsConnection` wrapping an inner `shared_ptr<IWsConnection>`. Forwards `connect/disconnect/is_connected/send/set_on_open/set_on_close/set_on_error`. In `set_on_message(cb)` it stores `cb` and installs its **own** handler on the inner connection that: parses the frame, and if `method == "public/heartbeat"` replies `{"id":<echoed id>,"method":"public/respond-heartbeat"}` via the inner `send` and **swallows** it; otherwise forwards to `cb`. Malformed frames pass through to `cb` unchanged.
  - `ws.hpp/.cpp` (+ `.inl` if templated) — `MARKET_WS_URL`/`USER_WS_URL`; push-event structs with `from_json` for `ticker`, `book`, `trade`, `candlestick`, and the user lifecycle (`user.order`, `user.trade`, `user.balance`); `cryptocom_frame_descriptor(const json&)` — `method` in `{subscribe, public/auth}` with an `id` → `MethodResponse{correlation_id = std::to_string(id)}`; `method == "push"` → `PushMessage{route_key = result.subscription}`; else `Unknown` (heartbeats never arrive — the decorator handles them). `CryptoComSubscribeRequest<PushMsg>` (mirrors Binance `TypedStreamSubscribeRequest`: `route_key()` = the channel string, `unsubscribe_json()` = `{method:"unsubscribe",params:{channels:[channel]}}`, `response_type = SubscribeAck`, `push_type = PushMsg`, `req_id` slot written as `id`; one channel per subscribe). `CryptoComAuthRequest` (`public/auth`, signs `method+id+api_key+nonce`) for the user feed. Client aliases `CryptoComMarketClient` / `CryptoComUserClient` = `exchange::ws::ExchangeWsClient`; factories `make_cryptocom_market_client(conn, eh=nullptr)` / `make_cryptocom_user_client(conn, eh=nullptr)` wrap `conn` in `HeartbeatResponder` then call `exchange::ws::make_exchange_ws_client(wrapped, cryptocom_frame_descriptor, eh)`; plus URL-based factories that build `IxWsConnection`, wrap it, and connect. **User-feed flow:** connect → `execute(CryptoComAuthRequest{creds})` → `subscribe(user.*)`.
- **CMake:** append `cryptocom/ws.cpp` + `cryptocom/heartbeat_connection.cpp`; wire `test_cryptocom_ws_client`; the WS test/example link only `cryptocom` (+ `ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward` for the example), never `kraken`/`binance`/`coinbase`.
- **Tests** (`test_cryptocom_ws_client.cpp`, driving shared `MockWsConnection`):
  - **HeartbeatResponder:** an injected `public/heartbeat` frame produces a `public/respond-heartbeat` (echoing the id) in `sent_messages` **and** does **not** reach the wrapped `on_message`; a non-heartbeat frame passes through unchanged.
  - **Client:** `fire_open()` flushes the queued subscribe; the outbound subscribe frame shape is correct (`method:"subscribe"`, `params.channels:[…]`, auto-assigned `id`); an injected subscribe ack (matching `id`) resolves the handle; an injected `method:"push"` with `result.subscription` reaches the typed callback; the user-feed `public/auth` via `execute()` signs correctly and its ack resolves; `cancel()` sends `unsubscribe` and is idempotent. `MockWsConnection`, no sockets, no sleeps.

### Step 7 — Build-matrix validation (the decoupling proof)
- `cmake -B build && cmake --build build` (all flags `ON`) clean; `ctest --output-on-failure` fully green.
- `cmake -B build-off -DCRYPTOCOGS_BUILD_CRYPTOCOM=OFF && cmake --build build-off` — **no `cryptocom` target, header, test, or example** exists (matches plan step-9-cmake-build-validation's matrix cell for Binance and plan coinbase-exchange-adapter's for Coinbase).
- `cmake -B build-cdc -DCRYPTOCOGS_BUILD_KRAKEN=OFF -DCRYPTOCOGS_BUILD_BINANCE=OFF -DCRYPTOCOGS_BUILD_COINBASE=OFF` — a **Crypto.com-only** tree builds and its tests pass.
- Confirm `cmake --install` ships `include/exchange/cryptocom` only when the flag is `ON`. Commit (validation + any install-gating fix only).

### Step 8 — CLI examples (live public verification)
- `cryptocom_rest_client_example.cpp` (CLI11, mirrors `coinbase_rest_client_example`): a subcommand per public endpoint; run live against prod (no creds) and confirm parsed output.
- `cryptocom_ws_client_example.cpp`: subscribe to `ticker`/`book`/`trade`/`candlestick` for an instrument, print pushes, **stay connected long enough to receive ≥1 heartbeat** (proving the decorator keeps the socket alive), and demonstrate connection reuse + `cancel()`; run live.
- Both build only when the flag is `ON` and vanish when `OFF`. Commit.

### Step 9 — Docs
- Update top-level `CLAUDE.md`: add a **Crypto.com adapter reference** section (the two-credential **plaintext-secret** HMAC + the `params_to_str` quirk + the strings-as-params convention; the unified method envelope shared by REST and WS; REST endpoint tables; the §2 WS model and the heartbeat decorator), plus the build-outputs table, the test-binary table (+ new test count), the namespace-layout table, and the project-structure tree. Mark this plan **Done** in `docs/plans.md`. Commit.

---

## 5 — Test inventory (per global policy — tests at every step)

| Test file | Verifies | Step |
|---|---|---|
| `test_cryptocom_auth.cpp` | `params_to_str` matrix (sorting, nesting, `null`, 3-level cap, string values) + `sign()` construction + `make_nonce()` | 1 |
| `test_cryptocom_types.cpp` | enum converters round-trip/reject; `parse_cryptocom_response` `code`/status mapping | 2 |
| `test_cryptocom_rest_requests.cpp` | each request builds correct method/path/query/body; signed envelope shape + `sig` validity (private) | 3,4 |
| `test_cryptocom_rest_responses.cpp` | `from_json` field assertions vs. fixtures (public live, private synthetic), incl. terse keys | 3,4 |
| `test_cryptocom_client.cpp` | signed `execute()` round-trip via mock performer; `code`→error mapping | 5 |
| `test_cryptocom_ws_client.cpp` | `HeartbeatResponder` auto-reply + swallow; id-correlated subscribe/ack/push; user `public/auth`; `cancel()` idempotency | 6 |

All tests are network-free (mock HTTP performer / `MockWsConnection`) and
deterministic (no `sleep_for`, no wall-clock polling).

---

## 6 — Self-review: risks & assumptions

**Risks**
- **R1 — `params_to_str` signature correctness (the big one).** Crypto.com's
  digest depends on a recursive, sort-then-concat serialization with a 3-level
  cap, and the reference is Python — so `str(bool)`/`str(number)` differ across
  languages (`str(True)=="True"`). *Mitigation:* serialize **all** param values
  as strings on the wire (so `str(value)` is the string itself), port the
  algorithm exactly, and cover it with an exhaustive unit matrix (nesting, `null`,
  cap, ordering). *Residual:* the server's exact canonicalization of edge types
  is only fully provable against a live signed call — covered by construction
  pinning + (if creds appear) one live private smoke test.
- **R2 — No live private creds.** Private REST and the user feed are tested with
  **synthetic** fixtures (marked) and construction-pinned signing, not an official
  vector. Live verification covers **public** endpoints only (Step 8).
- **R3 — Terse single-letter response keys.** `from_json` must map Crypto.com's
  `i/b/k/a/t/v/h/l/c`-style keys exactly, and numeric fields may be JSON strings
  *or* numbers depending on endpoint. *Mitigation:* pin every field from live
  captures in Step 3; choose `std::stod` vs. `.get<double>()` per captured type.
- **R4 — Heartbeat is mandatory.** Missing the `public/respond-heartbeat` reply
  disconnects the socket. *Mitigation:* the `HeartbeatResponder` decorator,
  unit-tested for auto-reply + swallow; the live WS example holds a connection
  long enough to receive and answer ≥1 heartbeat.
- **R5 — WS design choice (§2).** Option A reuses the id-correlated generic
  client (which Crypto.com fits) plus a decorator; if Rob prefers a bespoke
  client (Option B), only Step 6 changes — the rest of the plan is unaffected.
- **R6 — Exact push `route_key` string.** `route_key()` must equal what the
  server echoes in `result.subscription` (e.g. `book.BTC_USD.50` incl. depth; the
  candlestick period token format). *Mitigation:* pin from live captures in
  Step 6; the live example confirms callbacks actually fire.
- **R7 — Scope.** Advanced orders (OCO/OTO/OTOCO), bots, staking, fiat, margin,
  sub-accounts, and deposits/withdrawals are **out of scope** and recorded here.
  A later plan can add `private/advanced/*` if needed (mirrors how Coinbase FIX
  became plan coinbase-fix-order-entry).
- **R8 — Namespace slug.** `cryptocom` is pervasive and hard to change; it is
  visually close to the project name `cryptocogs`. Confirm `cryptocom` vs. `cdc`
  / `crypto_com` at approval.
- **R9 — Sandbox/UAT not targeted.** Live checks use **public prod** (no creds),
  mirroring Coinbase R5.

**Assumptions**
- Target is **Crypto.com Exchange v1** (`api.crypto.com/exchange/v1`), with the
  unified method envelope and HMAC-SHA256 over a **plaintext** secret — per the
  docs URL given (not the legacy v2/derivatives or any JWT scheme).
- REST order placement is sufficient for v1; advanced order types are a separate,
  deferred effort.
- The adapter is **purely additive** — `exchange_common`, `exchange_http`, and the
  Kraken/Binance/Coinbase adapters are untouched (Option A guarantees this).
- Two-credential auth (no passphrase).

**Done criteria** (mirror the playbook §5): all-flags-ON build + full green
ctest; `-DCRYPTOCOGS_BUILD_CRYPTOCOM=OFF` leaves no trace; a Crypto.com-only build
passes; both CLI examples print live public output and the WS example survives
≥1 heartbeat cycle.
