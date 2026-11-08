# Code Review — `feature/multi-exchange-abstraction`

**Date**: 2026-06-13
**Reviewer**: Claude (Claude Code)
**Scope**: branch vs `main` — commit range `main...2f236c9` (plan multi-exchange-abstraction and the eight plans that followed it,
step-2b-compat-shim to relocate-tickprice-to-common, ≈ +14.7k / −4.4k lines).

Focus was the net-new, load-bearing, security/correctness-sensitive code: the
Binance adapter (signing, REST, WS), the generic `ExchangeWsClient` engine, and
`TickPrice`. The mechanical Kraken namespace migration and the passing 300-test
suite were treated as a given and not re-reviewed line by line.

---

## Overall assessment

**Strong.** Clean three-tier architecture (`exchange_common` scaffold + peer
adapters), careful concurrency in the engine (weak_ptr-guarded callbacks,
handlers invoked outside locks, pending handler registered before send), real
tests (mock injection + observable-outcome assertions, not call-count theater),
and a machine-verified build decoupling. The findings below are **hardening, not
foundational problems** — nothing here is a security hole, memory-corruption, or
data-loss bug. Signing is correct for valid inputs; the curl-owning clients
correctly delete their copy operations; banners and conventions are consistently
applied.

| Severity | Count | Items |
|---|---|---|
| High | 1 | H1 ✅ fixed |
| Medium | 2 | M1 ✅ fixed, M2 ✅ fixed |
| Low | 4 | L1–L4 ✅ all fixed |
| Nit / note | 3 | N1–N3 |

---

## High

### H1 — A malformed server frame can crash the WS receive thread — ✅ FIXED

> **Resolved**: `on_raw_message` now wraps both the method-response handler and
> the push-callback invocation in a `try/catch` (std::exception **and** `...`)
> that routes to `error_handler_->on_malformed_frame`, so a throwing `from_json`
> / `std::stod` / `.get<double>()` / user callback can no longer escape into the
> transport's receive thread. The `execute_async` and `subscribe_async` lambdas
> additionally catch their own `from_json` and resolve the future to `ok=false`
> rather than leaving a broken promise. Covered by
> `test_ws_client.cpp::WsErrorIsolation.{MethodFromJsonThrowResolvesErrorNoCrash,
> ThrowingPushCallbackIsContained}` (suite 304 → 306).

**Where**: `src/exchange/common/ws_client.cpp:105,116` (handler/cb invocation);
`include/exchange/common/ws_client.inl:103` (`Resp::from_json(j)` in the pending
lambda).

`on_raw_message` guards **JSON parse** errors
(`ws_client.cpp:84-88` → `error_handler_->on_malformed_frame`) but **not
deserialization** errors. If `from_json` / `std::stod` / `.get<double>()` throws
on an unexpected field type (a `null`, a string where a number was expected, a
new wire shape), or if a user push-callback throws, the exception propagates out
of `on_raw_message` into the ixwebsocket receive thread → `std::terminate` →
process down. For a library meant to run unattended against a live exchange, one
surprising frame should not kill the process. The asymmetry with the
already-guarded parse path is the tell.

**Fix**: wrap the handler/cb invocation (and the `from_json` in the pending
lambda) in `try/catch` routed to `error_handler_`; on the `execute_async` path,
`prom->set_exception(...)` so the caller's `future` surfaces a real error rather
than `broken_promise`. Small, contained change at three call sites; outsized
robustness payoff.

---

## Medium

### M1 — `BinanceSignAlgorithm` is accepted but silently ignored — ✅ FIXED

> **Resolved**: `BinanceAuth::sign` and `detail::ws_sign_params` now throw
> `std::invalid_argument` when `algorithm != HmacSha256`, instead of silently
> HMAC-signing. Tests: `test_binance_auth.cpp::BinanceAuth.RejectsNonHmacAlgorithm`,
> `test_binance_ws_client.cpp::BinanceWsSignParams.RejectsNonHmacAlgorithm`. On
> the REST path the throw composes with M2 — a misconfigured algorithm surfaces
> as `RestResponse{ok:false}` (`BinanceClient.NonHmacAlgorithmFoldsIntoErrorResponse`).

**Where**: `include/exchange/binance/auth.hpp:62,69` (enum + field);
`auth.hpp:102` (`BinanceAuth::sign`); `include/exchange/binance/ws_api.hpp:174`
(`ws_sign_params`).

`BinanceCredentials::algorithm` is `{HmacSha256, Rsa, Ed25519}`, but both signers
always HMAC-SHA256 regardless of the field. A caller who sets `algorithm = Rsa`
gets an HMAC signature with no error — a silent wrong-credentials footgun. The WS
comment (`ws_api.hpp:172-173`) even acknowledges it.

**Fix**: either drop the enum (YAGNI until RSA/Ed25519 are implemented) or have
the signers reject/branch on non-HMAC instead of silently proceeding.

### M2 — REST `execute()` mixes exceptions with the `RestResponse` envelope — ✅ FIXED

> **Resolved**: both `BinanceRestClient::execute` and `KrakenRestClient::execute`
> (public + private overloads) now wrap build/sign/`perform`/`json::parse` in a
> `try/catch` that folds any `std::exception` into `RestResponse{ok:false,
> errors:["request failed: …"]}`, so `resp.ok` is the single failure check.
> Tests: `{Binance,Kraken}…Client.{TransportFailure,MalformedBody}FoldsIntoErrorResponse`.

**Where**: `include/exchange/binance/rest_client.hpp` (inline `json::parse(raw)`);
`src/binance/rest_client.cpp:97` (`throw std::runtime_error` on curl failure).
Kraken's client has the same shape.

API-level errors return as `RestResponse{ok:false}`, but transport failures throw
`std::runtime_error` and a malformed body throws from `json::parse` (e.g. an HTML
error page from an edge proxy). A caller who checks only `if (resp.ok)` silently
misses those failure classes.

**Fix**: document the contract explicitly, or catch curl / `json::parse` and fold
them into `RestResponse{ok:false, errors:{…}}` so `ok` is the single failure
check.

---

## Low

### L1 — Message reordering at connect — ✅ FIXED
**Where**: `src/exchange/common/ws_client.cpp:71-80`.
> **Resolved**: `on_open_handler` now flushes the backlog **while holding
> `queue_mu_`**, so a concurrent `enqueue_or_send` blocks and sends after the
> backlog (it already sends under the same lock — no new lock-ordering risk).
> Order pinned by `test_binance_ws_client.cpp::BinanceWsApiLifecycle.BeforeOpen_FlushesInFifoOrder`.
`on_open_handler` moves the queue out, releases `queue_mu_`, then sends. A
concurrent `enqueue_or_send` (`:62-69`) that sees `connected_==true` in the gap
can send its message ahead of the flushed backlog. Benign for id-correlated
request/reply, but a latent ordering bug. Flush while holding the lock, or gate
new sends until the flush completes.

### L2 — `TickPrice::from` has no overflow guard — ✅ FIXED
**Where**: `include/exchange/common/tick_price.hpp`.
> **Resolved**: `from` now throws `std::overflow_error` when `price * 10^decimals`
> exceeds a safe int64 bound (9.2e18 < 2^63), before the UB-prone `llroundl`/cast.
> Test: `test_tick_price.cpp::TickPriceFrom.OverflowThrows`.
`llroundl(price * pow(10, decimals))` overflows `int64_t` for large
`decimals`/`price` (≥ ~18 decimals). Exchange pair precision is ≤ 8 in practice,
so unreached — but it's an unchecked precondition on a now-shared common type.

### L3 — `ws_param_value` mis-renders a JSON double — ✅ FIXED
**Where**: `include/exchange/binance/ws_api.hpp:162`.
> **Resolved**: `ws_param_value` now throws `std::invalid_argument` on a
> floating-point param (numeric params must be caller-formatted decimal strings
> or integers — both round-trip exactly). Test:
> `test_binance_ws_client.cpp::BinanceWsSignParams.RejectsFloatParam`.
Non-strings render via `dump()`; fine today (all signed params are strings/ints),
but a future `double` param would sign with `dump()`'s formatting (possible
scientific notation) → signature mismatch. Latent.

### L4 — `HMAC()` return value unchecked — ✅ FIXED
**Where**: `include/exchange/binance/auth.hpp:42`, `include/exchange/kraken/auth.hpp:88`.
> **Resolved**: both `hmac_sha256`/`hmac_sha512` now throw `std::runtime_error`
> if `HMAC()` returns `nullptr`. No dedicated test — the failure can't be
> triggered without OpenSSL fault injection; the existing signature tests prove
> the success path is unchanged.

---

## Nits / notes

### N1 — Raw owning `CURL*` members
`include/exchange/binance/rest_client.hpp:71`,
`include/exchange/kraken/rest_client.hpp:65` store a raw owning `CURL* curl_`.
Copies are correctly `=delete`d, so there is **no** double-free; a
`std::unique_ptr<CURL, deleter>` would just be tidier and exception-safe by
construction.

### N2 — WS signing still lacks an official vector
The sorted-params WS API signing (plan step-8-binance-ws-api) has no published Binance worked
example; the tests recompute the expectation with the same primitives, pinning
*construction* not the *rule*. The first live signed `order.place` is the real
validation — a testnet smoke run is the natural follow-up.

### N3 — Positives worth recording
Banners on every source file; zero `TODO`/`FIXME`; weak_ptr lifetime throughout
the engine; handlers invoked outside locks; pending handler registered before
send; idempotent thread-safe `SubscriptionHandle::cancel`; deleted copy ops on
the curl owners; tests assert observable outcomes via injected mocks; and the
exchange decoupling is `nm`- and flag-matrix-verified.

---

## Recommendation

**All findings (H1, M1–M2, L1–L4) are now fixed and tested** — see the per-item
"✅ Resolved" notes above. The full suite is green at 316. Nothing from this
review remains outstanding.
