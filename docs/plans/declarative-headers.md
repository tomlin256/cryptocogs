# Make all headers purely declarative (project-wide hpp/inl/cpp split)

**Status:** Done
**Branch:** `feature/declarative-headers`

## 1. Motivation

The C++ guideline (`guidelines/cpp.md` → *File Organisation — hpp/inl/cpp Split*) requires:

- **Non-templated code:** `.hpp` (declaration) + `src/<name>.cpp` (definition).
- **Templated code visible at instantiation:** `.hpp` + `.inl` (included at the bottom of the `.hpp`).
- **Never** put non-templated function bodies in `.inl` files.

Today most headers carry their implementation inline. The user has chosen the
**strict** scope: every header becomes purely declarative — template bodies move
to `.inl`, non-template bodies move to `.cpp` — with a small number of
explicitly-documented architectural carve-outs (Section 4).

This is a **behaviour-preserving** refactor. No logic changes. The existing
test suite (12 binaries / 312 tests) is the regression net and must stay green
at every checkpoint.

## 2. Audit — every header and where its bodies go

| Header | Templates → `.inl` | Non-template bodies → `.cpp` | New files |
|---|---|---|---|
| `common/http_client.hpp` | — | already clean (→ `http_client.cpp`) | none |
| `common/types.hpp` | — | enum converters (`to_string`/`*_from_string`, 9) | `src/exchange/common/types.cpp` |
| `common/tick_price.hpp` | — | `from()`, `str()`, `to_json()` (→ join existing `tick_price.cpp` which already has `from_json`) | none (existing `.cpp`) |
| `common/rest.hpp` | `RestResponse<T>`, `TypedPublicRequest<R>`, `TypedPrivateRequest<R>` | — | `rest.inl` |
| `common/ws.hpp` | `WsResponse<T>`, `TypedWsRequest<R>` | `SubscriptionHandle::is_active/cancel`, `RateLimitedWsErrorHandler::*` (currently mis-placed in `ws_client.inl`) | `ws.inl`, `src/exchange/common/ws.cpp` |
| `common/ws_client.hpp` / `.inl` | keep template bodies in `.inl` | **remove** the non-template bodies from `.inl` (→ `ws.cpp`) | — |
| `common/reconnect_session.hpp` / `.inl` | already correct | — | none |
| `common/ix_ws_connection.hpp` | — | **CARVE-OUT — stays header-only** (Section 4, D1) | none |
| `kraken/types.hpp` | `RestResponse<T>`, `parse_rest_response<T>` | enum converters, `kraken_order_type_*`, DTO `from_json` (`OrderInfo`/`TradeInfo`/`LedgerEntry`/…) | `kraken/types.inl`, `src/kraken/types.cpp` |
| `kraken/auth.hpp` | — | `Credentials::sign`, `make_nonce`, `detail::` crypto | `src/kraken/auth.cpp` |
| `kraken/rest_api.hpp` | base/typed request wrappers (if any have bodies) | every `build()` + response `from_json` (~77) | `kraken/rest_api.inl`*, `src/kraken/rest_api.cpp` |
| `kraken/rest_client.hpp` | `execute()` ×2 | — | `kraken/rest_client.inl` |
| `kraken/ws_api.hpp` | `TypedWsRequest<R>`, `TypedSubscribeRequest<PushMsg,Ch>` | `to_string(SubscribeChannel)`, `identify_message`, `kraken_frame_descriptor`, req `to_json`/resp `from_json` (~73) | `kraken/ws_api.inl`, `src/kraken/ws_api.cpp` |
| `kraken/ws_client.hpp` | — | `make_kraken_ws_client` (URL constants stay — `constexpr`) | `src/kraken/ws_client.cpp` |
| `binance/types.hpp` | — | enum converters + DTO (12) | `src/binance/types.cpp` |
| `binance/auth.hpp` | — | `BinanceAuth::sign`, `detail::hmac_sha256/to_hex` (3) | `src/binance/auth.cpp` |
| `binance/rest_api.hpp` | `parse_binance_response<T>`, typed request bases | `build()` + `from_json` (~34) | `binance/rest_api.inl`, `src/binance/rest_api.cpp` |
| `binance/rest_client.hpp` | `execute()` ×2 | — | `binance/rest_client.inl` |
| `binance/ws_streams.hpp` | `TypedStreamSubscribeRequest<PushMsg>` | stream-name helpers, `binance_stream_frame_descriptor`, event `from_json`, `make_binance_stream_client` (28) | `binance/ws_streams.inl`, `src/binance/ws_streams.cpp` |
| `binance/ws_api.hpp` | typed request templates (if bodied) | `parse_ws_api_envelope`, `binance_ws_api_frame_descriptor`, `ws_sign_params`, `ws_now_ms`, req `to_json`/resp `from_json`, `make_binance_ws_api_client` (16) | `binance/ws_api.inl`*, `src/binance/ws_api.cpp` |

\* If a file turns out to have **no** template body, its `.inl` is omitted (the
step verifies this rather than creating an empty file).

**Net:** ~10 new `.inl`, ~11 new `.cpp`, every header edited. The Binance WS
layer stops being header-only; `src/CMakeLists.txt` adapter targets and the
CLAUDE.md "header-only" notes are updated accordingly.

## 3. The split policy (applied uniformly)

**Moves to `.inl`** (re-opened namespace, banner, `#pragma once`, "do not include
directly" note, `#include`d at the very bottom of the `.hpp` after the namespace
close — exactly like `ws_client.hpp`/`.inl`):
- Every function template and every member of a class/struct **template**, including one-line accessors (`RestResponse<T>::has_error`, `route_key()`, `unsubscribe_json()`).
- SFINAE gotcha: a defaulted template parameter (`typename = std::enable_if_t<…>`)
  stays on the **declaration**; the out-of-line definition repeats the parameter
  list **without** the default and **names** the previously-unnamed parameter
  (e.g. `template<typename Req, typename Enable>`). Repeating the default on the
  definition is a hard error.

**Moves to `.cpp`** (compiled into the owning library target; banner; `inline`
keyword dropped on free functions so there is exactly one definition):
- Every non-template free function and non-template member function body.

**Stays in the `.hpp`** (these are declarations/data, not "function bodies"):
- Default member initializers (`int64_t ticks{0};`), `= default`, `= delete`,
  pure-virtual `= 0`, plain struct/enum definitions, `using` aliases/re-exports,
  `inline constexpr` URL/string-view constants, `static constexpr` members.

**Verification gate** (run at the final step, and ad-hoc per step):
```
grep -rnE '\)\s*(const\s+)?(noexcept\s*)?\{' include/exchange
```
must return **only** the documented `ix_ws_connection.hpp` carve-out lines.
(Default-initializers `{0}`, `struct {`, `enum {`, namespaces do not match `)\s*{`.)

## 4. Architectural carve-outs (decisions baked into the plan)

- **D1 — `ix_ws_connection.hpp` stays header-only.** It is the *only* header that
  `#include <ixwebsocket/IXWebSocket.h>`. Moving `IxWsConnection` to a `.cpp`
  would force a library that links ixwebsocket, but ixwebsocket is (a) fetched
  **only** under `CRYPTOCOGS_BUILD_TESTS` and (b) deliberately **not installed**
  (plan install-and-package-config). A downstream consumer that uses `IxWsConnection` relies on it being
  header-only (they link their own ixwebsocket). Keeping it header-only preserves
  the "core static libs are ixwebsocket-free" invariant and the install contract.
  The header gets an explicit comment marking it the documented exception.
  *(Alternative considered and rejected: a test-gated `libexchange_ix.a` — adds a
  5th lib, breaks downstream header-only use, contradicts plan install-and-package-config's "ix not
  installed". Available if the user prefers it.)*
- **D2 — Binance WS becomes compiled.** `make_*_client(conn)` factories,
  `*_frame_descriptor`, signing, and event `from_json` do **not** touch
  ixwebsocket (verified: the WS headers don't include `ix_ws_connection.hpp`), so
  they move into `libbinance.a` cleanly. Same for Kraken `make_kraken_ws_client`
  → `libkraken.a`. The `make_exchange_ws_client(url,…)` factory in
  `ix_ws_connection.hpp` is unaffected (it lives in the carved-out header).
- **D3 — Fix the pre-existing `.inl` violation.** `SubscriptionHandle::cancel()`
  and `RateLimitedWsErrorHandler::*` are non-template bodies currently in
  `ws_client.inl`; they move to `src/exchange/common/ws.cpp`.

## 5. Steps (each = one checkpoint commit; full build + `ctest` must pass before the next)

Build/test command on this machine (per project memory — OpenSSL via Homebrew):
```
cmake -B build -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3
cmake --build build -j
( cd build && ctest --output-on-failure )
```

- **Step 0 — Baseline.** Branch `feature/declarative-headers`. Configure, build,
  `ctest`. Record the green baseline (expect 312 tests). Add this plan + index row.
  *Done when:* baseline build + 312/312 green recorded.

- **Step 1 — Common scaffold.** `types.hpp`→`types.cpp`; `tick_price` `from()/str()/to_json()`→ existing `tick_price.cpp`; `rest.hpp`→`rest.inl`; `ws.hpp`→`ws.inl` + new `ws.cpp`; relocate the non-template bodies out of `ws_client.inl` into `ws.cpp` (D3). Add `types.cpp`/`ws.cpp` to `exchange_common`. Document the D1 carve-out comment in `ix_ws_connection.hpp` (no code move).
  *Covered by:* `test_tick_price`, `test_order_type`, `test_ws_client`, `test_ws_reconnect_session`, `test_ws_responses`.
  *Done when:* the four common headers have no function bodies; build + 312 green.

- **Step 2 — Kraken types + auth.** `kraken/types.hpp`→`kraken/types.inl` + `kraken/types.cpp`; `kraken/auth.hpp`→`kraken/auth.cpp`. Add both `.cpp` to the `kraken` target.
  *Covered by:* `kraken_unit_tests` (`test_signature`, `test_rest_responses`, `test_order_type`).
  *Done when:* both headers declarative; build + 312 green.

- **Step 3 — Kraken REST.** `kraken/rest_api.hpp`→ `rest_api.inl`(if needed)+`rest_api.cpp`; `kraken/rest_client.hpp`→`rest_client.inl`. Add to `kraken` target.
  *Covered by:* `test_rest_requests`, `test_rest_responses`, `test_client`.
  *Done when:* both declarative; build + 312 green.

- **Step 4 — Kraken WS.** `kraken/ws_api.hpp`→ `ws_api.inl`+`ws_api.cpp`; `kraken/ws_client.hpp`→`ws_client.cpp` (`make_kraken_ws_client`). Add to `kraken` target.
  *Covered by:* `test_ws_client`, `test_ws_responses`.
  *Done when:* both declarative; build + 312 green.

- **Step 5 — Binance types + auth.** Mirror of Step 2 for Binance. Add `binance/types.cpp`, `binance/auth.cpp` to the `binance` target.
  *Covered by:* `test_binance_types`, `test_binance_auth`.

- **Step 6 — Binance REST.** Mirror of Step 3. `binance/rest_api.{inl,cpp}`, `binance/rest_client.inl`.
  *Covered by:* `test_binance_rest_requests`, `test_binance_rest_responses`, `test_binance_client`.

- **Step 7 — Binance WS (ends header-only).** `binance/ws_streams.{inl,cpp}`, `binance/ws_api.{inl,cpp}`. Add both `.cpp` to the `binance` target; update the "No binance/ws_client.cpp … header-only" comment in `src/CMakeLists.txt`.
  *Covered by:* `test_binance_ws_client`.

- **Step 8 — Flag-matrix + install validation.** Build three configs —
  `-DCRYPTOCOGS_BUILD_BINANCE=OFF`, `-DCRYPTOCOGS_BUILD_KRAKEN=OFF`, both ON —
  each with `ctest`. Run a `cmake --install` smoke test into a temp prefix and
  confirm the `.inl` files install (they ride the existing
  `install(DIRECTORY include/exchange/*)` rules — no install-rule change needed)
  and a trivial `find_package(cryptocogs)` consumer links `cryptocogs::kraken` /
  `cryptocogs::binance`. Confirm no ixwebsocket symbols leak into
  `libexchange_common/http/kraken/binance` (`nm | grep -i ixwebsocket` empty).
  *Done when:* 147 (Binance-only) / 176 (Kraken-only) / 312 (both) all green; install consumer links.

- **Step 9 — Docs + gate.** Run the Section 3 grep gate (only the D1 carve-out
  remains). Update CLAUDE.md: project-structure file list, the "Binance WS …
  header-only" claims, "Kraken contributes no `.cpp` for the WS client" claim, and
  the coding-conventions paragraph about the lib sources. Flip this plan and the
  index row to **Done**.
  *Done when:* gate clean; CLAUDE.md accurate; docs updated.

## 6. Self-review — risks & assumptions

- **SFINAE default-arg on out-of-line template definitions** (the `execute()`
  overloads, any `enable_if`'d templates) — the single most likely compile break;
  handled by the Section 3 rule. Build after each file catches it immediately.
- **`inline` removal / ODR.** Free functions moved to `.cpp` must drop `inline`
  and exist in exactly one TU. Risk: a function defined in a `.cpp` but also still
  `#include`d elsewhere → multiple-definition link error. Per-step link is the check.
- **ixwebsocket invariant.** Step 8 asserts no ix symbols enter the core libs
  (`nm` check). D1 keeps the only ix-bound header header-only.
- **Inlining / performance.** Moving hot `from_json`/`to_json` to `.cpp` loses
  cross-TU inlining. For a REST/WS client library this is immaterial; noted for
  completeness. (If a future hot path needs it, that struct can move to `.inl`.)
- **Coverage gaps.** `ix_ws_connection` has no unit test (exercised only by
  examples) — it isn't moved (D1), so no new gap. Everything that moves to `.cpp`
  (`build()`, `from_json`, `execute()`, frame descriptors, signing) is already
  exercised by the existing suites mapped per step; this is a move, not new logic,
  so no new unit tests are required — but `ctest` must be green at every checkpoint,
  no exceptions.
- **Banners.** Every new `.inl`/`.cpp` gets the cryptocogs banner; `.inl` files
  also get `#pragma once` + "do not include directly" (matching `ws_client.inl`).
- **CMake/install.** New `.cpp` are added to existing targets (installed via
  `TARGETS`); new `.inl`/`.hpp` ride the existing un-filtered
  `install(DIRECTORY …)` rules — **no new install rule needed**. Verified in Step 8.
- **Big mechanical diff → review/merge risk.** Mitigated by per-file checkpoints,
  the grep gate, and behaviour-preservation (no logic edits inside moves).
- **Assumption:** strict scope as chosen; D1 (and the ix-touching inline URL
  factory) are accepted documented exceptions. If the user wants D1 done as a
  separate `libexchange_ix.a` instead, that's a one-step addition.

## 7. Out of scope

No behavioural changes, no API/signature changes, no new endpoints/channels, no
move of `ix_ws_connection.hpp` to a `.cpp` (D1). Plan generic-rest-client (generic `RestClient`)
is independent and unaffected.
