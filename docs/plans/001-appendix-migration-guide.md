# Appendix to multi-exchange-abstraction — Migration Guide for Existing `cryptocogs` Users

Companion to [001-multi-exchange-abstraction.md](001-multi-exchange-abstraction.md).

This refactor is a **deliberate breaking change**: the `kraken_*.hpp` headers and the `kraken::` namespaces are replaced by an `exchange/`-rooted layout (§ "Proposed Repository Layout"). No backwards-compatibility forwarding headers ship in the library. This guide tells existing callers exactly what to change.

The good news: the change is **almost entirely mechanical** — include paths and namespace prefixes. Every request/response struct, every field name, and the `execute()` / `subscribe()` call shapes are unchanged. Most call sites change by find-and-replace, and a drop-in compatibility shim (below) lets a codebase compile against the new library with **zero per-call-site edits** if you prefer to migrate gradually.

---

## 1. What changed, in one sentence

Kraken-specific code moved from `kraken::…` (headers `kraken_*.hpp`) to `exchange::kraken::…` (headers `exchange/kraken/*.hpp`); the exchange-agnostic scaffold it was built on was lifted into `exchange::` / `exchange::rest::` / `exchange::ws::` (headers `exchange/common/*.hpp`).

## 2. Is my code affected? (quick triage)

| If your code… | Impact | Fix |
|---|---|---|
| `#include "kraken_*.hpp"` | **Yes** | Update include paths (§3) |
| names `kraken::…` anywhere | **Yes** | Update namespaces (§4), or use the shim (§7) |
| only uses `auto` + factory functions | Low | Rename `make_ws_client` → `make_kraken_ws_client` (§5) |
| injects a mock `IWsConnection` in tests | Yes | `make_ws_client(conn)` → `make_generic_ws_client(conn, …)` (§5, §8) |
| reads response fields / sets request fields | **No** | Struct and field names are unchanged |

---

## 3. Include-path mapping

| Before | After |
|---|---|
| `kraken_types.hpp` | `exchange/common/types.hpp` (shared enums) **and/or** `exchange/kraken/types.hpp` (Kraken structs) |
| `kraken_rest_api.hpp` | `exchange/kraken/rest_api.hpp` |
| `kraken_rest_client.hpp` | `exchange/kraken/rest_client.hpp` |
| `kraken_ws_api.hpp` | `exchange/kraken/ws_api.hpp` |
| `kraken_ws_client.hpp` | `exchange/kraken/ws_client.hpp` (pulls in `exchange/common/ws_client.hpp` transitively) |
| `kraken_ix_ws_connection.hpp` | `exchange/kraken/ws_client.hpp` (provides `make_kraken_ws_client`; `IxWsConnection` itself lives in `exchange/common/ix_ws_connection.hpp`) |
| `ws_reconnect_session.hpp` | `exchange/common/reconnect_session.hpp` |

Rule of thumb: include `exchange/kraken/<area>.hpp` and the common headers come along transitively. Mock-only test TUs that previously included just `kraken_ws_client.hpp` now include `exchange/common/ws_client.hpp` + `exchange/kraken/ws_api.hpp` (for `identify_message`), with no ixwebsocket dependency — same split as today.

## 4. Namespace mapping

**Exchange-agnostic things lifted out of `kraken::`** (now shared by all exchanges):

| Before | After |
|---|---|
| `kraken::Side`, `kraken::OrderType`, `kraken::TimeInForce`, `kraken::OrderStatus` | `exchange::Side`, `exchange::OrderType`, `exchange::TimeInForce`, `exchange::OrderStatus` |
| `kraken::rest::TypedPublicRequest`, `…TypedPrivateRequest`, `…HttpRequest`, `…RestResponse`, `…IRestAuth` | `exchange::rest::…` |
| `kraken::ws::IWsConnection`, `…IWsErrorHandler`, `…IxWsConnection`, `…WsResponse`, `…SubscriptionHandle`, `…WsReconnectSession` | `exchange::ws::…` |
| `kraken::ws::KrakenWsClient` (the class) | `exchange::ws::GenericWsClient` |

**Kraken-specific things** (just re-prefixed `kraken::` → `exchange::kraken::`):

| Before | After |
|---|---|
| `kraken::TickPrice`, `kraken::OrderParams`, `kraken::Triggers`, `kraken::Conditional`, `kraken::OrderInfo`, `kraken::TradeInfo`, `kraken::LedgerEntry`, `kraken::OrderDescription` | `exchange::kraken::…` |
| `kraken::PriceType`, `kraken::TriggerReference`, `kraken::StpType`, `kraken::FeePreference` | `exchange::kraken::…` |
| `kraken::rest::*Request` / `*Result`, `kraken::rest::Credentials`, `kraken::rest::parse_rest_response` | `exchange::kraken::rest::…` |
| `kraken::ws::*SubscribeRequest`, `*Message`, `SubscribeChannel`, `MessageKind`, `identify_message`, `WsCredentials`, `PUBLIC_WS_URL`, `PRIVATE_WS_URL` | `exchange::kraken::ws::…` |

> To keep the §7 compatibility shim working, `exchange::kraken::rest` re-exports the common bases it builds on (`using exchange::rest::RestResponse; using exchange::rest::HttpRequest;`) and `exchange::kraken::ws` likewise re-exports `WsResponse`, `SubscriptionHandle`, `GenericWsClient`. So `exchange::kraken::rest::RestResponse` etc. resolve even though the definitions live in `exchange::rest`.

## 5. Function / type renames

| Before | After |
|---|---|
| `kraken::ws::make_ws_client(url)` | `exchange::kraken::ws::make_kraken_ws_client(url)` |
| `kraken::ws::make_ws_client(conn)` (managed/mock connection) | `exchange::ws::make_generic_ws_client(conn, exchange::kraken::ws::identify_message)` |
| `kraken::parse_rest_response<T>(j)` | `exchange::kraken::rest::parse_rest_response<T>(j)` |
| `std::shared_ptr<kraken::ws::KrakenWsClient>` | `std::shared_ptr<exchange::ws::GenericWsClient>` (usually hidden behind `auto`) |

## 6. What does **not** change

- Every request/response struct name (`GetTickerRequest`, `AddOrderRequest`, `TickerMessage`, `AccountBalanceResult`, …) and every field on them.
- `client.execute(req)` / `client.execute(req, creds)` / `client->subscribe(req, cb, timeout)` / `client->execute(req, timeout)` call shapes.
- `RestResponse` semantics (`.ok`, `.result`, `.errors`) and `WsResponse` (`.ok`, `.error`, `.result`).
- `SubscriptionHandle::cancel()`, `is_active()`.
- `Credentials::from_file("default")`, `WsCredentials{token}`.
- The `{ "error": [], "result": … }` Kraken envelope handling.
- The requirement to call `curl_global_init(CURL_GLOBAL_ALL)` / `curl_global_cleanup()` around `KrakenRestClient` (now `exchange::kraken::rest::KrakenRestClient`).

---

## 7. Fast path — the shipped compatibility shim (recommended)

**The library ships a deprecated compatibility shim** (CMake option `CRYPTOCOGS_BUILD_COMPAT_SHIM`, default **ON**). With it, your existing `#include "kraken_*.hpp"` lines and `kraken::…` code compile and run **completely unchanged** — including the factory calls — because the shipped shim adds real forwarder functions, not just aliases. This is the recommended path: adopt the new version, keep building, migrate at your pace, then configure with `-DCRYPTOCOGS_BUILD_COMPAT_SHIM=OFF` to confirm you have fully migrated. See [001-appendix-compat-shim.md](001-appendix-compat-shim.md) for the design and the step-by-step adoption workflow.

You normally do **not** need to write your own shim. The hand-rolled version below is only a fallback for callers who have turned the shipped shim **off** but still want a partial local bridge — note it cannot cover the two factory edits in §8 (a local namespace *alias* can't host forwarder functions; the shipped shim uses real namespaces and does cover them).

```cpp
// kraken_compat.hpp — LOCAL fallback only; prefer the shipped shim above.
// Lets pre-refactor code keep using kraken::… when CRYPTOCOGS_BUILD_COMPAT_SHIM=OFF.
#pragma once
#include "exchange/kraken/rest_client.hpp"
#include "exchange/kraken/rest_api.hpp"
#include "exchange/kraken/ws_api.hpp"
#include "exchange/kraken/ws_client.hpp"        // make_kraken_ws_client
#include "exchange/common/reconnect_session.hpp"

namespace kraken {
    // shared enums lifted to exchange::
    using exchange::Side;
    using exchange::OrderType;
    using exchange::TimeInForce;
    using exchange::OrderStatus;

    // Kraken-specific sub-namespaces
    namespace rest = exchange::kraken::rest;
    namespace ws   = exchange::kraken::ws;

    // Kraken-specific structs that used to sit in kraken::
    using exchange::kraken::TickPrice;
    using exchange::kraken::OrderParams;
    using exchange::kraken::Triggers;
    using exchange::kraken::Conditional;
    using exchange::kraken::OrderInfo;
    using exchange::kraken::TradeInfo;
    using exchange::kraken::LedgerEntry;
    using exchange::kraken::OrderDescription;

    // parse_rest_response was called unqualified via `using namespace kraken`
    using exchange::kraken::rest::parse_rest_response;
}
```

Two residual edits the shim **cannot** paper over (because the names genuinely changed, not just moved):

1. `make_ws_client(url)` → `make_kraken_ws_client(url)`. Add `namespace kraken::ws { using exchange::kraken::ws::make_kraken_ws_client; }` and a thin `make_ws_client` forwarder if you want even this hidden.
2. The mock-connection `make_ws_client(conn)` overload (§8).

Treat the shim as a bridge, not a destination — it lets you migrate a large codebase in two commits (compile green with the shim, then delete the shim and find-and-replace at leisure).

## 8. Migrating test harnesses (MockWsConnection)

`MockWsConnection` and the `IWsConnection` interface are unchanged in shape — only their namespace moved to `exchange::ws::`. The one edit is the factory that wraps a mock connection, which now needs the exchange's message identifier:

```cpp
// Before
auto conn   = std::make_shared<MockWsConnection>();
auto client = kraken::ws::make_ws_client(
                  std::static_pointer_cast<kraken::ws::IWsConnection>(conn));

// After
auto conn   = std::make_shared<MockWsConnection>();
auto client = exchange::ws::make_generic_ws_client(
                  std::static_pointer_cast<exchange::ws::IWsConnection>(conn),
                  exchange::kraken::ws::identify_message);
```

`conn->fire_open()`, `conn->inject_message(raw)`, `conn->sent_messages`, `conn->fire_close()` are all unchanged.

---

## 9. Worked before/after examples

### Public REST

```cpp
// ── Before ──────────────────────────────────────────────
#include "kraken_rest_client.hpp"
using namespace kraken::rest;

curl_global_init(CURL_GLOBAL_ALL);
KrakenRestClient client;
auto resp = client.execute(GetServerTimeRequest{});
if (resp.ok) spdlog::info("{}", resp.result->unixtime);
curl_global_cleanup();

// ── After ───────────────────────────────────────────────
#include "exchange/kraken/rest_client.hpp"
using namespace exchange::kraken::rest;   // request/response types + KrakenRestClient

curl_global_init(CURL_GLOBAL_ALL);
KrakenRestClient client;
auto resp = client.execute(GetServerTimeRequest{});   // unchanged
if (resp.ok) spdlog::info("{}", resp.result->unixtime);
curl_global_cleanup();
```

### Private REST (credentials + a shared enum)

```cpp
// ── Before ──────────────────────────────────────────────
#include "kraken_rest_client.hpp"
auto creds = kraken::rest::Credentials::from_file("default");
kraken::rest::KrakenRestClient client;
auto resp = client.execute(kraken::rest::GetTradesHistoryRequest{}, creds);
for (auto& [id, t] : resp.result->trades)
    bool buy = (t.type == kraken::Side::Buy);

// ── After ───────────────────────────────────────────────
#include "exchange/kraken/rest_client.hpp"
auto creds = exchange::kraken::rest::Credentials::from_file("default");
exchange::kraken::rest::KrakenRestClient client;
auto resp = client.execute(exchange::kraken::rest::GetTradesHistoryRequest{}, creds);
for (auto& [id, t] : resp.result->trades)
    bool buy = (t.type == exchange::Side::Buy);   // Side moved to exchange::
```

### WebSocket subscription

```cpp
// ── Before ──────────────────────────────────────────────
#include "kraken_ix_ws_connection.hpp"
auto client = kraken::ws::make_ws_client(std::string(kraken::ws::PUBLIC_WS_URL));
kraken::ws::TickerSubscribeRequest req;
req.symbols = std::vector<std::string>{"BTC/USD"};
auto [ack, handle] = client->subscribe(
    req,
    [](const kraken::ws::TickerMessage& m) { /* … */ },
    std::chrono::milliseconds{10000});
handle.cancel();

// ── After ───────────────────────────────────────────────
#include "exchange/kraken/ws_client.hpp"
auto client = exchange::kraken::ws::make_kraken_ws_client(
                  std::string(exchange::kraken::ws::PUBLIC_WS_URL));
exchange::kraken::ws::TickerSubscribeRequest req;
req.symbols = std::vector<std::string>{"BTC/USD"};
auto [ack, handle] = client->subscribe(           // subscribe() shape unchanged
    req,
    [](const exchange::kraken::ws::TickerMessage& m) { /* … */ },
    std::chrono::milliseconds{10000});
handle.cancel();
```

---

## 10. Suggested mechanical migration recipe

For a codebase that does not use the shim, this ordered find-and-replace handles the bulk (review each — namespaces are context-sensitive, so apply within Kraken-only TUs):

1. Includes: `kraken_rest_client.hpp` → `exchange/kraken/rest_client.hpp`, etc. (table §3).
2. `kraken::ws::make_ws_client(` → `exchange::kraken::ws::make_kraken_ws_client(` (URL form) — then hand-fix any mock-connection form (§8).
3. `kraken::ws::` → `exchange::kraken::ws::`  *(do this before the next two so the generic types can be pulled back)*.
4. `kraken::rest::` → `exchange::kraken::rest::`.
5. `kraken::Side`/`kraken::OrderType`/`kraken::TimeInForce`/`kraken::OrderStatus` → `exchange::…` (and bare `kraken::TickPrice` etc. → `exchange::kraken::…`).
6. Build. The compiler flags any remaining `kraken::` reference; resolve each against the §4 table.

Because the generic types are re-exported into the `exchange::kraken::*` adapter namespaces (§4 note), steps 3–4 alone leave most code compiling; step 5 mops up the shared enums.
