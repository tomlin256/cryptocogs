# Rename the project `krakenapi` → `cryptocogs`

**Status:** Done — released as **v0.2.0** (branch `feature/rename-to-cryptocogs` → `main`).
Verified: clean build, 312/312 ctest, and a `find_package(cryptocogs)` consumer
links `cryptocogs::kraken` + `cryptocogs::binance`. Repo-only — the GitHub repo /
local dir / Claude-memory follow-ups remain for Rob (see below).

## Goal

The library now supports **two** exchanges (Kraken + Binance) on a shared,
exchange-agnostic engine, so the project-level name `krakenapi` is misleading.
Rename the **project / package / build-system / banners / docs** to `cryptocogs`.

This is a breaking change for downstream consumers (package name and link-target
names change), so it ships as **v0.2.0**.

## Decisions (confirmed)

1. **C++ namespaces stay `exchange::`** (`exchange::`, `exchange::kraken::`,
   `exchange::binance::`). They are already exchange-neutral and contain no
   "krakenapi" — **zero C++ identifier churn**. `KrakenRestClient`,
   `make_kraken_ws_client`, `kraken_frame_descriptor`, etc. are Kraken-specific
   and likewise unchanged.
2. **Adapter library targets renamed** `krakenapi`→`kraken`, `binanceapi`→`binance`,
   exported as `cryptocogs::kraken` / `cryptocogs::binance` (symmetric; resolves
   the project/adapter name collision).
3. **Scope = repo contents only.** The GitHub repo rename, local checkout-dir
   rename, and Claude project-memory relocation are **follow-ups for Rob** (see
   below) — they can't be done safely from inside this session (a dir rename
   breaks the working directory; the repo rename is outward-facing).
4. **Version** bumped `0.1.1`→`0.2.0` (breaking).
5. **Banner wording**: `krakenapi — A type-safe C++ library for the Kraken Spot
   REST and WebSocket v2 APIs` → `cryptocogs — A type-safe C++ library for the
   Kraken and Binance Spot REST and WebSocket APIs`.
6. **Repo URLs** in README/FetchContent examples updated to `tomlin256/cryptocogs`
   (valid once Rob renames the GitHub repo; GitHub auto-redirects the old URL).
7. **Historical plan docs (the fourteen made before this one)**: mechanical project-token sweep so the name
   is consistent repo-wide (`git grep -i krakenapi` → 0). Their bodies otherwise
   stay as the historical record; plan rename-to-cryptocogs (this file) is the authoritative
   record of the rename.

## Out of scope / follow-ups for Rob (post-merge)

- `gh repo rename cryptocogs` + `git remote set-url origin …/cryptocogs.git`.
- `mv ~/src/krakenapi ~/src/cryptocogs` (re-open the session there afterward).
- Relocate Claude memory: `ai_assistants/claude/memory/krakenapi` → `cryptocogs`,
  re-point the `~/.claude/projects/-Users-rob-src-cryptocogs/memory` symlink, and
  sweep `krakenapi` inside the memory files.

## Naming map

| Old | New | Notes |
|---|---|---|
| `project(KRAKENAPI VERSION 0.1.1)` | `project(CRYPTOCOGS VERSION 0.2.0)` | |
| `KRAKENAPI_BUILD_{KRAKEN,BINANCE,TESTS}`, `KRAKENAPI_INSTALL` | `CRYPTOCOGS_BUILD_*`, `CRYPTOCOGS_INSTALL` | CMake-only; no C++ `#ifdef`s use them |
| adapter target `krakenapi` → `libkrakenapi.a` | `kraken` → `libkraken.a` | the Kraken adapter |
| adapter target `binanceapi` → `libbinanceapi.a` | `binance` → `libbinance.a` | the Binance adapter |
| `krakenapi::{common,http,krakenapi,binanceapi}` | `cryptocogs::{common,http,kraken,binance}` | ALIAS + export namespace |
| `install(EXPORT krakenapiTargets NAMESPACE krakenapi::)` | `cryptocogsTargets`, `cryptocogs::` | |
| `cmake/krakenapiConfig.cmake.in`; `lib/cmake/krakenapi`; `find_package(krakenapi)` | `cmake/cryptocogsConfig.cmake.in`; `lib/cmake/cryptocogs`; `find_package(cryptocogs)` | `git mv` the template |
| file banner (63 files) + CLAUDE.md banner spec | new wording (above) | |
| **Unchanged** | | `exchange::*`, `exchange::kraken::*`, `Kraken*` classes, `kraken_*` test/exe names, `kraken/`+`binance/` source dirs |

## Steps

Work on branch `feature/rename-to-cryptocogs`. Each step ends in a **full build +
full ctest (expect 312/312)** and a checkpoint commit. Builds use the macOS
OpenSSL root (`-DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3`).

**Step 1 — Build system (the load-bearing step; deliberate, not a blind sed).**
- `CMakeLists.txt`: project name + version 0.2.0; rename the 4 options + internal
  vars; export set + namespace + config-file names + `lib/cmake/cryptocogs`
  destination; the install header/config blocks.
- `src/CMakeLists.txt`: adapter targets `krakenapi→kraken`, `binanceapi→binance`;
  aliases → `cryptocogs::{common,http,kraken,binance}`; export-set name; the
  `if(KRAKENAPI_BUILD_*)` / `if(TARGET …)` guards.
- `cmake/krakenapiConfig.cmake.in` → `git mv` to `cmake/cryptocogsConfig.cmake.in`
  + update its body (`krakenapiTargets.cmake` include, `check_required_components`).
- `tests/CMakeLists.txt` (7 example links) + `tests/unit/CMakeLists.txt` (3 Kraken
  + 6 Binance links): `krakenapi`→`kraken`, `binanceapi`→`binance`, and the
  `KRAKENAPI_BUILD_*` guards.
- **Done:** clean configure + build; **312/312** ctest; `cmake --install` to a temp
  prefix succeeds and a throwaway 5-line consumer does
  `find_package(cryptocogs REQUIRED)` + links `cryptocogs::kraken` +
  `cryptocogs::binance` and compiles. **Tests:** no new tests; all existing pass
  under the new target names (this is the real proof the target rename is correct).

**Step 2 — Source-file banners.**
- Replace the banner line in all **63** source files; update the mandated banner
  template in `CLAUDE.md` (the "File header" section).
- **Done:** `git grep -c 'krakenapi — A type-safe'` = 0; build + 312/312 ctest
  (banners are comments — no functional change, but verified per convention).

**Step 3 — Docs sweep.**
- Living docs: `README.md` (incl. repo URLs → cryptocogs, target names
  `cryptocogs::kraken`/`binance`, option names, the three-lib→ description),
  `CLAUDE.md`, `docs/plans.md` (+ add this plan's row), `docs/agent-add-exchange.md`.
- Historical plan docs (the fourteen made before this one): mechanical `krakenapi`→`cryptocogs` /
  `KRAKENAPI`→`CRYPTOCOGS` token sweep.
- **Done:** `git grep -i krakenapi` → 0 (or only an explicit "renamed from
  krakenapi" note in this plan); build + 312/312 ctest.

**Step 4 — Verify + release.**
- Fresh clean build (wipe `build/`), full ctest, install-smoke re-run.
- Merge `feature/rename-to-cryptocogs` → `main` (ff), push, tag **v0.2.0**, push tag.
- Hand Rob the follow-ups list above.
- **Done:** `main` == `origin/main`; `v0.2.0` tag on the rename commit; install-smoke green.

## Self-review

- **Risk — blind sed corrupts the adapter target.** A naive `krakenapi`→`cryptocogs`
  replace would turn `add_library(krakenapi …)` (the *Kraken adapter*) into
  `cryptocogs`, not `kraken`. Mitigation: Step 1 edits CMake **by hand**; the
  mechanical token sweep (Step 3) is **docs-only**.
- **Risk — sweeping the exchange "kraken".** Must touch only the `krakenapi` /
  `KRAKENAPI` tokens, never `kraken`/`Kraken` (exchange), `kraken/` dirs,
  `kraken_unit_tests`, or `make_kraken_ws_client`. The two tokens never collide
  with the exchange name, so a token-boundary replace is safe; verified by the
  build (any over-replace breaks compilation/links).
- **Risk — consumer breakage.** Intended and signalled by the v0.2.0 bump. The
  install-smoke step proves `find_package(cryptocogs)` + the new targets work
  end-to-end before tagging.
- **Risk — README URLs dangle** until the GitHub repo is renamed. Accepted:
  decision 3/6 assume Rob renames it; GitHub redirects the old URL meanwhile.
- **Decided (Rob):** library filenames change `libkrakenapi.a`→`libkraken.a`,
  `libbinanceapi.a`→`libbinance.a` — the natural target-name default, **no**
  `OUTPUT_NAME` override.
- **Assumption:** nlohmann vendoring, OpenSSL/libcurl `find_dependency`, and the
  ixwebsocket v12 setup are unaffected (no name dependency).
