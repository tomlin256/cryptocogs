# CLAUDE.md — cryptocogs

Type-safe C++23 client library for the Kraken, Binance, Coinbase (Exchange API) and Crypto.com (Exchange v1) Spot REST and WebSocket APIs; each request type is bound to its response type at compile time. MIT License. This repository is public and stands alone: code, comments, tests, docs, issues and commit messages describe the library only; never name or describe an application that uses it.

Every `.hpp`, `.inl` and `.cpp` begins with this banner (before `#pragma once` or the first `#include`), as every existing file does:

```cpp
// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
```

## Layout

Headers are in `include/exchange/<tier>/`, bodies in `src/`.

- `common/` (`exchange::`, `::rest::`, `::ws::`) is the scaffold; an adapter never edits it. The generic WS client and reconnect machinery, `TickPrice`, the canonical enums and `read_toml_credentials` are the curl-free `exchange_common`; the shared libcurl transport (`CurlHttpClient`, `HttpTimeouts`) is `exchange_http`.
- `kraken/`, `binance/`, `coinbase/`, `cryptocom/` (`exchange::<venue>::`, `::rest::`, `::ws::`; the namespace of a header is its directory) are peer libraries behind `CRYPTOCOGS_BUILD_<VENUE>`. Each links `exchange_common`, `exchange_http` and OpenSSL (PUBLIC: `auth.hpp` declares HMAC helpers); none links another. `exchange::kraken::*` is the only Kraken surface.
- Headers are declarative (plan 016): non-template bodies in `src/`, template bodies in a sibling `.inl`. Exception: `ix_ws_connection.hpp` is header-only so that no library links ixwebsocket; a caller that uses `IxWsConnection` links it itself.
- Read the comment block at the top of an adapter header (`auth.hpp`, `rest_api.hpp`, `ws*.hpp`) before editing it: wire formats, signing and venue quirks are written there. Binance captures: `docs/plans/001-appendix-binance-message-formats.md`.
- Shapes. Kraken has two frame classifiers: `kraken_frame_descriptor` binds `ExchangeWsClient`, `identify_message` / `MessageKind` serves callers that handle raw frames. Binance has two WS protocols (streams, trading API), each an `ExchangeWsClient` alias. Coinbase (Exchange API, ex-Coinbase Pro) places orders over REST only; its feed has no correlation id, so `CoinbaseStreamClient` is bespoke pub/sub, not an `ExchangeWsClient` (plan 018 §2); FIX is deferred (plan 019). Crypto.com places orders over REST; its WS is an `ExchangeWsClient` behind a `HeartbeatResponder` decorator (plan 020 §2); advanced orders, bots, staking, fiat and margin are out of scope.
- A new venue follows `docs/agent-add-exchange.md`. A new Kraken endpoint, WS method or channel copies an existing one (`rest_api.hpp`, `ws_api.hpp`) with tests in the matching `test_*` file; a new WS message needs a `MessageKind` value handled in **both** `identify_message()` and `kraken_frame_descriptor()` (which needs the `correlation_id` / `route_key`).
- Plans are `docs/plans/NNN-*.md`, one per non-trivial change, linked both ways with its GitHub issue; the issue is the index and there is no index file. Procedures read for one kind of task are in `docs/dev-recipes.md`.

## Build and test

CMake 4.0+ (a plain `cmake_minimum_required`; `test_cmake_floor` holds one minimum in the checkout and "CMake 4.0 or later" in the README), a C++23 compiler, system OpenSSL, libcurl and zlib (`libssl-dev libcurl4-openssl-dev zlib1g-dev`; Ubuntu's libcurl package only suggests zlib, which ixwebsocket needs). Everything else is fetched at configure time (pins in `CMakeLists.txt`, `tests/CMakeLists.txt`). `CMakePresets.json` stays at `cmakeMinimumRequired` 3.21 on purpose: it names which CMake can read the file, and 4.0 would turn CMake 3's "CMake 4.0 or higher is required" into "major version 4 must be less than 3".

```bash
cmake --preset debug-macos-arm64 && cmake --build --preset debug-macos-arm64 && ctest --preset debug-macos-arm64
cmake --list-presets      # this host's trees; a test preset adds --output-on-failure, a 120 s timeout, "no tests" = error
cmake -S . -B build/release-macos-arm64 -DCMAKE_BUILD_TYPE=Release -DCRYPTOCOGS_BUILD_TESTS=OFF   # hand-made tree, library only
```

Executables land in `<tree>/bin/`, libraries in `<tree>/src/`. Options (`CMakeLists.txt`):

- `CRYPTOCOGS_BUILD_{KRAKEN,BINANCE,COINBASE,CRYPTOCOM}` (ON) are independent and each drops its library, tests and examples; `exchange_common` and `exchange_http` are always built; all four OFF warns "nothing will be built".
- `CRYPTOCOGS_BUILD_TESTS` (ON): unit tests and examples; the test-only dependencies are fetched only then. `CRYPTOCOGS_BUILD_BENCHMARKS` (OFF): google-benchmark binaries, independent of TESTS.
- `CRYPTOCOGS_WARNINGS_AS_ERRORS` (ON at top level), `CRYPTOCOGS_SANITIZE` (OFF), `CRYPTOCOGS_CLANG_TIDY` and `_ARGS` (empty): below.
- `CRYPTOCOGS_INSTALL` (ON at top level, OFF as a subproject): install rules and the package config; `find_package(cryptocogs 2)` accepts any 2.y.z (`SameMajorVersion`, from `project(CRYPTOCOGS VERSION …)`; `test_package_version`). nlohmann_json's headers are vendored into the prefix by our own install block (`JSON_Install` OFF keeps its own rules out); every other fetched dependency must have its own install suppressed (`IXWEBSOCKET_INSTALL`, `INSTALL_GTEST`, `TOMLPLUSPLUS_INSTALL` OFF) so that the prefix is cryptocogs-only.
- A caller of any REST client calls `curl_global_init(CURL_GLOBAL_ALL)` before the first client and `curl_global_cleanup()` after the last.

### Build trees

One tree per kind and platform, `build/<kind>-<os>-<arch>`: `<kind>` is one lower-case word, `<os>` is `macos` or `linux`, `<arch>` is `arm64` or `x86_64` (`aarch64` is spelled `arm64`, `amd64` is `x86_64`). Kinds: `release`, `debug`, `asan`, `tsan` (RelWithDebInfo plus `CRYPTOCOGS_SANITIZE`), each on `macos-arm64`, `linux-arm64` and `linux-x86_64`, with a build and a test preset of the same name. A kind preset sets `CMAKE_BUILD_TYPE` (and the mode) and nothing else, because CI adds its own flags (`test_presets`). `.clangd` reads `build/debug-macos-arm64`. The Linux half is built in the `ubuntu-dev` container.

The guard (top of `CMakeLists.txt`; `cmake/cryptocogs_platform.cmake`; top-level project only; `test_tree_name`, `test_platform_name`) refuses a directory named for another platform than the host's, an `asan-…` / `tsan-…` tree not configured for that mode, and any tree first configured on another platform. A free-form name (`build`, `scratch`) is not examined; a hand-made `<kind>-<os>-<arch>` is checked for its platform only. A refused configure may rewrite the tree's `CMakeCache.txt` as it stood; the next valid configure restores it and rebuilds nothing.

### Sanitizers

`-DCRYPTOCOGS_SANITIZE=asan` (AddressSanitizer + UBSan) or `tsan` instruments the whole tree, fetched dependencies included (the options are set before the first `FetchContent_MakeAvailable`: `cmake/cryptocogs_sanitize.cmake`). One mode per tree: use the presets.

```bash
cmake --preset asan-macos-arm64 && cmake --build --preset asan-macos-arm64 -j
ASAN_OPTIONS=detect_stack_use_after_return=1:strict_string_checks=1 UBSAN_OPTIONS=print_stacktrace=1 ctest --preset asan-macos-arm64
TSAN_OPTIONS=second_deadlock_stack=1 ctest --preset tsan-macos-arm64     # after configuring and building tsan-macos-arm64 the same way
```

In the container (`asan-linux-arm64`, `tsan-linux-arm64`) use CI's options: `ASAN_OPTIONS` plus `detect_leaks=1` (macOS has no LeakSanitizer), `TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1`.

- A sanitizer tree adds `sanitizer_canary_*` and `test_sanitizer_coverage` (every compile unit and link line carries the options); `test_sanitize_flags` and `test_sanitize_option` run in every tree. A red canary means the options no longer reach the compiler or the optimiser deleted the violation (GCC drops an unused sum's UBSan check, so `canary_ubsan` stores it to a volatile). Options set below an `add_subdirectory` leave the canaries green; the compile half of `test_sanitizer_coverage` goes red.
- UBSan aborts (`-fno-sanitize-recover=undefined`): the first report fails the test. TSan reports a race only when it sees both accesses, so a clean TSan run proves less than a clean ASan run. There is no TSan suppression file; adding one needs Rob's OK. Hand-made `-fsanitize=…` trees are unguarded. Proving a leak fix: `docs/dev-recipes.md`.

### clang-tidy (opt-in)

`-DCRYPTOCOGS_CLANG_TIDY=<tool>` registers clang-tidy as ctest tests over the project's own sources; empty (the default) registers nothing. Nothing is built: clang-tidy reads the tree's `compile_commands.json`. The variable stays in the tree's cache.

```bash
cmake --preset debug-macos-arm64 -DCRYPTOCOGS_CLANG_TIDY=clang-tidy
ctest --preset debug-macos-arm64 -R '^clang_tidy_' -j8        # '^clang_tidy_src_binance_' = one adapter; -j6 in the container (a unit peaks near 0.5 GB)
cmake --preset debug-macos-arm64 -DCRYPTOCOGS_CLANG_TIDY=     # off again
```

- Tests: `clang_tidy_<path>` (one per unit: a `.cpp` that a target of this project compiles, read at configure time, never under `_deps/` or generated, so a new adapter needs nothing), `clang_tidy_version`, `clang_tidy_checks`, `clang_tidy_units` and three `clang_tidy_canary_*` (fixtures in `tests/clang_tidy_checks/canaries/`). They prove the gate bites, because a lint gate that silently checks nothing passes. The registration is the last thing in the root `CMakeLists.txt`. `test_clang_tidy_{module,wiring,config}` need no clang-tidy and run in every tree.
- `.clang-tidy` is the only list of checks; clangd reads it too, so `.clangd` carries none, and it has no `WarningsAsErrors` (clangd would show every hint as an error): the tests pass `--warnings-as-errors=*` and `-Wno-error=deprecated-declarations`. Every header the checkout owns must match its `HeaderFilterRegex`. `-Werror` is in the compile commands, so the gate also reports a compiler warning in our code as `clang-diagnostic-*`. The gate reports more than clangd; clangd's three hints in `ix_ws_connection.hpp` are not findings (no ixwebsocket on its include path).
- One LLVM major, 23 (`CRYPTOCOGS_CLANG_TIDY_MAJOR` in `cmake/cryptocogs_clang_tidy.cmake`; `clang_tidy_version` fails on any other): Homebrew's keg-only `llvm` on the Mac (link `clang-tidy` and `clangd` into `/opt/homebrew/bin`; the SDK comes from `xcrun --show-sdk-path`), `clang-tidy-23` from apt.llvm.org in the container. Moving the major is one commit: that number, `LLVM_MAJOR` in `clang-tidy.yml`, and whatever the new major reports.
- Fix a finding. `NOLINT(<check>)` is for a tool that is wrong or cannot see an invariant: by check name, with a comment that says which. The ones that exist are the closed list (`grep -rn NOLINT include src tests`); a new one needs Rob's OK.

### Compiler warnings

Our own targets compile with `-Wall -Wextra -Wpedantic -Werror=deprecated-declarations` and, by default, `-Werror` (`cmake/cryptocogs_warnings.cmake`; plan 036): every build is strict, CI's and the nightly sanitizer jobs' included. `CRYPTOCOGS_WARNINGS_AS_ERRORS` is declared at the top level only, so a project that fetches cryptocogs keeps its own flags. `-DCRYPTOCOGS_WARNINGS_AS_ERRORS=OFF` is the way out when a newer compiler's new warning cannot wait (the deprecation error stays); a tree keeps the value it was configured with, so `cmake --preset <tree> -UCRYPTOCOGS_WARNINGS_AS_ERRORS` adopts the default.

- Fix a warning, never silence it. The one exception is a confirmed compiler false positive: silence it at that one site, for that one compiler, with a comment naming the bug. A warning from a dependency's header in our unit is cured by marking the dependency `SYSTEM` in its `FetchContent_Declare`. Turning a warning off for the whole build needs Rob's OK.
- Each directory of our targets applies the flags itself, as its first act and after any dependency it fetches (`src/`, `tests/`, `tests/benchmarks/`); a fetch added to `tests/CMakeLists.txt` goes above its call. The reasons are in the root `CMakeLists.txt`; `test_warning_option` fails for a directory that misses it, `test_warning_flags` pins the flags.
- The compilers disagree (Apple clang, GCC 13, CI's GCC 15.2): build the whole tree on Apple clang and on GCC before pushing code, in every configuration the workflows build (`release-`, `asan-`, `tsan-linux-…`; a Release tree alone missed a tsan-only failure). Known differences and the container recipe: `docs/dev-recipes.md`.

### CI (`.github/workflows/`)

- `cmake-single-platform.yml` builds and tests `release-linux-x86_64` on `ubuntu-latest` (ccache, fetched-dependencies cache). `clang-tidy.yml` runs the gate on `debug-linux-x86_64` with the benchmarks on. `sanitizers.yml` runs the `asan` and `tsan` x86_64 trees nightly (03:41 UTC) and on demand: `gh workflow run sanitizers.yml --ref main -f which=<asan|tsan|both>`, one `which` at a time (a newer dispatch of the same `which` cancels the one in flight). It is separate so a red run cannot mask the normal gate, and has no caches (a tree restored from the wrong mode would be partly instrumented).
- Documentation-only changes start no run: `paths-ignore` is `docs/**` and `CLAUDE.md` under `push` and `pull_request` in the first two workflows. `README.md` is not listed because `test_cmake_floor` reads it. A push that changes only `CLAUDE.md` starts no run and so skips `test_claude_md_budget`: run ctest before pushing it. `test_ci_paths` holds the four lists equal, each entry a file name or `<dir>/**` that exists and none covering a file a check reads or a CMake or C++ source: a new documentation path goes into all four lists.
- A file that gains a `FetchContent_Declare` call goes into the `hashFiles(...)` list of the `deps-` cache key, on that one line (`test_ci_cache_key`); editing a listed file misses that cache once.
- No workflow passes `-Werror`: it is the default, and `test_warning_option` fails if that changes. `test_ci_clang_tidy` holds `LLVM_MAJOR` equal to `CRYPTOCOGS_CLANG_TIDY_MAJOR` and rejects a `continue-on-error` or an `|| true`.

## Tests

`ctest --preset <tree>`, `-R <regex>` for a subset. Unit tests: `tests/unit/` (gtest); demos: `tests/examples/`; benchmarks: `tests/benchmarks/`; the `cmake -P` checks named in this file: `tests/*_checks/`. A check script's header says what it holds, and it takes the checkout as a parameter so that a hand-made change to a scratch copy (`git archive`, or `rsync --exclude 'build*'`) must fail on the rule it breaks; a new check ships with accept and reject cases, as `test_claude_md_budget_fixtures` does.

- No network and no credentials. All I/O is mocked, except tests that talk to a server of their own on 127.0.0.1 (`test_ix_ws_connection`, `test_http_client`) and one test in each venue's `*_client` test that connects the production client to a `SilentListener` that never answers (proving `HttpTimeouts` reach the transport). No `sleep_for`, polling or wall-clock waits (`test_ws_reconnect_session.cpp` still does both, cryptocogs#42: do not copy it). The helpers (`mock_ws_connection.hpp`, `callback_probe.hpp`, `silent_listener.hpp`, `keepalive_server.hpp`, `assert_has_value.hpp`) explain themselves in their header comments.
- Each venue flag drops only its own suite. The exchange-agnostic tests (`test_tick_price`, `test_credentials_file`, `test_mock_ws_connection`, `test_ix_ws_connection`, `test_assert_has_value`, `test_curl_header_list`, `test_http_client`) are registered outside the venue guards and must build with any single venue on.
- Check an optional with `ASSERT_HAS_VALUE(opt)`, never `ASSERT_TRUE(opt.has_value())`: clang-tidy 23 does not follow gtest's assertion, so every later dereference is `bugprone-unchecked-optional-access`.
- A destroyed `std::function` is still callable in practice and a sanitizer may not notice, so a test of callbacks that could outlive their owner uses `CallbackProbe`. ASan's defaults miss that class; `ASAN_OPTIONS=max_free_fill_size=4096:free_fill_byte=221` reports it (cryptocogs#45).
- No allocating object at namespace scope (`bugprone-throwing-static-initialization`): use a `constexpr` / `std::string_view` or a function-local static; benchmarks wrap their `BENCHMARK(...)` lines in `NOLINTBEGIN` / `NOLINTEND` for the same check.
- An example's `main()` catches `std::exception` and `...`, writes to `std::cerr` (not spdlog: its error path allocates) and returns `EXIT_FAILURE` (`bugprone-exception-escape`). The private Kraken demos read `~/.kraken/default`; every demo takes `--help`.
- `test_kraken_example` holds the frames `kraken_example` prints to `tests/example_checks/kraken_example_ws_frames.txt`; a deliberate change to the demo regenerates that file (the command is in the script's header) and the diff is read.

## Coding conventions

- C++23 only. Every public header refuses to compile below it: a new public header includes `exchange/common/cxx_standard.hpp`, directly or through another `exchange/` header (`test_standard_guard`); a library added to `src/` carries `target_compile_features(<name> PUBLIC cxx_std_23)` (`test_standard_requirement`).
- Conventions for adapter types (optional fields, JSON, enum converters, each venue's own `OrderType` wire string): `docs/agent-add-exchange.md` §2 and §4.
- REST monetary and size fields arrive as JSON strings: `std::stod(j.value("field", "0"))`, not `.get<double>()`. A price or size sent to a venue as text is an exact decimal from `TickPrice` (integer ticks plus a decimal position; `str()` inserts the point in integers, no floating-point formatting).
- Public enums are `enum class E : std::uint8_t` (`performance-enum-size`), with `<cstdint>` and a `static_assert` on the underlying type in the nearest test. An enum member of a request struct has a `{}` initialiser, so a bare request is defined and serialises to a definite frame (`test_kraken_ws_api_defaults`).
- Discard a result with `std::ignore = f();` (`<tuple>`), not `static_cast<void>(f())`: `bugprone-unused-return-value` has `AllowCastToVoid` off.
- `CurlHttpClient::perform` resets its handle first and sets every per-request option after the reset (the reset clears them). A POST or a DELETE is sent at most once: it gets `CURLOPT_FRESH_CONNECT` so that it never rides a reused connection, because libcurl writes a request again on a new connection when a reused one closes with no reply and no option stops that. `CURLOPT_FORBID_REUSE` is not enough (a POST after a GET still rides the GET's connection), and either option on a GET would end connection reuse. Reasons: the comments in `http_client.cpp` and `http_client.hpp`. A per-request flag for read-only POSTs (cryptocogs#59) must never be set on an order.

## Releasing

A release is a `chore: bump version to X.Y.Z` commit (the `project(CRYPTOCOGS VERSION X.Y.Z …)` line of `CMakeLists.txt`, which `test_package_version` derives its table from, and the `GIT_TAG` example in `README.md`; a major also changes the `find_package(cryptocogs N)` lines there), an annotated tag `vX.Y.Z` whose message starts `vX.Y.Z - <summary>`, and a GitHub Release: `gh release create vX.Y.Z --verify-tag --title … --notes-file …`, Markdown notes with a `compare/vPREV...vX.Y.Z` link. A tag alone is not a release. A layout change to a public type is a major (`find_package(cryptocogs N)` is `SameMajorVersion`). What to check before the notes are written: `docs/dev-recipes.md` "Before the release notes".

## Keeping this file short

Rules, commands and traps only. Test counts, timings, run ids, finding counts and plan outcomes rot: they belong in `docs/plans/NNN-*.md` and the issue. What a header, a CMake comment or a check script already says is a pointer here, not a copy. A plan's docs step updates the comment at the declaration it changes, and this file only for a new rule, command or trap. `test_claude_md_budget` fails above 21000 bytes for this file and 38000 characters for any CLAUDE.md.
