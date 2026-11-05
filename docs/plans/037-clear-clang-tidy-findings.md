# Plan 037 — Clear the clang-tidy findings so the opt-in gate passes

**Status: Approved (2026-11-05); Steps 0 and 1 done, Step 2 next.** Rob's request: *"fix #46 using agents so you can do it in parallel"*.

Closes [#46](https://github.com/tomlin256/cryptocogs/issues/46). Answers the `performance-enum-size` half of
[#38](https://github.com/tomlin256/cryptocogs/issues/38) (D1); #38's two unused includes in `common/ws.hpp` are not in this plan.

The issue is its title, *"Clear the clang-tidy findings so the opt-in gate passes (329 sites, 72 of 77 units)"*.
[Plan 035](035-clang-tidy-opt-in-test-step.md) landed the gate off by default and red, and left the fixes to a plan of their own: this is that
plan. The findings are almost all independent of each other, so the plan is also a division of the work into six workstreams on disjoint
files (D6), each done by an agent, with one integration step that builds, tests and commits.

## Problem

In a tree configured with `-DCRYPTOCOGS_CLANG_TIDY=clang-tidy`, `ctest -R '^clang_tidy_'` fails 72 of 83 tests on the Mac: **329 distinct
findings ("sites") in 47 files**, in this project's own `src/`, `include/`, `tests/unit/` and `tests/examples/`. The gate is off by default, so
the default build and CI never see them. With the change the gate is green on the Mac and in the Linux container: 84 of 84, or 91 of 91 with the
benchmarks (the issue's 83 and 90, plus one unit for the new test of D2); from then on a unit that goes green stays green or its test turns red,
and a CI job (not in this plan) becomes worth adding.

### What I measured (2026-11-05, `f6cb165`)

Homebrew clang-tidy 23.1.4 on the Mac (`ubuntu-dev` has 23.1.6 and GCC 13.3). The gate was read from a configure-only hand-made tree,
`build/bench-macos-arm64` (`Debug`, gate on, benchmarks on), with the issue's own commands; no source was touched.

**1. The baseline holds.** 329 sites, 72 of 83 red, 11 green (the six proofs of the gate and five units), the same numbers as the issue's
baseline at `a94b0e1`. Eight C++ files changed since (the #44 fix, plan 036's fixes), and the only effect is 14 sites that moved by line:
`test_ws_client.cpp` ×9, `test_cryptocom_ws_client.cpp` ×4, `kraken_example.cpp` ×1; none appeared and none went. The issue's first
comment is still the right list to diff against. The default debug tree builds clean and passes 528 of 528 (9 s), and so does the container's `debug-linux-arm64`
(GCC 13.3, 528 of 528, 12 s): the start is green on both.

| Sites | Check | Where |
|---:|---|---|
| 237 | `bugprone-unchecked-optional-access` | tests 176, examples 61 |
| 19 | `performance-unnecessary-value-param` | tests 17, `ws_client.inl` and `reconnect_session.inl` 1 each |
| 16 | `bugprone-exception-escape` | the `main()` of 15 examples; `ws_client.inl:119` (Mac only) |
| 14 | `performance-enum-size` | public headers only |
| 13 | `bugprone-assignment-in-if-condition` | the two REST examples |
| 7 | `cppcoreguidelines-pro-type-member-init` | `kraken/ws_api.hpp` 3, `kapi.cpp` 4 |
| 6 | `bugprone-throwing-static-initialization` | `const std::string` / credentials at namespace scope in three tests, two statics in `backward_init.cpp` |
| 4 + 4 | `bugprone-signed-bitwise`, `performance-inefficient-string-concatenation` | `kraken/auth.cpp`, `coinbase/auth.cpp`; `credentials_file.cpp`, `http_client.cpp` |
| 4 | `bugprone-optional-value-conversion` | `public_ws.cpp`, `ws_client_example.cpp` |
| 2 + 1 + 1 + 1 | `bugprone-empty-catch`; `performance-use-std-move`; `performance-inefficient-vector-operation`; `bugprone-reserved-identifier` | `reconnect_session.inl`; `kraken_example.cpp`; `test_mock_ws_connection.cpp`; `kapi.hpp` |

**2. The benchmarks add 36** (hand-made tree, `CRYPTOCOGS_BUILD_BENCHMARKS=ON`: 90 tests, 79 red, 365 sites, exactly the issue's count). **All 31
`bugprone-throwing-static-initialization` are the `BENCHMARK(...)` lines**: google benchmark's macro declares a static whose initialiser registers
the benchmark, which allocates. The 4 `performance-inefficient-vector-operation` are in the four `BM_VectorPushBack_NoReserve` benchmarks, which
measure the no-`reserve` growth on purpose ([plan 026](026-kraken-reserve-vector-growth.md)). The last is `bench_coinbase_ws.cpp:95`, a by-value parameter.

**3. What clang-tidy 23's dataflow analysis accepts for `std::optional`** (scratch probes on the Mac, repeated with the container's 23.1.6 and
GCC 13.3 `-Wall -Wextra -Wpedantic -Werror` for the macro of D2). This decides the largest bucket, 237 of 329.

| Pattern after which `opt` is dereferenced | Reported |
|---|---|
| `ASSERT_TRUE(opt.has_value());`, `ASSERT_TRUE(opt);`, `ASSERT_NE(opt, std::nullopt);`, with or without `<< "…"` | **yes**, at each later use. The cause is not established: gtest's assertion branches on an `AssertionResult`, but a hand-written copy of that expansion in a project header *is* followed (Step 1's mutants), so what proves the macro is followed is the gate's own unit for its test, not a belief about gtest |
| `opt.value()` in place of `*opt` or `opt->` | **yes**: the issue's claim, confirmed |
| `if (!opt.has_value()) { FAIL() << …; }` | no |
| the same check in a macro, on a member chain (`resp.result`), a local captured by reference in a lambda, `*shared_ptr_to_optional`, two optionals one after the other | no |
| a helper `const T& unwrap(const std::optional<T>&)` with one visible check inside | no |
| an unguarded `EXPECT_EQ(opt->a, 1)` in the same test | **yes**, so the macro does not blind the check |

**3b. `NOLINTBEGIN`/`NOLINTEND` covers a `BENCHMARK(...)` expansion** (D5(a)): in a probe with google benchmark as a plain `-I`, as the build
passes it, a pair around two registrations silences those two and a third, after the pair, is still reported; a one-line `// NOLINT(…)` works
too. (The same probe also shows `benchmark.h:508` reporting a finding of its own, `stream_init_anchor`: that is a dependency header, which
`.clang-tidy`'s `ExcludeHeaderFilterRegex` keeps out of the gate, so there is nothing to do.)

**4. The `main()`s**: `backward_init.cpp` installs a `std::set_terminate` handler that prints the backtrace of the **throw site** (an unhandled
exception on the main thread is not unwound before `terminate` runs). A `catch` in `main` discards that trace for main-thread exceptions; D4 is the
choice.

**5. The three editor-only hints** of #46 in `ix_ws_connection.hpp` are still not reported by the gate: a standalone parse of the header with the
gate's command is clean, 324 findings suppressed as non-user code. Whether any is real is settled in W1 by reading the three lines.

## Decisions to confirm

**Confirmed by Rob, 2026-11-05, as proposed:** the plan; D1 (`: std::uint8_t`; the version of the release that carries it stays his to choose);
D4 (the `main()`s catch and report); the five test and example commits being verified together on the combined tree rather than each alone (D8
and the self-review); and the push of this plan and the comment on [#46](https://github.com/tomlin256/cryptocogs/issues/46#issuecomment-6667307704).
D2, D3, D5, D6, D7 and D8 were not asked about one by one, and stand as written. Nothing in D1 to D8 changed.

- **D1. `performance-enum-size`: give the 14 enums `: std::uint8_t`.** The issue asks for the answer to #38's ABI question first, so it was audited
  (scratch work, nothing in the checkout: record layouts of every complete type in 85 units, on Apple clang 21 / libc++ and GCC 13.3 /
  libstdc++, the deltas identical on both):
  - **Nothing depends on the width.** No `sizeof`, `underlying_type`, `std::hash`, `memcpy`/`bit_cast`, container keyed by one, bit-flag operator,
    stream or `fmt` formatter, or integer cast of any of the 14 (my own grep agrees). nlohmann/json never sees one: with the `to_json`/`from_json` of
    all 14 deleted, 82 of 84 units still compile (the other two have no exchange include path). Every conversion goes through the `to_string` /
    `*_from_string` functions, so **no wire or stored value changes**, and `uint8_t`'s habit of printing as a character has no site to bite.
  - **The layout changes.** An enum goes from 4 bytes to 1 and `std::optional<E>` from 8 to 2. Of 37 structs that hold one, **13 shrink** (three by
    24 bytes on the Mac: `kraken::OrderParams` 440 → 416, `kraken::rest::AddOrderRequest` 448 → 424, `kraken::ws::AddOrderRequest` 472 → 448; ten by 8,
    among them `kraken::Triggers` 32 → 24 and the Binance, Coinbase and Kraken order requests and responses), **24 keep their size** (padding absorbs
    it), and in seven of those 24 a member moves 3 bytes earlier. No alignment changes. All 13 are order-entry structs, built once per order: the
    saving is 2 to 5.5 % of each (25 % of `Triggers`); the structs that carry market data and hold an enum (`kraken::rest::PublicTrade`) keep their
    size; no timing was measured.
  - **A scratch Debug tree with the 14 changed passes 528 of 528,** and the full `.clang-tidy` set over the 77 units differs from today by exactly
    the 14 sites gone and none new; both compilers are warning-free under `-Wall -Wextra -Wpedantic -Werror`, `-Wswitch` included (21 `switch`es over
    them). The one visible effect: gtest prints a failing `EXPECT_EQ` of a scoped enum as raw bytes, `4-byte object <…>` today and
    `1-byte object <00>` after.
  - **The cost is the layout of public headers, and the version is yours.** No document promises ABI stability (the compat-shim plan calls its own
    compatibility source-only, "callers recompile", and `find_package(cryptocogs 1)` accepts any 1.y.z), but #38 calls the change ABI-visible and
    puts the cheap moment at v1.0.0 and the next at a major release. **Nothing here cuts a release:** the change sits on `main` until you do, as a
    1.1.0 or as a 2.0.0, and CLAUDE.md records the layout change.
  - **What goes in:** `: std::uint8_t` after the name of each (`common/types.hpp` ×4, `common/rest.hpp`, `common/ws.hpp`, `kraken/types.hpp` ×4,
    `kraken/ws_api.hpp` ×2, `binance/types.hpp`, `binance/auth.hpp`), `#include <cstdint>` in the four of those that lack it (`common/{types,rest,ws}.hpp`,
    `binance/types.hpp`), and one `static_assert(std::is_same_v<std::underlying_type_t<E>, std::uint8_t>)` per enum in the nearest existing test, so
    the width cannot drift unseen. GCC 15.2 is not measured (the nine `std::optional<E>` members now have a 1-byte payload, the family of
    `-Wmaybe-uninitialized` false positives that `CLAUDE.md` records): Step 5 is where it would show.
  - *Alternative:* leave them `int` and put `// NOLINT(performance-enum-size)` with a comment naming #38 on the 14 `enum class` lines. That keeps the 1.x
    layout and the check stays on for new enums, and it is what you choose if you want the layout stable until a major release; the sizes above are
    the price. (Switching the check off in `.clang-tidy` is the third way, and the worst: it silences it for new enums too.)
- **D2. Optional access in the tests: one macro, `ASSERT_HAS_VALUE(opt)`,** in a new `tests/unit/assert_has_value.hpp`, replaces
  `ASSERT_TRUE(opt.has_value())`. It is the form the analysis sees (a plain `if (opt.has_value()) {} else` whose else branch returns through
  `GTEST_FATAL_FAILURE_`, the macro under gtest's own `ASSERT_TRUE`), streams like gtest's (the message first, whatever is streamed after it on its
  own line), fails the test in the same cases, and names the expression. Where a macro cannot go (a lambda or helper that returns a value), the code
  checks the optional in plain sight (`if (!opt) { ADD_FAILURE() << …; return …; }`). *Alternatives rejected below.*
  **As built (Step 1):** the plan said the macro would carry gtest's `switch (0) case 0: default:` blocker, so that `if (x) ASSERT_HAS_VALUE(o); else …`
  compiles under `-Wdangling-else`. The probes of Step 1 (Apple clang 21, GCC 13.3, `-Wall -Wextra -Wpedantic -Werror`) show that use compiles
  without the blocker, and that the blocker changes one case only: an `if` with no `else` at all (`if (x) ASSERT_HAS_VALUE(o);`), which clang then
  accepts and GCC rejects, with the blocker too, exactly as it rejects gtest's own assertions there. A blocker would let the Mac accept what CI's
  GCC refuses, so the macro has none, and an `if` around it needs an `else` or braces on both compilers.
- **D3. Optional access and `if ((ok = report(resp)))` in the two REST examples.** `report()` returns `const Resp*` (the result when the response is
  ok **and** has one, otherwise `nullptr` after logging), and each branch reads `const auto* r = report(resp); ok = (r != nullptr); if (r) { … r->… }`.
  That clears the 13 assignments in conditions and 58 of the 61 optional accesses in the examples at once; the other three (`kraken_example.cpp`)
  become `if (result.ok && result.result)`.
- **D4. The `main()`s catch and report.** Each of the 15 wraps its body in `try { … } catch (const std::exception& e) { <log "fatal: …">; return
  EXIT_FAILURE; } catch (...) { <log>; return EXIT_FAILURE; }`, using `spdlog` where the example links it and `std::cerr` where it does not. The
  issue says so, and it is a fix. **The cost:** an exception on the main thread no longer reaches `backward_init.cpp`'s terminate handler, so its
  throw-site backtrace is lost for those; exceptions on other threads (the WebSocket I/O threads) and crashes (signals) still go through backward.
  *Alternative:* `// NOLINT(bugprone-exception-escape)` on each `main` with that reason. I recommend the catch, which also gives a demo a clean
  message and a non-zero exit.
- **D5. The closed list of `NOLINT`s.** Only where the tool cannot see the intent, named by check, with a comment that says which: **(a)** the 31
  `BENCHMARK(...)` registrations, one `NOLINTBEGIN`/`NOLINTEND` pair per file around them (google benchmark's own macro; nothing in our code
  to fix); **(b)** the four `BM_VectorPushBack_NoReserve` loops (they measure the missing `reserve`); **(c)** the two empty `catch (...)` in
  `reconnect_session.inl` only if W1 confirms that swallowing a throw from the user's connect or disconnect callback on the worker thread is the
  contract (there is no error channel on that thread), and otherwise they are handled, not silenced. **An agent that wants any other `NOLINT`
  stops and reports it**; it is not added. A throwing static (`backward_init.cpp`, the credentials in three tests) is fixed in the code
  (a `constexpr` literal, or moved into a function-local static, or a `noexcept` initialiser), not silenced.
- **D6. Six workstreams, five in this checkout, one in a worktree.** W2 to W6 (tests, examples, benchmarks) edit disjoint files in this checkout
  and **do not build**: a header or test mid-edit in one workstream would break another's build, and several `make`s in one tree race on the
  dependency files. Each checks its own files with the gate's own command (read-only, against the configure-only `bench-macos-arm64` tree). W1
  (the library) changes what every other unit includes, so it works in a detached `git worktree` of its own (no branch, nothing pushed) with its
  own gate tree and its own build tree, does a full build and `ctest` there, and commits there; I cherry-pick that onto `main`. **Only I build the
  combined tree, run the gate on it, and commit the other five** (Step 3). Agents never commit in this checkout, never reconfigure a tree, never
  edit docs, and never touch a file outside their list.
- **D7. The second check is the Linux container, then GCC 15.2.** Step 4: `ubuntu-dev` (GCC 13.3, clang-tidy 23.1.6): the default suite and the gate,
  both must be green there too. Step 5: GCC 15.2 in a throwaway `ubuntu:26.04` container for **each configuration the workflows build** (`release-`,
  `asan-`, `tsan-linux-…`), because plan 036's Release-only check missed a GCC 15 `tsan` warning in `std::optional`-heavy test code, and this plan edits
  that code.
- **D8. Commits go straight to `main`, one per workstream (and one per step), with the `Claude-Session` trailer and no `Co-Authored-By`, and are
  pushed once the step's checkpoint is green.** Step 3's verification runs on the combined tree and the commits follow it; the five workstreams share
  no file and no commit depends on another, but they are verified together, not each alone (see the self-review). No release or version bump: D1 changes the
  layout of public types, and the version of the release that carries it is yours to choose.

## The change

New:

- **`tests/unit/assert_has_value.hpp`** (the banner, as every source file has) and **`tests/unit/test_assert_has_value.cpp`**, registered in
  `tests/unit/CMakeLists.txt` (`GTest::gtest_main` only, like `test_tick_price`). The tests use gtest's SPI facility (`gtest-spi.h`,
  `EXPECT_FATAL_FAILURE`) to see the macro fail a test without failing the run.
- **`tests/unit/test_kraken_ws_api_defaults.cpp`** (W1, registered in the Kraken block of `tests/unit/CMakeLists.txt`): pins the defaults that
  the `{}` initialisers give `AddOrderRequest`, `SubscribeRequest` and `UnsubscribeRequest`.

Edited, by workstream (sites at `f6cb165`):

| WS | Files | Sites | Done by |
|---|---|---:|---|
| **W1 library** | `include/exchange/**` (9 files with sites: `common/{types,rest,ws}.hpp`, `common/{ws_client,reconnect_session}.inl`, `kraken/{types,ws_api}.hpp`, `binance/{types,auth}.hpp`; and `common/reconnect_session.hpp`, which declares the one signature that changes, `scheduleReconnect(std::string reason = "")`, whose parameter nothing reads), `src/` (4 files: `kraken/auth.cpp`, `coinbase/auth.cpp`, `exchange/common/{credentials_file,http_client}.cpp`); reads, and fixes if real, the three editor hints in `common/ix_ws_connection.hpp`; and the tests that pin what it changes, none of which has a site: `test_kraken_auth.cpp`, `test_signature.cpp`, `test_coinbase_auth.cpp` (base64 vectors), `test_credentials_file.cpp` (message text), `test_ws_reconnect_session.cpp`, `test_order_type.cpp` and `test_binance_types.cpp` (D1's `static_assert`s) | 30 | a worktree agent |
| **W2 unit tests A** | `test_ws_responses.cpp` 60, `test_client.cpp` 9, `test_rest_responses.cpp` 7, `test_binance_rest_responses.cpp` 5, `test_cryptocom_rest_responses.cpp` 1, `test_cryptocom_types.cpp` 1, `test_coinbase_types.cpp` 1 | 84 | an agent |
| **W3 unit tests B** | `test_coinbase_ws_client.cpp` 52, `test_binance_ws_client.cpp` 29, `test_binance_client.cpp` 7, `test_coinbase_client.cpp` 4 | 92 | an agent |
| **W4 unit tests C and the benchmarks** | `test_ws_client.cpp` 10, `test_cryptocom_client.cpp` 6, `test_cryptocom_ws_client.cpp` 4, `test_cryptocom_rest_requests.cpp` 1, `test_mock_ws_connection.cpp` 1; and `tests/benchmarks/bench_*.cpp` (7 files; benchmarks tree only) | 22 + 36 | an agent |
| **W5 REST examples** | `coinbase/coinbase_rest_client_example.cpp` 43, `cryptocom/cryptocom_rest_client_example.cpp` 30 | 73 | an agent |
| **W6 the other examples** | `backward_init.cpp`, `get_balance.cpp`, `kapi.{cpp,hpp}`, `kraken_example.cpp`, `private_{rest,ws}.cpp`, `public_{rest,ws}.cpp`, `rest_client_example.cpp`, `ws_client_example.cpp`, `binance/*` (3), `coinbase/coinbase_ws_client_example.cpp`, `cryptocom/cryptocom_ws_client_example.cpp` | 28 | an agent |

The 329 are W1 to W6 without the benchmarks (30 + 84 + 92 + 22 + 73 + 28). Docs (`CLAUDE.md`, this plan) are Step 6, mine.

How each kind of site is fixed (the agents' brief; the plan, not the agent, decides):

| Check | Fix | Hazard and how it is covered |
|---|---|---|
| `unchecked-optional-access` (tests) | D2: `ASSERT_TRUE(x.has_value())` → `ASSERT_HAS_VALUE(x)`; an `EXPECT_TRUE(x.has_value())` followed by a dereference becomes `ASSERT_HAS_VALUE`; a check on a function's result binds it to a local first (the macro evaluates its argument twice) | the same tests, still passing; a missing optional now fails with the expression's name |
| `unchecked-optional-access`, `assignment-in-if-condition` (examples) | D3 | examples are not unit-tested: compiled by the build, `--help` and offline paths run, `kraken_example` (offline) output compared before and after |
| `exception-escape` (`main`) | D4 | as above; an unknown subcommand or flag still exits as CLI11 decides |
| `unnecessary-value-param` | `const&` when the parameter is only read, `std::move` into the sink when it is stored (the hint's own wording picks); **read the callee first**: a `const&` is unsafe where the callee reads the parameter after something frees what the caller's argument aliases | existing tests; no new one unless a callee does that |
| `throwing-static-initialization` | D5's last sentence | the same tests |
| `signed-bitwise` (base64 in two `auth.cpp`) | cast each operand to `unsigned` before the shift; the shift count in the decoder becomes unsigned | `test_kraken_auth`, `test_signature` (output equal to the legacy reference), `test_coinbase_auth` (RFC 4231); plus RFC 4648 vectors if the helper is reachable from a test |
| `inefficient-string-concatenation` | `append` / `+=` onto one string, reserved | `test_credentials_file` (every error branch, message text); `http_client.cpp` has **no unit test** (libcurl transport; `test_client` uses the mock performer): the header line is checked by a hand-made comparison of the old and new expression on sample input, and the plan says so |
| `member-init` | `{}` default member initialisers, so a default-constructed request is defined | a test per type that can be constructed bare and has a meaningful default |
| `empty-catch` | D5(c) | `test_ws_reconnect_session` |
| `exception-escape` at `ws_client.inl:119` (Mac only) | read clang-tidy's own notes for the closure at that line and fix the cause | `test_ws_client`, and the container run (Step 4) |
| `enum-size` | D1: `: std::uint8_t`, `<cstdint>` where missing | the audit; a `static_assert` on the underlying type of each enum in `test_order_type.cpp` (the canonical four, `HttpRequest::Method`, `FrameKind`, the Kraken enums), `test_binance_types.cpp` (the Binance two) and `test_kraken_ws_api_defaults.cpp` (`SubscribeChannel`, `MessageKind`); the whole suite |
| `optional-value-conversion`, `use-std-move`, `vector-operation`, `reserved-identifier` | the check's own suggestion (`*opt` where the engagement is already checked, `std::move`, `reserve`, a non-reserved guard name) | the same tests |

### The tests

| Test | Runs | Passes when |
|---|---|---|
| `test_assert_has_value` (new, every tree with the tests on; 10 tests) | the macro in `TEST` bodies, and in void lambdas whose failures a `ScopedFakeTestPartResultReporter` (gtest-spi.h) collects, so the exact text is compared | **present:** no failure, the dereference after the macro works, the code after it runs; **empty:** exactly one fatal failure whose text is exactly `o has no value`, and the statement after the macro did not run; **streaming:** `ASSERT_HAS_VALUE(name) << "context"` gives `name has no value`, a newline, then the context; **as a branch of an `if` that has an `else`** (then and else position): compiles and picks the right branch; **forms:** a member chain, a pointer dereference, a local that a callback sets, an `std::optional<std::string>`, the expression named as written |
| the gate's own `clang_tidy_tests_unit_test_assert_has_value` | clang-tidy on that test | green from the start, and the proof that the analysis sees the macro: a mutant that deletes the macro line turns this unit red |
| the 528 existing, and the gate's 84 (91 with the benchmarks) | | all pass at the end; the gate's per-unit tests go green as the workstreams land |

## Steps

Each commit is `type: … (step N/6)` straight to `main`, with the `Claude-Session` trailer and no `Co-Authored-By`. A checkpoint is a configure that
prints nothing a configure of the unchanged tree does not, a full build with no warning (`-Werror` is on), and a full `ctest`, all passing. The gate
goes green only at Step 3, so the checkpoints of Steps 1 and 2 are the **default suite**. Hand-made changes (mutants) are made on a scratch copy
made with `git archive` (not `rsync`, which carried `build*` and filled the disk on 2026-11-04) and never restored; probe scripts stay in the scratch
directory. The agents' helper is `tidy_files.sh <file>…` in the scratch directory: the gate's own command for one unit, read-only.

### Step 0 — Approval

The plan is committed as Proposed. On approval: it is pushed (documentation only, so CI starts no run), #46 gets a comment with the path, and
your answers go into the plan in a second commit (`docs(plan): 037 approved`). **Done when:** `git status` is clean, #46 carries the path, and the
approval, with any change to D1 to D8, is in the plan.

### Step 1 — Start from green, and the macro, test first

Mac `debug-macos-arm64` and container `debug-linux-arm64` are green at the tip (528 of 528 each, measured above; the Mac's debug tree has had its
gate switched off again). At this step the configure, build and `ctest` are run again after each change below. Then `test_assert_has_value.cpp`
and its registration go in first, so the test is red (no header), then `assert_has_value.hpp`.

**Mutants of the macro,** made on copies in the scratch directory, each caught by the test meant to catch it: the condition inverted; a non-fatal
failure for the fatal one (the test that the statement after the macro did not run); the message without the expression; `FAIL() <<` for
`GTEST_FATAL_FAILURE_` (the layout test); gtest's own `ASSERT_TRUE(opt.has_value()) << …` in its place (the gate's unit for that file turns red);
and the macro line deleted from the test (the gate's unit turns red, and nothing else does). **Done when:** the checkpoint is green with 528 + the
new tests on the Mac and in the container; the new test was red first; the six mutants are killed; and `clang_tidy_tests_unit_test_assert_has_value`
is green. Commit: `test: add ASSERT_HAS_VALUE, an optional check that clang-tidy can see (step 1/6)`.

**As built (2026-11-05).** `tests/unit/assert_has_value.hpp`, `tests/unit/test_assert_has_value.cpp` (10 tests; its `capture()` trims the newline
that gtest ends every message with) and the registration in `tests/unit/CMakeLists.txt`. The test was red first (`fatal error:
'assert_has_value.hpp' file not found`). The checkpoint: a configure that prints what the unchanged tree's does, no warning, and **538 of 538** on
the Mac (Apple clang 21) and in the container (GCC 13.3, `-Werror`). The gate: `clang_tidy_tests_unit_test_assert_has_value` passes on the Mac
(clang-tidy 23.1.4) and clang-tidy 23.1.6 in the container reports nothing for the file. The mutants: condition inverted, all 10 tests fail;
non-fatal failure, 4 fail; message without the expression, 5; `FAIL() <<`, 5 (the layout); gtest's own `ASSERT_TRUE(opt.has_value()) << …`, 5 tests
fail and the gate reports 6 `bugprone-unchecked-optional-access`; the macro line deleted from the first test, all 10 tests pass and the gate reports
1, the one kill that is the gate's alone. A hand-written copy of gtest's own expansion (`if (const AssertionResult ar = AssertionResult(opt.has_value()))
; else …`) in the macro's place passes the gate, so nothing here rests on a belief about why gtest's own form fails. The blocker of the plan's
list of mutants does not exist (D2, *as built*).

### Step 2 — The fan-out

Six agents at once (D6), each briefed with its file list, the fix table above, the decisions, and these rules: edit only your files; no build, no
reconfigure, no commit (W1 commits in its worktree), no docs, no `git stash`/`checkout`/`reset`; run `tidy_files.sh` on your units until your
own files show **0** sites (findings in headers another workstream owns are not yours); read a callee before making a parameter `const&`; add no
`NOLINT` beyond D5; follow the banner and naming rules in `CLAUDE.md`. Each reports: the files changed, sites before and after per file, every
`NOLINT` (file:line, check, reason), anything it could not fix and why, and any edit that could change behaviour. W1 also reports its build and
`ctest` result in the worktree and its commit hashes.

**Done when:** all six have reported; `git status` in this checkout shows changes only in the files of W2 to W6; each owned file reports 0 sites;
W1's worktree has a clean full gate under `include/` and `src/` and a green full build and `ctest`; and the `NOLINT`s found are D5's list and no more.

### Step 3 — Integrate and verify

1. Cherry-pick W1's commits onto `main` (disjoint files, so clean), remove the worktree.
2. Mac `debug-macos-arm64`: full build (no warning) and `ctest --preset debug-macos-arm64`: 528 + the new tests, all pass.
3. The gate in `build/bench-macos-arm64`: `ctest -R '^clang_tidy_' -j8`: **91 of 91**, and an empty list of sites (the baseline's 365 less every site fixed:
   the diff against the frozen list of #46 is the whole list, line shifts aside). The default debug tree gets the gate switched on, runs
   `ctest -R '^clang_tidy_'` for **84 of 84**, and has it switched off again (`-DCRYPTOCOGS_CLANG_TIDY=`: the variable stays in the cache). A site that
   remains or appears is fixed by me when it is a line, or sent back to its agent with `SendMessage` when it is a file; never silenced outside D5.
4. Commits, one each, in this order: `fix: clear the clang-tidy findings in the library (step 3/6)` (W1's, already made), then `test: …` for W2,
   W3 and W4 (the tests, then the benchmarks), then `fix: …` for W5 and W6 (the examples).

**Done when:** the default suite passes, the gate is 91 of 91 (benchmarks) and 84 of 84 (default) on the Mac, the site list is empty, the commits
are made and `git status` is clean.

### Step 4 — The second check: the Linux container

`ubuntu-dev`: `cmake --preset debug-linux-arm64`, build, `ctest --preset debug-linux-arm64` (GCC 13.3, `-Werror`): all pass. The gate there is a
hand-made `build/bench-linux-arm64` tree (gate on, benchmarks on, dependency sources from the debug tree's `_deps`, google benchmark's from the Mac
tree's), `ctest -R '^clang_tidy_' -j6`: **91 of 91** with clang-tidy 23.1.6, `ASSERT_HAS_VALUE` included (probed above, re-proved by the gate). A
Linux-only site is fixed the same way; none is expected, as the Mac is the stricter gate. **Done when:** both pass in the container.

### Step 5 — GCC 15.2 as CI builds it, then CI

The throwaway-container recipe of `CLAUDE.md` (*Compiler warnings*): `ubuntu:26.04`, `-DCMAKE_BUILD_TYPE` and `-DCRYPTOCOGS_SANITIZE` as each preset
sets them, built with `-- -k`, for `release-`, `asan-` and `tsan-linux-arm64`. Release and asan run their tests; tsan only compiles (its tests
die with `SIGILL` on this host). Then push, read the `cmake-single-platform.yml` run on the last code commit, and dispatch `sanitizers.yml` once
(`-f which=both`) and read both jobs. **Done when:** the three trees build with no warning, release and asan pass, CI and both sanitizer jobs are
green. A warning from a newer GCC is fixed in code, or, if it is a confirmed false positive, silenced at one site for one compiler with a comment
naming the bug (the rule in `CLAUDE.md`), and I tell you.

### Step 6 — Docs, follow-ups, close

`CLAUDE.md`: the clang-tidy section loses *Red until #46 is done* and says the gate is green, with the numbers; the `ASSERT_HAS_VALUE` convention
under Coding conventions (and the `ASSERT_TRUE(opt.has_value())` trap, with the probe table); the closed `NOLINT` list (D5); the `main()` convention (D4);
the layout rows and the test counts (one more executable). This plan: the Progress table and an Outcome. Then: #46 closed with a comment (the
numbers and the commits); #38 gets D1's outcome; one issue raised for **a clang-tidy CI job** (plan 035 left it out on purpose; the gate is now
green, so it is small: `clang-tidy-23` from apt.llvm.org, configure only, `ctest -R '^clang_tidy_'`), labelled `enhancement`. Commit:
`docs: the clang-tidy gate is green (step 6/6)`. **Done when:** the docs say what the tree does, #46 is closed, and the follow-up issue exists.

## Self-review

**Risks**

- **Five agents in one checkout.** The ownership is by file and the briefs name the files, but nothing in git enforces it. Step 2's check
  (`git status` shows only owned files) catches a stray edit; one that lands in another workstream's file is the dangerous case, because it
  mixes with that agent's work. The brief says so, and the file lists are disjoint by construction (the table sums to 329).
- **The agents do not build.** A compile error shows in their gate run as a clang-tidy error, so it is caught early; a link error or a failing test
  is caught only in Step 3, by me, and goes back to the owning agent. This is the price of not racing five `make`s in one tree.
- **The five workstream commits are verified together, not each alone.** Step 3's full build, `ctest` and gate run on the combined tree, then the
  commits follow. The workstreams share no file, W2 to W6 change no library code, and W1 was built and tested alone in its worktree, so any subset
  builds; but the rule says each checkpoint is verified, and this is a deviation from its letter that you accept or refuse at approval.
- **`ASSERT_HAS_VALUE` evaluates its argument twice** (once in the test, once in the later use), like `ASSERT_TRUE(x.has_value())` followed by `x->…`
  did. A call such as `ASSERT_HAS_VALUE(f())` would check one object and use another: the brief says to bind a call to a local first, and the
  gate reports the later use, because it cannot see a check on a different object.
- **The analysis could change.** The probes are on 23.1.4 (Mac) and 23.1.6 (container); `clang_tidy_version` holds the major at 23 and
  `clang_tidy_tests_unit_test_assert_has_value` turns red if a future release stops seeing the macro, which is the point of that unit.
- **A `const&` in place of a by-value parameter is not always safe.** The hazard is a callee that reads the parameter after something frees what
  the caller's argument aliases (a reference into a container that the call mutates). Most sites are lambdas in tests that store or compare the
  argument; the brief says to read the callee, to `std::move` into a sink where the parameter is stored, and to report any callee that does the
  dangerous thing instead of fixing around it.
- **The enum change (D1) is layout-visible.** 13 order-entry structs shrink and seven members move; nothing in the repo depends on it (audited) and
  nothing is released here, but a consumer that mixes headers and libraries of different versions would see it, which is why D1 leaves the version to
  you. Nine `std::optional<E>` members get a 1-byte payload, which is the shape GCC 15 once gave a `-Wmaybe-uninitialized` false positive for (plan
  036): Step 5's three configurations are where that would show, and the rule for a false positive (one site, one compiler, a comment naming the
  bug) applies.
- **The base64 shifts are in the signing path.** A wrong cast changes every signature. The tests compare output with the legacy reference and an
  RFC vector, so a change is caught, and the signed-bitwise edit is the one the brief marks as read-twice.
- **`http_client.cpp` has no unit test.** The header-building edit is small and checked by hand on sample input; it is the one changed function
  here that no test reaches, and the plan says so rather than adding a network test.
- **GCC 15.2 and `std::optional`.** This plan edits the test code where GCC 15 once gave `-Wmaybe-uninitialized` under `tsan` (plan 036). Step 5
  builds all three workflow configurations for that reason.
- **Six agents use CPU and disk together** (12 cores, 64 GB free; a clang-tidy unit peaks near 0.5 GB). W1's build tree is about 1 GB in the
  scratch directory, and is removed with its worktree.
- **A header edit rebuilds everything.** Expected: one W1 commit touches 9 headers, and the next build of every tree takes the full time once.

**Assumptions**

- The gate's findings are deterministic for a given source and tool, so "0 sites in my files" in a run before the integration is still 0 after it,
  except where another workstream's header change reaches the file; Step 3's full gate is the check.
- The configure-only tree `build/bench-macos-arm64` has the same compile commands as the default debug tree plus the benchmarks, so a gate run
  there is the gate run of the default tree plus seven units.
- `git worktree add --detach` and a `FETCHCONTENT_SOURCE_DIR_*` override per dependency configure offline, as plan 036's scratch trees did.
- A `NOLINTBEGIN`/`NOLINTEND` pair suppresses a finding that clang-tidy reports at the expansion of a macro in the same file (probed, 3b above).

## Not in this plan

- **A clang-tidy CI job.** Step 6 raises the issue; plan 035 left it out until the gate was green.
- **#38's two unused includes** (`<cstdio>`, `<type_traits>` in `common/ws.hpp`): a clangd analysis, not a tidy check.
- **Wider clang-tidy checks** (`readability-*`, `modernize-*`, `cppcoreguidelines-*` beyond `pro-type-member-init`): each is a decision with
  findings of its own, and `.clang-tidy`'s list does not change here unless D1 says so.
- **A release.** Nothing is installed differently unless D1 changes a public type.

## Designs I rejected

- **A `NOLINT(bugprone-unchecked-optional-access)` on each of the 237 sites, or the check switched off.** The check finds a real class of bug and
  the macro makes the intent visible instead of hiding it; the issue says fixed, not silenced.
- **A helper `unwrap(opt)` at every use.** It works (probed), and touches all 237 uses where the macro replaces one line per check.
- **`opt.value()` in place of `*opt` / `->`.** Still reported (probed).
- **One agent, in order.** Slower, and the plan's size (47 files) is exactly the case for the split; the risk of the split is the ownership rule above.
- **A worktree for every agent.** Each would need its own configure and full build (about 1 GB and minutes) to verify what I verify once on
  the combined tree; only W1 changes what the others compile, which is why only W1 gets one.
- **Agents building in this checkout.** Concurrent `make`s on one tree race on its dependency files, and a header mid-edit breaks every other
  agent's build.
- **A shared `run_example()` helper for the `main()`s.** It would be one more header and a test, to save six lines in each of 15 examples that are
  not meant to share code.

## Progress

| Step | Status | Notes |
|---|---|---|
| 0 Approval | Done (2026-11-05) | Approved as proposed (D1 `: std::uint8_t`, D4 catch and report). The plan is pushed, and #46 carries its path |
| 1 Start from green, and the macro, test first | Done (2026-11-05) | `ASSERT_HAS_VALUE` and its 10 tests: 538 of 538 on the Mac and in the container; test red first; six mutants killed, one by the gate alone; no blocker (D2, *as built*) |
| 2 The fan-out | Pending | |
| 3 Integrate and verify | Pending | |
| 4 The second check: the Linux container | Pending | |
| 5 GCC 15.2 as CI builds it, then CI | Pending | |
| 6 Docs, follow-ups, close | Pending | |
