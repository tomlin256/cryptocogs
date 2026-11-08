# Step 9: CMake target split, build toggles, and full-matrix validation

**Status**: Done — implemented in commits `9e73dc1`, `28179fd`, `edf4081`
**Parent**: [Plan multi-exchange-abstraction, Step 9](001-multi-exchange-abstraction.md#step-9--cmake-build-validation-final-cleanup)
**Branch**: `feature/multi-exchange-abstraction`

---

## Goal

Finish the build-system half of the multi-exchange refactor. Today the generic
`ExchangeWsClient` implementation (`src/exchange/common/ws_client.cpp`) is
compiled **into `libkraken.a`**, so every Binance target that touches the WS
client must link `binance cryptocogs` purely to borrow that one object file —
a false dependency the Step 7/8 plans flagged and deferred to here. Step 9
extracts that file into a standalone `exchange_common` static library that both
exchange adapters depend on as peers, adds the per-exchange `option()` toggles
and their configure-time dependency guards (§F of plan multi-exchange-abstraction), guards every test
and example behind its exchange flag, and proves the decoupling with a
configure/build/`ctest` flag matrix.

**Overall done when**: from a clean tree, `cmake -B build && cmake --build build
&& ctest` is green (300 tests), **and** `-DCRYPTOCOGS_BUILD_KRAKEN=OFF` builds the
Binance library + its tests/examples with **zero reference to `cryptocogs`**
(the real proof the coupling is gone), **and** CLAUDE.md/README describe the new
three-library layout.

This is a build-graph and documentation change. **No library `.cpp`/`.hpp`
source is modified** — the unchanged 300-test suite is the behavioural
regression net; the flag matrix is the build-graph test.

---

## Current state (verified in tree)

`src/CMakeLists.txt` — two libraries:
- `cryptocogs` STATIC = `exchange/common/ws_client.cpp` + `kraken/types.cpp` +
  `kraken/rest_client.cpp`; links `CURL OpenSSL::SSL nlohmann_json` (unqualified
  → effectively PUBLIC).
- `binance` STATIC = `binance/rest_client.cpp`; same link line.

Top-level `CMakeLists.txt` declares only `CRYPTOCOGS_BUILD_TESTS` and
`CRYPTOCOGS_BUILD_COMPAT_SHIM` (the latter currently **gates nothing** — it is a
declared no-op; the shim's install/test wiring is plan step-2b-compat-shim's job and is not yet
done). No `CRYPTOCOGS_BUILD_KRAKEN` / `_BINANCE`. No `install()` rules anywhere.

Three targets carry the false `cryptocogs` link (the coupling to remove):
- `tests/CMakeLists.txt`: `binance_ws_client_example`, `binance_ws_api_example`
- `tests/unit/CMakeLists.txt`: `test_binance_ws_client`

The source tree has exactly four `.cpp` under `src/`; there is **no**
`binance/ws_client.cpp` (Binance WS streams + API are header-only, like
`KrakenWsClient`), so `binance` stays single-source.

---

## Design decisions

1. **Guard in place — no file relocation, no test-executable consolidation.**
   §F sketched `tests/examples/kraken/` and a single `binance_unit_tests`, but
   the implemented tree keeps Kraken examples flat in `tests/examples/` and ships
   six separate `test_binance_*` executables. Step 9 wraps the **existing target
   definitions** in their exchange guards and changes link lines — it does not
   move `.cpp` files on disk or merge executables. Rationale: the per-binary
   granularity is working and aids `ctest -R`; relocating files is churn with
   regression risk and no functional payoff. This is the only material
   divergence from §F's letter, and it matches §F's actual intent ("move the
   *targets* inside the guard").

2. **`exchange_common` is STATIC, compiling only `exchange/common/ws_client.cpp`**
   (§F's post-Step-1 correction — it cannot be `INTERFACE`; that file is real
   non-template object code). It carries the `include/` dir and
   `nlohmann_json` as **PUBLIC** usage requirements, and notably **not**
   OpenSSL/libcurl (the generic dispatch needs neither). Alias
   `cryptocogs::common`.

3. **The single critical edit**: `cryptocogs` **sheds**
   `exchange/common/ws_client.cpp` from its source list and gains
   `PUBLIC exchange_common`. If that file stayed in *both* archives, the
   `ExchangeWsClient` symbols would be duplicated. 9.1's done-check greps the
   archives (`nm`) to confirm the symbols live in `libexchange_common.a` and are
   **absent** from `libkraken.a`.

4. **OpenSSL/libcurl stay PUBLIC on both exchange libs; add `OpenSSL::Crypto`
   explicitly.** *(Corrected during 9.1 — §F assumed PRIVATE on the premise that
   the crypto lives in a `.cpp`. It does not: `auth.hpp` defines the HMAC/SHA
   helpers **inline in a public header** against libcrypto, and `rest_client.hpp`
   `#include <curl/curl.h>`, so every consumer's own translation unit compiles
   and links those symbols. PRIVATE breaks the examples at compile time — it did,
   on `private_rest.cpp`.)* PUBLIC matches today's effective behaviour (the
   existing unqualified link is PUBLIC); the only real change is naming
   `OpenSSL::Crypto` — the actual dependency, since the inline helpers use
   libcrypto, not libssl. Moving the crypto into a `.cpp` to make OpenSSL
   genuinely PRIVATE would change the public header surface and is deliberately
   out of scope (decision: no library source is modified).

5. **The three Binance WS targets drop `cryptocogs` → link `binance` only.**
   They reach the generic impl transitively via `binance → exchange_common`.
   The now-stale "cryptocogs supplies the generic ExchangeWsClient" comments are
   replaced with a one-line note that `exchange_common` (via `binance PUBLIC`)
   provides it. This edit *is* the decoupling.

6. **`CRYPTOCOGS_BUILD_COMPAT_SHIM` stays a declared option with only its
   dependency-rule guard** (`COMPAT_SHIM requires KRAKEN → warn + force OFF`).
   There are no compat targets to nest (no `test_compat_shim`, no `tests/compat/`
   — that is plan step-2b-compat-shim, still Draft). Step 9 does not invent them; it just makes
   the option's guard correct for when plan step-2b-compat-shim lands.

7. **No `install()`/export work.** None exists today; the `$<INSTALL_INTERFACE>`
   generator expressions and `cryptocogs::*` aliases are kept as-is
   (forward-looking, harmless). Packaging is out of scope for this step.

8. **`test_ws_reconnect_session` is left exactly as-is** — it links no exchange
   library (the reconnect session is header+`.inl` only) and adds the include dir
   manually. It moves inside the `CRYPTOCOGS_BUILD_KRAKEN` guard with the other
   Kraken-suite binaries but its link line is untouched. Explicitly called out so
   it is not "tidied" into a needless `exchange_common` link.

9. **Validation is the flag matrix + the unchanged 300-test suite** (decision
   rationale in [Testing](#testing-strategy)). No new C++ is written, so there is
   no new unit test; the six configure/build/`ctest` matrix cells are the test
   for a build-graph change, run in throwaway build dirs so the working `build/`
   is never disturbed.

---

## Sub-step 9.1 — Extract `exchange_common`; add the four toggles + guards

**Done when**: clean configure + full build + `ctest` 300 green; the three
libraries `libexchange_common.a`, `libkraken.a`, `libbinance.a` are all
produced; `nm` shows `ExchangeWsClient` symbols **only** in
`libexchange_common.a`.

### `CMakeLists.txt` (top level)
- Add `option(CRYPTOCOGS_BUILD_KRAKEN … ON)` and
  `option(CRYPTOCOGS_BUILD_BINANCE … ON)` (now four options total).
- Immediately after the options, add the two §F dependency guards:
  - `COMPAT_SHIM AND NOT KRAKEN` → `message(WARNING …)` + force `COMPAT_SHIM OFF`.
  - `NOT KRAKEN AND NOT BINANCE` → `message(WARNING "… nothing will be built.")`.
- `find_package(OpenSSL REQUIRED)` / `find_package(CURL REQUIRED)` stay
  unconditional (both adapters need them; the package cache makes this free).

### `src/CMakeLists.txt` — replace the two-library block with the three-target form
- `exchange_common` STATIC = `exchange/common/ws_client.cpp`;
  `target_include_directories(… PUBLIC $<BUILD_INTERFACE:include> …)`;
  `target_link_libraries(… PUBLIC nlohmann_json::nlohmann_json)`; alias
  `cryptocogs::common`.
- `if(CRYPTOCOGS_BUILD_KRAKEN)`: `cryptocogs` STATIC = `kraken/rest_client.cpp` +
  `kraken/types.cpp` (ws_client.cpp **removed**); `PUBLIC exchange_common`,
  `PRIVATE OpenSSL::SSL OpenSSL::Crypto CURL::libcurl`; alias.
- `if(CRYPTOCOGS_BUILD_BINANCE)`: `binance` STATIC = `binance/rest_client.cpp`;
  same `PUBLIC exchange_common` / `PRIVATE` link pattern; alias.

> Tests/examples are **not** touched in 9.1. The existing
> `binance cryptocogs` link lines still compile because `cryptocogs` still
> exists (flags default ON) and no longer double-compiles `ws_client.cpp`.

### Checkpoint commit
`feat: step 9.1 — extract exchange_common library + add build toggles`
Full build + full `ctest` green (300).

---

## Sub-step 9.2 — Decouple Binance WS targets; guard tests/examples by exchange

**Done when**: full build + `ctest` 300 green, **and** the full
[flag matrix](#flag-matrix) below passes in throwaway build dirs — including
`CRYPTOCOGS_BUILD_KRAKEN=OFF`, which links every Binance target with no
`cryptocogs` present (the decoupling proof).

### `tests/CMakeLists.txt`
- Move the `kapi` legacy library and the seven Kraken example targets
  (`private_rest`, `public_rest`, `public_ws`, `private_ws`, `kraken_example`,
  `ws_client_example`, `rest_client_example`) inside `if(CRYPTOCOGS_BUILD_KRAKEN)`.
- Move the three Binance example targets inside `if(CRYPTOCOGS_BUILD_BINANCE)`,
  and change `binance_ws_client_example` + `binance_ws_api_example` link lines
  from `binance cryptocogs …` → `binance …`; replace the stale
  "cryptocogs supplies…" comments with the `exchange_common`-via-`binance`
  note.
- The shared FetchContent (`backward`, `spdlog`, `CLI11`) and the
  `example_backward` OBJECT library stay unconditional at the top (used by both
  exchanges).

### `tests/unit/CMakeLists.txt`
- Wrap the Kraken-suite binaries — `kraken_unit_tests`, `test_ws_client`,
  `test_ws_responses`, `test_tick_price`, `test_order_type`,
  `test_ws_reconnect_session` — in `if(CRYPTOCOGS_BUILD_KRAKEN)` (link lines
  unchanged; `test_ws_reconnect_session` untouched per decision 8).
- Wrap the six `test_binance_*` binaries in `if(CRYPTOCOGS_BUILD_BINANCE)`, and
  change `test_binance_ws_client` from `binance cryptocogs …` → `binance …`
  with the comment replaced.

### Checkpoint commit
`feat: step 9.2 — decouple Binance WS targets; guard tests/examples by exchange`
Full build + full `ctest` green (300); flag matrix passes.

---

## Sub-step 9.3 — Documentation: CLAUDE.md + README

**Done when**: CLAUDE.md and README describe the three-library layout and four
build options accurately; a fresh `cmake -B build && cmake --build build &&
ctest` (sanity re-run) stays green.

### `CLAUDE.md`
- Build-options table: add `CRYPTOCOGS_BUILD_KRAKEN` / `CRYPTOCOGS_BUILD_BINANCE`
  rows; correct the `CRYPTOCOGS_BUILD_COMPAT_SHIM` description to "declared;
  gating lands with plan step-2b-compat-shim."
- Build-outputs table: add `libexchange_common.a` and `libbinance.a`; note
  `libkraken.a` no longer contains the WS-client impl.
- The coding-conventions paragraph that says `libkraken.a` "compiles both
  `src/exchange/common/ws_client.cpp` … and `src/kraken/{rest_client,types}.cpp`"
  → rewrite: `exchange_common` compiles the generic impl; `cryptocogs` /
  `binance` are peers linking it `PUBLIC`.
- Project-structure tree comment on `src/CMakeLists.txt` (line ~50) → reflect the
  three targets.

### `README.md`
- Add (or correct) a short "Build targets / options" section listing the three
  libraries and four toggles; link the migration guide
  ([001-appendix-migration-guide.md](001-appendix-migration-guide.md)) for
  pre-refactor callers. Confirm the `exchange::kraken::*` re-exports (Step 2)
  still compile so the guide's examples hold (compile-check, no code change).

### Checkpoint commit
`docs: step 9.3 — update CLAUDE.md/README for multi-library build`
Full build + full `ctest` green (300).

---

## Wrap-up (after 9.3)

- Append a "**Done**:" paragraph to plan multi-exchange-abstraction Step 9 (commit trail
  `9.1`/`9.2`/`9.3` hashes; the extraction; the four options + two guards; the
  decoupling of the three Binance WS targets; PRIVATE ssl/curl; flag-matrix
  evidence; what stayed out of scope — compat-shim wiring → plan step-2b-compat-shim,
  `install()` → later).
- Flip plan step-9-cmake-build-validation to **Done** in `docs/plans.md` and this file's Status header.
- Commit: `docs: mark plan 007 done`.

---

## Flag matrix

Run each in a throwaway dir (`cmake -B /tmp/bm-<n> -S . <flags>`), so the working
`build/` is untouched. "Green subset" = `ctest` passes for the targets that
exist.

| # | Flags | Expected |
|---|---|---|
| 1 | *(none — all ON)* | 3 libs; all examples + all tests; `ctest` 300 green |
| 2 | `-DCRYPTOCOGS_BUILD_BINANCE=OFF` | `exchange_common` + `cryptocogs` only; **no** `binance`/`binance_*` targets; Kraken `ctest` subset green |
| 3 | `-DCRYPTOCOGS_BUILD_KRAKEN=OFF` | `exchange_common` + `binance` only; **no** `cryptocogs`/Kraken targets; Binance `ctest` subset green — **links with no `cryptocogs` present (decoupling proof)**; COMPAT_SHIM auto-forced OFF with warning |
| 4 | `-DCRYPTOCOGS_BUILD_KRAKEN=OFF -DCRYPTOCOGS_BUILD_BINANCE=OFF` | "nothing will be built" warning; only `exchange_common` configures; build is a no-op |
| 5 | `-DCRYPTOCOGS_BUILD_TESTS=OFF` | 3 libs, **no** tests/examples; build succeeds |
| 6 | `-DCRYPTOCOGS_BUILD_COMPAT_SHIM=ON -DCRYPTOCOGS_BUILD_KRAKEN=OFF` | "disabling shim" warning; `COMPAT_SHIM` forced OFF; Binance-only build green |

Cell 3 is the headline check: a successful link there is the machine-checked
proof that the false `cryptocogs` dependency is gone.

---

## Testing strategy

Step 9 adds **no C++** — it rewires CMake and edits docs. Per the project's
"unit tests for all code" rule, the honest analogue for a build-graph change is:

1. **Behavioural regression**: the existing **300-test suite is unchanged and
   must stay green** at every checkpoint (the library objects are byte-identical;
   only their archive membership and link edges change).
2. **Build-graph test**: the **six-cell flag matrix**, which mechanically
   verifies target presence/absence, the configure-time warnings, and — in cell
   3 — that the Binance libraries link with no Kraken target in the graph.
3. **Symbol-placement check** (9.1): `nm` confirms `ExchangeWsClient` lives in
   `libexchange_common.a` and not `libkraken.a`.

If you would rather have a committed artifact than transient matrix runs, an
option is a tiny `tests/cmake/` linkage smoke (a 3-line `main` linking only
`binance`) wired under `CRYPTOCOGS_BUILD_BINANCE`; I left it out by default as
redundant with cell 3, but will add it if you want the guarantee version-pinned.

---

## Self-review — risks & assumptions

| Risk / assumption | Likelihood | Mitigation |
|---|---|---|
| `ws_client.cpp` left in *both* `cryptocogs` and `exchange_common` → duplicate `ExchangeWsClient` symbols | Med if rushed | The `nm` archive check in 9.1's done-criteria catches it before commit; decision 3 names it the critical edit |
| ~~Making OpenSSL/curl `PRIVATE`~~ — **realized in 9.1**: PRIVATE broke `private_rest.cpp` because `auth.hpp`/`rest_client.hpp` expose OpenSSL/curl inline in public headers | Happened | Resolved by keeping both **PUBLIC** (decision 4, corrected) — matches today's effective link; 300-suite + all examples green afterward |
| Cell 3 (`KRAKEN=OFF`) still fails to link — a hidden Kraken dependency in a Binance target | Low (audited: only the 3 WS targets named `cryptocogs`, all fixed in 9.2) | This is exactly what cell 3 is for; if it fails, the plan has surfaced a real leak to fix before "done" |
| `OpenSSL::Crypto` target name unavailable on the host's CMake/OpenSSL module | Low (standard since CMake 3.x FindOpenSSL) | Cell-1 configure fails loudly; fall back to `OpenSSL::SSL` only (it transitively pulls crypto) if so |
| `example_backward` / spdlog / CLI11 fetched at top but only used inside guards → wasted fetch when an exchange is OFF | Cosmetic | Acceptable; fetches are cached and harmless. Not worth conditionalising |
| Reviewer expects §F's literal `binance_unit_tests` / `tests/examples/kraken/` reorg | Med | Decision 1 documents the deliberate divergence and its rationale up front |
| Compat-shim guard implies shim targets exist | Low | Decision 6 scopes it: option + dependency guard only; targets are plan step-2b-compat-shim |
| Pre-existing test failures surface mid-refactor | Low (300 green now) | Per the standing rule, stop and fix or escalate — do not proceed past a red suite |

**Assumptions**: no consumer outside this repo currently links the targets (so
renaming link edges is safe); `install()`/packaging remains out of scope;
plan step-2b-compat-shim (compat-shim wiring) and Step 10 (agent onboarding guide) are tracked
separately and not pulled into this step.
