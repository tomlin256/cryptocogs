# Plan 036 — The build's own warnings, and warnings as errors

**Status: Done (2026-11-05). Approved the same day with one change, to D2.** Rob: *"i want -Werror on by default (in fact i want that also in
flywheel-dag so raise an issue) then approved"*. D2 now makes `-Werror` the default; D6, D8 and Steps 4 to 6 changed with it, and say so. D1, D3,
D4, D5 and D7 stand as written. The same request for flywheel-dag is [flywheel-dag#67](https://github.com/tomlin256/flywheel-dag/issues/67). The
Progress table at the end records each step, and the Outcome says where the steps differ from this text.

Closes [#47](https://github.com/tomlin256/cryptocogs/issues/47) and [#24](https://github.com/tomlin256/cryptocogs/issues/24) (six discarded
`[[nodiscard]]` results, which are part of the twelve sites below).

The issue is its title, *"Build with warnings as errors, as flywheel-dag does (plan 036, after plan 035)"*. It came from your *"D10 -- i want
warnings as errors"* on approving [plan 035](035-clang-tidy-opt-in-test-step.md), which kept to clang-tidy and left the build's own flags to a
plan of its own. The reference is flywheel-dag's
[compiler_warnings_plan.md](https://github.com/tomlin256/flywheel-dag/blob/main/docs/plans/compiler_warnings_plan.md) (flywheel-dag#4):
`-Wall -Wextra -Wpedantic -Werror=deprecated-declarations` on the project's own targets and an option that adds `-Werror`, off by default and on
in CI (until flywheel-dag#67). This plan carries flywheel-dag's design here, adapted
to a repo whose dependencies are fetched in two files, which makes **placement** the design question, and with the option on by default.

## Problem

The build has no warning flag at all: the compile commands carry `-g` or `-O3 -DNDEBUG`, `-std=gnu++23`, `-I`, `-isystem` and, on the Mac,
`-arch`. A discarded `[[nodiscard]]` result, an unused parameter or a dead variable compiles in silence, and the only places that show
one are the editor and clang-tidy's gate (as `clang-diagnostic-*`). There are twelve such sites today, none in `src/` or `include/`.

With the change: the project's own targets compile with `-Wall -Wextra -Wpedantic -Werror=deprecated-declarations`; an option,
`CRYPTOCOGS_WARNINGS_AS_ERRORS`, adds `-Werror` and is on by default, so every build of this project is strict, CI's among them; a test holds
that the flags are on every unit of this project and on no unit of a dependency, and that the default is on; and a project that fetches
cryptocogs keeps its own flags.

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
  nothing for MSVC, which no job uses. The deprecation error is always on, with or without the option, as in flywheel-dag; own code has none
  today on either compiler. *Not in this set:* `-Wshadow`, `-Wconversion`, `-Wold-style-cast` and the like, each a decision with findings of
  its own.
- **D2 (changed at approval). `-Werror` is on by default; the option relaxes it.** `CRYPTOCOGS_WARNINGS_AS_ERRORS` (declared at the top
  level only, so a consumer's cache never shows a switch that does nothing) defaults to `ON`: every build of this project's own code is
  strict, yours, CI's and the nightly sanitizer jobs'. `-DCRYPTOCOGS_WARNINGS_AS_ERRORS=OFF` relaxes it for a build where a newer
  compiler's new warning would otherwise stop you. The proposal was off by default and on in CI, as flywheel-dag has it; Rob chose on by
  default, and asked for the same there (flywheel-dag#67). **What it forces:** (1) CI needs no flag, so the workflow edit and the check of it
  (`test_ci_werror`) are gone, and `test_warning_option` configures with no `-D` and holds the default in every tree, CI's included; (2) the
  sanitizer workflow is strict too (D6); (3) the order: the flags land first with the default **off**, so that CI's first GCC 15.2 run only
  prints warnings and cannot leave `main` red, and the default flips to on in Step 6, test first, after Step 5 has read that run and built the
  other trees. The cost, which Rob accepted by choosing it, is that an Apple clang or Homebrew update that adds a warning stops a build until
  the warning is fixed or the option is turned off.
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
- **D6 (adjusted by D2). The workflows change nothing,** and no check of them is added. `cmake-single-platform.yml` is strict through the
  default, and `test_warning_option` holds the default in every tree, CI's included; a workflow could only switch it off by passing `=OFF`,
  which is a deliberate edit, and a typo in a `-D` cannot matter when no `-D` is needed. `sanitizers.yml` is strict too, and `-O2`
  instrumented GCC is where a spurious warning is likeliest (`-Wuse-after-free` on `canary_asan`, `-Wtsan` on an `atomic_thread_fence`). So
  Step 5 builds the sanitizer trees with the option on before the default flips, and Step 6 dispatches that workflow once and reads it. If a
  sanitizer tree cannot be made clean by a code fix, the workflow passes `-DCRYPTOCOGS_WARNINGS_AS_ERRORS=OFF` with a comment that says why,
  and I tell you first. Presets are unchanged: a kind preset sets the build type and nothing else.
- **D7. CMake's own `-Werror=dev -Werror=deprecated` stay out,** as #47 says: [#26](https://github.com/tomlin256/cryptocogs/issues/26)'s
  pins (backward-cpp v1.6, CLI11 v2.4.2) print CMake deprecation notices at every configure and would fail it.
- **D8. Commits go straight to `main`, one per step, with the `Claude-Session` trailer and no `Co-Authored-By`, and are pushed once the
  step's checkpoint is green.** No release and no version bump: nothing in `include/`, the libraries' sources or the installed package
  changes. If a step finds a site there, I stop and ask. Step 5, which reads CI's first run and builds the trees CI does not, comes after the
  push of the mechanical change, as you asked on plan 029; the default flips only after it (D2).

## The change

New:

- **`cmake/cryptocogs_warnings.cmake`**: one pure function, `cryptocogs_warning_flags(<compiler-id> <top-level> <werror> <out>)`. For GNU,
  Clang and AppleClang at the top level it returns `-Wall;-Wextra;-Wpedantic;-Werror=deprecated-declarations`, and `-Werror` last when
  `<werror>` is true by CMake's rules; for every other compiler, or not at the top level, it returns the empty list. It refuses nothing: an
  unknown compiler is a normal case.
- **`tests/warning_checks/check_warning_flags.cmake`** and **`check_warning_option.cmake`**, each with `cmake_minimum_required(VERSION 4.0)`
  as `test_cmake_floor` requires. The directory is not called `build_checks` because `.gitignore` ignores `build*`.

Edited:

- **Root `CMakeLists.txt`**: a "Compiler warnings" block after the clang-tidy block and before `find_package(OpenSSL)`: the option (top level
  only; `OFF` from Step 4, `ON` from Step 6), the module, and `_cryptocogs_warning_options`, empty in a subproject.
- **`src/CMakeLists.txt`**, **`tests/CMakeLists.txt`** (after `FetchContent_MakeAvailable(CLI11)`, with `example_backward` below it) and
  **`tests/benchmarks/CMakeLists.txt`**: `add_compile_options(${_cryptocogs_warning_options})` with a comment that points here. `tests/unit/`
  and the examples inherit it from `tests/`.
- **`tests/CMakeLists.txt`**: the two registrations. **`CLAUDE.md`** and **`README.md`**: Step 7. The workflows are not edited (D6).
- **The twelve sites** (D5): `tests/unit/test_ws_client.cpp`, `tests/examples/kraken_example.cpp`, `tests/benchmarks/bench_{binance,cryptocom,kraken}_ws.cpp`.

### The tests

| Test | Runs | Passes when |
|---|---|---|
| `test_warning_flags` (new, every tree) | the function under `cmake -P`, in process | Step 3. **Flags:** GNU, Clang and AppleClang give exactly the four, in that order, at the top level with the option off, and the four then `-Werror` with it on (`ON`, `TRUE`, `1`, `YES`, `Y` are on; `OFF`, `0`, an empty string, `FALSE`, `NOTFOUND` are off); **none:** MSVC, Intel, IntelLLVM, NVHPC and an empty id give the empty list, with the option on too; **not top level:** every id gives the empty list. The module text has no `FetchContent_Declare` (`test_ci_cache_key` reads that literal) |
| `test_warning_option` (new, every tree with the tests on) | the real root, configured offline into a scratch tree (the seven dependency sources of this build, the running compiler and generator passed in, nothing compiled), then `compile_commands.json` read | Steps 4 and 6. **Top level, no option:** every unit of this project (under the checkout, not under a `_deps` directory, not in the scratch tree) has all four flags, with at least one unit each under `src/`, `tests/unit/` and `tests/examples/`; no dependency unit has `-Wpedantic` or `-Werror=deprecated-declarations`, and at least one dependency unit was seen, so a leak check cannot pass by reading nothing (those two stand for the whole list, which one call applies; a dependency that sets `-Werror` for itself, as google benchmark does, cannot be told from a leak); the cache has an empty `CMAKE_CXX_FLAGS`. Every own unit has `-Werror` too from Step 6 (the default is on), and none does in Step 4 (it is off). **Top level, the other value** (`=ON` in Step 4, `=OFF` from Step 6): the same flags, with `-Werror` on every own unit for `ON` and on none for `OFF`. **Subproject:** a parent project that adds the checkout, with the tests off and a one-file executable that links `cryptocogs::common`: no unit of cryptocogs and not the executable's has any of the four flags; the option is not in the parent's cache; `CMAKE_CXX_FLAGS` is as the parent left it; no library of this project has `INTERFACE_COMPILE_OPTIONS`. **Benchmarks:** if this build has google benchmark (`CRYPTOCOGS_BUILD_BENCHMARKS=ON`), one more tree with it, and the benchmark units carry the flags. Paths are compared after `REALPATH` |
| the 524 existing, `test_ws_client` among them | | the six strengthened tests pass |

The Mac's debug tree has 524 tests today. It becomes 526; a Crypto.com-only tree goes from 134 to 136.

## Steps

Each commit is `type: …(step N/7)`, straight to `main`, with the `Claude-Session` trailer and no `Co-Authored-By`. A checkpoint is a
configure that prints nothing a configure of the unchanged tree does not, a full build with no new compiler warning, and a full `ctest`, all
passing. CI is not waited for between steps, except where a step says so (Steps 5 and 6). Hand-made changes are made on a scratch copy made
with `git archive` (not `rsync`: a copy that carries `build*` filled the disk on 2026-11-04) and never restored. The probe scripts stay in the
scratch directory.

### Step 0 — Approval

The plan was committed as Proposed and pushed (`507f80b`, documentation only, so CI starts no run), and a comment on #47 links it and corrects
its count of nine. Rob's answer, with the change to D2, goes into the plan in a second commit (`docs(plan): 036 approved, with D2 changed`),
which is pushed too, and flywheel-dag#67 is raised for the same default there. **Done when:** `git status` is clean, #47 carries the path,
the approval is in the plan, and flywheel-dag#67 exists.

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

### Step 4 — The flags on and placed, the option off for now, and guarded

`check_warning_option.cmake` and its registration first, so the test is red (no flags yet, and the test names the 77 units); then the root
block, the three calls and the `example_backward` move. The option is declared with the default **off** here (D2's order): the flags are
visible in every build and CI's first GCC 15.2 run only prints warnings.

| Test | Asserts |
|---|---|
| `test_warning_option` (new) | as the table above, with Step 4's values: no option has no `-Werror`, `=ON` has it |
| the 525 existing | pass |

**Done when:**

- **No option (off for now):** the checkpoint is green, 526 of 526 on the Mac and in the container; the configure prints what Step 1's did;
  the compile database differs from Step 1's in exactly the 77 own units, each gaining the four flags, and the 50 dependency commands are
  byte-identical.
- **`=ON`** (`-DCRYPTOCOGS_WARNINGS_AS_ERRORS=ON`): a full build prints no warning and ctest passes 526 of 526, on the Mac (`debug-` and
  `release-macos-arm64`) and in the container (`debug-` and `release-linux-arm64`: GCC 13.3 at `-O3` with `-Werror`). The `release-`
  trees are the nearest local match to CI's.
- **Pushed,** and CI's run (`release-linux-x86_64`, GCC 15.2, `-O3`, flags on, `-Werror` off) passes 526 of 526. Step 5 reads its log.
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
  | M7 the `-Werror` branch removed from the module | `test_warning_flags` and the `=ON` case |
  | M8 the top-level test dropped (flags in a subproject) | the subproject case |
  | M9 `target_compile_options(exchange_common INTERFACE -Wall)` | the subproject case: the consumer's unit has `-Wall`, and `INTERFACE_COMPILE_OPTIONS` is not empty |
  | M10 `CMAKE_CXX_FLAGS` set to the flags, in the cache with `FORCE` | the dependency units and the subproject case |
  | M11 a scratch top-level directory `tools/` that the root adds, with one executable and no call | `test_warning_option`, naming its unit (a directory that `tests/` adds after its own call inherits the flags, which is by design) |
  | M12 the `tests/benchmarks/` call removed | the benchmark case, in a benchmarks-on tree |

Commit: `build: build the project's own targets with -Wall -Wextra -Wpedantic (step 4/7)`.

### Step 5 — What CI's run and the other trees say

No commit unless something is found; it comes after the push, as you asked on plan 029 (finish and push the mechanical change, then
measure), and the default does not flip until it is done.

1. **CI's run of Step 4** (GCC 15.2, `-O3`, flags on, `-Werror` off): read its log (`gh run view --log`) and list every `warning:` in
   Progress. Expected: none. One in this project's code is fixed under D5's rule, in a commit of its own (`fix: …`), before Step 6.
2. **The trees CI does not build,** each configured with `-DCRYPTOCOGS_WARNINGS_AS_ERRORS=ON`, built in full and tested: the Mac's `asan-`
   and `tsan-macos-arm64`; a `bench-macos-arm64` tree (network for google benchmark); in the container `asan-linux-arm64`,
   `tsan-linux-arm64` and a benchmarks tree; and GCC 15.2 in a throwaway `ubuntu:26.04` container, as flywheel-dag did, if the image can be
   pulled.

**Done when:** CI's log has no warning, or each is fixed; and every tree builds with no warning and passes, or what it found is in Progress with
a recommendation. The likeliest findings are in the sanitizer trees: `canary_asan` is a deliberate use-after-free, which GCC's
`-Wuse-after-free` may report once `-Wall` is on, and ThreadSanitizer's `-Wtsan` warns about `std::atomic_thread_fence`. A finding that needs
more than a one-line fix is raised as an issue, with the evidence, and not fixed inline; if it blocks a tree, D6's fallback is the way out and
I tell you first.

### Step 6 — `-Werror` on by default, test first

`test_warning_option` changes first: the no-option case now expects `-Werror` on every own unit, and `=ON` gives way to `=OFF`, which expects
none. It is red, because the default is still off. Then the root's `option(CRYPTOCOGS_WARNINGS_AS_ERRORS … ON)` and its comment, which says
how to relax it. The workflows are not touched (D6).

**Done when:**

- The checkpoint is green, 526 of 526 on the Mac and in the container; a configure with no `-D` puts `-Werror` on all 77 own units and on
  none of the 50 dependency units, and `=OFF` takes it off the 77. A full build of the `release-` trees with the default prints no warning.
- The clang-tidy gate on a default tree: 5 green, 72 red, no `clang-diagnostic-*` site.
- **Hand-made changes**, each on a scratch copy, fail exactly the case stated:

  | Change | Fails |
  |---|---|
  | M13 the default put back to `OFF` | `test_warning_option`: the no-option case has no `-Werror` |
  | M14 the option ignored, so `-Werror` is always on | the `=OFF` case |
  | M15 the option's value not passed to the module (`-Werror` never) | the no-option case |

- **Pushed;** CI's run (no flag in the workflow, `-Werror` from the default) passes 526 of 526, `test_warning_option` among them, and has no
  warning in its log. Then `gh workflow run sanitizers.yml --ref main -f which=both` once, and both jobs are green. The edited root and
  `tests/CMakeLists.txt` of Step 4 already missed the two caches; this step's change to the root misses the dependency cache once more.
  If a run is red on a warning, D5's rule applies and I fix forward and tell you; if the fix is anything but code or a `SYSTEM` mark, I stop.

Commit: `build: build with -Werror by default (step 6/7)`.

### Step 7 — Docs, the sibling check, and close

- **`CLAUDE.md`**: the option row; a "Compiler warnings" subsection after clang-tidy (the flags, `-Werror` on by default and how to relax it
  with `-DCRYPTOCOGS_WARNINGS_AS_ERRORS=OFF`, the placement rule of D3, D5's rule for suppressions, that the clang-tidy gate sees compiler
  warnings as errors because `-Werror` is in the commands); the layout rows for `cmake/cryptocogs_warnings.cmake` and `tests/warning_checks/`;
  the two tests in the tables and the counts (526 in ctest, 136 in a Crypto.com-only tree); the CI section; and the clang-tidy section's
  sentence that the override is "inert until #47", which is now exercised.
- **`README.md`**: the option, in the build commands.
- **Sibling repo, read only.** flywheel-dag#67 is raised. Compare the two ideas that go beyond flywheel-dag's: the offline configure that
  classifies every unit of a tests-on tree (flywheel-dag reads the running tree's database) and the subproject case with a consumer's unit
  (flywheel-dag#67 already offers the first). Raise an issue where it applies, with the measurement, and tell you the numbers; edit nothing there.
- **This plan**: Status, Progress and an Outcome. Then `gh issue close 47` and `gh issue close 24` with summaries.

**Done when:** the pushed tip is green and #47 and #24 are closed. Commits: `docs: say what the build warns about (step 7/7)`, then
`docs(plan): 036 is done`.

## Self-review

**Risks**

- **CI's GCC 15.2 is the first compiler of its kind to see the flags.** Local evidence is GCC 13.3 at `-O3` (clean) and Apple clang 21. A
  newer GCC adds warnings, and its libstdc++ may warn in a dependency header that our unit includes: nlohmann/json is a `-I`, not a
  `-isystem`, include here. The order of D2 is the answer: Step 4's run prints GCC 15's warnings without failing, Step 5 reads them, and
  only then does the default flip. D5's rule decides what is done with one.
- **Default on makes every build strict, yours and the nightly sanitizer jobs' included.** That is D2, and the cost is the one it names: a
  toolchain update that adds a warning stops a build until it is fixed or `=OFF` is passed. `-O2` instrumented GCC is where a spurious warning
  is likeliest, which is why Step 5 builds the sanitizer trees first and Step 6 dispatches the workflow.
- **`-Werror=deprecated-declarations` is on even with the option off.** A toolchain update that deprecates something a dependency header uses
  stops a local build, as flywheel-dag#60 saw for clang-tidy 23 with nlohmann/json's `char_traits<unsigned char>` (plan 035, D10). Own code has
  none today. The cure is the `SYSTEM` mark of D5, one line per dependency.
- **The Mac is not in CI.** Apple clang's warnings are checked only on your machine until [#29](https://github.com/tomlin256/cryptocogs/issues/29)
  adds a macOS leg; the default protects the GCC build in CI and whichever builds you run.
- **Placement is a rule, and a rule can be forgotten.** A new directory of targets that does not apply the list is not flagged, so
  `test_warning_option` is what notices (M11); one the offline configure does not reach (behind an option the test does not set) is
  noticed only in a tree that sets it, which is why the benchmark case exists.
- **`example_backward` moves.** It is today the first thing in `tests/CMakeLists.txt`; it moves below the three fetches so the flags reach it.
  `add_backward` needs backward-cpp fetched, which it still is. A full build and ctest show the examples still link.
- **The benchmarks are in no default tree.** Their three sites are fixed on the strength of a syntax-only Apple clang run and a GCC build;
  Step 5 builds a benchmarks tree with the option on, and `test_warning_option` has the benchmark case wherever google benchmark is present.
- **The sanitizer canaries are deliberate violations** that now compile under `-Wall -Werror`. Apple clang reports nothing for them; GCC is
  unknown until Step 5, and with the default on a finding would turn the nightly red, so it is found before the flip (D6).
- **The strengthened tests could fail for a real reason.** `EXPECT_TRUE(fut.get().ok)` checks what the reply says. If one of the six replies
  is not a success, the test was passing by not looking; that is a finding to report, not to weaken.
- **`test_warning_option` configures the real root up to four times** (about 1.5 s each, no network, nothing compiled) in every tree with the
  tests on, the sanitizer trees and CI included. It passes the running compiler and generator, so what it classifies is what the tree builds.
- **Editing the root and `tests/CMakeLists.txt` misses CI's dependency cache** (in Step 4, and the root again in Step 6), and the changed command
  lines miss ccache for the 77 own units (in Step 4, and again when `-Werror` joins them). `test_ci_cache_key` reads the literal declare call,
  so the module and the scripts must not contain it, even in a comment.
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
- **A macOS CI leg** ([#29](https://github.com/tomlin256/cryptocogs/issues/29)).
- **flywheel-dag's default,** which is [flywheel-dag#67](https://github.com/tomlin256/flywheel-dag/issues/67); flywheel-dag is not edited.
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
- **The default on from the commit that adds the flags.** One step fewer, but CI's first GCC 15.2 run would be the strict one and could leave
  `main` red on a warning that no local compiler shows. The staged flip costs one test-first step and one CI cycle.
- **A workflow that passes the option, with a check that it does** (this plan's first draft). Unnecessary once the default is on: a typo in a
  `-D` cannot matter when none is needed, and `test_warning_option` holds the default in CI's tree too.
- **The flags in the presets,** which say only what makes each tree's name true.
- **A test of the running tree's own database** besides the offline configure. The same root is configured either way; the benchmark case
  covers the one directory a default tree lacks. The sanitizer feature has both because its dependencies' flags differ per tree.
- **Marking every dependency `SYSTEM` now.** No dependency unit warns today, and a `SYSTEM` mark also changes include order. It is the cure
  if one does (D5).

## Progress

| Step | Status | Notes |
|---|---|---|
| 0 Approval | Done | Proposed in `507f80b` and linked from #47, which said nine sites and has twelve. Rob approved on 2026-11-05 with D2 changed (`-Werror` on by default) and asked for the same in flywheel-dag, which is flywheel-dag#67. |
| 1 Start from green | Done | No commit. Mac `debug-macos-arm64` and container `debug-linux-arm64`: the configure prints only the two CMake deprecation notices that #26 tracks, the builds were up to date, and ctest passes 524 of 524 on both (7 s at `-j8`). The probes on the tip: Apple clang 7 (default), 7 (`asan-`), 7 (`tsan-`) and 10 (a hand-made `bench-macos-arm64` tree with `CRYPTOCOGS_BUILD_BENCHMARKS=ON`, its dependency sources taken from the debug tree and google benchmark fetched); GCC 13.3: 3. |
| 2 Fix the twelve sites | Done | `fix: build the tests, example and benchmarks without compiler warnings (step 2/7)`. The probes with `-Wall -Wextra -Wpedantic -Werror` give 0 on all five. Hand-made changes on a scratch copy: a failed reply injected at each of the six sites fails exactly its test (`FlushedInOrder` holds two of the six sites, so they are five tests), and a failed ack makes each of the three benchmarks report `ERROR OCCURRED: 'the subscribe ack failed'`; with the real ack they run as before. The gate on a scratch tree configured with `-DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror'`: 5 green, 72 red (the same set), no `clang-diagnostic-*` site, the six proofs pass. 524 of 524 on the Mac and in the container. **Differs from the plan:** `kraken_example` prints the same 29 lines as before except two that differ between any two runs, the nonce in the AddOrder body and its `API-Sign`, so it was compared with those masked. CI run 42064129822 is green. |
| 3 The module, test first | Done | `build: add cmake/cryptocogs_warnings.cmake and the test that holds it (step 3/7)`. `test_warning_flags` was red first (the module did not exist) and then green, 186 cases in 0.01 s. 23 hand-made mutants of the function are each killed by the assertions meant to catch them; beyond the plan's list they include `-Wpedantic` misspelt, `-Werror` first, the unanchored match flywheel-dag has, a case-insensitive match, the top-level flag read as `STREQUAL "ON"`, the result lost to the function's scope, and a stray flag. 525 of 525 on the Mac and in the container; the configure printed what Step 1's did. CI run 42064642925 is green. |
| 4 The flags on and placed, the option off for now, and guarded | Done | `build: build the project's own targets with -Wall -Wextra -Wpedantic (step 4/7)`. `test_warning_option` was red first (it named the 77 units), then green in 3.2 s (three trees, four where google benchmark is present). **Option off:** the configure printed what Step 1's did; the compile database differs from the one before in exactly the 77 own units, each gaining the four flags before `-o`, and the 50 dependency commands are byte-identical (Mac and container); full builds with no warning; 526 of 526 on both. **`=ON`:** full builds with `-Werror` and no warning on `debug-` and `release-macos-arm64` (Apple clang) and on `debug-` and `release-linux-arm64` (GCC 13.3, `-O3`), 526 of 526 each. **The gate** on a tree with the option on: 5 green, 72 red (the same set), no `clang-diagnostic-*` site, the six proofs pass. **Hand-made changes** M1 to M12 are each killed by the case stated: M1 the 29 `src/` units; M2 the 48 others; M3 9 dependency units (spdlog's 7, backward-cpp's 2); M4 `backward_init.cpp`; M5 the 29 and the 9; M6 50 of 50; M7 the `=ON` case and `test_warning_flags`; M8 29 of 30 subproject units and `test_warning_flags`; M9 the parent's own check ("exchange_common passes compile options to its consumers"); M10 the 50 dependency units, the cache and the subproject; M11 `tools/main.cpp`; M12 the 7 benchmark units. **Differs from the plan:** the leak sentinels are `-Wpedantic` and `-Werror=deprecated-declarations` only, because google benchmark sets `-Werror` for itself; M11 is a directory that the root adds, because one that `tests/` adds after its call inherits the flags; and the first run counted each leaked flag, so it said "100 of 50", which now counts units. |
| 5 What CI's run and the other trees say | Done | No commit; nothing found *in what it covered*. **CI's run of Step 4** (42067692222; GCC 15.2.0, CMake 4.4.5, `release-linux-x86_64`, flags on, `-Werror` off, 127 units compiled, 5 min 10 s): no `warning:` and no `error:` in the log, apart from the two CMake deprecation notices that #26 tracks, and 526 of 526. **The other trees, each with `-DCRYPTOCOGS_WARNINGS_AS_ERRORS=ON`, built in full and tested:** Mac `asan-macos-arm64` (79 units compiled, 529 of 529), `tsan-macos-arm64` (78, 528 of 528) and `bench-macos-arm64` (134, 526 of 526; the 7 benchmark units carry `-Werror`), no warning; container (GCC 13.3) `asan-linux-arm64` (129 units, 529 of 529, with `detect_leaks=1`), `tsan-linux-arm64` (128, 528 of 528) and a `bench-linux-arm64` Release tree (154, 526 of 526), no diagnostic; and GCC 15.2.0 in a throwaway `ubuntu:26.04` container (aarch64, CMake 4.2.3, the same compiler as CI's), **a Release tree only**: 127 units in 47 s, no diagnostic, 526 of 526. The canaries compile cleanly with `-Wall -Wextra -Wpedantic -Werror` on both compilers (`canary_asan` hides its use-after-free behind `noinline` and `volatile`, so `-Wuse-after-free` has nothing to see), and the code has no `atomic_thread_fence`, so `-Wtsan` has nothing to report. **What it missed:** GCC 15 under ThreadSanitizer, which was never built in that container; see Step 6. |
| 6 `-Werror` on by default, test first | Done | `build: build with -Werror by default (step 6/7)`, then `fix: pass the subscribe requests of four tests without copying them`. `test_warning_option` was red first (its default expectation flipped: 77 of 77 units without `-Werror`, and the cache not holding `ON`), then green. M13 to M15 are each killed by the case stated (M13 the no-option case and the benchmarks case; M14 the `=OFF` case; M15 the no-option case). **Default tree:** `debug-` and `release-macos-arm64` and `debug-` and `release-linux-arm64`, each with the cache entry unset (`-UCRYPTOCOGS_WARNINGS_AS_ERRORS`): 77 of 77 own units have `-Werror` and none of the 50 dependency units has `-Wpedantic`; full builds with no warning (Apple clang, and GCC 13.3 at `-O3`); 526 of 526 each. The clang-tidy gate on a default tree: 5 green, 72 red (the same set), no `clang-diagnostic-*` site, the six proofs pass. **CI** (42070558947, no flag in the workflow, GCC 15.2): 127 units compiled, no warning, 526 of 526. **Found, 1: `option()` caches its value,** so a tree configured between Steps 4 and 6 holds `OFF` and keeps it when the default changes: my four trees did, and a plain reconfigure left them at 0 of 77 until `-U` removed the entry. A tree configured before Step 4, or a new one, takes the default; CLAUDE.md, the README and the root say so. **Found, 2: the `tsan` job of the sanitizers workflow went red.** The dispatch of 42070614384 (`asan` green, `tsan` red) failed to compile `test_ws_client.cpp` on GCC 15.2: `-Werror=maybe-uninitialized` on the destructor of the local `TickerSubscribeRequest` in four tests (its never-engaged `std::optional<std::string> token`), a false positive of GCC's family with `std::optional` (PR 109561, PR 121173) that shows at `-O2` under ThreadSanitizer only. Reproduced in a throwaway container (8 errors, the only failing unit of 128); `std::move(sub_req)` into `subscribe_async` cleared three tests and a temporary the fourth, and the whole tree then built with no diagnostic. A `#pragma GCC diagnostic` around the test also worked, and D5 prefers the code. Dispatch 42073767196 on `5fec847`: `asan` (129 units, 529 of 529) and `tsan` (128 units, 528 of 528) both green with no warning in the logs; CI 42073742216 green. Without Step 6's dispatch the nightly of 2026-11-06 would have been red. |
| 7 Docs, the sibling check, and close | Done | `docs: say what the build warns about (step 7/7)`: `CLAUDE.md` (the option row, the layout rows, a `Compiler warnings` subsection with the rules, the GCC differences and the container recipe, the two tests in the tables and the counts: 526 in ctest, 136 in a Crypto.com-only tree, the CI and sanitizer notes, the clang-tidy section's sentence about the override) and `README.md` (the option and the cache note). **Sibling check, read only, nothing edited there:** [flywheel-dag#67](https://github.com/tomlin256/flywheel-dag/issues/67) (`-Werror` on by default there, as you asked; it also offers the offline-configure test, and the subproject case with a consumer's own unit is what `test_consumer_subproject` already has). |

## Outcome

**Done 2026-11-05.** The project's own targets compile with `-Wall -Wextra -Wpedantic -Werror=deprecated-declarations` and, by default, `-Werror`
(`CRYPTOCOGS_WARNINGS_AS_ERRORS`, declared at the top level only). The flags are applied per directory (`src/`, `tests/` after its last fetch,
`tests/benchmarks/`): they reach the 77 units of this project (84 with the benchmarks), none of the 50 of a dependency's, and never a project
that fetches cryptocogs. The twelve sites are fixed, and #24 with them. 526 tests pass on the Mac, in the container and in CI (run
42073742216 on the last code commit, and 42076077906 on the docs commit; GCC 15.2, no warning in either log), two of them new: `test_warning_flags` (186 cases, 0.01 s) and
`test_warning_option` (three trees, four where google benchmark is present, 3 s). The workflows are unchanged: CI and the nightly sanitizer jobs
are strict through the default, and the sanitizers workflow, dispatched twice, is green on the commit after the fix (run 42073767196, `asan`
and `tsan`). Pushed as `82fb080` (step 2), `7839985` (3), `ecba792` (4), `5876c13` (6), `5fec847` (the tsan fix) and the Step 7 commit. No
release: nothing in `include/`, the libraries or the package changed. #47 and #24 are closed.

Where the steps differ from the text above, which was left as written where it is history:

- **D2 changed at approval,** and the plan changed with it: `-Werror` is on by default, so the workflow edit and `test_ci_werror` went (526
  tests, not 527; 136 in a Crypto.com-only tree, not 137); the flags landed first with the default off, CI's first GCC 15.2 run only printed
  warnings (none), and the default flipped in Step 6 with the test first; the sanitizer workflow became strict, which is what Step 6's dispatch
  was for.
- **Twelve sites, not nine.** The benchmarks are in no default tree and have three. `kraken_example`'s output was compared with its nonce and
  signature masked, since those differ between any two runs, and the six `fut.get()` sites are five tests (`FlushedInOrder` has two).
- **The leak sentinels are `-Wpedantic` and `-Werror=deprecated-declarations`.** The plan listed `-Werror` too, but google benchmark sets it
  for itself, and the two stand for the list, which one call applies. The first run of the test counted each leaked flag and said "100 of 50
  dependency units"; it counts units now. M11 became a directory that the root adds: one that `tests/` adds after its call inherits the flags.
- **Step 5 missed the tsan failure.** Its GCC 15 check built a Release tree only; the `tsan` job of the sanitizers workflow, dispatched in Step
  6, failed on GCC 15.2 (a `-Wmaybe-uninitialized` false positive in four tests, fixed in code in `5fec847`). The plan had named the risk (D6)
  and not enforced the check on every configuration the workflows build. CLAUDE.md now says to build each one in the container, and that the
  container compiles the tsan tree but cannot run its tests (`SIGILL` at the first thread on aarch64), which only CI does.
- **`option()` caches its value.** A tree configured while the default was off keeps it, so a plain reconfigure did not turn `-Werror` on in
  any of the four trees that had been; `-U` does. The root, CLAUDE.md and the README say so.
- **Mutants.** 23 of the module (all killed), M1 to M12 of the placement and the interface (M3 was 9 dependency units, as measured), and M13 to
  M15 of the default. The six strengthened tests and the three benchmarks were each made to receive a failed reply.

What this does not settle:

- **Apple clang's warnings are checked only on your machine** until [#29](https://github.com/tomlin256/cryptocogs/issues/29) adds a macOS CI
  leg; CI's GCC is what the default protects.
- **Wider warnings** (`-Wshadow`, `-Wconversion`, `-Wold-style-cast`), CMake's own `-Werror=dev -Werror=deprecated` (D7), and the **329
  clang-tidy sites** ([#46](https://github.com/tomlin256/cryptocogs/issues/46)): the gate now also reports a compiler warning as an error, and
  has none.
- **One sibling issue,** [flywheel-dag#67](https://github.com/tomlin256/flywheel-dag/issues/67), for you to take or leave.
