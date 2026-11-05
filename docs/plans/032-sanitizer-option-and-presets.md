# Plan 032 — A sanitizer option and presets: `asan` and `tsan` trees, and a nightly that runs them

**Status:** Done (2026-11-05), except for one done-when that was not met. Rob approved the plan as written, so D1 to D8 stood at the defaults below, and asked for one
addition: an issue in flywheel proposing the compile-side coverage check (D4) for its own sanitizer tests, which is
[flywheel#209](https://github.com/tomlin256/flywheel/issues/209). Steps 0 to 9 are done and pushed: #27 and #28 were fixed by
[plan 033](033-tsan-races-in-the-tests.md), and `tsan` is in the nightly. Step 9 asked for a green `schedule` run before #30 closed. The tick of 03:41 UTC on 2026-11-05 made no
run (see the Outcome), and Rob asked for #30 to be closed that day without one, so no scheduled run has been read.

Closes [#30](https://github.com/tomlin256/cryptocogs/issues/30) — "A sanitizer option and presets: asan and tsan trees are
hand-made today".

**Design source:** flywheel's sanitizer work, which is built, running nightly, and the record of what went wrong when it was
first built: `cmake/FlywheelSanitize.cmake`, `tests/check_sanitize_flags.cmake`, `tests/run_canary.cmake`,
`tests/check_sanitizer_link_lines.cmake`, `tests/sanitizer_canaries/`, `.github/workflows/sanitizers.yml` and
`docs/plans/sanitizer_ci_plan.md` (a private repository). This plan differs from it in D4 (the coverage check also reads
`compile_commands.json`, so the fetched dependencies' own sources are checked), D7 (no caches in the nightly) and in what it
measures.
**Follows:** [plan 028](028-platform-build-dirs.md), which made the `asan` and `tsan` trees by hand, found the two TSan races,
and left this issue for later.
**Blocked, for Step 9 only, by:** [#27](https://github.com/tomlin256/cryptocogs/issues/27) and
[#28](https://github.com/tomlin256/cryptocogs/issues/28), the two races. They get plans of their own (D7).

## Problem

Plan 028 built an AddressSanitizer tree and a ThreadSanitizer tree by hand — `-DCMAKE_CXX_FLAGS="-fsanitize=…"` in
`build/asan-linux-arm64` and `build/tsan-linux-arm64` — and ran them once. They cannot be rebuilt from the repository: the flags
live in a plan's prose; nothing stops a tree called `tsan-…` from being configured without them, because the guard reads the OS
and the architecture in a tree's name and nothing else; and nothing in CI runs a sanitizer. A bug of the kind sanitizers find
shows itself only when a toolchain change happens to expose it, or when someone builds the tree again by hand.

### What I measured (2026-11-04)

- **The hand-made trees** (plan 028, Step 4; container, GCC 13.3.0, Linux aarch64, `RelWithDebInfo`, `--parallel 8`):
  `-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer` with
  `ASAN_OPTIONS=detect_stack_use_after_return=1:strict_string_checks=1:detect_leaks=1` ran 499 of 499 with no report (configure
  40 s, build 133 s, ctest 8 s). `-fsanitize=thread` ran 497 of 499 with two reports, both races in the tests and not the
  library: #27 (`Execute.BlocksUntilResponse` reads the mock's `sent_messages` from another thread, ordered by a `sleep_for`)
  and #28 (`WsReconnectSession.BackoffResetsAfterSuccessfulReconnect` shares a plain `int`). Both issues are open, labelled `bug`.
- **Nothing else runs a sanitizer.** No CMake file or workflow contains `-fsanitize`; `CLAUDE.md` says `ubsan`, `asan` and
  `tsan` "are made by hand". LeakSanitizer is the part macOS cannot do (Apple's runtime refuses `detect_leaks`).
- **What a coverage check can see.** `build/debug-macos-arm64` (Unix Makefiles, Apple clang 21.0.0, CMake 4.4.5) has 125
  translation units in `compile_commands.json`, 50 of them in fetched dependencies (ixwebsocket 37, spdlog 7, googletest 4,
  backward-cpp 2), and 40 executable link lines. So "the fetched dependencies are instrumented too" is something a tree can be
  checked for, from files the build already writes.
- **CI** is one workflow on `ubuntu-latest`, `release-linux-x86_64`. The repository is public, so Actions minutes are free.

## The change

**The option.** `cmake/cryptocogs_sanitize.cmake` holds one pure function,
`cryptocogs_sanitizer_flags(<mode> <compile-out> <link-out>)`, so that `cmake -P` can test the mapping in milliseconds with no
configure, and the root `CMakeLists.txt` calls the very same function:

| Mode | Compile options | Link options |
|---|---|---|
| `OFF` or empty (default) | none | none |
| `asan` | `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined` | `-fsanitize=address,undefined` |
| `tsan` | `-fsanitize=thread -fno-omit-frame-pointer` | `-fsanitize=thread` |

Anything else is a `FATAL_ERROR` that names the value and lists the modes. `-fno-sanitize-recover=undefined` is there because
UBSan's default is to print and carry on, which leaves a green run over a real report; ASan aborts by itself and TSan exits
non-zero once it has reported. The root file, after the `CRYPTOCOGS_BUILD_*` options and before `find_package(OpenSSL)` and the
first `FetchContent_MakeAvailable`:

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/cryptocogs_sanitize.cmake)
set(CRYPTOCOGS_SANITIZE "OFF" CACHE STRING "Sanitizer: OFF, asan (address + undefined) or tsan (thread)")
set_property(CACHE CRYPTOCOGS_SANITIZE PROPERTY STRINGS OFF asan tsan)
cryptocogs_sanitizer_flags("${CRYPTOCOGS_SANITIZE}" _cryptocogs_sanitize_compile _cryptocogs_sanitize_link)
if(_cryptocogs_sanitize_compile)
    add_compile_options(${_cryptocogs_sanitize_compile})
    add_link_options(${_cryptocogs_sanitize_link})
    message(STATUS "cryptocogs: sanitizer '${CRYPTOCOGS_SANITIZE}' (${_cryptocogs_sanitize_link})")
endif()
```

**The proof that the flags are live.** Three tiny programs (`tests/sanitize_checks/canaries/`), each committing one deliberate
violation — a heap use-after-free, a signed overflow, a data race between two joined threads — are built and registered only in
the tree whose mode must catch them. `run_canary.cmake` runs one and requires a **non-zero exit and the sanitizer's own report
header**: exit 0 with no report means the flags never applied; exit 0 *with* a report means live but not stopping (UBSan's
default); non-zero with no report means it died of something else. And `check_sanitizer_coverage.cmake` reads what the build
wrote: every translation unit in `compile_commands.json` must carry the mode's compile options, and every executable's
`link.txt` its link options, so an `add_subdirectory` placed above the option, or a dependency that resets its flags, goes red
instead of being quietly uninstrumented.

**The guard.** `cryptocogs_tree_problem` gets a fifth argument, the value of `CRYPTOCOGS_SANITIZE`, and a second rule: a tree
whose kind is `asan` or `tsan` must be configured for that mode. Nothing else is required of a sanitizer tree.

**The presets.** Hidden kind presets `asan` and `tsan` set `CMAKE_BUILD_TYPE` to `RelWithDebInfo` and `CRYPTOCOGS_SANITIZE` to
their own mode; six visible configure presets (`asan-macos-arm64`, `asan-linux-arm64`, `asan-linux-x86_64`, and the same for
`tsan`), each with a build preset and a test preset of the same name, built exactly as the existing six:

```json
{"name": "asan", "hidden": true, "cacheVariables": {"CMAKE_BUILD_TYPE": "RelWithDebInfo", "CRYPTOCOGS_SANITIZE": "asan"}},
{"name": "asan-macos-arm64", "inherits": ["base", "asan", "macos-arm64"]},
```

**The workflow.** `.github/workflows/sanitizers.yml`, separate from the main one: `schedule` (03:41 UTC) and
`workflow_dispatch` with a `which` input (`both`, `asan`, `tsan`); one matrix job per mode on `ubuntu-latest`, running
`cmake --preset <mode>-linux-x86_64`, the build preset with `-j $(nproc)`, and `ctest --preset <mode>-linux-x86_64`, with the
runtime options in the job's `env:` (they are CI's own and are repeated in `CLAUDE.md` for local runs):
`ASAN_OPTIONS=detect_stack_use_after_return=1:strict_string_checks=1:detect_leaks=1`, `UBSAN_OPTIONS=print_stacktrace=1`,
`TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1`. The tsan job first logs the runner's kernel, GCC and CMake versions and
`vm.mmap_rnd_bits`, and lowers that to 28 only if it is higher: recent kernels randomise `mmap` beyond what TSan's shadow layout
supports and the process then dies at start-up with "unexpected memory mapping". The first dispatch run says whether this image
needs it; I do not assume.

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 Nightly, not on every push**, as the issue proposes. A separate scheduled workflow means a red sanitizer run cannot mask
  whether the normal gate is green. Cost is not the constraint here (public repository), so per-push is a trigger edit later if
  you want faster feedback; the price of nightly is a finding reported up to a day after the push.
- **D2 Two named modes, not free-form flags.** ASan and TSan cannot share a binary, a mistyped list must not quietly mean
  "no sanitizer", and the guard (D5) needs a closed set of modes to compare a tree's name with. The match is exact and
  case-sensitive. `asan` includes UBSan, as the hand-made `asan` tree did.
- **D3 Dependencies are instrumented too, and the option leaves the build type alone.** The flags go in before the first
  `FetchContent_MakeAvailable`, because a directory's options reach only the targets created after the call. TSan needs it: it
  sees only synchronisation it can intercept or instrument, so a hand-rolled handshake inside uninstrumented code reads as a
  race on our side. The presets, not the option, choose `RelWithDebInfo` (line numbers for reports, `-O2` speed; what plan 028's
  trees used).
- **D4 Prove it with canaries and a coverage check** (about 300 lines of CMake and C++ in all), ported from flywheel, plus the
  compile-side half that flywheel's link-line check lacks. A sanitizer tree whose flags never reached the compiler is green on
  every real test; that is the failure this issue is about (the name and the flags drifting apart), in another form.
- **D5 The guard checks the sanitizer in a tree's name.** `asan-…` and `tsan-…` trees must be configured for that mode;
  `release-…` with `asan` stays legal. This changes plan 028's "the guard examines the OS and the architecture in the name,
  not the kind" for these two kinds only. flywheel's guard has the same rule. The platform table itself is untouched, so the
  three copies of it stay the same.
- **D6 Findings policy.** A sanitizer finding is a bug outside this plan: one `bug`-labelled issue, and I tell you; if the fix
  is small and gets a regression test that is red under the sanitizer first, it may be fixed here, otherwise I stop and ask. No
  test is skipped, excluded or weakened to get a green job, and no suppression file lands without your OK and a comment naming
  its cause.
- **D7 The nightly's shape, and #27 and #28.** Linux x86_64 only (the CI host; macOS and `linux-arm64` get presets but no CI
  leg, [#29](https://github.com/tomlin256/cryptocogs/issues/29) is the macOS leg). **No caches**: a cold run is a few minutes
  of free time, and a dependency tree restored from the wrong mode is partial instrumentation with no error to say so. The
  schedule runs **asan alone** until tsan is green; tsan is dispatch-only until #27 and #28 are fixed and five consecutive
  `tsan` dispatch runs are green (one green TSan run proves little: it reports a race only when it sees both accesses). #27 and
  #28 each say "for its own plan"; I will write those plans after Step 7 is pushed, and the presets make their red-then-green
  provable with `ctest --preset tsan-…` and not a hand-made tree. #30 stays open until Step 9.
- **D8 Pushes and a public comment.** Each step is one commit on `main`, pushed once its checkpoint is green; approving the plan
  approves those pushes, the dispatch runs of Step 6 and Step 9, and a comment on #30 linking this plan (public, since the
  repository is).

## Steps

Every checkpoint is a full build and ctest of `debug-macos-arm64` (the Mac's editing tree), all green and warning-free; the
push then starts the main workflow, which must stay green. That tree is not built at the moment (`ctest -N` there lists 31
entries, all `_NOT_BUILT`), so Step 1 begins with a baseline. The counts below are derived from the 505 that `CLAUDE.md` and
plan 031's CI run record, and are replaced by the real numbers in the Outcome. Commits are `<type>: … (step N/9)`; the last
says `Closes #30`.

### Step 0 — Approval

You approve or edit. Commit this plan (`docs(plan): plan 032 approved as written`), push, and comment on #30 with its path.
*Done when:* #30 links the plan and the plan links #30.

### Step 1 — The flags module and its test

In this order:

0. **Baseline.** `cmake --preset debug-macos-arm64`, build, ctest, on the unmodified tree: 505 of 505, no warning. Anything
   else: stop and tell you (a pre-existing failure is not mine to step over).
1. Red: `tests/sanitize_checks/check_sanitize_flags.cmake` and `test_sanitize_flags` in `tests/CMakeLists.txt`, a plain
   `cmake -P` registered at the top level in every tree, as `test_platform_name` is. It fails: the module is missing.
2. Green: `cmake/cryptocogs_sanitize.cmake`. The test passes.

*Test (new) `test_sanitize_flags`:* `OFF` and the empty string give no flags; `asan` and `tsan` give exactly the lists in the
table, compared as strings so that the order counts; each of `address ASAN Asan thread TSAN msan adress ON off asan,tsan
-fsanitize=address ubsan lsan` is refused, each in a child `cmake -P` run (a `FATAL_ERROR` ends the process) whose exit status
is non-zero and whose message names `CRYPTOCOGS_SANITIZE='<value>'` and lists `OFF, asan, tsan` (compared by words, since CMake
word-wraps the message).
*Checkpoint:* 506 of 506. Nothing is wired yet, so the default tree is unchanged.
*Done when:* the test was seen red and then green, and the checkpoint is green and pushed.

### Step 2 — The option, and an offline test that it reaches the project's targets

Red then green: the test first (it fails, there is no option), then the root `CMakeLists.txt` block above.

*Test (new) `test_sanitize_option`:* `check_sanitize_option.cmake`, registered at the top level in every tree. It configures the
real project offline, as `test_tree_name` does (tests off, `FETCHCONTENT_FULLY_DISCONNECTED`, the two header-only dependencies
taken from the running build), in scratch trees named so the guard does not examine them (`opt-default`, `opt-asan`,
`opt-tsan`, `opt-bad`), and requires:

- no option: no sanitizer status line, `CRYPTOCOGS_SANITIZE:STRING=OFF` in the cache, no `-fsanitize` in `compile_commands.json`;
- `asan`, `tsan`: the status line names the mode, and every translation unit in `compile_commands.json` carries each option of
  the mode (the expected list is read from the module, so the flags test is the only place that pins the text); it refuses to
  pass having scanned fewer than 5 units;
- `adress`: the configure fails, naming the value and listing the modes.

*Checkpoint:* 507 of 507; the default configure prints nothing new; `debug-macos-arm64` is reconfigured so that clangd's
database is current.
*Done when:* red then green, checkpoint green, pushed.

### Step 3 — The canaries and the coverage check, in sanitizer trees only

Files: `tests/sanitize_checks/canaries/canary_asan.cpp`, `canary_ubsan.cpp`, `canary_tsan.cpp` (each with the project banner, and
written so the optimiser cannot delete the violation: `volatile`, `noinline`, and for the overflow a `volatile` sink, because GCC
builds the check into the sum and drops both when the sum is unused), `run_canary.cmake`, `check_sanitizer_coverage.cmake`, and
their registration in `tests/CMakeLists.txt`:

| Test | Registered when | Requires |
|---|---|---|
| `sanitizer_canary_asan` | mode `asan` | non-zero exit and `AddressSanitizer: heap-use-after-free` |
| `sanitizer_canary_ubsan` | mode `asan` | non-zero exit and `runtime error: signed integer overflow` |
| `sanitizer_canary_tsan` | mode `tsan` (links `Threads::Threads`) | non-zero exit and `ThreadSanitizer: data race` |
| `test_sanitizer_coverage` | any mode, generator is Makefiles | every unit in `compile_commands.json` and every executable `link.txt` carries the mode's options; refuses to pass having scanned fewer than 20 units and 5 executables (125 and 40 today) |

*By hand,* two trees under `build/`: `cmake -S . -B build/asan-macos-arm64 -DCMAKE_BUILD_TYPE=RelWithDebInfo
-DCRYPTOCOGS_SANITIZE=asan` and the same for `tsan`. Build only the canaries (`cmake --build <tree> --target canary_asan
canary_ubsan`), and run `ctest -R 'sanitizer|test_sanitize'`. The whole suite is Step 8's, after the push.
*Mutations,* each in a scratch copy (`git archive HEAD | tar -x`, or `rsync -a --exclude 'build*' --exclude .git` for uncommitted
edits — never `--exclude build`, which leaves the sibling trees in; `df -h /` first; delete each copy once its result is
recorded), configured with `FETCHCONTENT_SOURCE_DIR_<NAME>` pointing at the checkout's `_deps` so that nothing is cloned. Each
must fail the tests named, and the results go in the Outcome:

| # | Mutation | Must go red |
|---|---|---|
| A | the root stops calling `add_compile_options`/`add_link_options` (asan, then tsan) | the canaries, `test_sanitizer_coverage`, `test_sanitize_option`; not `test_sanitize_flags` |
| B | `-fno-sanitize-recover=undefined` removed from the module | `sanitizer_canary_ubsan` (exit 0 with a report), `test_sanitize_flags` |
| C | `add_subdirectory(src)` moved above the option block | `test_sanitizer_coverage`, naming the `src/` units; the canaries stay green, which is why the compile-side check exists |
| D | `-fsanitize` removed from the link options only | the build fails at link, before a test can run |

*Checkpoint:* default tree 507 of 507, and `ctest -N` there lists no canary.
*Done when:* in the asan tree `sanitizer_canary_asan`, `sanitizer_canary_ubsan`, `test_sanitizer_coverage`, `test_sanitize_flags`
and `test_sanitize_option` pass; in the tsan tree the same with `sanitizer_canary_tsan`; each mutation fails what the table
says; pushed. GCC's behaviour on the canaries is first seen in Step 6.

### Step 4 — The guard learns the sanitizer

Red then green. `check_platform.cmake` and `check_tree_name.cmake` first.

- `cmake/cryptocogs_platform.cmake`: `cryptocogs_tree_problem(<out> <name> <platform> <recorded> <sanitize>)` and rule 2. The
  header comment says which copies have the rule (today this one and flywheel's; flywheel-dag's has none).
- Root `CMakeLists.txt`: the guard call passes `"${CRYPTOCOGS_SANITIZE}"`, which it reads from the cache before the option is
  declared, as the platform is.

*Tests (modified) `test_platform_name`:* its three helpers take the fifth argument. Accepted: `asan-linux-arm64` with `asan`,
`asan-linux-arm64-gcc13` with `asan`, `tsan-macos-arm64` with `tsan`, `release-linux-arm64` with `asan` (legal), and
`asanx-linux-arm64` with nothing (a kind that only starts like a sanitizer's). Refused, naming the tree and the mode:
`asan-linux-arm64` with nothing, with `tsan`, and `tsan-macos-arm64` with `asan`. Every existing case gets an empty fifth argument.
*Test (modified) `test_tree_name`,* real configures: `asan-<host>-gcc13` with `-DCRYPTOCOGS_SANITIZE=asan` configures;
`asan-<host>` with nothing and with `tsan` are refused, naming the tree and both modes, and leave no compiler detection behind
(the existing `expect_no_compiler`); `release-<host>` with `asan` configures.
*Mutation:* rule 2 deleted (in a scratch copy) must fail both tests.
*Checkpoint:* 507 of 507. *Done when:* red then green, the mutation red, pushed.

### Step 5 — The presets

Red then green: `check_presets.cmake` first.

- `CMakePresets.json`: the two hidden kind presets and the six visible ones, with their build and test presets (the test
  presets as the existing six: `outputOnFailure`, `noTestsAction: error`).
- *Test (modified) `test_presets`:* the `kinds` table becomes `<name>:<CMAKE_BUILD_TYPE>:<CRYPTOCOGS_SANITIZE or empty>`
  (`asan:RelWithDebInfo:asan`, `tsan:RelWithDebInfo:tsan`); R4 requires a sanitizer kind to set `RelWithDebInfo` and its own
  mode, which the module must accept, and no other kind to set the variable; R5 and R6 already loop over the kinds and the
  hosts; R7 now expects four presets per section on Darwin and eight on Linux.
- *Mutations* in a scratch copy, each must fail `test_presets` naming the rule: `tsan` kind preset sets `asan`; a build preset
  missing; a test preset without `noTestsAction`; `asan-linux-arm64` pointing at the `tsan` kind.

*Checkpoint:* 507 of 507; `cmake --list-presets` on this Mac shows `release`, `debug`, `asan` and `tsan` for `macos-arm64`;
`cmake --preset asan-macos-arm64` and `tsan-macos-arm64` reuse Step 3's trees and change no cache value in them.
*Done when:* red then green, mutations red, pushed.

### Step 6 — The workflow

`.github/workflows/sanitizers.yml` as designed, without caches, with `permissions: contents: read`, a `concurrency` group per
`which` that cancels a run in flight, `fail-fast: false` (one mode red must not hide the other), the matrix expression
`fromJSON(inputs.which == 'tsan' && '["tsan"]' || inputs.which == 'both' && '["asan","tsan"]' || '["asan"]')` (the schedule has
no `which`, so it gets asan alone, as does `which: asan`), and a `timeout-minutes` that starts at 45 and is set from the cold
run. *Tests:* none new. The workflow is configuration, and its one offline-testable invariant, that the presets it names exist,
is `test_presets`' (Step 5); the dispatch is its test. `test_ci_paths` and `test_ci_cache_key` read only the main workflow and
must stay green.

Push, then `gh workflow run sanitizers.yml -f which=both` and `gh run watch`. Linux with GCC's runtime is a different sanitizer
from Apple's, so this is the first real pass and can find what the Mac could not.
*Checkpoint:* full build and ctest as above; the main workflow's run for the push is green.
*Done when:* the `asan` job is green, with the canaries and `test_sanitizer_coverage` in its log; the `tsan` job's result is
recorded and is either green or exactly the reports of #27 and #28 and nothing else (any other finding follows D6); the runner's
kernel, GCC, CMake, cores and `vm.mmap_rnd_bits` are in the Outcome; cold durations are recorded and `timeout-minutes` is set
from them; no deprecation annotation beyond what the main workflow already shows. The `schedule:` trigger (asan alone) is
added in this step's last commit only if the `asan` job is green; if not, the findings are issues, the trigger waits, and I stop
and tell you.

### Step 7 — Words

`CLAUDE.md`: the build-options table gains `CRYPTOCOGS_SANITIZE`; the build-trees section says `asan` and `tsan` have presets
(twelve trees), and the guard bullet gains rule 2; a Sanitizers subsection with the local commands

```bash
cmake --preset asan-macos-arm64 && cmake --build --preset asan-macos-arm64 -j
ASAN_OPTIONS=detect_stack_use_after_return=1:strict_string_checks=1 UBSAN_OPTIONS=print_stacktrace=1 ctest --preset asan-macos-arm64
cmake --preset tsan-macos-arm64 && cmake --build --preset tsan-macos-arm64 -j
TSAN_OPTIONS=second_deadlock_stack=1 ctest --preset tsan-macos-arm64
```

and the notes that earned a line in flywheel's (LeakSanitizer is Linux-only; a canary gone red means the flags no longer reach the
compiler or the optimiser deleted the violation; a clean TSan run proves less than a clean ASan run; on the Mac the runtimes
symbolize through `atos`, which can stall behind a Developer Tools prompt — `symbolize=0` — flywheel's note, kept only if it
shows up here); a CI paragraph for the second workflow; the test counts and the table of `cmake -P` checks (eleven, 507 in
ctest) with the two new rows. `test_ci_paths` already allows `CLAUDE.md` to change without a run.
*Done when:* every number in `CLAUDE.md` matches `ctest -N`, pushed. The workflow does not run for this commit.

### Step 8 — The one-off pass, locally (after the push)

On the Mac, the two preset trees, the whole suite: `ctest --output-on-failure` with the options of Step 7. Every report goes
in a table in the Outcome: sanitizer, test, class, location, ours or a dependency's, disposition (D6). Linux comes from Step 6's
dispatch runs; the `ubuntu-dev` container is a bonus only if Docker is up, and is skipped if it is not.
*Done when:* each run ends with no report, or every remaining report has an open issue and you have said how to proceed. The
expected table is: ASan none; TSan #27 and #28, and whatever Apple's runtime and ixwebsocket add.

### Step 9 — tsan joins the nightly; the issue closes

Blocked on #27 and #28 being closed by their own plans. Then: five consecutive green `tsan` dispatch runs (one `which` at a
time; a newer dispatch cancels the one in flight); the matrix fallthrough becomes `["asan","tsan"]`; `CLAUDE.md`'s CI paragraph
says both run; a scheduled run is read from `gh run list --workflow sanitizers.yml` with both jobs green. The last commit says
`Closes #30`, and #30 gets a closing comment with the outcome.
*Done when:* a `schedule` run on `main` shows both jobs green and #30 is closed.

## Designs I rejected

- **Free-form flags** (`CRYPTOCOGS_SANITIZE=address,undefined`) — D2.
- **`CMAKE_CXX_FLAGS` in the preset,** as the hand-made trees did — the flags would live in JSON where no pure-function test can
  reach them, a `-DCMAKE_CXX_FLAGS` on a command line would replace them, and the guard would have no variable naming the mode.
- **Sanitizer legs in the main workflow** — a red sanitizer leg would hide whether the normal gate is green, and plan 030's
  caches and plan 031's path filter would have to be keyed per leg.
- **Per-push sanitizers** — D1. **Caches in the nightly** — D7.
- **Folding #27 and #28 into this plan** — each says "for its own plan"; #27 changes a mock shared by four adapters' WebSocket
  tests; and this plan's mechanical steps are what make their fixes provable without a hand-made tree.
- **Skipping the canaries** — D4.

## Not in this plan

Fixing #27 and #28 (D7), or any non-trivial finding (D6). MemorySanitizer (it needs an instrumented standard library). Clang on
Linux, a macOS or `linux-arm64` CI leg ([#29](https://github.com/tomlin256/cryptocogs/issues/29)). A TSan suppression file.
Sanitizers on every push (D1). The benchmarks under a sanitizer (they would measure the instrumentation). Fuzzing and Valgrind.
A check of the workflow file beyond what `test_presets` already pins.

## Self-review — risks and assumptions

- **The size of the one-off pass is unknown.** Plan 028 ran it once, on GCC/aarch64, and found two test races. Apple's runtime
  and GCC on the runner may find more. If a pass finds more than a handful of independent bugs, I stop after Step 6 and come
  back with the list, not with a growing pile of fixes.
- **macOS is not Linux.** Step 8's Mac result is provisional; Step 6's Linux run is the real one, and LeakSanitizer exists only
  there.
- **Toolchain drift is the point.** `ubuntu-latest` moves GCC and its runtime; a nightly that goes red with no code change is
  the early warning this plan exists to give. The response is to fix the cause, never to silence it.
- **A clean TSan run proves less than a clean ASan run,** hence five dispatch runs before it is scheduled (D7).
- **Uninstrumented system libraries** (libstdc++, OpenSSL, libcurl) can hide a race from TSan or, rarely, invent one. A
  suppression needs your OK and a comment naming the cause (D6).
- **TSan on the runner image:** the `vm.mmap_rnd_bits` handling is a conditional step that logs before and after; I will read
  the first run's log rather than assume the image needs it, or doesn't.
- **Canary fragility.** An optimiser can delete a deliberate violation (flywheel's first TSan canary was removed by Apple clang,
  and GCC 15.2 dropped its first UBSan canary with the unused sum). The ports carry the fixes; a compiler upgrade that defeats
  one turns the job red, which is the right failure. GCC is first seen in Step 6.
- **`compile_commands.json` and `link.txt` are CMake's layout, not a contract.** The coverage check refuses to pass having
  scanned fewer than a floor, so a layout change fails loudly instead of passing on nothing. The floors are about a sixth and an
  eighth of today's counts, low enough for an adapter-only tree.
- **Scheduled workflows** in a public repository are disabled by GitHub after a stretch with no repository activity (60 days,
  from my memory of its documentation — Step 9 reads the scheduled run, which would show it).
- **Disk.** A sanitizer tree is about 0.5 to 0.9 GB. `df -h /` before building each, and delete scratch copies as soon as their
  result is recorded.
- **The guard change touches a tree that every build goes through.** Rule 2 only examines `asan-…` and `tsan-…` names in the
  platform shape, and the tests above include the near misses (`asanx-…`, `release-…` with `asan`).
- **I read the unit-test rule as covering code and the invariants of configuration that can be tested offline,** as plans 030
  and 031 did. The workflow has no test of its own; its dispatch run is its test. Say if you want a check of the file.

## Outcome

Steps 1 to 9 are done and pushed. Step 9 waited for [#27](https://github.com/tomlin256/cryptocogs/issues/27) and
[#28](https://github.com/tomlin256/cryptocogs/issues/28), which [plan 033](033-tsan-races-in-the-tests.md) fixed; the first scheduled run, which its
done-when asks for, has not happened (see "The scheduled run" at the end).
Measured on the Mac (Apple clang 21.0.0, CMake 4.4.5, Unix Makefiles, 12 cores) and on `ubuntu-latest` (GCC 15.2.0, CMake 4.4.4,
4 cores, kernel 7.0.0-1017-azure).

**Baseline.** `debug-macos-arm64` before any change: 505 of 505 in 14.9 s. It was not warning-free, as the plan assumed it would be:
six `-Wunused-result` in `tests/unit/test_ws_client.cpp` ([#24](https://github.com/tomlin256/cryptocogs/issues/24)) and two CMake
deprecation warnings, from backward-cpp and CLI11 ([#26](https://github.com/tomlin256/cryptocogs/issues/26)). Every checkpoint below
means "no warning beyond these".

**Step 1** (`7e3af8d`). `test_sanitize_flags` was red (`include could not find requested file`) and green once the module existed.
Six hand-made changes to the module, in a scratch copy, each failed it for the right reason: `-fno-sanitize-recover=undefined`
dropped (the asan options differ), `thread` accepted, the modes missing from the message, the error turned into a warning (`address`
accepted), tsan's options reordered (the order counts), and `off` accepted. ctest 506 of 506; CI run 41891917990 green.

**Step 2** (`463f59e`). `test_sanitize_option` was red (the cache lacked `CRYPTOCOGS_SANITIZE:STRING=OFF`, and there was no status
line) and green after the block. Five hand-made changes to the working tree, in a scratch copy:

| Change | Result |
|---|---|
| the two `add_*_options` calls removed | asan: 29 of 29 units miss an option |
| the status line removed | asan: the output lacks the status line |
| the default changed from `OFF` to `asan` | no option: the status line is there, so is `-fsanitize`, and the cache is not `OFF` |
| the mode ignored (`OFF` passed to the function) | asan: no status line |
| the block moved below `add_subdirectory(src)` | asan: 29 of 29 units miss an option |

My first version counted missing options and not units, and said "87 of 29 units"; it counts units now. ctest 507 of 507; CI run
41892709701 green.

**Step 3** (`9836143`). The hand-made trees `build/asan-macos-arm64` and `build/tsan-macos-arm64`, canaries built only: asan 5 of 5,
tsan 4 of 4. The coverage test scanned 127 compile units and 42 executable link lines in the asan tree and 126 and 41 in the tsan
tree, in 0.07 s. The canaries stop with `AddressSanitizer: heap-use-after-free`, `runtime error: signed integer overflow` and
`ThreadSanitizer: data race`. Five hand-made changes in a scratch copy, each configured and built for real from the fetched sources:

| # | Change | flags | option | canaries | coverage |
|---|---|---|---|---|---|
| A1 | the root stops applying the options (asan) | pass | **fails** | both **fail** | **fails**: 169 findings, 127 units and 42 executables |
| A2 | the same (tsan) | pass | **fails** | **fails** | **fails**: 167 findings, 126 units and 41 executables |
| B | `-fno-sanitize-recover=undefined` dropped (asan) | **fails** | pass | the UBSan canary **fails**, the ASan canary passes | pass |
| C | the block moved below `add_subdirectory(src)` (asan) | pass | **fails** | **both pass** | **fails**: 29 findings, every one a compile unit under `src/`; the link half is green |
| D | the link options alone dropped (asan) | pass | pass | not built: the build fails at link, `Undefined symbols for architecture arm64` | **fails**: 42 executables named |

C is the case [flywheel#209](https://github.com/tomlin256/flywheel/issues/209) is about: the canaries and the link half stay green
while 29 library units are uninstrumented, and only the compile-side half sees it. The plan wrote C as "`add_subdirectory(src)` moved
above the block"; `src` needs the dependency targets that the block precedes, so it is the block that moves. ctest 507 of 507, and
`ctest -N` lists no canary; CI run 41893861242 green.

**Step 4** (`1a4cef5`). `test_platform_name` and `test_tree_name` were red ("accepted", and "the configure succeeded, want it
refused") and green after rule 2. The refusal reads: *cryptocogs: the build directory "asan-macos-arm64" is named for the asan
sanitizer, but CRYPTOCOGS_SANITIZE is OFF. A sanitizer tree is configured for the mode in its name: use -DCRYPTOCOGS_SANITIZE=asan
(cmake --preset asan-macos-arm64), or name the tree for what it is.* The hand-made trees of Step 3 still reconfigure. Three changes in
a scratch copy each failed both tests: rule 2 disabled, the kind matched without its dash (so `asanx-` was refused), and the message
hiding the value. ctest 507 of 507; CI run 41894693656 green.

**Step 5** (`8b1231d`). `test_presets` was red (no kind preset `asan` or `tsan`, no visible preset) and green after the JSON;
`cmake --list-presets` on the Mac shows `release`, `debug`, `asan` and `tsan`. Six changes to the file each failed it naming the rule:
`tsan` setting `asan` (R4), `asan` setting `Release` (R4), `release` setting a mode (R4), `asan-linux-arm64` inheriting `tsan` (R5), a
missing build preset (R6), and a test preset with `noTestsAction` `ignore` (R6). `cmake --preset`, `cmake --build --preset` and
`ctest --preset` for the two presets drove the trees of Step 3 and changed no cache value. My first loop passed two targets as one
word (zsh does not split `$tg`), so the asan build step "failed" with `No rule to make target`; with the targets spelled out it exited
0. ctest 507 of 507; CI run 41895254455 green.

**Step 6** (`fb28ce4`, `153f8cc`). The first dispatch of both modes, run 41895626568, with nothing cached:

| Job | Result | Time |
|---|---|---|
| asan | **green**, 510 of 510, both canaries and `test_sanitizer_coverage` among them | 7 min 43 s: configure 28 s, build 6 min 16 s, test 19 s |
| tsan | 509 tests, 507 pass, **exactly two fail**: `Execute.BlocksUntilResponse` (reported at `test_ws_client.cpp:275`, #27) and `WsReconnectSession.BackoffResetsAfterSuccessfulReconnect` (`test_ws_reconnect_session.cpp:233`, #28); the tsan canary and the coverage test pass | 6 min 21 s: configure 30 s, build 5 min 7 s, test 29 s |

The runner: `ubuntu-latest`, kernel 7.0.0-1017-azure, `c++` Ubuntu 15.2.0-16ubuntu1, CMake 4.4.4, 4 cores. **That kernel has no
`vm.mmap_rnd_bits`** (the log says `none`), so the TSan start-up workaround did nothing and ThreadSanitizer ran. The only annotation
on either job is the `ubuntu-latest` migration notice that the main workflow carries too. So the schedule (03:41 UTC, asan alone)
went in with `timeout-minutes` set to 20, about 2.6 times the slower cold run. A second dispatch with `which: asan` (run 41897406933)
exercised the explicit-asan path under the new ceiling and was green, 510 of 510, in 8 min 46 s: dependencies 1 min 4 s (the apt step
varies, #41), configure 32 s, build 6 min 33 s, test 31 s. So the ceiling is 2.3 times the slowest run seen. **Not yet seen:** the
first scheduled run.

**Step 7** (`57e103d`, then the CI paragraph in the commit that records this Outcome). `CLAUDE.md` counts eleven `cmake -P` checks and
507 in ctest, and a Crypto.com-only tree registers all eleven (checked by configuring one); no stale count remains. The push changed
only `CLAUDE.md` and started no run (`gh api …/actions/runs?head_sha=57e103d` found none).

**Step 8.** The Mac, after the push, whole suites, `RelWithDebInfo`:

| Tree | Build | Test | Result |
|---|---|---|---|
| `asan-macos-arm64` | 42 s | 28 s | **510 of 510**, no AddressSanitizer, UBSan or other report |
| `tsan-macos-arm64` | 30 s | 49 s | 507 of 509; the two failures are the same two tests, and nothing else |

TSan on the Mac reports #28 at `test_ws_reconnect_session.cpp:244` (CI at 233: the other side of the same pair) and #27 inside libc++'s
`std::string` and `std::vector` (CI at `test_ws_client.cpp:275`). No `atos` stall was seen. LeakSanitizer is Linux-only; CI ran with
`detect_leaks=1` and was clean. Nothing turned up that the plan did not expect, so D6 had nothing to do beyond #27 and #28, and
[plan 033](033-tsan-races-in-the-tests.md) proposes their fix.

**Opened along the way.** [flywheel#209](https://github.com/tomlin256/flywheel/issues/209) (the compile-side check, with a comment
that holds the demonstration of C), [flywheel#210](https://github.com/tomlin256/flywheel/issues/210) (two comments there that call
cryptocogs' platform module sanitizer-free), and [#42](https://github.com/tomlin256/cryptocogs/issues/42) (`test_ws_reconnect_session.cpp`
sleeps and polls although `CLAUDE.md` says it is deterministic).

**Step 9** (`a940546`). Plan 033 fixed #27 and #28 (`6e711c0`, `b3ef1e2`; the Mac's tsan tree passes 514 of 514), and five consecutive
`tsan` dispatch runs on `b3ef1e2` were green: 41903619210, 41904405730, 41905586543, 41906615392 and 41907779281, 4 min 19 s to 7 min 6 s
each. The matrix now falls through to both modes: `which == 'tsan'` gives tsan, `which == 'asan'` gives asan, anything else gives both.
I checked the four inputs (`tsan`, `asan`, `both`, none) offline, and a dispatch with `which: both` exercised the branch that the
schedule takes (run 41909134424, `a940546`): both jobs green, asan 515 of 515 in 10 min 2 s and tsan 514 of 514 in 4 min 10 s. The asan
run is the slowest of the three, and the build step is what varied (8 min 36 s, against 6 min 16 s on the first run; apt 13 s,
configure 31 s, test 35 s), so the 20-minute ceiling is 2 times the slowest cold run and not the 2.3 that Step 6 wrote down; the
comment and `CLAUDE.md` say so (`2ff151d`). `CLAUDE.md`'s CI paragraph says that both modes run nightly and what the runs cost.

**The scheduled run (2026-11-05).** The tick of 03:41 UTC made no run. A waiter polled
`gh run list --workflow sanitizers.yml --event schedule` every ten minutes from 20:43 UTC on 2026-11-04 and gave up at 05:43 UTC with
nothing found; `--event schedule` lists no run of any workflow in the repository, and the newest `Sanitizers` run is still the dispatch
of both (41909134424). What I checked: the workflow is `active`; `main` is the default branch and has carried the cron since `153f8cc`
(19:29 UTC on 2026-11-04); the repository is public and not a fork; githubstatus.com lists no incident in the window (its only Actions
incident that day ran 17:40 to 18:16 UTC, before the cron was on `main`). GitHub's documentation says a scheduled run "can be delayed
during periods of high loads" and that under load "some queued jobs may be dropped"; it says nothing about the first tick of a new
cron. So one miss does not say whether the run was dropped or the schedule is not registered, and I did not try a diagnostic cron on
`main`, which would start runs that nobody asked for. Rob asked for #30 to be closed at this point, so the done-when of Step 9 ("a
`schedule` run on `main` shows both jobs green") is **unmet**, and the issue is closed without it. The next tick is 03:41 UTC on
2026-11-06; read it with the command above. If that tick also makes no run, the question is the schedule and not the build, since a
dispatch of both takes the matrix branch that the schedule takes, and it wants an issue of its own.

**Left for later.** The first scheduled run with both jobs, as above. The `ubuntu-dev` container (Linux arm64) was not used: CI covers
Linux x86_64, and the plan did not need a second Linux result.
