# Coinbase FIX order entry (deferred)

**Status:** Proposed (deferred)
**Depends on:** [plan coinbase-exchange-adapter — Coinbase Exchange adapter](018-coinbase-exchange-adapter.md)
(REST + WebSocket) must land first.

This is a **design record**, not an actionable step list. FIX order entry is
deferred because plan coinbase-exchange-adapter already places and cancels orders over REST
(`POST`/`DELETE /orders`). FIX buys **lower-latency / higher-frequency order
entry** — value only when that is actually needed. When scheduled, expand §4 into
detailed, testable steps (the cadence of plan multi-exchange-abstraction and the playbook) before writing code.

---

## 1 — Why FIX, and why deferred

Coinbase Exchange has **no WebSocket order entry**. Orders go over either:
- **REST** — covered by plan coinbase-exchange-adapter; sufficient for correctness, fine for
  low/medium frequency. *(The v1 order path.)*
- **FIX Order Entry gateway** — a persistent session for latency-sensitive,
  high-throughput order flow.

Deferred now (decision, 2026-06): REST satisfies the functional need; FIX is an
optimization to revisit when latency/throughput demands it.

### Gateway facts (verified 2026-06)

| Aspect | Value |
|---|---|
| FIX 4.2 order entry | **Deprecated 2025-06-03** — do not target |
| **FIX 5.0 SP2 order entry (prod)** | `tcp+ssl://fix-ord.exchange.coinbase.com:6121` |
| FIX 5.0 SP2 order entry (sandbox) | `tcp+ssl://fix-ord.sandbox.exchange.coinbase.com:6121` |
| Transport | TLS over TCP (persistent session) |
| Session msgs | Logon `A`, Heartbeat `0`, Test Request `1`, Resend `2`, Reject `3`, Sequence Reset / Gap Fill `4`, Logout `5` |
| App msgs (order entry) | NewOrderSingle `D`, OrderCancelRequest `F`, OrderStatusRequest `H`, ExecutionReport `8`, OrderCancelReject `9` |
| Logon auth | `RawData` carries `base64(HMAC-SHA256(base64_decode(secret), prehash))` over the Coinbase-specified logon fields; passphrase sent in the password field — **reuses plan coinbase-exchange-adapter's `coinbase::rest::detail` signer** |

> Target **FIX 5.0 SP2 (FIXT.1.1 session)**. 4.2 is end-of-life.

---

## 2 — Why this is net-new for the library

Nothing in cryptocogs speaks FIX. Every existing surface is either request/response
HTTP (`CurlHttpClient`) or the ixwebsocket-backed `IWsConnection`. FIX adds, for
the first time:

- a **persistent TLS TCP** transport (not HTTP, not WebSocket framing);
- a **session layer**: sequence numbers, logon/logout, heartbeats/test-requests,
  resend & sequence-reset/gap-fill — none of which the WS/REST layers model;
- a **tag=value (SOH-delimited)** wire codec with a versioned field dictionary.

So FIX is a **new common scaffold** (`exchange::fix::`), a *peer* to
`exchange::ws::`, plus a Coinbase FIX adapter on top — mirroring how
`exchange::ws::` + the Coinbase WS adapter relate.

---

## 3 — Recommended approach: hand-rolled `exchange::fix::` scaffold

Chosen (at planning, 2026-06) over integrating **QuickFIX**, to keep the library
dependency-light and MIT-clean and to match its existing abstractions. QuickFIX
is a heavy FetchContent dependency with XML data dictionaries, a non-MIT license,
and a programming model foreign to the rest of the codebase; it was considered
and rejected for those reasons. *(Revisit only if hand-rolling the session layer
proves disproportionate.)*

The scaffold deliberately parallels the WS layer so the patterns and tests
transfer:

| WS layer (exists) | FIX layer (to build) | Role |
|---|---|---|
| `IWsConnection` | `IFixConnection` | abstract transport (TLS TCP); a `MockFixConnection` drives tests with **no sockets** |
| `IxWsConnection` | `OpenSslFixConnection` | real transport (OpenSSL TLS over a TCP socket; OpenSSL already linked) |
| `ExchangeWsClient` + `MessageIdentifier` | `FixSession` engine | session state machine: seqnums, logon/heartbeat/test-request, resend & gap-fill, logout |
| `WsReconnectSession` | reuse as-is | reconnect/backoff is protocol-agnostic |
| frame `to_json`/`from_json` | tag=value **codec** (`encode`/`decode`) + typed messages | wire ⇄ typed structs |
| `<name>_frame_descriptor` | `coinbase_fix_*` builders/parsers | NewOrderSingle/Cancel out; ExecutionReport/Cancel-Reject in |

**Coinbase FIX adapter** (`exchange::coinbase::fix`): logon signing (reusing
plan coinbase-exchange-adapter's `detail::hmac_sha256`/base64), `NewOrderSingle`/`OrderCancelRequest`
builders, `ExecutionReport`/`OrderCancelReject` parsers, and a typed
request→response binding echoing the WS `TypedWsRequest<R>` idiom. Reuse
`exchange::OrderType`/`Side`/`TimeInForce`/`OrderStatus` and `TickPrice`. New
peer lib `libcoinbase_fix.a` (or a `coinbase` sub-target) behind
`CRYPTOCOGS_BUILD_COINBASE_FIX` (default `ON`, implies `CRYPTOCOGS_BUILD_COINBASE`).

---

## 4 — Sketch of work (expand into steps when scheduled)

1. `exchange::fix::` codec — tag=value encode/decode, `BodyLength`/`CheckSum`,
   field-map message type. Tests: round-trip canned FIX strings (SOH-delimited).
2. `IFixConnection` + `MockFixConnection`. Tests: deterministic inject/observe.
3. `FixSession` engine — logon→ready, heartbeat/test-request, **seqnum tracking +
   resend/gap-fill + sequence-reset**, logout. Tests: drive every session
   transition via `MockFixConnection`, synchronous step injection (no sleeps).
4. Coinbase logon signing + `OpenSslFixConnection` (real TLS TCP).
5. Coinbase order-entry messages — `NewOrderSingle`/`OrderCancelRequest` build;
   `ExecutionReport`/`OrderCancelReject` parse. Tests vs. **synthetic** fixtures
   (no public vectors → pin construction, marked `synthetic`).
6. CMake wiring (`CRYPTOCOGS_BUILD_COINBASE_FIX`, full flag-matrix proof) + a
   live CLI example (logon, place, cancel) against the **sandbox** gateway.

---

## 5 — Self-review: risks & assumptions

**Risks**
- **Session-layer correctness** is the hard part — sequence reset, resend
  requests, and gap-fill are subtle and must be exactly right or the gateway
  drops the session. Mitigation: a thorough `MockFixConnection`-driven state-machine
  test matrix before any live attempt.
- **TLS TCP transport** is new code (OpenSSL `SSL_*` over a raw socket); keep it
  behind `IFixConnection` so all logic is testable without a network.
- **No public signing/test vectors** for logon or messages → pin construction
  with the same primitives (the precedent of plan coinbase-exchange-adapter) and mark fixtures `synthetic`.
- **Live testing needs credentials + sandbox** (Coinbase sandbox can be flaky);
  unit tests stay fully offline.
- **Scope creep:** FIX **market data** is *not* in scope — WS (plan coinbase-exchange-adapter) covers
  market data; this plan is **order entry only**.

**Assumptions**
- Target **FIX 5.0 SP2 / FIXT.1.1**, never the deprecated 4.2 gateway.
- Hand-rolled scaffold (no QuickFIX), consistent with §3.
- Built strictly on top of plan coinbase-exchange-adapter (reuses its enums, `TickPrice`, and signer);
  `exchange_common`/`exchange_http` and other adapters remain untouched —
  `exchange::fix::` is a **new** peer scaffold, not a modification of existing ones.
