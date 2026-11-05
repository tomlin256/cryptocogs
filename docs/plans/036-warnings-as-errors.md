# Plan 036 — The build's own warnings, and warnings as errors in CI

**Status: Proposed (2026-11-05). Not approved.** Eight decisions below are my defaults; each is a place to edit before approving.

Closes [#47](https://github.com/tomlin256/cryptocogs/issues/47) and [#24](https://github.com/tomlin256/cryptocogs/issues/24) (six discarded
`[[nodiscard]]` results, which are part of the twelve sites below).

The issue is its title, *"Build with warnings as errors, as flywheel-dag does (plan 036, after plan 035)"*. It came from your *"D10 -- i want
warnings as errors"* on approving [plan 035](035-clang-tidy-opt-in-test-step.md), which kept to clang-tidy and left the build's own flags to a
plan of its own. The reference is flywheel-dag's
[compiler_warnings_plan.md](https://github.com/tomlin256/flywheel-dag/blob/main/docs/plans/compiler_warnings_plan.md) (flywheel-dag#4):
`-Wall -Wextra -Wpedantic -Werror=deprecated-declarations` on the project's own targets and an option that adds `-Werror`, off by default and on
in CI. flywheel carries `-Werror=deprecated-declarations` alone. This plan carries flywheel-dag's design here, adapted to a repo whose
dependencies are fetched in two files, which makes **placement** the design question.

## Problem

The build has no warning flag at all: the compile commands carry `-g` or `-O3 -DNDEBUG`, `-std=gnu++23`, `-I`, `-isystem` and, on the Mac,
`-arch`. A discarded `[[nodiscard]]` result, an unused parameter or a dead variable compiles in silence, and the only places that show
one are the editor and clang-tidy's gate (as `clang-diagnostic-*`). There are twelve such sites today, none in `src/` or `include/`.

With the change: the project's own targets compile with `-Wall -Wextra -Wpedantic -Werror=deprecated-declarations`; an option,
`CRYPTOCOGS_WARNINGS_AS_ERRORS`, adds `-Werror`; CI turns it on; a test holds that the flags are on every unit of this project and on no
unit of a dependency; and a project that fetches cryptocogs keeps its own flags.

### What I measured (2026-11-05, `f525b5a`)

Scratch work only: nothing is in the checkout, the copies were made with `git archive` (no `build*`), and the container's scratch tree is
removed. A syntax-only run is `-fsyntax-only` on every unit that is not under `_deps`, with the tree's own flags plus the ones named, counting
distinct diagnostics located in this repo's files.

**1. What the flags find.** The same nine sites as #47 recorded at `6c34c53`, and three more that no default tree builds:

| Compiler and tree | Run | Distinct diagnostics in own code |
|---|---|---|
| Apple clang 21.0.0, `debug-macos-arm64`, 77 units | syntax-only, `-Wall -Wextra -Wpedantic` | **7**: six `-Wunused-result` at `tests/unit/test_ws_client.cpp` 177, 535, 575, 576, 598, 712 ([#24](https://github.com/tomlin256/cryptocogs/issues/24)), and `-Wunused-parameter` at `tests/examples/kraken_example.cpp:36:74` |
| the same, `asan-` and `tsan-macos-arm64` (79 and 78 units, the canaries among them) | the same | the same 7; the canaries add none |
| the same, a scratch tree with `CRYPTOCOGS_BUILD_BENCHMARKS=ON` (84 units) | the same | **10**: those 7, and `-Wunused-result` at `tests/benchmarks/bench_binance_ws.cpp:114`, `bench_cryptocom_ws.cpp:121` and `bench_kraken_ws.cpp:124` |
| GCC 13.3.0, `ubuntu-dev`, `debug-linux-arm64`, 77 units | syntax-only, the same flags | **3**: `kraken_example.cpp:36:74` (`-Wunused-parameter`), and `-Wmissing-field-initializers` at 198:22 and 200:22 (19 `OrderParams` members each; libstdc++'s `std::future::get` is not `[[nodiscard]]`, so the six do not appear) |
| GCC 13.3.0, a full build, `Release` (`-O3 -DNDEBUG`), all 127 units compiled in 51 s at `-j6` | the flags, option off | the same 3, **nothing else**: none of the optimiser-dependent warnings (`-Wmaybe-uninitialized`, `-Wstringop-overflow`, …) appeared, and no dependency unit warned |
| GCC 13.3.0, the four benchmark targets, `Release` | the same | 0 |
| both | `-Werror=deprecated-declarations` | 0 |

So **twelve sites in five files**: `test_ws_client.cpp` ×6, `kraken_example.cpp` ×3, the three benchmark files ×1. #47 said nine, because the
default trees leave the benchmarks out. **Not measured:** GCC 15.2 (CI's compiler; a newer GCC adds warnings, and `-Wmissing-field-initializers`
on designated initialisers is one place where GCC versions differ), an Apple clang other than 21, and the sanitizer trees under GCC.

**2. Where the flags land.** Three placements, each a scratch copy of `f525b5a` configured offline (the dependency sources taken from the
debug tree's `_deps`), classifying the 127 entries of `compile_commands.json`: 77 units of this project (29 under `src/`, 17 examples,
31 unit tests) and 50 of dependencies (ixwebsocket 37, spdlog 7, googletest 4, backward-cpp 2; nlohmann/json, toml++ and CLI11 are
header-only). `-Wpedantic` is the sentinel for a leak, not `-Wall`, because ixwebsocket sets `-Wall` for itself.

| Placement | Own units with the flags | Dependency units with them |
|---|---:|---:|
| A1. `add_compile_options` at the top of the root, before any dependency | 77 of 77 | **50 of 50** |
| A2. one call in the root after its last dependency (googletest), before `add_subdirectory(tests)`: flywheel-dag's placement | **48 of 77**: the 29 units of `src/` miss it, because `add_subdirectory(src)` ran first | **9**: spdlog's 7 and backward-cpp's 2, which `tests/CMakeLists.txt` fetches afterwards |
| B. a call per directory: first in `src/`, after the last fetch in `tests/` (with `example_backward` moved below the fetches), first in `tests/benchmarks/` | 77 of 77 | 0 of 50 |

The benchmarks directory could not be configured offline here (google benchmark is in no tree on this host). It was fetched with the
network and built in the container under B: its 7 units carry the flags. The dependencies' units in that tree are not classified.

**3. The clang-tidy gate with the flags in the compile commands** (placement B, Homebrew clang-tidy 23.1.4). With the option off the flags
add nothing to the gate: 72 of 77 units red, no `clang-diagnostic-*` site, because `.clang-tidy`'s `Checks` does not enable them. With the
option on, so that `-Werror` is in the commands, the same 72 units are red (the same set) and the six proofs pass, and the gate gains nine
`clang-diagnostic-*` errors in units that are already red: the six `unused-result`, the `unused-parameter`, and
`missing-designated-field-initializers` at `kraken_example.cpp` 199:87 and 201:87, which clang-tidy 23 reports at the two lines where
GCC 13.3 warns and Apple clang 21 does not. They are nine of this plan's twelve sites (the benchmarks are in no default tree). So with
the option on the gate sees compiler warnings too, as errors; once the sites are fixed it is back at its baseline. Plan 035's D10 override
(`-Wno-error=deprecated-declarations`) keeps the SDK's libc++ deprecations out, as its M11 and M12 showed.

## Decisions to confirm

- **D1. The flags are flywheel-dag's.** `-Wall -Wextra -Wpedantic -Werror=deprecated-declarations` on GCC and Clang (Apple clang included);
  nothing for MSVC, which no job uses. The deprecation error is always on, with or without the option, as in both siblings; own code has none
  today on either compiler. *Not in this set:* `-Wshadow`, `-Wconversion`, `-Wold-style-cast` and the like, each a decision with findings of
  its own.
- **D2. `-Werror` is an option, off by default and on in CI.** `CRYPTOCOGS_WARNINGS_AS_ERRORS` (declared at the top level only, so a
  consumer's cache never shows a switch that does nothing). A newer local compiler's new warning prints and does not stop your build; CI
  refuses it. That is how I read *"i want warnings as errors"* and what #47 says. *Alternative:* default **on** at the top level, so every
  local build is strict and CI needs no flag. The cost is that an Apple clang or Homebrew update that adds a warning stops your build until
  it is fixed or the option is turned off. It is one word in the root; say which.
- **D3. Placement is per directory (B), with the flags computed once.** The root computes one list (D4) and `src/`, `tests/` and
  `tests/benchmarks/` each apply it with `add_compile_options` at the right place; the measurements above are the reason. Two edits come
  with it: `tests/CMakeLists.txt` creates `example_backward` after the three fetches (today it is the first thing in the file), and the
  rule is written down: *a directory of the project's own targets applies the list at its top, after any dependency it fetches*. A directory
  that forgets is caught by `test_warning_option`, so the rule needs no reviewer.
- **D4. The list lives in a module with a test of its own,** `cmake/cryptocogs_warnings.cmake`, the shape `cryptocogs_platform.cmake`,
  `cryptocogs_sanitize.cmake` and `cryptocogs_clang_tidy.cmake` have, so the mapping (compiler, top level, option → flags) is tested
  under `cmake -P` in milliseconds and the root calls the very function that is tested. *Alternative:* inline in the root, as flywheel-dag
  has it: about ten lines and one test fewer, with the mapping proved only through a configure.
- **D5. Fix the twelve sites first, then the rule for what comes next.** The six `fut.get()` drains in `test_ws_client.cpp` assert on the
  response (`EXPECT_TRUE(fut.get().ok)`; the subscribe one on the pair's ack), which is #24's "stronger test", each test having arranged a
  success reply; the three benchmark setups skip the benchmark with `state.SkipWithError` if the ack fails, where today they would measure
  nothing without saying so; `ws_send`'s unused `creds` gets `[[maybe_unused]]`; the two designated-initialiser lists in `kraken_example.cpp`
  are built by a small local lambda that assigns the four fields. **The rule for anything a compiler finds later:** fix the code. A
  confirmed compiler false positive is suppressed as narrowly as possible: one site, one compiler, a comment naming the bug. A warning that
  comes from a dependency's header in our unit is cured by marking that dependency `SYSTEM` in its `FetchContent_Declare` (its own
  one-line change), not by a flag. Turning a warning off for the whole build asks you first.
- **D6. CI turns the option on in `cmake-single-platform.yml` only,** and a check holds it there. CMake silently accepts an unknown `-D`
  variable, so a misspelt `-DCRYPTOCOGS_WARNING_AS_ERRORS=ON` would leave CI green and ungated: `test_ci_werror` reads the Configure step and
  the root and fails unless the option is named in both. `sanitizers.yml` is left alone: its jobs prove the sanitizers, and `-O2`
  instrumented GCC is where a spurious warning is likeliest. Step 6 builds those trees with the option on and tells you what it found, so
  adding it there is a one-line choice with a measurement. Presets are unchanged: a kind preset sets the build type and nothing else, and
  CI adds its own flags on the command line.
- **D7. CMake's own `-Werror=dev -Werror=deprecated` stay out,** as #47 says: [#26](https://github.com/tomlin256/cryptocogs/issues/26)'s
  pins (backward-cpp v1.6, CLI11 v2.4.2) print CMake deprecation notices at every configure and would fail it.
- **D8. Commits go straight to `main`, one per step, with the `Claude-Session` trailer and no `Co-Authored-By`, and are pushed once the
  step's checkpoint is green.** No release and no version bump: nothing in `include/`, the libraries' sources or the installed package
  changes. If a step finds a site there, I stop and ask. Step 6, which verifies trees that cannot break `main`, comes after the push of the
  mechanical change, as you asked on plan 029.

## The change

New:

- **`cmake/cryptocogs_warnings.cmake`**: one pure function, `cryptocogs_warning_flags(<compiler-id> <top-level> <werror> <out>)`. For GNU,
  Clang and AppleClang at the top level it returns `-Wall;-Wextra;-Wpedantic;-Werror=deprecated-declarations`, and `-Werror` last when
  `<werror>` is true by CMake's rules; for every other compiler, or not at the top level, it returns the empty list. It refuses nothing: an
  unknown compiler is a normal case.
- **`tests/warning_checks/check_warning_flags.cmake`** and **`check_warning_option.cmake`**, **`tests/ci_checks/check_werror.cmake`**, each
  with `cmake_minimum_required(VERSION 4.0)` as `test_cmake_floor` requires. The directory is not called `build_checks` because
  `.gitignore` ignores `build*`.

Edited:

- **Root `CMakeLists.txt`**: a "Compiler warnings" block after the clang-tidy block and before `find_package(OpenSSL)`: the option (top level
  only), the module, and `_cryptocogs_warning_options`, empty in a subproject.
- **`src/CMakeLists.txt`**, **`tests/CMakeLists.txt`** (after `FetchContent_MakeAvailable(CLI11)`, with `example_backward` below it) and
  **`tests/benchmarks/CMakeLists.txt`**: `add_compile_options(${_cryptocogs_warning_options})` with a comment that points here. `tests/unit/`
  and the examples inherit it from `tests/`.
- **`tests/CMakeLists.txt`**: the three registrations. **`.github/workflows/cmake-single-platform.yml`**: the Configure step. **`CLAUDE.md`**
  and **`README.md`**: Step 7.
- **The twelve sites** (D5): `tests/unit/test_ws_client.cpp`, `tests/examples/kraken_example.cpp`, `tests/benchmarks/bench_{binance,cryptocom,kraken}_ws.cpp`.

### The tests

| Test | Runs | Passes when |
|---|---|---|
| `test_warning_flags` (new, every tree) | the function under `cmake -P`, in process | Step 3. **Flags:** GNU, Clang and AppleClang give exactly the four, in that order, at the top level with the option off, and the four then `-Werror` with it on (`ON`, `TRUE`, `1`, `YES`, `Y` are on; `OFF`, `0`, an empty string, `FALSE`, `NOTFOUND` are off); **none:** MSVC, Intel, IntelLLVM, NVHPC and an empty id give the empty list, with the option on too; **not top level:** every id gives the empty list. The module text has no `FetchContent_Declare` (`test_ci_cache_key` reads that literal) |
| `test_warning_option` (new, every tree with the tests on) | the real root, configured offline into a scratch tree (the seven dependency sources of this build, the running compiler and generator passed in, nothing compiled), then `compile_commands.json` read | Step 4. **Top level, option off:** every unit of this project (under the checkout, not under a `_deps` directory, not in the scratch tree) has all four flags and no `-Werror`, with at least one unit each under `src/`, `tests/unit/` and `tests/examples/`; no dependency unit has `-Wpedantic`, `-Werror` or `-Werror=deprecated-declarations`, and at least one dependency unit was seen, so a leak check cannot pass by reading nothing; the cache holds the option as `OFF` and an empty `CMAKE_CXX_FLAGS`. **Option on:** the same, and every own unit has `-Werror`, no dependency unit does. **Subproject:** a parent project that adds the checkout, with the tests off and a one-file executable that links `cryptocogs::common`: no unit of cryptocogs and not the executable's has any of the four flags; the option is not in the parent's cache; `CMAKE_CXX_FLAGS` is as the parent left it; no library of this project has `INTERFACE_COMPILE_OPTIONS`. **Benchmarks:** if this build has google benchmark (`CRYPTOCOGS_BUILD_BENCHMARKS=ON`), one more tree with it, and the benchmark units carry the flags. Paths are compared after `REALPATH` |
| `test_ci_werror` (new, every tree) | reads the workflow and the root | Step 5. The Configure step's command names `-DCRYPTOCOGS_WARNINGS_AS_ERRORS=ON` (not `OFF`, not in a comment) and the root declares `option(CRYPTOCOGS_WARNINGS_AS_ERRORS`; an unreadable file fails and does not pass empty. It reads neither `CLAUDE.md` nor anything under `docs/`, which the workflow's `paths-ignore` names (`test_ci_paths` forbids an ignored file that a check reads) |
| the 524 existing, `test_ws_client` among them | | the six strengthened tests pass |

The Mac's debug tree has 524 tests today. It becomes 527; a Crypto.com-only tree goes from 134 to 137.

## Steps

Each commit is `type: …(step N/7)`, straight to `main`, with the `Claude-Session` trailer and no `Co-Authored-By`. A checkpoint is a
configure that prints nothing a configure of the unchanged tree does not, a full build with no new compiler warning, and a full `ctest`, all
passing. CI is not waited for between steps; Step 5 reads it. Hand-made changes are made on a scratch copy made with `git archive` (not
`rsync`: a copy that carries `build*` filled the disk on 2026-11-04) and never restored. The probe scripts stay in the scratch directory.

### Step 0 — Approval

The plan is committed as Proposed and pushed (documentation only, so CI starts no run), and a comment on #47 links it and corrects its count
of nine. Your answer goes into the plan in a second commit (`docs(plan): 036 approved`). **Done when:** `git status` is clean, #47 carries the
path, and the approval is in the plan.

### Step 1 — Start from green

No commit. Mac: `cmake --preset debug-macos-arm64`, a full build, `ctest --preset debug-macos-arm64`. Container: the same in
`debug-linux-arm64`. Re-run the syntax-only probes above on the tip, so the plan's numbers are the tip's: Apple clang (default, `asan-`,
`tsan-` and a new hand-made `bench-macos-arm64` tree with `CRYPTOCOGS_BUILD_BENCHMARKS=ON`) and GCC 13.3. **Done when:** both trees configure
and build with nothing new and pass 524 of 524, and the probes give 7, 7, 7, 10 and 3, as measured. Anything else: stop and tell you.

### Step 2 — Fix the twelve sites (#24)

| Site | Change |
|---|---|
| `test_ws_client.cpp` 177, 535, 575, 576, 598 | `EXPECT_TRUE(fut.get().ok);` (575 and 576: `fut1` and `fut2`) |
| `test_ws_client.cpp` 712 | `EXPECT_TRUE(fut.get().first.ok);`: `subscribe_async` returns a pair of the ack and the handle |
| `bench_binance_ws.cpp:114`, `bench_cryptocom_ws.cpp:121`, `bench_kraken_ws.cpp:124` | `if (!fut.get().first.ok) { state.SkipWithError("the subscribe ack failed"); return; }` |
| `kraken_example.cpp:36:74` | `[[maybe_unused]]` on `creds`, which a real helper would use |
| `kraken_example.cpp` 198, 200 | a local lambda builds the order from its price; the same four fields are set, so the same JSON |

**Tests:** `test_ws_client.cpp` is modified, six tests strengthened; there is no new test, because the examples and benchmarks are not
unit-tested and the flags (Step 4) are what hold these sites for good. **Hand-made changes**, one at a time on a scratch copy: each of the six
tests with its injected reply made a failure fails alone (and no other test does), and the same for the three benchmarks, which skip with the
message; `kraken_example`'s stdout is compared byte for byte before and after. **Done when:** the checkpoint is green (524 of 524 on the Mac
and in the container); the syntax-only probes with `-Wall -Wextra -Wpedantic -Werror` give **0** on all five (Apple clang default, `asan-`,
`tsan-`, benchmarks; GCC 13.3), where Step 1 gave 7, 7, 7, 10 and 3; the six strengthened tests and the example pass, and the hand-made
changes fail as stated; and the clang-tidy gate (on, in a scratch tree configured with `-DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror'`,
which is what Step 4's option will put in the commands) is at its baseline: 5 green, 72 red, no `clang-diagnostic-*` site, where before the
fixes it showed the nine. Commit: `fix: build the tests, example and benchmarks without compiler warnings (step 2/7)`.

### Step 3 — The module, test first

`tests/warning_checks/check_warning_flags.cmake` and its registration come first, so the test is red (the module does not exist), then
`cmake/cryptocogs_warnings.cmake`. Cases as in the table above. **Done when:** the checkpoint is green with 525 of 525 on the Mac and in the
container; the new test was red first; and hand-made mutants of the function are each caught by the assertion meant to catch them, none
surviving: `-Wall`, `-Wextra`, `-Wpedantic` or `-Werror=deprecated-declarations` dropped one at a time; `-Werror` first, or added with the
option off, or missing with it on; `GNU` (or `Clang`, or `AppleClang`) left out of the match, or `Intel` or `MSVC` let in; the top-level
test dropped; an option value tested with `STREQUAL "ON"` in place of CMake's truthiness. Commit: `build: add cmake/cryptocogs_warnings.cmake and
the test that holds it (step 3/7)`.

### Step 4 — The flags on, placed, and guarded

`check_warning_option.cmake` and its registration first, so the test is red (no flags yet, and the test names the 77 units); then the root
block, the three calls and the `example_backward` move.

| Test | Asserts |
|---|---|
| `test_warning_option` (new) | as the table above |
| the 525 existing | pass |

**Done when:**

- **Option off (the default):** the checkpoint is green, 526 of 526 on the Mac and in the container; the configure prints what Step 1's did;
  the compile database differs from Step 1's in exactly the 77 own units, each gaining the four flags, and the 50 dependency commands are
  byte-identical.
- **Option on** (`-DCRYPTOCOGS_WARNINGS_AS_ERRORS=ON`): a full build prints no warning and ctest passes 526 of 526, on the Mac (`debug-` and
  `release-macos-arm64`) and in the container (`debug-` and `release-linux-arm64`: GCC 13.3 at `-O3` with `-Werror`). The `release-`
  trees are the nearest local match to CI's.
- **The gate:** with `-DCRYPTOCOGS_CLANG_TIDY=clang-tidy` on a tree with the option on, the picture is plan 035's: 5 green, 72 red, the six
  proofs pass, no `clang-diagnostic-*` site.
- **Hand-made changes**, each on a scratch copy, fail exactly the case stated:

  | Change | Fails |
  |---|---|
  | M1 the `src/` call removed | `test_warning_option`, naming the 29 units of `src/` |
  | M2 the `tests/` call removed | `test_warning_option`, naming the 48 units under `tests/unit/` and `tests/examples/` |
  | M3 the `tests/` call placed before the three fetches | `test_warning_option`: `-Wpedantic` on spdlog's and backward-cpp's units |
  | M4 `example_backward` left above the call | `test_warning_option`, naming `tests/examples/backward_init.cpp` |
  | M5 the three calls replaced by one in the root after its last dependency (A2) | `test_warning_option`: the 29 `src/` units lack the flags, 9 dependency units have them |
  | M6 the three calls replaced by one at the top of the root (A1) | `test_warning_option`: 50 dependency units have them |
  | M7 the `-Werror` branch removed from the module | `test_warning_flags` and the option-on case |
  | M8 the top-level test dropped (flags in a subproject) | the subproject case |
  | M9 `target_compile_options(exchange_common INTERFACE -Wall)` | the subproject case: the consumer's unit has `-Wall`, and `INTERFACE_COMPILE_OPTIONS` is not empty |
  | M10 `CMAKE_CXX_FLAGS` set to the flags, in the cache with `FORCE` | the dependency units and the subproject case |
  | M11 a scratch `tests/extra/` with one executable and no call | `test_warning_option`, naming its unit |
  | M12 the `tests/benchmarks/` call removed | the benchmark case, in a benchmarks-on tree |

Commit: `build: build the project's own targets with -Wall -Wextra -Wpedantic (step 4/7)`.

### Step 5 — CI turns the option on

`tests/ci_checks/check_werror.cmake` and its registration first (red: the workflow has no flag), then the Configure step of
`cmake-single-platform.yml` gains `-DCRYPTOCOGS_WARNINGS_AS_ERRORS=ON`, with a comment that says why and that it is off by default.
Hand-made changes: the flag dropped, misspelt (`-DCRYPTOCOGS_WARNING_AS_ERRORS=ON`), set `OFF`, left in a comment only, and the option renamed
in the root but not in the workflow each fail `test_ci_werror`. **Done when:** 527 of 527 on the Mac and in the container; the commit is pushed;
and CI's run (`release-linux-x86_64`, GCC 15.2, `-O3`, `-Werror`) is **green with no compiler warning in its log**, 527 of 527. The workflow
edit and the two edited CMake files miss CI's two caches once. If the run is red on a GCC 15 warning, D5's rule applies and I fix forward and
tell you; if the fix is anything but code or a `SYSTEM` mark, I stop. Commit: `ci: make every compiler warning an error in CI (step 5/7)`.

### Step 6 — The trees CI does not build

No commit unless something is found; after the push, as you asked on plan 029 (finish and push the mechanical change, then measure). Each tree is configured with the option on, built in
full and tested: the Mac's `asan-` and `tsan-macos-arm64`; a `bench-macos-arm64` tree (network for google benchmark); in the container
`asan-linux-arm64`, `tsan-linux-arm64` and a benchmarks tree; and GCC 15.2 in a throwaway `ubuntu:26.04` container, as flywheel-dag did, if the
image can be pulled. **Done when:** each builds with no warning and passes, or what it found is recorded in Progress with a recommendation.
The likeliest finding is a sanitizer canary: `canary_asan` is a deliberate use-after-free, which GCC's `-Wuse-after-free` may report once
`-Wall` is on. A finding that needs more than a one-line fix is raised as an issue, with the evidence, and not fixed inline. The same results
settle D6's `sanitizers.yml` question.

### Step 7 — Docs, the sibling check, and close

- **`CLAUDE.md`**: the option row; a "Compiler warnings" subsection after clang-tidy (the flags, the placement rule of D3, D5's rule for
  suppressions, how to build strict locally: `cmake --preset debug-macos-arm64 -DCRYPTOCOGS_WARNINGS_AS_ERRORS=ON`, and that CI does); the
  layout rows for `cmake/cryptocogs_warnings.cmake` and `tests/warning_checks/`; the three tests in the tables and the counts (527 in ctest,
  137 in a Crypto.com-only tree); the CI section; and the clang-tidy section's sentence that the override is "inert until #47", which is now
  exercised.
- **`README.md`**: the option, in the build commands.
- **Sibling repos, read only.** Compare the two ideas that go beyond flywheel-dag's: the offline configure that classifies every unit of a
  tests-on tree (flywheel-dag reads the running tree's database) and the subproject case with a consumer's unit. Raise an issue where it
  applies, with the measurement, and tell you the numbers; edit neither repo.
- **This plan**: Status, Progress and an Outcome. Then `gh issue close 47` and `gh issue close 24` with summaries.

**Done when:** the pushed tip is green and #47 and #24 are closed. Commits: `docs: say what the build warns about (step 7/7)`, then
`docs(plan): 036 is done`.

## Self-review

**Risks**

- **CI's GCC 15.2 is the first compiler of its kind to see the flags with `-Werror`.** Local evidence is GCC 13.3 at `-O3` (clean) and Apple
  clang 21. A newer GCC adds warnings, and its libstdc++ may warn in a dependency header that our unit includes: nlohmann/json is
  a `-I`, not a `-isystem`, include here. D5's rule is the answer, and Step 5 reads the run; a throwaway GCC 15 container (Step 6) would have
  said so earlier, but it is after the push on purpose.
- **`-Werror=deprecated-declarations` is on even with the option off.** A toolchain update that deprecates something a dependency header uses
  stops a local build, as flywheel-dag#60 saw for clang-tidy 23 with nlohmann/json's `char_traits<unsigned char>` (plan 035, D10). Own code has
  none today. The cure is the `SYSTEM` mark of D5, one line per dependency.
- **The Mac is not in CI.** Apple clang's warnings are checked only on your machine until [#29](https://github.com/tomlin256/cryptocogs/issues/29)
  adds a macOS leg; the option protects the GCC build in CI and whichever builds you run with it on.
- **Placement is a rule, and a rule can be forgotten.** A new directory of targets that does not apply the list is not flagged, so
  `test_warning_option` is what notices (M11); one the offline configure does not reach (behind an option the test does not set) is
  noticed only in a tree that sets it, which is why the benchmark case exists.
- **`example_backward` moves.** It is today the first thing in `tests/CMakeLists.txt`; it moves below the three fetches so the flags reach it.
  `add_backward` needs backward-cpp fetched, which it still is. A full build and ctest show the examples still link.
- **The benchmarks are in no default tree.** Their three sites are fixed on the strength of a syntax-only Apple clang run and a GCC build;
  Step 6 builds a benchmarks tree with the option on, and `test_warning_option` has the benchmark case wherever google benchmark is present.
- **The sanitizer canaries are deliberate violations** that now compile under `-Wall`. Apple clang reports nothing for them; GCC is unknown
  until Step 6. The nightly sanitizer jobs do not pass the option, so a finding there cannot turn them red.
- **The strengthened tests could fail for a real reason.** `EXPECT_TRUE(fut.get().ok)` checks what the reply says. If one of the six replies
  is not a success, the test was passing by not looking; that is a finding to report, not to weaken.
- **`test_warning_option` configures the real root up to four times** (about 1.5 s each, no network, nothing compiled) in every tree with the
  tests on, the sanitizer trees and CI included. It passes the running compiler and generator, so what it classifies is what the tree builds.
- **Editing the root and `tests/CMakeLists.txt` misses CI's dependency cache once,** and the changed command lines miss ccache for the 77 own
  units once. `test_ci_cache_key` reads the literal declare call, so the module and the scripts must not contain it, even in a comment.
- **Warnings that only an optimising GCC shows** did not appear at `-O3` on 13.3; they remain possible on 15.2 and on `-O2` instrumented code.

**Assumptions**

- `compile_commands.json` of the Makefiles and Ninja generators has one `command` string per unit, and the root sets
  `CMAKE_EXPORT_COMPILE_COMMANDS` as a normal variable, so it is always written (plan 035 relied on both).
- A source directory override makes `FetchContent_MakeAvailable` call `add_subdirectory` on that directory, so the directory properties the
  placement depends on behave as in an online configure. The measurements above use overrides throughout.
- Nothing is generated at build time, so no target is created that the offline configure cannot see.

## Not in this plan

- **The 329 clang-tidy sites** and a clang-tidy CI job: [#46](https://github.com/tomlin256/cryptocogs/issues/46).
- **CMake's own `-Werror=dev -Werror=deprecated`** (D7), **wider warnings** (D1), and the **dependencies' own warnings**
  ([#26](https://github.com/tomlin256/cryptocogs/issues/26), [#36](https://github.com/tomlin256/cryptocogs/issues/36)): the flags do not reach
  them.
- **A macOS CI leg** ([#29](https://github.com/tomlin256/cryptocogs/issues/29)), and `-Werror` in the sanitizer workflow unless Step 6 and
  you say so (D6).
- **A release.** Nothing in `include/`, the libraries or the package changes (D8).

## Designs I rejected

- **A1, the root, first** (50 of 50 dependency units get the flags) and **A2, the root after its last dependency** (the 29 `src/` units miss
  them and 9 dependency units have them): measured above. A2 is flywheel-dag's placement and is right for a repo whose dependencies are all
  fetched before its own targets.
- **Fetching every dependency in the root, then one call.** It moves three pins (and google benchmark's) between files, empties
  `tests/CMakeLists.txt` of its own fetches, and is a larger change than the three-line one that gives the same result; the CI cache key
  hashes both files.
- **A flag set per target** (a helper called by about sixty targets, or a "warnings" interface library linked by each): a target that
  forgets it is unchecked, which is the failure the directory rule has too, at twenty times the lines. `test_warning_option` would catch both.
- **`-DCMAKE_CXX_FLAGS=-Werror` in CI, or `CMAKE_COMPILE_WARNING_AS_ERROR`.** Both reach the dependencies (the second has the same placement
  problem as `add_compile_options`) and add nothing the option does not.
- **`-Werror` on by default** (D2's alternative), and **the flags in the presets**, which say only what makes each tree's name true.
- **A test of the running tree's own database** besides the offline configure. The same root is configured either way; the benchmark case
  covers the one directory a default tree lacks. The sanitizer feature has both because its dependencies' flags differ per tree.
- **Marking every dependency `SYSTEM` now.** No dependency unit warns today, and a `SYSTEM` mark also changes include order. It is the cure
  if one does (D5).

## Progress

| Step | Status | Notes |
|---|---|---|
| 0 Approval | Proposed | Committed as Proposed and linked from #47, which said nine sites and has twelve. |
| 1 Start from green | Pending | |
| 2 Fix the twelve sites | Pending | |
| 3 The module, test first | Pending | |
| 4 The flags on, placed, and guarded | Pending | |
| 5 CI turns the option on | Pending | |
| 6 The trees CI does not build | Pending | |
| 7 Docs, the sibling check, and close | Pending | |
