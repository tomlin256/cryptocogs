# Relocate `TickPrice` from the Kraken adapter to the common layer

**Status**: Done — implemented in commits `77f6eec` (move) + the docs/wrap-up commit
**Branch**: `feature/multi-exchange-abstraction`

> **Done**: `TickPrice` now lives in `exchange::`
> (`include/exchange/common/tick_price.hpp` + `src/exchange/common/tick_price.cpp`,
> compiled into `libexchange_common.a`); `src/kraken/types.cpp` was deleted (git
> tracked it as a rename) and dropped from the `cryptocogs` target.
> `exchange/kraken/types.hpp` re-exports it (`using exchange::TickPrice;`), so
> every `exchange::kraken::TickPrice` reference and the `kraken::TickPrice` compat
> alias resolve unchanged — zero Kraken call-site edits. `nm`-verified: the
> `TickPrice::from_json` symbol is in `libexchange_common.a` and absent from
> `libkraken.a`. One discovery during 9.1: `test_tick_price.cpp` had a single
> Kraken-coupled case (`AddOrderRequestLimitPriceIsNumber`, exercising
> `kraken::ws::AddOrderRequest`) — it was **relocated** to the Kraken-guarded
> `test_ws_client.cpp` (`KrakenAddOrderSerialisation.LimitPriceIsJsonNumber`), so
> `test_tick_price.cpp` is now purely common (links `exchange_common`, runs in any
> build). Total test count unchanged at 300. The single-exchange split shifts as
> expected: `-DCRYPTOCOGS_BUILD_KRAKEN=OFF` now runs **139** (129 Binance + the 10
> common `TickPrice` tests, which build with no `cryptocogs` in the graph — the
> proof of independence); `-DCRYPTOCOGS_BUILD_BINANCE=OFF` stays **171**.
> `CLAUDE.md`/`README.md` updated to match.

---

## Goal

`TickPrice` (exact-decimal price representation) currently lives in
`exchange::kraken::` (`include/exchange/kraken/types.hpp` + `src/kraken/types.cpp`),
but it is **not Kraken-specific** — it is a generic integer-ticks + decimal-place
value type with no Kraken dependency. It sits under Kraken only because Kraken is
its sole current consumer; Binance does not use it. `src/kraken/types.cpp`
contains *nothing but* `TickPrice::from_json`, so the whole file is generic.

Move `TickPrice` to the common scaffold (`exchange::`, built into
`libexchange_common.a`), and have `exchange::kraken::` re-export it so every
existing call site — Kraken REST/WS types, the tests, and the `kraken::TickPrice`
compat alias — keeps compiling unchanged.

**Done when**: `TickPrice` is defined in `exchange::`; `src/kraken/types.cpp` is
gone; the full build + 300-test `ctest` is green; and a Binance-only build
(`-DCRYPTOCOGS_BUILD_KRAKEN=OFF`) compiles **and runs** the `TickPrice` test —
proof it no longer depends on Kraken.

## Footprint (verified)

- **Definition**: `struct TickPrice` (`kraken/types.hpp:163`, fields + inline
  `from`/`str`/`to_json`, out-of-line `from_json`); body in `src/kraken/types.cpp`
  (the file's only content). Needs `<cmath>`, `<cstdint>`, `<string>`, nlohmann.
- **Users**: `kraken/types.hpp` (own fields), `kraken/ws_api.hpp`,
  `kraken_compat.hpp:93` (`using exchange::kraken::TickPrice;` → `kraken::TickPrice`),
  and tests `test_tick_price`, `test_client`, `test_rest_requests`,
  `test_ws_client`, plus `kraken_example`. **Binance: none.**
- **Test wiring**: `test_tick_price` links `cryptocogs`, inside the
  `CRYPTOCOGS_BUILD_KRAKEN` guard.

## Design decisions

1. **Home = new `include/exchange/common/tick_price.hpp` + `src/exchange/common/tick_price.cpp`**, namespace `exchange::`. Keeps `common/types.hpp` enum-focused; `TickPrice` is a standalone value type and reads better in its own header (it already had its own doc section). *(Alternative: fold into `common/types.hpp`. Not chosen — would mix a value type into the enum header.)*
2. **`exchange::kraken::` re-exports it**: `kraken/types.hpp` gains
   `#include "exchange/common/tick_price.hpp"` + `using exchange::TickPrice;`.
   Every `exchange::kraken::TickPrice` reference and the compat `kraken::TickPrice`
   alias resolve unchanged — **zero call-site edits** in Kraken code, tests, or
   the shim.
3. **`src/kraken/types.cpp` is deleted** (its only content moved) and dropped from
   the `cryptocogs` CMake target; `tick_price.cpp` joins the `exchange_common`
   target. `exchange_common` already links nlohmann; it needs nothing new.
4. **`test_tick_price` becomes a common test**: moved out of the
   `CRYPTOCOGS_BUILD_KRAKEN` guard to run in any build, re-pointed to link
   `exchange_common` and include the new header. Consequence: its cases now also
   run in a Binance-only build (the count moves from the Kraken-only subset to
   always-on; the all-flags total stays 300). This is the right signal — a
   common type is tested regardless of which adapters are enabled.
5. **Docs**: `CLAUDE.md` moves `TickPrice` from the `exchange::kraken::` rows/
   sections to the common layer (it is already under "Shared types reference"),
   updates the `src/` tree (drop `kraken/types.cpp`, add
   `exchange/common/tick_price.cpp`), and the namespace-layout table.
6. **Banner** on both new files (the standard MIT block, before `#pragma once`).

## Step 1 — Move the type + rewire build/tests

- Create `include/exchange/common/tick_price.hpp`: banner, `#pragma once`,
  `namespace exchange {`, the `TickPrice` struct verbatim (inline `from`/`str`/
  `to_json` + declared `from_json`), includes `<cmath> <cstdint> <string>
  <nlohmann/json.hpp>`.
- Create `src/exchange/common/tick_price.cpp`: banner, `#include
  "exchange/common/tick_price.hpp"`, the `from_json` body (namespace `exchange`).
- Edit `include/exchange/kraken/types.hpp`: remove the `TickPrice` struct; add the
  include + `using exchange::TickPrice;` where it stood. Keep all Kraken types
  that *use* `TickPrice` unchanged.
- Delete `src/kraken/types.cpp`.
- `src/CMakeLists.txt`: add `exchange/common/tick_price.cpp` to `exchange_common`;
  remove `kraken/types.cpp` from `cryptocogs` (leaving just `kraken/rest_client.cpp`).
- `tests/unit/CMakeLists.txt`: move `test_tick_price` out of the Kraken guard
  (top of the file, unconditional), link `exchange_common GTest::gtest_main`.
- `tests/unit/test_tick_price.cpp`: include `exchange/common/tick_price.hpp`; if it
  refers to `exchange::kraken::TickPrice`, switch to `exchange::TickPrice` (the
  re-export means either compiles, but the common name is correct here).
- **Gate**: `cmake -B build && cmake --build build && ctest` → 300 green. Then
  spot-check `-DCRYPTOCOGS_BUILD_KRAKEN=OFF` builds + runs `test_tick_price`.
- **Checkpoint**: `refactor: move TickPrice to exchange:: (common) from kraken`.

## Step 2 — Docs + wrap-up

- Update `CLAUDE.md` per decision 5.
- Flip plan relocate-tickprice-to-common → Done (`docs/plans.md` + header). Final build + `ctest` sanity.
- **Checkpoint**: `docs: mark plan 009 done — TickPrice relocation`.

## Self-review — risks & assumptions

| Risk / assumption | Likelihood | Mitigation |
|---|---|---|
| Removing the Kraken `TickPrice` def before the re-export is in place breaks Kraken call sites | Med if done out of order | Step 1 is one atomic edit set; the re-export (`using exchange::TickPrice;`) lands in the same change before any build |
| Compat `kraken::TickPrice` stops resolving | Low | `kraken_compat.hpp` aliases `exchange::kraken::TickPrice`, which the re-export preserves; covered by the existing build |
| `tick_price.cpp` in `exchange_common` needs OpenSSL/curl | None | It uses only `<cmath>`/`<string>`/nlohmann — `exchange_common` already links nlohmann and nothing else |
| Moving the test out of the Kraken guard is unwanted | Low | Surfaced as decision 4; it's the consistent choice for a now-common type. Trivially reversible if you prefer it stay Kraken-guarded |
| Other genuinely-generic code is also misfiled under `kraken/` | Out of scope | This plan is scoped to `TickPrice`/`types.cpp` per the observation; a wider audit can be a follow-up |

**Assumption**: `TickPrice`'s public shape and behaviour do not change — this is a
pure relocation + re-export, so the existing `test_tick_price` cases are the
full regression net.
