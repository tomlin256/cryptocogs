# Fix two CMake install nits

**Status:** Done
**Branch:** `feature/cmake-install-nits`

## 1. Motivation

Two pre-existing install-quality defects surfaced during plan declarative-headers's install
validation. Both are CMake-only; no library code changes.

- **N1 — ixwebsocket pollutes the install.** `cmake --install` lays down
  `libixwebsocket.a`, ~30 `IX*.h` headers, `lib/cmake/ixwebsocket/`, and
  `lib/pkgconfig/ixwebsocket.pc` into the prefix — directly contradicting the
  top-level CMakeLists comment ("ixwebsocket … never installed by cryptocogs")
  and plan install-and-package-config's "ixwebsocket is deliberately not installed". The core libs
  don't link ixwebsocket (only examples/tests do, in-tree), and the cryptocogs
  export set does not reference it, so a consumer never needs it.
- **N2 — the package doesn't carry its C++ standard.** The project sets
  `CMAKE_CXX_STANDARD 17` directory-wide, which is **not** exported. A downstream
  `find_package(cryptocogs)` consumer that doesn't set the standard itself fails
  to compile (`std::optional` etc. unavailable) — observed in plan declarative-headers Step 8.

## 2. Root cause (verified)

- **N1:** ixwebsocket (pinned `v12.0.0`) declares
  `option(IXWEBSOCKET_INSTALL "Install IXWebSocket" TRUE)` and gates *all* its
  `install()` rules behind it. Because `FetchContent_MakeAvailable(ixwebsocket)`
  does an `add_subdirectory`, those rules run as part of cryptocogs's install.
  The project already neutralises the same class of problem for its other fetched
  deps — `set(JSON_Install OFF …)` and `set(INSTALL_GTEST OFF …)` — so ixwebsocket
  is simply the one that was missed.
- **N2:** `cxx_std_17` is never attached to the exported targets, so the
  generated `cryptocogsTargets.cmake` carries no `INTERFACE_COMPILE_FEATURES`.

## 3. The fixes

- **N1** — in the top-level `CMakeLists.txt`, before
  `FetchContent_MakeAvailable(ixwebsocket)` (alongside the existing
  `USE_TLS` / `USE_OPEN_SSL` sets):
  ```cmake
  set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
  ```
  This disables ixwebsocket's `install(TARGETS …)`, its config/`.pc`/export, and
  its header install in one shot — exactly mirroring `INSTALL_GTEST OFF`. Tighten
  the adjacent comment so it's accurate (it currently *claims* the behaviour the
  fix actually delivers).
- **N2** — in `src/CMakeLists.txt`, attach the standard as an exported
  **interface** feature on each library target:
  ```cmake
  target_compile_features(exchange_common PUBLIC cxx_std_17)
  target_compile_features(exchange_http   PUBLIC cxx_std_17)
  target_compile_features(kraken          PUBLIC cxx_std_17)   # if built
  target_compile_features(binance         PUBLIC cxx_std_17)   # if built
  ```
  `cxx_std_17` is a *minimum* (consumers may still use C++20/23). Putting it on
  the always-built `exchange_common`/`exchange_http` already covers the adapters
  transitively (they link both `PUBLIC`); the explicit per-target lines make the
  contract obvious and survive future link-graph changes.

## 4. Steps (each = checkpoint commit; full build + `ctest` must pass before the next)

Build/test command (per project memory — Homebrew OpenSSL):
```
cmake -B build -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3
cmake --build build -j && ( cd build && ctest --output-on-failure )
```

- **Step 0 — Baseline.** Branch `feature/cmake-install-nits`. Build + `ctest`
  (expect 312). Install into a temp prefix and **record the current bad state**:
  `find <prefix> -iname 'IX*' -o -name 'libixwebsocket*' -o -path '*ixwebsocket*'`
  is non-empty; a consumer that omits `CMAKE_CXX_STANDARD` fails to compile.
  *Done when:* baseline captured.

- **Step 1 — N1: stop installing ixwebsocket.** Add `set(IXWEBSOCKET_INSTALL OFF
  CACHE BOOL "" FORCE)`; tighten the comment. Reconfigure, build, `ctest` (312),
  re-install to a clean prefix.
  *Done when:* `find <prefix> \( -iname 'libixwebsocket*' -o -path '*cmake/ixwebsocket*'
  -o -path '*pkgconfig/ixwebsocket*' -o -path '*include/ixwebsocket*' \)` returns
  **nothing**; `ls <prefix>/lib/*.a` shows exactly the four cryptocogs libs;
  312/312 still green; examples still build (they link the in-tree ixwebsocket
  target, unaffected).

- **Step 2 — N2: export the C++17 requirement.** Add the four
  `target_compile_features(... PUBLIC cxx_std_17)` lines. Reconfigure, build,
  `ctest` (312), re-install.
  *Done when:* the generated `cryptocogsTargets.cmake` carries
  `INTERFACE_COMPILE_FEATURES … cxx_std_17`; the plan declarative-headers consumer
  (`main.cpp` + `find_package(cryptocogs)`) **configures and builds with no
  `CMAKE_CXX_STANDARD` set** and runs.

- **Step 3 — Full matrix + docs.** Reconfirm the flag matrix installs clean and
  consumer-links in all three configs: both-on (312), `KRAKEN=OFF` (147),
  `BINANCE=OFF` (176) — each install prefix ixwebsocket-free and consumer-buildable
  without a consumer-set standard. Update CLAUDE.md's install paragraph (the
  "ixwebsocket … not installed" / "use `-DCRYPTOCOGS_BUILD_TESTS=OFF` for a clean
  install" wording — a clean install no longer *requires* `TESTS=OFF`). Optional
  doc touch-up: the CLAUDE.md dependency table still says ixwebsocket `v11.4.6`
  but the build pins `v12.0.0` — fix if cheap. Flip this plan + index to Done.
  *Done when:* matrix clean; docs accurate.

## 5. Verification approach (no new unit tests)

This is a build-system change with **no library code change**, so there are no
new gtest unit tests — `cxx_std_17` and install contents aren't gtest-testable.
The verification is (a) the existing **312-test suite stays green** at every
checkpoint (proves the build itself isn't regressed), plus (b) two scripted
install assertions that become the plan's acceptance gate:
1. install prefix is ixwebsocket-free and contains only the four cryptocogs
   archives + cryptocogs/nlohmann headers + the `cryptocogs` cmake package;
2. a `find_package(cryptocogs)` consumer with **no** `CMAKE_CXX_STANDARD` set
   builds, links `cryptocogs::kraken` + `cryptocogs::binance`, and runs.

## 6. Self-review — risks & assumptions

- **N1 safety.** Disabling ixwebsocket's install is safe *only because* nothing
  cryptocogs installs depends on it: the export set doesn't reference ixwebsocket
  (verified), the four core libs carry no ixwebsocket symbols (plan declarative-headers `nm`
  check), and examples/tests link the in-tree target, not the installed one. If a
  future change made a *core* lib link ixwebsocket, this would need revisiting —
  but that would also break the deliberate "libs are ixwebsocket-free" invariant
  first.
- **N1 assumption.** The `IXWEBSOCKET_INSTALL` option exists at the pinned tag
  `v12.0.0` (verified in the fetched source). A future tag bump must re-verify the
  option name; otherwise the install pollution silently returns. Step 1's gate
  catches that.
- **N2 over-constraint?** No — `cxx_std_17` is a floor, not a pin; consumers on
  C++20/23 are unaffected. It does *not* force `-std=c++17` over a higher consumer
  standard.
- **Interaction with `CRYPTOCOGS_INSTALL` as a subproject.** When cryptocogs is
  itself FetchContent'd, `CRYPTOCOGS_INSTALL` defaults OFF, so neither fix changes
  subproject behaviour; both only affect a top-level install. The compile-feature
  is still useful in-tree (it documents the requirement on the targets).
- **Scope.** Only the two named nits. If Step 3's clean-prefix assertion surfaces
  *other* fetched-dep stragglers (spdlog/CLI11/backward-cpp install rules), those
  are the same one-line `*_INSTALL OFF` treatment — note them, but they are not in
  this plan's stated scope unless found.

## 7. Out of scope

No library/behaviour changes; no change to which deps are fetched or to the
ixwebsocket version; no change to the public headers. Plan declarative-headers (declarative
headers) is already merged and independent.
