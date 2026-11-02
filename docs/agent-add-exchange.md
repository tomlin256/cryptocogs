# Adding a New Exchange — Agent Playbook

This is a self-contained playbook for integrating a **new exchange** into this
library. It assumes no prior context beyond what the user gives you and the two
adapters already in the repo:

- **Binance** (`include/exchange/binance/`, `src/binance/`) — the **primary
  reference**: REST public + private, WebSocket market streams, *and* a
  bidirectional WebSocket trading API. Richest, newest, most complete.
- **Kraken** (`include/exchange/kraken/`, `src/kraken/`) — the **secondary
  reference**, notably for the optional `identify_message`/`MessageKind`
  caller-facing frame classifier.

Everything generic lives in `include/exchange/common/` (the `exchange_common`
library). **You will not modify it.** A new exchange is built entirely on top of
it, the same way Binance and Kraken are.

Where this guide says "follow this pattern", it names a real file and — when the
change is non-obvious — a commit on the `feature/multi-exchange-abstraction`
branch you can `git show`.

---

## 0 — How to use this guide

### The per-item loop

Work the [§3 checklist](#3--implementation-checklist) **one row at a time**.
For every row:

1. Write the header(s) and/or source named in the row.
2. Write its unit test(s) **in the same step** — tests are required for all
   code, no exceptions.
3. `cmake --build --preset release-macos-arm64` — must compile clean. (Your host's presets are listed by
   `cmake --list-presets`; `CLAUDE.md`'s "Build trees" says what the names mean.)
4. `ctest --preset release-macos-arm64` — must be **fully green** (the whole
   suite, not just your new test).
5. Checkpoint commit with a `feat: <exchange> — <what>` message. Never batch
   multiple checklist items into one commit.

Do not start a row until the previous row's build + tests are green and
committed. If a test fails — yours or a pre-existing one — **stop and fix it**;
never skip, weaken, or delete a test to move on.

### The build model (Step 9)

Three peer static libraries:

- `exchange_common` — the generic engine, always built. **Never edited by an
  adapter.**
- `kraken`, `binance` — peers, each linking `exchange_common PUBLIC`;
  neither links the other.

Your new exchange becomes a **fourth peer** library, `<name>`, behind a
`CRYPTOCOGS_BUILD_<NAME>` flag (default `ON`). When the flag is `OFF`, none of its
targets exist. See [checklist item 8](#3--implementation-checklist) and
reference commits `9e73dc1`, `28179fd`.

---

## 1 — Inputs to collect from the user first

**Ask for all of this before writing any code.** Missing wire-format detail is
the #1 cause of a broken adapter.

| Input | What to ask for |
|---|---|
| Exchange name + namespace slug | e.g. `coinbase` → `exchange::coinbase::`, `src/coinbase/`, `include/exchange/coinbase/` |
| Auth | Signing algorithm (HMAC-SHA256/512, RSA, Ed25519); header names; nonce or timestamp scheme; **the exact string that gets signed** and how it is assembled |
| REST base URL | e.g. `https://api.exchange.com` |
| REST endpoints | At least one **public** and one **private**, each with a sample request and the **raw JSON response** the server returns |
| WebSocket URL(s) | Market-data stream URL and/or trading-API URL |
| WS connection model | Single vs. combined streams; the subscribe/unsubscribe wire format; the **correlation field name** (`id`, `req_id`, …) used to match a reply to its request |
| WS channels | Each channel with **at least one captured push frame** (raw JSON exactly as sent) |
| Quirks | Non-standard error shapes; mandatory keepalives; reconnect rules; per-request vs. session auth; numbers-as-strings vs. numbers-as-JSON-numbers |

Capture every sample frame verbatim — they become your test fixtures
(`tests/unit/<name>_*_example_json.hpp`). Mark any frame you synthesise (because
the docs lacked one) as `synthetic` in a comment.

---

## 2 — Architecture primer

The contracts your adapter must satisfy. All live in `include/exchange/common/`
(`b20dc35`, step 1). Read them once before starting.

### REST: typed request → response binding

Each request type binds its result type at compile time via
`using response_type = …`, and a client templated on the request resolves it
with no casts. The common base templates are
`exchange::rest::TypedPublicRequest<R>` / `TypedPrivateRequest<R>`
(`exchange/common/rest.hpp:77,87`); each adapter re-derives its own thin
`PublicRequest`/`PrivateRequest` bases and mirrors the `Typed*` templates.

```cpp
// exchange/binance/rest_api.hpp:55 — the adapter's own Typed* over its bases
template<typename R> struct TypedPublicRequest  : PublicRequest  { using response_type = R; };
template<typename R> struct TypedPrivateRequest : PrivateRequest { using response_type = R; };

struct BinancePingRequest : TypedPublicRequest<BinancePing> {  // rest_api.hpp:119
    HttpRequest build() const;                                 // path + query
};
```

- **Public** requests implement `build() -> HttpRequest`.
- **Private** requests implement `build(const Credentials&)` (adds the signature).
- `HttpRequest` is the shared struct `exchange::rest::HttpRequest`
  (`rest.hpp:32`): `{ method, path, query, body, headers }`.

### REST: the response envelope

The exchange wraps results in an envelope; you parse it into
`exchange::rest::RestResponse<T>` (`rest.hpp:57` — `{ ok, errors, result }`).
Binance does this with a status-aware free function:

```cpp
// exchange/binance/rest_api.hpp:71
template<typename T>
exchange::rest::RestResponse<T> parse_binance_response(int http_status, const json& j);
```

Kraken instead keeps its own `exchange::kraken::RestResponse<T>` +
`parse_rest_response<T>` (its `{error[], result}` envelope predates the common
one). **Either is fine** — pick whichever matches your exchange's envelope; just
return a `RestResponse`-shaped type the client can hand back. Always check
`resp.ok` before `resp.result`.

### Auth (usually header-only)

`BinanceAuth : exchange::rest::IRestAuth` (`auth.hpp:91`) implements the
`IRestAuth` interface (`rest.hpp:43`). The crypto helpers (`hmac_sha256`,
`to_hex`, …) are **inline in the header** under a `detail::` namespace — there
is **no `src/binance/auth.cpp`**. Only add a `src/<name>/auth.cpp` if your
signing genuinely needs out-of-line code (rare). Credentials are a plain struct:

```cpp
struct BinanceCredentials { std::string api_key, secret_key; int recv_window_ms{5000}; };  // auth.hpp:66
```

Reference: `4350ce6` (step 4) introduced both the auth header and the REST
client infra together.

### Enums: re-export the canonical four

`Side`, `OrderType`, `TimeInForce`, `OrderStatus` are canonical in
`exchange/common/types.hpp`. Re-export them into your namespace so adapter code
uses bare names, and add **per-exchange string converters** (wire formats differ):

```cpp
// exchange/binance/types.hpp:47
using exchange::Side; using exchange::OrderType;
using exchange::TimeInForce; using exchange::OrderStatus;
// then: binance_side_to_string(...), binance_order_type_to_string(...), etc.
```

`OrderType` is the one enum whose wire string almost always differs per exchange
— give it a dedicated `<name>_order_type_{to,from}_string` pair. Reference:
`145bf70` (step 6.1).

### WebSocket: the one contract that matters — `MessageIdentifier`

`ExchangeWsClient` (the generic client, `exchange/common/ws_client.hpp`) is **not
subclassed** — you alias it and parameterise it at construction with one
function:

```cpp
using MessageIdentifier = std::function<FrameDescriptor(const json&)>;   // ws.hpp
struct FrameDescriptor {
    FrameKind                  kind;            // MethodResponse | PushMessage | Unknown
    std::optional<std::string> correlation_id;  // MethodResponse: matches a pending request
    std::string                route_key;       // PushMessage: matches an active subscription
};
```

You write **one free function**, `<name>_frame_descriptor(const json&) ->
FrameDescriptor`, that classifies every inbound frame:

- **Reply to a request** → `MethodResponse`, `correlation_id =
  stringify(<the correlation field>)`. The client keys pending requests by
  `std::to_string(req_id)`, so your `correlation_id` must match that exactly
  (stringify ints; pass strings through).
- **Push/stream frame** → `PushMessage`, `route_key = <channel/stream name>`
  (matches what `route_key()` on the subscribe request returns).
- Otherwise → `Unknown`.

Two worked examples, two shapes:
- `binance_ws_api_frame_descriptor` (`ws_api.hpp`, `398d2a8`) — *every* reply is
  a `MethodResponse` by `id`; no push concept on the trading endpoint.
- `binance_stream_frame_descriptor` (`ws_streams.hpp`, `7d461fc`) — `"stream"`
  key → `PushMessage` by stream name; `"id"`+`result`/`error` → `MethodResponse`.

> **Optional, not required**: a richer caller-facing classifier like Kraken's
> `identify_message(json) -> MessageKind` (`exchange/kraken/ws_api.hpp`, `13671f1`)
> is for code that bypasses the typed client and handles raw frames. It is
> **separate from** `<name>_frame_descriptor` (which is what the client needs).
> Binance ships none. Add one only if your callers want fine-grained manual
> dispatch.

### WebSocket: request types

- **Method calls** (request → one reply): derive
  `exchange::ws::TypedWsRequest<R>` (brings `response_type` and the `req_id`
  slot from `WsRequestBase`); implement `to_json()` writing `req_id` into your
  correlation field. Responses that carry `success`/`error` derive
  `exchange::ws::BaseWsResponse` so `WsResponse::ok` is derived for free.
  Reference: `BinanceWsPingRequest`/`BinanceWsNewOrderRequest` (`ws_api.hpp`,
  `398d2a8`, `d9592c7`).
- **Subscriptions** (request → ack + continuous pushes): your subscribe request
  supplies `route_key()` and `unsubscribe_json()` — the two members the generic
  `subscribe_async` calls. Reference: `TypedStreamSubscribeRequest` in
  `ws_streams.hpp` (`b833587`).

### WebSocket: the client is a bare alias + inline factory

There is **no `src/binance/ws_client.cpp`**. The WS client is:

```cpp
using BinanceWsApiClient = exchange::ws::ExchangeWsClient;                 // ws_api.hpp
inline std::shared_ptr<BinanceWsApiClient>
make_binance_ws_api_client(std::shared_ptr<IWsConnection> conn,
                           std::shared_ptr<IWsErrorHandler> eh = nullptr) {
    return exchange::ws::make_exchange_ws_client(std::move(conn),
                                                 binance_ws_api_frame_descriptor, std::move(eh));
}
```

Budget for **zero** non-template WS source in your adapter — same as Binance and
Kraken. The factory binds your descriptor once; everything else is inherited.

---

## 3 — Implementation checklist

Work top to bottom. Each row → files to create + the Binance file to copy the
pattern from (and a commit for the non-obvious ones). `<name>` is your slug.

| # | Implement | Create | Reference (file · commit) |
|---|---|---|---|
| 1 | **Auth** (header-only by default) | `include/exchange/<name>/auth.hpp` *(+ `src/<name>/auth.cpp` only if signing needs out-of-line code)* | `exchange/binance/auth.hpp` · `4350ce6` |
| 2 | **Exchange types + enum converters** | `include/exchange/<name>/types.hpp` | `exchange/binance/types.hpp` · `145bf70` |
| 3 | **REST request/response structs** (`+ parse_<name>_response`) | `include/exchange/<name>/rest_api.hpp` | `exchange/binance/rest_api.hpp` · `352efd0`, `c8e42c4` |
| 4 | **REST client** | `include/exchange/<name>/rest_client.hpp`, `src/<name>/rest_client.cpp` | `exchange/binance/rest_client.hpp`, `src/binance/rest_client.cpp` · `4350ce6` |
| 5 | **REST fixtures + tests** | `tests/unit/<name>_rest_example_json.hpp`, `test_<name>_auth.cpp`, `test_<name>_rest_requests.cpp`, `test_<name>_rest_responses.cpp`, `test_<name>_client.cpp` | `binance_rest_example_json.hpp`, `binance_account_example_json.hpp`, `test_binance_auth.cpp`, `test_binance_rest_*.cpp`, `test_binance_client.cpp` · `352efd0`, `8b63fa9` |
| 6 | **WS structs + `<name>_frame_descriptor`** *(+ separate `ws_streams.hpp` if the exchange splits stream vs. trading protocols)* | `include/exchange/<name>/ws_api.hpp` *(`ws_streams.hpp`)* | `exchange/binance/ws_api.hpp`, `exchange/binance/ws_streams.hpp` · `7d461fc`, `398d2a8` |
| 7 | **WS client alias + factory** (header-only) | in `ws_api.hpp` / `ws_streams.hpp` — bare `using` alias + `inline make_<name>_*_client` | inline factories in `exchange/binance/ws_api.hpp` + `ws_streams.hpp`; separate-header style: `exchange/kraken/ws_client.hpp` |
| — | **WS source** — *almost never needed*; only if you have genuinely non-template WS code | `src/<name>/ws_client.cpp` | *(Binance has none — header-only)* |
| 8 | **CMake wiring** | `CRYPTOCOGS_BUILD_<NAME>` option + guard in `CMakeLists.txt`; `<name>` target in `src/CMakeLists.txt`; test + example targets behind the guard in `tests/CMakeLists.txt` + `tests/unit/CMakeLists.txt` | Binance blocks in each · `9e73dc1`, `28179fd` |
| 9 | **WS fixtures + tests** | `tests/unit/<name>_ws_example_json.hpp`, `test_<name>_ws_client.cpp` | `binance_ws_stream_example_json.hpp`, `binance_ws_api_example_json.hpp`, `test_binance_ws_client.cpp` · `b833587`, `d9592c7` |
| 10 | **REST CLI example** | `tests/examples/<name>/<name>_rest_client_example.cpp` | `tests/examples/binance/binance_rest_client_example.cpp` |
| 11 | **WS CLI example** | `tests/examples/<name>/<name>_ws_client_example.cpp` | `tests/examples/binance/binance_ws_client_example.cpp`, `binance_ws_api_example.cpp` |

### CMake specifics (item 8)

- `src/CMakeLists.txt`: `add_library(<name> STATIC <name>/rest_client.cpp)`
  inside `if(CRYPTOCOGS_BUILD_<NAME>)`; link
  `PUBLIC exchange_common OpenSSL::SSL OpenSSL::Crypto CURL::libcurl`
  (OpenSSL/curl are **PUBLIC** — your `auth.hpp`/`rest_client.hpp` expose them
  inline; see `9e73dc1`).
- `tests/*`: wrap your test/example targets in `if(CRYPTOCOGS_BUILD_<NAME>)`.
  Link tests `<name> GTest::gtest_main`. WS tests/examples need **only**
  `<name>` (it reaches the engine via `exchange_common` — do **not** add
  `kraken`). WS examples also link `ixwebsocket spdlog::spdlog CLI11::CLI11
  example_backward`.

### Test patterns (no network — ever)

- **REST**: inject a mock HTTP performer via the adapter's `make_test_client(fn)`
  factory; assert `http.path`/`method`/`body` on the way out and feed canned
  JSON back. See `test_binance_client.cpp` (`8b63fa9`).
- **WS**: drive `MockWsConnection` (`tests/unit/mock_ws_connection.hpp`, shared):
  `fire_open()`, inspect `sent_messages`, `inject_message(raw)`, `fire_close()`.
  See `test_binance_ws_client.cpp` (`b833587`, `d9592c7`).
- **Signing**: if no official signature vector exists, recompute the expectation
  with the same primitives and pin the *construction* (the `test_binance_client`
  precedent) — and say so in a comment.

---

## 4 — Conventions to enforce

These apply to every adapter and are easy to miss:

- **File banner** on every `.hpp`/`.inl`/`.cpp` — the exact MIT block from
  `CLAUDE.md`, before `#pragma once`. (Markdown docs are exempt.)
- **`CRYPTOCOGS_BUILD_<NAME>` defaults `ON`.** If your exchange needs a compat
  shim, add the matching configure-time dependency rule.
- **REST numbers arrive as JSON strings** — deserialise monetary/qty fields with
  `std::stod(j.value("field", "0"))`, never `.get<double>()`. For prices that
  must round-trip to an exact decimal, use `TickPrice` (`exchange/kraken/types.hpp`).
- **Optional fields are `std::optional<T>`** and must **not** be serialised in
  `to_json()` when unset.
- **All WS tests use `MockWsConnection`** — no sockets, no sleeps. Drive timing
  with synchronous step injection (a real wall-clock wait is acceptable *only*
  in a live CLI example, never a test).
- **Both CLI examples build when the flag is `ON` and vanish when it is `OFF`.**
- **`ctest --output-on-failure` is green before any checklist item is "done".**
- **Don't touch `exchange_common` or the other adapters.** A new exchange is
  purely additive.

---

## 5 — Done criteria

Declare the integration complete only when **all** hold:

1. `cmake --preset release-macos-arm64 && cmake --build --preset release-macos-arm64` succeeds from a clean
   tree (`rm -rf build/release-macos-arm64` first) with every exchange flag `ON`.
2. `ctest --preset release-macos-arm64` passes **every** test, including your exchange's
   auth, REST, and WS suites.
3. Both CLI examples run against the **live** exchange and print parsed output
   (public endpoints need no credentials).
4. `cmake -S . -B build/decoupled-macos-arm64 -DCRYPTOCOGS_BUILD_<NAME>=OFF && cmake --build build/decoupled-macos-arm64`
   (a hand-made tree: any kind word) succeeds
   with **no trace** of your exchange's targets — the decoupling proof, exactly
   as Step 9's matrix cell 3 proved for Binance.

When all four pass, you have a working, tested, independently-buildable adapter —
the same bar Binance clears today.
