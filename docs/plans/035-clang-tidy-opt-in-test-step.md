# Plan 035 — clang-tidy as an opt-in test step

**Status: Approved (2026-11-05), with one change, to D10.** Rob: *"D10 -- i want warnings as errors, but the rest is approved (create
follow up gh issue for fixing the 329 issues found)"*. D1 to D9 and D11 stand as written. D10 now passes the deprecation override, and
the build's own warnings as errors are [#47](https://github.com/tomlin256/cryptocogs/issues/47), planned as plan 036 after this one,
which was Rob's choice from the three ways D10 could go. The follow-up issue for the 329 findings is
[#46](https://github.com/tomlin256/cryptocogs/issues/46), opened at approval. The Progress table at the end records each step.

Closes [#34](https://github.com/tomlin256/cryptocogs/issues/34).

The issue is its title, *"Setup clangtidy as opt in test step"*, and its body, *"As flywheel-dag"*. That is
[flywheel-dag#60](https://github.com/tomlin256/flywheel-dag/issues/60)
([plan](https://github.com/tomlin256/flywheel-dag/blob/main/docs/plans/clang_tidy_ci_plan.md)), which
[flywheel#205](https://github.com/tomlin256/flywheel/issues/205)
([plan](https://github.com/tomlin256/flywheel/blob/main/docs/plans/clang_tidy_opt_in_plan.md)) carried to a second repo under the
same title: as an opt-in step, without flywheel-dag's CI job and without its fixes, and with the glue in a module that has tests of
its own. This plan carries it here on flywheel's scope, adapted to a repo with four exchange adapters (D9).
[#38](https://github.com/tomlin256/cryptocogs/issues/38) is the editor's view of the same class of findings.

## Problem

clang-tidy runs in the editor only. `.clangd` enables `bugprone-*`, `performance-*` and `cppcoreguidelines-pro-type-member-init`
(less two checks), so a finding is a hint on one machine and nothing in a build or a test reads it. #38 is such a hint, and says so:
*"The opt-in clang-tidy step in #34 would report the same class of findings."*

The change is a cache variable that names the tool. With it set, `ctest` runs clang-tidy over the project's own translation units,
one test per file, plus tests that prove the gate bites. **Opt in**: unset, which is the default, the build, `ctest` and CI do not
change, apart from three tests that need no clang-tidy.

### What I measured (2026-11-05)

Scratch runs on the unchanged tree (`6c34c53`), with the `.clangd` checks, the header filter below and the flags the gate would
pass. Nothing is committed, and nothing was written in the checkout. A *site* is a unique file, line, column and check; a unit is
*red* when `clang-tidy --warnings-as-errors=*` exits non-zero.

| Where | Tool | Units | Green | Red | Sites | Time |
|---|---|---:|---:|---:|---:|---|
| Mac, `debug-macos-arm64`, SDK 27.2 | Homebrew clang-tidy 23.1.4 | 77 | 5 | 72 | 329 | 21 s at 8 jobs |
| Mac, `release-macos-arm64` | the same | 77 | 5 | 72 | 329 | the same sites as debug |
| `ubuntu-dev`, `debug-linux-arm64`, GCC 13.3 | Ubuntu LLVM 23.1.6 | 75 | 5 | 70 | 327 | 34 s at 6 jobs |
| Mac, a scratch tree with `CRYPTOCOGS_BUILD_BENCHMARKS=ON` | the same | 84 | 5 | 79 | 365 | the 7 benchmark units add 36 sites |

No unit fails for any reason but a finding: there is no compile error (`clang-diagnostic-*`) on either platform. Every Linux site is
also a Mac site. The container's tree is stale: it lacks `test_ix_ws_connection.cpp` and `test_mock_ws_connection.cpp`, which plans
033 and 034 added after its last configure. The Mac's two extra sites are `include/exchange/common/ws_client.inl:119`
(`bugprone-exception-escape`, Mac only; I did not look into why) and `tests/unit/test_mock_ws_connection.cpp:108` (in one of the two
units the container's tree lacks).

The 329 sites on the Mac:

| Sites | Check |
|---:|---|
| 237 | `bugprone-unchecked-optional-access` |
| 19 | `performance-unnecessary-value-param` |
| 16 | `bugprone-exception-escape` (15 on Linux) |
| 14 | `performance-enum-size` (#38's enums among them) |
| 13 | `bugprone-assignment-in-if-condition` |
| 7 | `cppcoreguidelines-pro-type-member-init` |
| 6 | `bugprone-throwing-static-initialization` |
| 4, 4, 4 | `performance-inefficient-string-concatenation`, `bugprone-signed-bitwise`, `bugprone-optional-value-conversion` |
| 2 | `bugprone-empty-catch` |
| 1 each | `performance-use-std-move`, `performance-inefficient-vector-operation`, `bugprone-reserved-identifier` |

By where the site is: `tests/unit` 198, `tests/examples` 101, `include/exchange` 22, `src` 8. 23 are in headers (22 in nine public
headers, 1 in `tests/examples/kapi.hpp`); 306 are in `.cpp` files. **35 of the 72 red units have no finding in their own file**: they
are red only through those 23 header sites. The benchmark units' 36 extra sites are 31 `bugprone-throwing-static-initialization`, 4
`performance-inefficient-vector-operation` and 1 `performance-unnecessary-value-param`.

**This is not #38's list.** #38 counts what the editor shows, and clangd leaves out the checks it knows to be slow:
flywheel-dag#60 showed that `bugprone-unchecked-optional-access` and `bugprone-use-after-move` never reach the editor, and here the
first is 237 of the 329 sites. A tidy-clean tree and a hint-clean tree are different goals; the gate measures the first.

**What each part of the command is for**, each measured:

- **The unit list.** The 77 units are exactly the `.cpp` files in the Mac tree's compile database (29 under `src/`, 17 examples,
  31 unit tests). A list taken from the targets at configure time, in a scratch copy, was identical to the database's own units in
  three configurations: 77 (the defaults), 25 (Crypto.com only: `CRYPTOCOGS_BUILD_KRAKEN`, `_BINANCE` and `_COINBASE` off) and 84
  (benchmarks on). D9 rests on this.
- **The header filter.** `HeaderFilterRegex: '/(include/exchange|tests)/'` with `ExcludeHeaderFilterRegex: '/_deps/'`. No header
  lives under `src/`. The `_deps` exclusion is for the dependencies that sit under a path the include expression matches.
- **`bugprone-signed-bitwise.IgnorePositiveIntegerLiterals`.** Without it the run has 353 sites and 4 green units: 24 more sites, in
  the base64 and hex helpers of `src/kraken/auth.cpp` (11), `src/coinbase/auth.cpp` (11) and `src/cryptocom/auth.cpp` (2), such as
  `static_cast<unsigned char>(data[i]) << 16` and `c >> 4`: a small unsigned value, promoted to `int`, shifted by a positive literal.
  The 4 that remain have no literal operand (`val >> bits`, `val |= … << 8`). It is carried over from flywheel-dag.
- **`-isysroot` from `xcrun --show-sdk-path`.** The compile commands carry no `-isysroot` (`/usr/bin/c++` finds the SDK itself).
  Homebrew's clang has `MacOSX15.2.sdk` compiled in as its default, while the build uses `MacOSX.sdk`, which is 27.2 here. The results
  are identical today (329 either way), so this is for correctness: a gate without it would pass quietly on an SDK the build never
  uses.
- **`-Wno-error=deprecated-declarations`.** flywheel-dag and flywheel pass it because their compile commands carry
  `-Werror=deprecated-declarations`, with which a clang-tidy newer than the SDK's libc++ stops on nlohmann/json's `char_traits`
  deprecation. This project's compile commands carry no `-Werror` today (`-g` or `-O3`, `-std=gnu++23`, `-I`, `-isystem`, `-arch`), and
  the runs above had no compile error without it. D10 passes it anyway, because Rob wants warnings as errors: with
  `-Werror=deprecated-declarations` added by `--extra-arg` (the same as the build carrying it), 65 of the 77 units fail on the Mac
  without the override, on `char_traits<unsigned char>` in the SDK's libc++ reached from nlohmann/json's `output_adapters.hpp`; with
  it the picture is the baseline again (5 green, 72 red, 329 sites, no compile error). Own code has no deprecation error either way.

## Decisions to confirm

My defaults, each one a place to edit before approving.

- **D1. The gate, not the findings.** This plan adds the opt-in step and clears none of the 329 sites, so **with the gate on, 72 of
  the 77 per-file tests are red on the Mac**. That is the measurement, not a defect; the default build and `ctest` stay green, and the
  six tests that prove the gate pass. The findings are recorded in a new issue, [#46](https://github.com/tomlin256/cryptocogs/issues/46)
  (opened at approval, from the scratch baseline; Step 5 re-measures with the gate), and cleared in a fresh session with this gate
  as its oracle. This is the scope you settled for flywheel#205: *"1. The basic scaffold to collect the status, but dont enable the
  gate pushed with a record of its findings in a new issue to fix the findings, 2. Do the fixes in a fresh session."* *Alternatives,
  not taken:* fix them here (four times flywheel-dag#60's 77 sites, 237 of them `unchecked-optional-access` behind gtest assertions the
  analysis cannot see through, and 14 `performance-enum-size` sites whose fix is #38's ABI question); a baseline file of known
  findings, which flywheel-dag#60 rejected because it parks closable defects. The per-file granularity already ratchets: a unit that
  goes green stays green, or its test turns red.
- **D2. LLVM 23 is pinned, and a test holds the major.** `clang_tidy_version` passes only on `LLVM version 23.`, so the Mac
  (Homebrew's `llvm`, with `clang-tidy` and `clangd` linked into `/opt/homebrew/bin` for flywheel-dag#60) and the `ubuntu-dev`
  container (Ubuntu LLVM 23.1.6) cannot drift apart unseen. Homebrew's `llvm` becoming 24 turns it red on purpose; the move is one
  commit (the constant, plus whatever 24 reports). Patch levels float.
- **D3. The check set does not change.** `.clangd`'s list moves into `.clang-tidy` as it is, and `.clangd` keeps the compilation
  database and the inlay hints, so one list serves the editor and the gate. `clang-analyzer-*`, `readability-*` and
  `misc-include-cleaner` are separate decisions, each with findings of its own. The one option, `IgnorePositiveIntegerLiterals`,
  removes 24 sites (above).
- **D4. No CI job, no preset.** A job with 72 red units gates nothing. The repository is public, so Actions minutes are not the
  reason. When wanted, the job is small: flywheel-dag's installs `clang-tidy-23` from apt.llvm.org on `ubuntu-latest`, configures with
  `-DCRYPTOCOGS_CLANG_TIDY=clang-tidy-23` (nothing is built) and runs `ctest -R '^clang_tidy_'`. Add it once the findings are cleared.
  `test_presets` pins the twelve trees, and a tree of its own would fetch every dependency again.
- **D5. Opt in by cache variable, in the tree you choose.** `cmake --preset debug-macos-arm64 -DCRYPTOCOGS_CLANG_TIDY=clang-tidy`,
  then `ctest --preset debug-macos-arm64 -R '^clang_tidy_' -j8`. The variable stays in that tree's cache, so a plain `ctest` there
  then runs the 83 too; `-DCRYPTOCOGS_CLANG_TIDY=` turns it off again. Debug is the tree `.clangd` names, and release gives the same
  sites.
- **D6. The glue is a module with its own tests.** The tool resolution, the test names, the unit selection and the registration go in
  `cmake/cryptocogs_clang_tidy.cmake`, the shape `cryptocogs_platform.cmake` and `cryptocogs_sanitize.cmake` have, so they are tested
  apart from the real root with `cmake -P`. flywheel-dag wrote them inline and proved them with hand-made changes alone; flywheel's
  module and its tests are the later design. The cost is a few hundred lines of CMake and tests more than inline; the gain is that the
  opt-in rule and the refusals are tests.
- **D7. Test names come from the path.** `clang_tidy_src_kraken_auth`, `clang_tidy_tests_unit_test_signature`, so
  `ctest -R '^clang_tidy_src_binance_'` runs one adapter's units. A name collision is a configure error, not a silently dropped test.
- **D8. Every run passes `--config-file=<root>/.clang-tidy`.** The file `clang_tidy_checks` lists, the file the canaries run and the
  file the units run are then one file by construction, and a `.clang-tidy` added later in a subdirectory cannot quietly change what
  one part of the tree is held to. (The editor still honours a nested file, so such a file would be a deliberate split; this makes it
  visible.)
- **D9. The unit list comes from the targets, and a test holds it to the compile database.** Both siblings glob the sources. This repo
  has four adapter flags, an opt-in benchmark option and sanitizer canaries, and a unit that is not in the tree's compile database is
  analysed with flags clang-tidy infers from its neighbours. That works (a Kraken unit run against a Crypto.com-only tree's database
  parsed and reported its real findings), but then the gate checks code the tree does not build, with guessed flags, and nothing can
  hold its list to the database. The list is therefore every `.cpp` that a target of this project compiles (read from `SOURCES`,
  walking the project's own directories, never `_deps` or anything outside the checkout), registered from the end of the root
  `CMakeLists.txt`, after the last `add_subdirectory`. `clang_tidy_units` (gate on) compares it with `compile_commands.json` at test
  time, so a target added after the call, or a file the walk misses, turns a test red. *Alternative:* the glob, which is simpler, also
  analyses a file no target compiles, and in a Crypto.com-only tree registers 52 units of adapters that are switched off. The cost of
  mine is the walk, about thirty lines, and a test for it.
- **D10 (changed at approval). The deprecation override and the SDK flag both stay; the build's own warnings as errors are plan 036.**
  The first draft passed no override, because the compile commands carry no `-Werror`. Rob: *"i want warnings as errors"*. Every run
  therefore passes `--extra-arg=-Wno-error=deprecated-declarations`, as flywheel-dag and flywheel do, so that the day the build carries
  `-Werror=deprecated-declarations` the gate does not stop on the SDK's libc++ (measured above: 65 of 77 units without it). It is
  inert until then, and Step 3 shows it is not dead: M11 and M12 below. `--warnings-as-errors=*` is unchanged, so a deprecation in
  this project's own files is still an error in the gate, and the header filter drops one in the SDK or a dependency. What Rob asked
  for beyond the gate, the build itself, is [#47](https://github.com/tomlin256/cryptocogs/issues/47). The choices were: a plan of its
  own after this one (taken), the same flags as a new step ahead of the gate in this plan, or `-Werror=deprecated-declarations` alone
  in this plan. The measurements behind them are in #47: nine warnings in two files today (six are #24), none in `src/` or `include/`.
- **D11. Commits go straight to `main`, one per step, and are pushed once the step's checkpoint is green.** No branch and no PR, as
  your global rules say. Say if you want the push held until the end.

## The change

New:

- **`.clang-tidy`.** `Checks` as `.clangd` has them today, `HeaderFilterRegex: '/(include/exchange|tests)/'`,
  `ExcludeHeaderFilterRegex: '/_deps/'`, and the one check option. No `WarningsAsErrors`: clangd would, I believe, show every hint as
  an error, so the tests pass `--warnings-as-errors=*`.
- **`cmake/cryptocogs_clang_tidy.cmake`.** The major (23) and six functions:
  `cryptocogs_clang_tidy_tool(<requested> <generator> <out>)` (empty is off; otherwise the path `find_program` gives for a name or a
  path; a generator that writes no `compile_commands.json`, or a tool this host cannot run, is a `FATAL_ERROR` that names the value);
  `cryptocogs_clang_tidy_sdk(<apple> <sysroot> <out>)` (the SDK for an Apple target whose sysroot is empty; a failing, silent or
  absent `xcrun` is a `FATAL_ERROR`);
  `cryptocogs_clang_tidy_args(<extra> <sdk> <out>)` (`--warnings-as-errors=*`, `--extra-arg=-Wno-error=deprecated-declarations`
  (D10), the SDK flags if any, then the extras);
  `cryptocogs_clang_tidy_test_name(<root> <source> <out>)`;
  `cryptocogs_clang_tidy_select(<root> <candidates> <out>)` (a pure filter: the `.cpp`, `.cc` and `.cxx` files under the root, less
  `tests/clang_tidy_checks/canaries/` and `tests/sanitize_checks/canaries/`, de-duplicated and sorted; none at all, or two with one
  test name, is a `FATAL_ERROR` that names both);
  and `cryptocogs_clang_tidy_add_tests(<tool> <root> <extra>)`, which walks the targets, calls `select`, and registers the tests below.
  The walk is the only part that needs a project, so it is the only part `cmake -P` cannot run; the wiring test covers it.
- **`tests/clang_tidy_checks/canaries/`**: `canary.cpp`, `canary_own.hpp`, `canary_dep.hpp`, each with the project banner. Configure
  copies them under the build tree, to `clang_tidy_canary/include/exchange/canary_own.hpp` and
  `clang_tidy_canary/_deps/canary-src/tests/canary_dep.hpp`, so their paths are the two kinds the filter must tell apart. The
  dependency one matches the include expression (`/tests/`), so only the exclusion keeps it out. They are in no compile database and
  no target.
- **`tests/clang_tidy_checks/check_clang_tidy_{module,wiring,config,units}.cmake`**, each with `cmake_minimum_required(VERSION 4.0)`
  as `test_cmake_floor` requires. The directory is not called `build_checks` because `.gitignore` ignores `build*`.

Edited:

- **Root `CMakeLists.txt`**: `CRYPTOCOGS_CLANG_TIDY` (the command; empty, the default, leaves the gate out) and
  `CRYPTOCOGS_CLANG_TIDY_ARGS` (a list), resolved right after the sanitizer block and before `find_package(OpenSSL)`, so a refused
  configure needs no network; and the call that registers the gate, after the benchmarks block. Top level only: in a parent project's
  tree the variable is inert.
- **`tests/CMakeLists.txt`**: the three always-on tests, in a section of their own.
- **`.clangd`** drops its `Diagnostics` block. **`CLAUDE.md`** and **`README.md`** get the commands and the rules (Step 6).

### The tests

| Test | When | Runs | Passes when |
|---|---|---|---|
| `test_clang_tidy_module` | always | each function under `cmake -P`, the refusals in child runs | Step 2 |
| `test_clang_tidy_wiring` | always | a stub project calls `cryptocogs_clang_tidy_add_tests`, then `ctest -N`; the real root is configured with a missing tool, with the tests off, and as a subproject | Step 3 |
| `test_clang_tidy_config` | always | reads `.clang-tidy`, `.clangd` and the headers the checkout owns | Step 3 |
| `clang_tidy_<path>` × 77 | gate on | `clang-tidy -p <tree> --quiet --config-file=… --warnings-as-errors=* [SDK flags] <file>` | exit 0 |
| `clang_tidy_version` | gate on | `clang-tidy --version` | the output has `LLVM version 23.` |
| `clang_tidy_checks` | gate on | `clang-tidy --config-file=… --list-checks` | lists `bugprone-use-after-move`, `cppcoreguidelines-pro-type-member-init` and `performance-unnecessary-value-param`, in that order; lists none of `bugprone-easily-swappable-parameters`, `bugprone-narrowing-conversions`, `readability-`, `modernize-` |
| `clang_tidy_units` | gate on | `cmake -P` over the tree's `compile_commands.json` | its own units, less the fixtures, are exactly the units registered |
| `clang_tidy_canary_source` | gate on | clang-tidy on the canary unit, with the real config | `canary.cpp:L:C: error: … [bugprone-use-after-move`: the config is read, `bugprone` is on, `--warnings-as-errors` bites |
| `clang_tidy_canary_own_header` | gate on | the same command | `include/exchange/canary_own.hpp:L:C: error: … [performance-unnecessary-value-param`: the filter admits this repo's path |
| `clang_tidy_canary_dependency_header` | gate on | the same command | the source error, and `canary_dep.hpp` appears nowhere |

*A lint gate that silently checks nothing passes, which is why the gate itself is tested.* The Mac's debug tree has 521 tests today.
It becomes 524, and 607 with the gate on (77 units and the six proofs); a Crypto.com-only tree registers 25 + 6, and one with
benchmarks on 84 + 6.

## Steps

Each commit is `type: …(step N/6)`, straight to `main`, with the `Claude-Session` trailer and no `Co-Authored-By`. A checkpoint is a
configure that prints nothing a configure of the unchanged tree does not, a full build with no new compiler warning, and a full
`ctest`, all passing. CI is not waited for between steps; Step 6 reads it. Fixtures and scripts stay in scratch directories, never in
the checkout, until a step commits them. Hand-made changes are made on a scratch copy made with `git archive` (not `rsync`: a copy
that carries `build*` filled the disk on 2026-11-04) and never restored.

### Step 0 — Approval

The plan was committed as Proposed (`1217ac6`, `docs(plan): add plan 035, clang-tidy as an opt-in test step`) and pushed, and a comment
on #34 links it. Rob's answer, with the change to D10, goes into the plan in a second commit (`docs(plan): 035 approved, with D10
changed`), which is pushed too. **Done when:** `git status` is clean, #34 carries the path, and the approval is in the plan.

### Step 1 — Start from green

No commit. Mac: `cmake --preset debug-macos-arm64`, a full build, `ctest --preset debug-macos-arm64`. Container (`ubuntu-dev`): the
same in `debug-linux-arm64`, which also refreshes its stale compile database. **Done when:** both configure and build with nothing
new, and both pass 521 of 521. Anything else: stop and tell you.

### Step 2 — The module, test first

`tests/clang_tidy_checks/check_clang_tidy_module.cmake` and its registration come first, so the test is red (the module does not
exist), then `cmake/cryptocogs_clang_tidy.cmake`. The script follows `check_sanitize_flags.cmake`: accepted inputs are checked in
process, each refusal in a child run whose exit status and message are checked.

| Test | Asserts |
|---|---|
| `test_clang_tidy_module` (new) | **tool:** empty gives empty, whatever the generator; `sh` (a name on `PATH`) and `${CMAKE_COMMAND}` (a path) resolve; a name no host has is refused with a message that names `CRYPTOCOGS_CLANG_TIDY=<value>`; `Xcode`, `Visual Studio 17 2022` and `Ninja Multi-Config` are refused with a message that names the generator, `Unix Makefiles` and `Ninja` accepted. **sdk:** not Apple gives empty and never runs `xcrun`; Apple with a non-empty sysroot gives empty; Apple with an empty sysroot gives the answer of a stand-in `xcrun` on a private `PATH`, and an absent one, one that fails after printing a path, and one that prints nothing are each refused, on every host. **args:** `--warnings-as-errors=*` comes first and `--extra-arg=-Wno-error=deprecated-declarations` second (D10); with an SDK, `--extra-arg=-isysroot` and `--extra-arg=<sdk>` follow; the extras come last, in order. **test_name:** `src/kraken/auth.cpp` gives `clang_tidy_src_kraken_auth`, a nested `tests/examples/binance/…` file maps likewise, a source outside the root is refused. **select:** over a scratch tree, the `.cpp`, `.cc` and `.cxx` files among the candidates in sorted order, once each, and none of a `.hpp`, a file outside the root, `tests/clang_tidy_checks/canaries/` or `tests/sanitize_checks/canaries/`; no candidate at all is refused; `a_b/c.cpp` with `a/b_c.cpp` is refused, naming both |
| the 521 existing | pass |

**Done when:** the checkpoint is green with 522 of 522 on the Mac; the new test was red first; and hand-made mutants of the module,
each run through the test and all caught by the assertion meant to catch them, none surviving (a tool that accepts a missing command;
the generator check dropped or widened to `Ninja Multi-Config`; `/` not mapped; the collision check dropped; a fixture directory not
excluded; an extension dropped; extras dropped or put first; a flag dropped; the sysroot absent on Apple; `xcrun` trusted when it
fails). Commit: `build: add cmake/cryptocogs_clang_tidy.cmake and the test that holds it (step 2/6)`.

### Step 3 — The configuration and the gate

`.clang-tidy`, `.clangd` trimmed, the two root variables and the two places that use them, `add_tests`, the fixtures, and the other
three scripts.

| Test | Asserts |
|---|---|
| `test_clang_tidy_wiring` (new) | **stub project** (written into the build tree, with a copy of the real fixtures): with the tool empty, `ctest -N` lists no `clang_tidy_` test; with `sh` as the tool (never run), over a fixture tree of units in two directories, one target built only when a stub option is on, and one canary directory, it lists exactly the tests of the targets that exist, by name, plus `clang_tidy_version`, `clang_tidy_checks`, `clang_tidy_units` and the three canaries; `clang_tidy_version` carries `LLVM version 23\.`; the three canaries and `clang_tidy_checks` name `--config-file=<root>/.clang-tidy`; the canary fixtures are copied to their two paths. **real root:** configured with `-DCRYPTOCOGS_CLANG_TIDY=<missing>` it fails with the module's message and before any dependency (as `test_tree_name` does for the guard); with a tool and `-DCRYPTOCOGS_BUILD_TESTS=OFF` it is refused, naming the option; as a subdirectory of a parent project, with the variable set, it configures and registers nothing (the two header-only dependencies are taken from the sources this build fetched, as `test_tree_name` does, so there is no network) |
| `test_clang_tidy_config` (new) | `ExcludeHeaderFilterRegex` is `/_deps/`; every header the checkout owns (`include/`, `src/`, `tests/`) matches `HeaderFilterRegex`, so a header added under a directory the filter does not name fails here; the file has no `WarningsAsErrors`; `.clangd` has no `ClangTidy` list |
| `clang_tidy_units` (new, gate on) | the compile database's units, less the fixtures, equal the registered ones |
| the 522 existing | pass |
| the 83 `clang_tidy_*` (new, gate on) | as in the table above |

**Done when:**

- **Gate off:** the checkpoint is green, 524 of 524 on the Mac, `ctest -N` lists no `clang_tidy_` test, and the compile database is
  byte-identical to the one before.
- **Gate on** (`-DCRYPTOCOGS_CLANG_TIDY=clang-tidy` in `debug-macos-arm64`): 83 tests are registered; the six proofs pass; the 77
  units give the measured picture, 5 green and 72 red, twice, identically, the gate's own log digesting to 329 sites, no compile
  error, and no red unit without a finding. A missing tool stops the configure with the message. A Crypto.com-only tree registers
  31 and a benchmarks tree 90, and `clang_tidy_units` passes in both.
- **The editor still sees the hints.** An LSP probe of `/opt/homebrew/bin/clangd` (flywheel's `tools/scripts/clangd_hints.py`, run
  against this checkout, or a scratch script as flywheel-dag#60 used), with `.clangd`'s list gone, still publishes
  `performance-unnecessary-value-param` on a file with a known site, and stops publishing it when `.clang-tidy` is removed. If not,
  `.clangd` keeps its block and I stop and tell you.
- **Hand-made changes**, each on a scratch copy, fail exactly the case stated:

  | Change | Fails |
  |---|---|
  | M1 `.clang-tidy` renamed | `clang_tidy_checks`, the three canaries and all 77 units (the file is named by every run, D8), and `test_clang_tidy_config` |
  | M2 `include/exchange` out of `HeaderFilterRegex` | the own-header canary, and `test_clang_tidy_config` |
  | M3 `ExcludeHeaderFilterRegex` removed | the dependency-header canary, and `test_clang_tidy_config` |
  | M4 `performance-*` out of `Checks` | `clang_tidy_checks` and the own-header canary |
  | M5 `--warnings-as-errors=*` left off | the three canaries (no `error:`) and `test_clang_tidy_module`; the 72 red units turn green, which is the vacuity the canaries exist to catch |
  | M6 a by-value `std::string` parameter seeded in one `include/exchange` header | exactly the per-file tests of the units that include it change (found with `-MM`): a green one turns red, a red one gains a site; no other test changes |
  | M7 a new unit added to a target (`add_executable` in `tests/unit/`) | its own `clang_tidy_…` test appears at the next configure, red with a seeded defect and green without; `clang_tidy_units` stays green |
  | M7b the same `add_executable` placed after the registration call in the root | `clang_tidy_units` fails, naming the unit |
  | M8 the major set to 22 | `clang_tidy_version` |
  | M9 `tests/sanitize_checks/canaries/` not excluded in `select` | `test_clang_tidy_module`, and in an `asan` tree two more units appear |
  | M10 `src/` walked without the `_deps` and build-tree exclusions | `test_clang_tidy_wiring`, and a dependency's sources appear among the units |
  | M11 `add_compile_options(-Werror=deprecated-declarations)` after the dependencies in the root (what plan 036 will do, and what flywheel does) | nothing: the gate's picture is unchanged, 5 green, 72 red, 329 sites, no compile error, which is what D10's override is for |
  | M12 M11, and the override dropped from `cryptocogs_clang_tidy_args` | `test_clang_tidy_module`, and on the Mac 65 of the 77 units, each with a `clang-diagnostic-deprecated-declarations` error in the SDK's libc++ |

Commit: `build: add .clang-tidy and the opt-in clang_tidy_ tests (step 3/6)`.

### Step 4 — The Linux half

No commit besides the plan's record. In the container, with `debug-linux-arm64` and `ctest -j6` (7 GB of memory; the slowest unit took
6 s on the Mac, and no unit's memory is measured): the gate on, then off. **Done when:** gate off passes 524 of 524; gate on registers
83, the six proofs pass, and the units give the Mac's picture less the Mac-only `ws_client.inl:119` site, every red one a site the Mac
also reports. A Linux-only site, or a compile error in a unit, is a stop.

### Step 5 — Record the findings in a new issue

No code. [#46](https://github.com/tomlin256/cryptocogs/issues/46) already exists: Rob asked for it at approval, so it was opened from the
scratch baseline of this plan's measurements (label `enhancement`; the repo has no priority labels), with the baseline, the tables by
check and by area, the 35 units that are red only through 23 header sites, how to re-measure, what done is, its relation to #38 (the 14
`performance-enum-size` sites are #38's ABI question, to be answered before they are "fixed"), a first comment with every site grouped
by file, and a note that it is planned from scratch in a fresh session. #34 and #38 each have a comment that links it. This step
makes the issue's numbers the gate's own: on the Mac and in the container, the gate on and `ctest -R '^clang_tidy_'` with the output
kept, digested by a scratch script with the counting command that #46 gives. **Done when:** the numbers in #46's description and
first comment equal what the gate printed on both platforms (any that differ are edited, and the first comment regenerated), and
#46's "Blocked by #34" line is replaced by the commit that landed the gate.

### Step 6 — Docs, the sibling check, and close

- **`CLAUDE.md`**: a "clang-tidy (opt-in)" subsection after Sanitizers (the commands, `-j8` on the Mac and `-j6` in the container, what
  each test proves, that the per-file tests are red until the findings issue is done and why that is not a failure of the gate, the
  pin and the one commit that moves it, that the variable sticks in the tree's cache, that a new adapter needs nothing because the
  units come from the targets, and where to get the tool); the layout rows for `.clang-tidy`, `cmake/cryptocogs_clang_tidy.cmake` and
  `tests/clang_tidy_checks/`; the test tables and counts (three more `cmake -P` checks: 524 in ctest, and 134 in a Crypto.com-only
  tree).
- **`README.md`**: the two commands under *Running the tests*.
- **Sibling repos.** Compare, read only, the two ideas that go beyond what flywheel and flywheel-dag have: the target-derived unit list
  with its compile-database proof (D9), and `clang_tidy_units` itself. Raise an issue in each sibling where it applies, with the
  measurement, and tell you the numbers; edit neither repo.
- **This plan**: Status, Progress and an Outcome. The memory note on running the gate by hand is added if something in it is not in
  `CLAUDE.md`.
- Read CI on the last commit (`gh run list`): the main run runs the three always-on tests on Linux x86_64 with GCC 15.2. Then
  `gh issue close 34 --comment "..."`.

**Done when:** the pushed tip is green and #34 is closed with a summary. Commits: `docs: say how to run clang-tidy (step 6/6)`, then
`docs(plan): 035 is done`.

## Self-review

**Risks**

- **The opted-in run is red by design (D1).** A reader who turns it on sees 72 failures. The six proofs are green, and `CLAUDE.md`
  says so. If you would rather the gate be green when on, that is D1's alternatives, not a smaller change.
- **The walk runs at the end of the root.** A target added by an `add_subdirectory` after the call is missed by the registered list,
  and `clang_tidy_units` is what notices (M7b). A unit whose sources are a generator expression or a generated file would be missed
  the same way; there is none today.
- **The filter is a path match.** A checkout under a directory named `tests` admits more than it should; the `_deps` exclusion and the
  dependency canary are what keep dependencies out. A local clone of a dependency (`FETCHCONTENT_SOURCE_DIR_*`) sits outside `_deps`
  and is reported only if a header path matches `/include/exchange/` or `/tests/`.
- **The Mac is the stricter gate.** One `bugprone-exception-escape` site appears on the Mac alone; a gate run only in the container
  would miss it.
- **Homebrew and the links are outside the repo.** `llvm` is keg-only and the links in `/opt/homebrew/bin` were made for
  flywheel-dag#60. A Homebrew `llvm` at 24 fails `clang_tidy_version` on purpose (D2).
- **Release and debug agree today** (329 sites each); a later `#ifndef NDEBUG` could split them. The gate is documented on the debug
  tree.
- **GCC-shaped compile commands.** The Linux trees' commands carry only `-g` or `-O3`, `-std=gnu++23` and the include flags, and the
  container run had no compile error. A GCC-only flag added to the project later would stop clang-tidy with an unknown-argument error;
  plan 036's warning flags (`-Wall -Wextra -Wpedantic`, `-Werror=deprecated-declarations`) are ones clang knows, and M11 and M12 are
  where this plan shows the gate surviving the one that matters.
- **The override is inert until plan 036 (D10).** A flag that nothing exercises rots, so M11 simulates the build carrying
  `-Werror=deprecated-declarations` and M12 removes the override, and `test_clang_tidy_module` asserts it is in the arguments.
- **A file no target compiles is outside the gate** (D9), where the siblings analyse it with inferred flags. It is also outside the
  build.
- **Only instantiated templates are analysed**, so a header template that no unit instantiates is not checked. The suites instantiate
  what they test.
- **The canaries read clang-tidy's wording** (`error:`, `[check-name`, `LLVM version`). A rewording turns them red, which is the right
  failure: the gate's output changed.
- **CI sees the new always-on tests.** They need no clang-tidy, but they configure the real root three times (no network) and run on
  GCC 15.2 and in the nightly sanitizer trees, which I can only reproduce here on GCC 13.3; Step 6 reads that run.
- **Editing the root `CMakeLists.txt` and `tests/CMakeLists.txt` misses CI's dependency cache once.** Its key hashes both. The first
  main run after Step 3 clones the seven dependencies again; ccache serves their objects.
- **`test_ci_cache_key` reads the literal declare call.** The new module and scripts must not contain it, even in a comment.

**Assumptions**

- clangd reads `.clang-tidy` (shown for flywheel-dag#60 by an LSP probe); Step 3 re-checks it here before `.clangd` loses its list.
- The suite stays under `ctest` with no sleeps; the gate needs no network beyond the configure's dependency fetches.
- Nothing is generated at build time (`git grep` finds no `add_custom_command`, `add_custom_target` or `file(GENERATE)` in the CMake
  files), so clang-tidy needs a configured tree and no build.
- `compile_commands.json` is complete after configure: the root sets `CMAKE_EXPORT_COMPILE_COMMANDS` as a normal variable, so a command
  line cannot turn it off (a scratch configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=OFF` still wrote it).

## Not in this plan

- **Clearing the 329 sites**: [#46](https://github.com/tomlin256/cryptocogs/issues/46), in a fresh session. #38's two parts stay with
  #38 until its ABI question is answered.
- **The build's own warnings as errors**: [#47](https://github.com/tomlin256/cryptocogs/issues/47), plan 036, written after this plan
  is done (D10). Nothing here changes a compile flag.
- **A CI job** (D4), **wider checks** (D3), and **Include Cleaner**.
- **Presets for the gate** (D5), and a gate in the sanitizer trees: it works there but proves nothing the debug tree does not.
- **Anything in flywheel and flywheel-dag** beyond the issues Step 6 may raise.

## Designs I rejected

- **`CMAKE_CXX_CLANG_TIDY`**, tidy during the compile. It runs only on what an incremental build recompiles, with the build's own
  flags; the compile database is what the editor uses, and one tool run per file is what ctest can count.
- **A glob of `src/` and `tests/`** (D9). Simpler, and it works: a unit absent from the database parses with inferred flags. But it
  checks code the tree does not build, and nothing holds its list to the compile database.
- **A baseline file of known findings.** It parks 329 closable defects (D1).
- **`WarningsAsErrors: '*'` in `.clang-tidy`.** clangd would then show every hint as an error.
- **A header filter built by CMake from the source directory.** It would anchor the match precisely, but a hand-run `clang-tidy` and
  the editor would not have it, and the canaries would test a command the editor never sees.
- **A floor, "23 or later", for the major.** It lets the Mac run 24 while a container runs 23.
- **`brew link --force llvm`, or `llvm/bin` first on `PATH`.** Both put Homebrew's `clang` ahead of Apple's; the two links that exist
  are enough.

## Progress

| Step | Status | Notes |
|---|---|---|
| 0 Approval | Done | Proposed in `1217ac6`, linked from #34. Rob approved on 2026-11-05 with D10 changed (override stays; the build's warnings as errors are #47, plan 036 after this). #46 opened for the 329 findings and linked from #34 and #38; #47 opened for warnings as errors. |
| 1 Start from green | Done | No commit. Mac, `debug-macos-arm64`: configure prints only the two CMake deprecation notices that #26 tracks (backward-cpp, CLI11) and exits 0, the build was up to date, ctest 521 of 521 in 36 s. Container, `debug-linux-arm64`: the same two notices, the build recompiled the two newest tests with no compiler warning in 1 min 50 s, ctest 521 of 521 in 12 s, and its compile database now has all 77 units. |
| 2 The module, test first | Done | `build: add cmake/cryptocogs_clang_tidy.cmake and the test that holds it (step 2/6)`. `test_clang_tidy_module` was red first (the module did not exist), then green in 0.2 s. 42 hand-made mutants of the module, each run through the test on a scratch copy, are all killed by the assertion meant to catch them. The first pass left two alive, the root's `NORMAL_PATH` dropped in `test_name` and in `select`, because no case spelled the root with dot segments; two (`/x/../r/.`) were added and both are killed. Mac: configure output identical to Step 1's, build up to date with no warning, ctest 522 of 522 in 21 s. Container: the same, 522 of 522 in 24 s. |
| 3 The configuration and the gate | Pending | |
| 4 The Linux half | Pending | |
| 5 Record the findings in a new issue | Pending | |
| 6 Docs, the sibling check, and close | Pending | |

## Outcome

Written when the plan is done.
