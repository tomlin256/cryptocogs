# Step 2b: Ship the Kraken Backwards-Compatibility Shim

> **MANDATORY — Branch and commit discipline** (inherited from plan multi-exchange-abstraction)
>
> All work continues on `feature/multi-exchange-abstraction`. Commit at the
> completion of every phase (and at meaningful sub-step checkpoints). Every
> checkpoint must include a full build and full `ctest` run — both green —
> before moving on.

**Goal**: Implement [Step 2b](001-multi-exchange-abstraction.md#step-2b--ship-the-kraken-backwards-compatibility-shim) of plan multi-exchange-abstraction — ship the deprecated `kraken::` compatibility shim described in [001-appendix-compat-shim.md](001-appendix-compat-shim.md), so existing `cryptocogs` consumers compile and run unchanged against the refactored library.

---

## Status / progress

| Phase | State |
|---|---|
| Phase 1 — consumer migration to `exchange::kraken::` | **Done** — `afc0de7`, `a6b82e3` |
| Phase 2 — shim machinery (thin forwarders + `kraken_compat.hpp`, delete duplicate `.cpp`s) | **Done** — `724846f` |
| Phase 3 — compat compile-proof + behavioural tests | **Done** — `test_compat_shim.cpp` (see the as-implemented note at the end of Phase 3) |

**Done elsewhere / superseded**:
- **`install()` / packaging** — was punted here, then done in [plan install-and-package-config](012-install-and-package-config.md): `cmake --install` ships the libs, public headers, and a `find_package(cryptocogs)` package config; the shim headers are installed when `CRYPTOCOGS_BUILD_COMPAT_SHIM` is on.
- **The `tests/compat/` example-relocation** (original Phase 3.1) — superseded; see the as-implemented note.

The default `CRYPTOCOGS_BUILD_COMPAT_SHIM=ON` now does real work: it gates the
compile-proof. The remaining phase text below is the original plan; Phase 3 was
implemented in a simpler, more thorough form.

---

## Why this plan is bigger than the appendix's bullet list

Before drafting this plan I audited the actual repo state against Step 2's stated "done when" criteria (`docs/plans/001-multi-exchange-abstraction.md` lines 558–570: *"All Kraken code lives under `exchange::kraken::*`; all tests pass"* + *"Update `tests/unit/` and `tests/examples/` includes and namespace references to the new API"*). That migration **did not happen**:

| Check | Finding |
|---|---|
| `grep -rl "exchange::kraken" tests/` | **zero matches** — every unit test and example still uses `kraken::` / old `kraken_*.hpp` includes |
| `kraken_types.hpp`, `kraken_rest_api.hpp`, `kraken_rest_client.hpp`, `kraken_ws_api.hpp` | still **full duplicate implementations** of `kraken::`/`kraken::rest::`/`kraken::ws::` (not the "Step 1 re-export shim" form) |
| `src/kraken_types.cpp`, `src/kraken_rest_client.cpp` | full duplicates of `src/kraken/types.cpp`, `src/kraken/rest_client.cpp` |
| `src/kraken_ws_client.cpp` | **not a duplicate** — it is the live, load-bearing implementation of the new `exchange::ws::ExchangeWsClient`, just sitting at the old filename/path |

So the build currently compiles two complete parallel copies of every Kraken REST/WS-protocol type. The appendix's shim design *assumes* migration is finished — converting the old headers into `[[deprecated]]`-emitting forwarders today would immediately break the existing test suite (it still references the very symbols being deleted).

Per Rob's direction, **this plan folds the missing migration into Step 2b** as Phase 1, then builds the shim on the now-clean foundation in Phases 2–3. Each phase is its own checkpoint.

## Naming corrections — verified against actual code, not the (stale) appendix docs

Both `001-appendix-compat-shim.md` and `001-appendix-migration-guide.md` were written against an earlier name for the generic WS client and reference a function that turned out to serve a different purpose. **Grep-verified actual names** (these are what this plan — and the code it produces — will use):

| Appendix says | Actual code has | Evidence |
|---|---|---|
| `exchange::ws::GenericWsClient` | `exchange::ws::ExchangeWsClient` | `include/exchange/common/ws_client.hpp:44` |
| `exchange::ws::make_generic_ws_client(conn, identifier)` | `exchange::ws::make_exchange_ws_client(conn, identifier, error_handler)` | `include/exchange/common/ws_client.inl:198` |
| `exchange::kraken::ws::identify_message` (as the `MessageIdentifier` passed to the factory) | `exchange::kraken::ws::kraken_frame_descriptor` — a **separate** function returning `FrameDescriptor`. `identify_message` also exists but returns the legacy `MessageKind` enum (kept for low-level raw-frame dispatch per `CLAUDE.md`'s documented API) and is *not* what `make_exchange_ws_client` wants. | `include/exchange/kraken/ws_api.hpp:1088` (`kraken_frame_descriptor`) vs. `:1045` (`identify_message` → `MessageKind`) |

Anywhere this plan needs to write `kraken_compat.hpp` content, it uses the verified names above — **not** the appendix's draft snippets verbatim.

---

## Phase 1 — Complete Step 2's consumer migration

**Done when**: every file in `tests/unit/` and `tests/examples/` *except* the two preserved originals (below) compiles against `exchange::kraken::*`; `grep -rl "kraken::" tests/` returns only those two files; `src/kraken_ws_client.cpp` lives at its architecturally-correct path; full build + `ctest --output-on-failure` green.

### 1.1 — Migrate 8 unit test files
`test_client.cpp`, `test_rest_requests.cpp`, `test_rest_responses.cpp`, `test_signature.cpp`, `test_tick_price.cpp`, `test_ws_client.cpp`, `test_ws_reconnect_session.cpp`, `test_ws_responses.cpp`

Apply the mechanical recipe from [001-appendix-migration-guide.md §10](001-appendix-migration-guide.md#10-suggested-mechanical-migration-recipe), **with the name corrections above substituted**:
1. Includes: `kraken_*.hpp` → `exchange/kraken/*.hpp` per §3's table.
2. `kraken::ws::` → `exchange::kraken::ws::`, `kraken::rest::` → `exchange::kraken::rest::`.
3. Shared enums: `kraken::Side` / `OrderType` / `TimeInForce` / `OrderStatus` / bare `kraken::TickPrice` etc. → `exchange::…` / `exchange::kraken::…` per §4.
4. **`test_ws_client.cpp` specifically** (6 call sites at lines 76, 543, 571, 691, 764, 811): `kraken::ws::make_ws_client(conn)` → `exchange::ws::make_exchange_ws_client(conn, exchange::kraken::ws::kraken_frame_descriptor)`, and `MockWsConnection : public kraken::ws::IWsConnection` → `exchange::ws::IWsConnection` (§8 of the guide, corrected per the naming table above — the guide's own `make_generic_ws_client` example would not compile).
5. Build; resolve any remaining `kraken::` reference the compiler flags.

**Tests**: these files *are* the test suite — migrating them in place is the change. No new test files. Gate: `ctest --output-on-failure` green with the migrated sources, identical pass/fail set to before (a behaviour-preserving rename, not a logic change).

### 1.2 — Migrate 5 example files
`kraken_example.cpp`, `private_rest.cpp`, `private_ws.cpp`, `public_rest.cpp`, `public_ws.cpp` — same recipe. These compile (examples aren't run by `ctest`); the gate is a green build.

### 1.3 — Preserve 2 example files **as-is**
`rest_client_example.cpp` and `ws_client_example.cpp` are **not migrated**. Confirmed byte-identical to their pre-refactor originals (`git diff --stat 8bc0316 HEAD -- tests/examples/{rest_client_example,ws_client_example}.cpp` → empty). They become the verbatim `tests/compat/` compile-proof in Phase 3 — untouched, they still build against the (currently still-full) old headers at this checkpoint, and will build through the shim once Phase 2 lands.

### 1.4 — Relocate `src/kraken_ws_client.cpp`
This file implements `exchange::ws::ExchangeWsClient::{init, set_on_disconnect, cancel_subscription, enqueue_or_send, on_open_handler, on_raw_message}` — generic common-scaffold code with zero Kraken logic, confirmed by its own `namespace exchange::ws {` and `#include "exchange/common/ws_client.hpp"`. It belongs beside its header.

- `git mv src/kraken_ws_client.cpp src/exchange/common/ws_client.cpp`
- Update `src/CMakeLists.txt`: remove `kraken_ws_client.cpp` from the legacy-block list, add `exchange/common/ws_client.cpp` (a new top-level source, outside both the `# Legacy` and `# New exchange::kraken::` comment groups — those comments will themselves be cleaned up in 1.5).

### 1.5 — Delete the now-dead duplicate REST/types implementation
Once 1.1–1.2 land, nothing references `kraken::`/`kraken::rest::` symbols except the two Phase-1.3 files — and *those* still compile against the old (still-full) headers, which still provide the symbols `src/kraken_types.cpp` / `src/kraken_rest_client.cpp` define. **Do not delete these `.cpp`s yet** — Phase 2 makes them genuinely dead (by turning `KrakenRestClient` into a type alias) and removes them then. Phase 1's `.cpp` changes are limited to 1.4's relocation.

- Update `src/CMakeLists.txt` comments to reflect the new state honestly (the "Legacy kraken:: namespace implementations (shim support)" comment is misleading — `kraken_ws_client.cpp` was never that; say so plainly, e.g. "Old kraken:: implementations — retired in Phase 2 of plan step-2b-compat-shim" pending that phase).

### Checkpoint commit
`feat: step 2b phase 1 — finish migrating tests/examples to exchange::kraken::`
Full build + `ctest --output-on-failure` green. At this point `kraken::` appears **only** in the two preserved example files and the (still-full, untouched) old headers that serve them.

---

## Phase 2 — Build the shim machinery

**Done when**: the 7 old-path headers are uniform thin forwarders; `kraken_compat.hpp` exists with verified-correct names; the dead old `.cpp` implementations are gone; the two preserved examples (1.3) now compile **through the shim** (their build success *is* the live compile-proof — formalized as a dedicated target in Phase 3); full build + `ctest` green.

### 2.1 — Add the CMake option
In top-level `CMakeLists.txt`, alongside the existing options:
```cmake
option(CRYPTOCOGS_BUILD_COMPAT_SHIM
       "Install deprecated kraken_*.hpp compatibility headers (source-compat for pre-refactor callers)"
       ON)
```
(Plan multi-exchange-abstraction §F lists this as one of four eventual options; the other three — `CRYPTOCOGS_BUILD_KRAKEN/BINANCE/TESTS` — and the dependency-rule guards belong to Step 8, not here. Adding only this one, scoped to what 2b needs, keeps the change minimal and avoids speculatively wiring guards for libraries that don't exist yet.)

### 2.2 — Create `include/kraken_compat.hpp` (Layer 2 — the namespace shim)
Reopens `namespace kraken { … }` on top of the new layout. Structure per the appendix's design, **with verified names**:

- Bulk re-exports via `using` for shared enums (`exchange::Side` etc.) and Kraken-specific structs (`exchange::kraken::TickPrice`, `OrderParams`, …).
- `namespace rest { using namespace exchange::kraken::rest; using exchange::rest::RestResponse; using exchange::rest::HttpRequest; … }` — explicit qualification of the generic bases (belt-and-suspenders per the appendix; harmless given Step 2's partial re-exports already pull some of these in).
- `namespace ws { using namespace exchange::kraken::ws; using exchange::ws::IWsConnection; … }`.
- `using KrakenWsClient [[deprecated("use exchange::ws::ExchangeWsClient")]] = exchange::ws::ExchangeWsClient;`
- **Two `[[deprecated]]` `make_ws_client` forwarders** — the crux of why this needs real namespaces, not aliases:
  ```cpp
  [[deprecated("use exchange::kraken::ws::make_kraken_ws_client; see migration guide")]]
  inline std::shared_ptr<exchange::ws::ExchangeWsClient>
  make_ws_client(const std::string& url,
                 std::shared_ptr<exchange::ws::IWsErrorHandler> eh = nullptr) {
      return exchange::kraken::ws::make_kraken_ws_client(url, std::move(eh));
  }

  [[deprecated("use exchange::ws::make_exchange_ws_client(conn, kraken_frame_descriptor)")]]
  inline std::shared_ptr<exchange::ws::ExchangeWsClient>
  make_ws_client(std::shared_ptr<exchange::ws::IWsConnection> conn,
                 std::shared_ptr<exchange::ws::IWsErrorHandler> eh = nullptr) {
      return exchange::ws::make_exchange_ws_client(
                 std::move(conn), exchange::kraken::ws::kraken_frame_descriptor,
                 std::move(eh));
  }
  ```
- `using exchange::kraken::rest::parse_rest_response;` at the `kraken::` level (it was reachable unqualified via `using namespace kraken` pre-refactor).

### 2.3 — Rewrite the 7 old-path headers as uniform thin forwarders (Layer 1)
Replace the body of each with the canonical pattern (the three already-shimmed ones get **simplified** to match — consolidating their ad-hoc inline `using`/forwarder logic into `kraken_compat.hpp` from 2.2, so there is one source of truth instead of two shim styles):

```cpp
#pragma once
#ifndef CRYPTOCOGS_SUPPRESS_DEPRECATION
#  pragma message("kraken_rest_client.hpp is deprecated; include exchange/kraken/rest_client.hpp. See docs/plans/001-appendix-migration-guide.md (removed in vNEXT_MAJOR).")
#endif
#include "exchange/kraken/rest_client.hpp"
#include "kraken_compat.hpp"
```

Files: `kraken_types.hpp`, `kraken_rest_api.hpp`, `kraken_rest_client.hpp`, `kraken_ws_api.hpp`, `kraken_ws_client.hpp`, `kraken_ix_ws_connection.hpp`, `ws_reconnect_session.hpp` — each pointing its `#include` at the corresponding new-layout header per the §3 table (e.g. `kraken_ix_ws_connection.hpp` → `exchange/common/ix_ws_connection.hpp` + `exchange/kraken/ws_client.hpp`, since `IxWsConnection` itself lives in common but `make_kraken_ws_client` is Kraken-specific).

### 2.4 — Delete the now-dead old `.cpp` implementations
With `KrakenRestClient` / Kraken types now type-aliases into `exchange::kraken::*`, nothing defines or needs `kraken::rest::KrakenRestClient::execute` or `kraken::AssetInfo::from_json` etc. as distinct symbols:
- `git rm src/kraken_types.cpp src/kraken_rest_client.cpp`
- Update `src/CMakeLists.txt` — remove both from the source list; the comment block from 1.5 collapses to a single accurate "exchange::kraken:: implementations" grouping plus the relocated common-scaffold entry from 1.4.

### Checkpoint commit
`feat: step 2b phase 2 — convert legacy kraken_*.hpp paths to the deprecated compat shim`
Full build + `ctest --output-on-failure` green — **including** `rest_client_example` and `ws_client_example` (Phase 1.3's untouched originals), which now compile and link entirely through `kraken_compat.hpp`. Their continued green build *is* the transparency proof; Phase 3 gives it a permanent home and a name.

---

## Phase 3 — Compat tests (formalize the proof)

**Done when**: `tests/compat/` exists with the preserved originals as a dedicated compile-proof target; `tests/unit/test_compat_shim.cpp` behaviourally exercises both `make_ws_client` forwarders and the REST round-trip; everything is gated on `CRYPTOCOGS_BUILD_COMPAT_SHIM`; a clean reconfigure with the option **OFF** builds the library + new-API tests with the old paths verifiably absent.

### 3.1 — `tests/compat/`
- `git mv tests/examples/rest_client_example.cpp tests/examples/ws_client_example.cpp tests/compat/`
- Add a `tests/compat/CMakeLists.txt` (or block in `tests/CMakeLists.txt`) that compiles both as object libraries / executables **with `-DCRYPTOCOGS_SUPPRESS_DEPRECATION`** (per the appendix — keeps the suite warning-clean while still proving the surface compiles). Link against `cryptocogs spdlog::spdlog CLI11::CLI11 example_backward` exactly as they did at their old `tests/examples/` location — zero source edits, only their CMake registration moves.
- Remove their old `add_executable` blocks from `tests/CMakeLists.txt`.

*(Optional, noted in self-review below rather than required: a second one-line target compiling one of the two **without** the suppress macro, proving the `#pragma message` actually fires — appendix calls this "optional".)*

### 3.2 — `tests/unit/test_compat_shim.cpp`
New GoogleTest file, three behavioural assertions per [the appendix §5](001-appendix-compat-shim.md#5-tests--proving-the-shim-is-transparent), built with `-DCRYPTOCOGS_SUPPRESS_DEPRECATION`:

1. **Public REST round-trip** — `make_test_client(...)` + `kraken::rest::GetServerTimeRequest{}` → assert `resp.ok` and parsed `unixtime` field, mirroring `test_client.cpp`'s existing pattern but through the old namespace.
2. **WS subscribe via the mock-connection forwarder** — `MockWsConnection` (reuse the one now living in the migrated `test_ws_client.cpp`, or a minimal local equivalent — see self-review) injected through **`kraken::ws::make_ws_client(conn)`** (exercises the `[[deprecated]]` conn-form forwarder from 2.2) → `fire_open()`, `inject_message(<captured ticker frame>)`, assert the callback fires with a populated `kraken::ws::TickerMessage`.
3. **`static_assert`** that `kraken::ws::make_ws_client(std::string{})` resolves to `std::shared_ptr<exchange::ws::ExchangeWsClient>` (compile-time only, no network — proves the URL-form overload resolution is unambiguous).

All-mock, deterministic, no `sleep` — consistent with `[[guidelines/cpp.md]]`'s "Tests Must Be Deterministic" and the project's existing `MockWsConnection` pattern.

### 3.3 — Wire `tests/CMakeLists.txt`
Wrap 3.1 and 3.2's registrations in `if(CRYPTOCOGS_BUILD_COMPAT_SHIM) … endif()`. Add `test_compat_shim` to `tests/unit/CMakeLists.txt` (links `cryptocogs GTest::gtest_main`, compiled with `target_compile_definitions(... PRIVATE CRYPTOCOGS_SUPPRESS_DEPRECATION)`), registered via `gtest_discover_tests`.

### 3.4 — Verify the OFF path
`cmake -B build-shim-off -DCRYPTOCOGS_BUILD_COMPAT_SHIM=OFF && cmake --build build-shim-off` — confirm:
- None of the 7 old-path headers, `kraken_compat.hpp`, `tests/compat/*`, or `test_compat_shim` targets exist/build.
- The library + all new-API (`exchange::kraken::`) tests still build and pass.

This is the "prove you've migrated" signal the appendix promises clients — worth running explicitly as part of this step's own done-criteria, not just describing it for others.

### Checkpoint commit
`feat: step 2b phase 3 — add compat compile-proof and behavioural shim tests`
Full build + `ctest --output-on-failure` green with the shim ON; documented OFF-path verification (3.4) run and confirmed clean.

### Done — as implemented (diverges from the 3.1/3.2 sketch above)

Rather than relocating the two preserved examples into `tests/compat/` (3.1) and
splitting the proof across files, Phase 3 was implemented as a **single, more
thorough** `tests/unit/test_compat_shim.cpp`, gated `if(CRYPTOCOGS_BUILD_KRAKEN
AND CRYPTOCOGS_BUILD_COMPAT_SHIM)` and compiled with
`CRYPTOCOGS_SUPPRESS_DEPRECATION` (so the forwarders' `#pragma message` stays
quiet), linking `cryptocogs ixwebsocket GTest::gtest_main`:

- **Compile-proof**: it `#include`s **all seven** old-path forwarders
  (`kraken_types/rest_api/rest_client/ws_api/ws_client/ix_ws_connection.hpp` +
  `ws_reconnect_session.hpp`) — strictly broader than the two examples' subset,
  and the reason the file links ixwebsocket (`kraken_ix_ws_connection.hpp` pulls
  in `IxWsConnection`). If any forwarder or a `kraken_compat.hpp` alias stops
  resolving against the new layout, this fails to compile — exactly the guard
  the TickPrice → `exchange::` move (plan relocate-tickprice-to-common) would have needed.
- **Forwarding identity** `static_assert`s: `kraken::{Side,OrderType,TickPrice,
  OrderParams}`, `kraken::rest::KrakenRestClient`, `kraken::ws::{KrakenWsClient,
  WsReconnectSession}` are the same types as their `exchange::…` originals.
- **Behavioural** (4 GoogleTests): the deliberate hyphenated `kraken::to_string(
  OrderType::StopLoss) == "stop-loss"` (the converter-overload trap
  `kraken_compat.hpp` warns about), `kraken::TickPrice` round-trip, a REST
  round-trip through `kraken::rest::make_test_client` + `GetServerTimeRequest`,
  and a WS client built through the **`[[deprecated]]` `kraken::ws::make_ws_client(
  conn)`** forwarder sending a `kraken::ws::PingRequest` via the shared
  `MockWsConnection`.
- **OFF-path verified**: `-DCRYPTOCOGS_BUILD_COMPAT_SHIM=OFF` drops the
  `test_compat_shim` target and builds the library + new-API tests clean (304
  tests with the shim ON → 300 with it OFF).

Implemented in commit (this phase); the original 3.1 `tests/compat/` relocation
and 3.2's separate file split are superseded by the above and were **not** done.

---

## Self-Review — Risks, Assumptions, Open Questions

### Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Mechanical migration (Phase 1) misses a `kraken::` reference the compiler doesn't catch (e.g. inside a string, comment, or `using namespace` that silently resolves to the wrong overload) | Low–Medium | The build is the gate — anything the compiler can catch, it will. For `using namespace kraken::rest` ambiguities specifically: Step 2's existing re-exports (`using exchange::rest::HttpRequest;` etc., confirmed present in `exchange/kraken/rest_api.hpp` / `ws_client.hpp` / `ws_api.hpp`) mean most dual-resolution risk is already retired — the namespaces were designed to coexist. |
| `[[deprecated]]` warnings firing inside our **own** `tests/compat/` build (intentional — they're calling deprecated names on purpose) could be mistaken for new bugs by anyone watching CI output, or could trip `-Werror` if the project ever adds it | Low | `-DCRYPTOCOGS_SUPPRESS_DEPRECATION` on those specific targets (3.1, 3.2) keeps that build leg warning-clean; the *absence* of suppression on a deliberate one-line proof target (noted as optional in 3.1) is the intentional, documented exception. |
| `exchange_common` is specced in plan multi-exchange-abstraction §F as a future `INTERFACE` (header-only) target, yet `src/exchange/common/ws_client.cpp` (Phase 1.4's relocation target) clearly needs to compile *somewhere* with real object code | Medium (pre-existing design tension, not introduced here) | Out of scope for 2b — the file already compiles into the single `cryptocogs` STATIC target today and will continue to after the path-only move; behaviour is unchanged. Step 8 ("wire all guards together") is where `exchange_common`'s final shape gets resolved (e.g. it may need to become a static lib after all, or `ws_client.cpp` may need to compile into both `cryptocogs` and `binance`). Flagging it now so it isn't a surprise at Step 8. |
| `test_compat_shim.cpp`'s mock-WS assertion (3.2.2) needs a `MockWsConnection` — the existing one lives as a private class inside `test_ws_client.cpp` | Low | Either duplicate a minimal local mock (small, ~40 lines, matches project's existing per-file mock pattern — `test_ws_client.cpp`'s own banner says tests are self-contained) or extract a shared test-only header. Given the project's stated preference for "real implementations over mocks" *of production code* (this mock is infrastructure, not the thing under test) and minimal abstraction, duplication-in-place is the more consistent choice — deferred to implementation time once the exact assertions are drafted. |
| Phase 2's header rewrite of the *already-shimmed* 3 files (`kraken_ws_client.hpp` etc.) removes their existing inline `make_ws_client` forwarders — if anything in the *migrated* (Phase 1) test/example code still calls `kraken::ws::make_ws_client(...)` directly (rather than the new `make_kraken_ws_client`), Phase 1's own checkpoint would already have caught it (compile error) — but worth double-checking no migrated file slipped through with an old factory call before Phase 2 removes the old inline forwarder it was relying on | Low | `grep -rn "kraken::ws::make_ws_client" tests/unit tests/examples` immediately before starting Phase 2 — should return zero hits outside the two preserved files. Cheap, mechanical pre-flight check. |

### Assumptions

1. The two preserved example files (`rest_client_example.cpp`, `ws_client_example.cpp`) are the *only* `tests/examples/` files that were byte-identical to pre-refactor — confirmed via `git diff --stat 8bc0316 HEAD`. If Rob has a different pair in mind for the compat-proof (e.g. wants `kraken_example.cpp` instead, since `CLAUDE.md` calls it the "combined REST + WebSocket demo"), that's a one-line change to which files Phase 1.3 skips and Phase 3.1 moves — flagging the assumption rather than guessing.
2. `CRYPTOCOGS_SUPPRESS_DEPRECATION` is a new macro this plan introduces (no existing precedent in the tree — confirmed via grep). Its name is fixed by the appendix design; this plan does not deviate from it.
3. The removal-version placeholder (`vNEXT_MAJOR` in the `#pragma message` text) stays a placeholder — plan multi-exchange-abstraction's compat-shim appendix says the actual version gets stated "in the shim headers and README" as part of release prep, which is Step 8/9 territory, not 2b.
4. Phase 1's `.cpp` deletions are deferred to Phase 2 (not done in Phase 1) specifically so each phase's checkpoint is independently buildable and green — Phase 1 alone leaves duplication in place but *complete and correct*; Phase 2 alone removes it. Splitting them the other way (delete first, migrate second) would leave an intermediate broken state.

### What could go wrong

- **Largest real risk**: Phase 1 touches 13 files mechanically — if the migration recipe's step ordering matters more than the guide suggests (e.g. a file that does `using namespace kraken;` *and* `using namespace exchange::kraken;` after a partial edit, creating ambiguity the compiler reports at a confusing call site rather than the `using` line), debugging could take longer than the edit itself. Mitigation: migrate and build **one file at a time** within 1.1/1.2 rather than batching all 13 then building once — slower but isolates any such ambiguity to a single, known file.
