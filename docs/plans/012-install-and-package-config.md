# Install rules + CMake package config

**Status**: Done — implemented in commits `cbbe1a8` + docs wrap-up
**Branch**: `feature/multi-exchange-abstraction`

> **Done**: `cmake --install` lays down the four static libs, the public headers
> (component-gated by the build flags), the deprecated shim headers (under
> `CRYPTOCOGS_BUILD_COMPAT_SHIM`), the vendored header-only `nlohmann_json`, and a
> `find_package(cryptocogs)` package config (`cryptocogs::{common,http,cryptocogs,
> binance}` targets + version file, `SameMajorVersion`, project `0.1.0`).
> Install is gated by `CRYPTOCOGS_INSTALL` (top-level default). The
> FetchContent-vs-export problem was resolved by vendoring nlohmann (decision 1)
> and suppressing the deps' own install rules (`JSON_Install`/`INSTALL_GTEST` OFF;
> ixwebsocket moved under `CRYPTOCOGS_BUILD_TESTS`), so a `-DCRYPTOCOGS_BUILD_TESTS=OFF`
> install is **cryptocogs-only** — verified, and a throwaway downstream consumer
> doing `find_package(cryptocogs)` + linking `cryptocogs::kraken` +
> `cryptocogs::binance` and naming `kraken::TickPrice` (the shim) compiled,
> linked, and ran against the installed prefix. Suite stayed 304 green.

---

## Goal

Make the library installable so a downstream project can
`find_package(cryptocogs)` and `target_link_libraries(app PRIVATE
cryptocogs::kraken)` (and/or `cryptocogs::binance`) against an installed
prefix — not only via in-tree `FetchContent`. This was the one piece punted from
[plan step-2b-compat-shim](002-step-2b-compat-shim.md); Rob's "punt" was a typo.

**Done when**: `cmake --install` into a prefix lays down the four static libs,
the public headers (component-gated), and a CMake package config; and a **fresh
downstream consumer** that does `find_package(cryptocogs REQUIRED)` +
`target_link_libraries(app cryptocogs::kraken cryptocogs::binance)` compiles
and links a small program against that prefix.

## What gets installed

- **Targets** → `EXPORT cryptocogsTargets`, namespace `cryptocogs::`,
  `ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}`:
  - `exchange_common`, `exchange_http` (always),
  - `cryptocogs` (if `CRYPTOCOGS_BUILD_KRAKEN`), `binance` (if
    `CRYPTOCOGS_BUILD_BINANCE`).
  The adapters carry no own include dir — they inherit the `INSTALL_INTERFACE`
  one from `exchange_common`/`exchange_http`, which are in the same export set.
- **Headers** (component-gated to match the flags):
  - `include/exchange/common/` always,
  - `include/exchange/kraken/` if KRAKEN, `include/exchange/binance/` if BINANCE,
  - the top-level shim headers (`kraken_*.hpp`, `kraken_compat.hpp`,
    `kraken_ws_client.inl`, `ws_reconnect_session.hpp/.inl`) if COMPAT_SHIM.
- **Package config**: `cryptocogsConfig.cmake` (from `cmake/cryptocogsConfig.cmake.in`
  via `configure_package_config_file`) + `cryptocogsConfigVersion.cmake`
  (`write_basic_package_version_file`, `SameMajorVersion`), and the exported
  `cryptocogsTargets.cmake`, all to `${CMAKE_INSTALL_LIBDIR}/cmake/cryptocogs`.

## Design decisions

1. **Dependency resolution for installed consumers.** OpenSSL/CURL are
   `find_package`d (imported targets), so the config `find_dependency(OpenSSL)` +
   `find_dependency(CURL)` and the export references them fine. **nlohmann_json
   needed a different answer than the plan first assumed**: it is FetchContent'd,
   which makes `nlohmann_json::nlohmann_json` a *live local target* — CMake then
   refuses to put it in our install export ("target nlohmann_json … not in any
   export set"), and the usual `$<INSTALL_INTERFACE:…::…>` deferral trick is
   blocked because CMake resolves the live alias rather than treating it as an
   external name. *(Switching nlohmann to `find_package` would fix the export but
   break the zero-setup FetchContent build.)* **Resolved by vendoring**: nlohmann
   is header-only, so its headers are installed into our prefix's `include/`
   (`install(DIRECTORY ${nlohmann_json_SOURCE_DIR}/include/nlohmann …)`), the libs
   link it `$<BUILD_INTERFACE:…>`-only, and the config carries **no** nlohmann
   `find_dependency`. The installed package is self-contained — a consumer needs
   no separate nlohmann_json. (Caveat: a consumer with their own nlohmann sees our
   copy on the cryptocogs include path; acceptable for a stable header-only lib —
   noted in the README.)

1a. **Keep the deps' own install rules out of our prefix.** FetchContent
   subprojects leak `install()` rules into a top-level `cmake --install`. Fixed:
   `JSON_Install OFF` and `INSTALL_GTEST OFF`, and **ixwebsocket is fetched only
   under `CRYPTOCOGS_BUILD_TESTS`** (no core library links it — it's
   example/test-only), so a `-DCRYPTOCOGS_BUILD_TESTS=OFF` install lays down
   *only* cryptocogs + the vendored nlohmann + the package config. Verified.

2. **Project version `0.1.0`** — `project(CRYPTOCOGS VERSION 0.1.0)`. The repo had
   none; `write_basic_package_version_file` needs one. Pre-1.0 signals the API is
   still settling (the `kraken::` shim is deprecated, the layout is new). Easy to
   bump later; flag if you'd prefer a different start.

3. **Gate install behind `CRYPTOCOGS_INSTALL`**, defaulting to **on for a
   top-level build, off as a subproject** (`if(CMAKE_SOURCE_DIR STREQUAL
   CMAKE_CURRENT_SOURCE_DIR)` — 3.15-compatible, avoids polluting a parent
   project's install when cryptocogs is `FetchContent`-ed). Tests/examples are
   **not** installed (dev artifacts).

4. **Use `GNUInstallDirs`** for `lib`/`include`/`bin` destinations (portable;
   respects `CMAKE_INSTALL_PREFIX`).

## Step 1 — Install machinery + downstream verification

- `project(CRYPTOCOGS VERSION 0.1.0)`; `option(CRYPTOCOGS_INSTALL …)` with the
  top-level default; `include(GNUInstallDirs)`.
- `src/CMakeLists.txt`: add each `add_library` to `install(TARGETS … EXPORT
  cryptocogsTargets …)` (gated by the same flags).
- New `cmake/cryptocogsConfig.cmake.in` (find_dependency block + include the
  targets file).
- Top-level (guarded by `CRYPTOCOGS_INSTALL`): the header `install(DIRECTORY …)`
  rules, `install(EXPORT …)`, `configure_package_config_file` +
  `write_basic_package_version_file` + their `install(FILES …)`.
- **Verify**: `cmake --build build` + `ctest` still green (304); then
  `cmake --install build --prefix /tmp/kapi-prefix`; then configure+build a tiny
  throwaway consumer (`find_package(cryptocogs REQUIRED)`, a `main` that
  constructs a `BinanceRestClient` and a `KrakenRestClient` and names a
  `kraken::TickPrice`) against `-DCMAKE_PREFIX_PATH=/tmp/kapi-prefix` — must
  configure, compile, link.
- **Checkpoint**: `feat: add install rules + cryptocogs CMake package config`.

## Step 2 — Docs + wrap-up

- README: an "Installing" subsection (`cmake --install`, the `find_package` +
  `target_link_libraries` snippet, the `nlohmann_json` note from decision 1).
- CLAUDE.md: note the install/package-config targets and `CRYPTOCOGS_INSTALL`.
- Plan step-2b-compat-shim: flip its punt note — install is now done here (link to plan install-and-package-config).
- Flip plan install-and-package-config → Done. Final build + `ctest`.
- **Checkpoint**: `docs: mark plan 012 done — install/package config`.

## Self-review — risks & assumptions

| Risk / assumption | Likelihood | Mitigation |
|---|---|---|
| Installed consumer can't find `nlohmann_json` (we fetch it; they may not have it) | Med | Decision 1 + README note make the dependency explicit; it's a ubiquitous package. The downstream-consumer verification compiles against a prefix where nlohmann *is* available (our build tree's find result) — and the config's `find_dependency` surfaces a clear error otherwise |
| Adapters lack their own `INSTALL_INTERFACE` include dir | None | They inherit it transitively from `exchange_common`/`exchange_http` (same export set) — verified those carry `$<INSTALL_INTERFACE:include>` |
| Component-gated headers miss a file (e.g. a shim `.inl`) → consumer include fails | Low | The downstream consumer test includes through `kraken::` and `exchange::{kraken,binance}::` headers, exercising the installed tree |
| `CRYPTOCOGS_INSTALL` default misfires under FetchContent | Low | `CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR` is the standard 3.15-safe top-level check |
| Static-lib export with FetchContent deps (OpenSSL/CURL system, nlohmann fetched) mixes find-module + config deps | Low | `find_dependency` handles both; the consumer build is the proof |

**Assumption**: installing the static libs (not shared) is the intent — matches
the current `STATIC` targets; no SONAME/versioned-.so concerns.
