# Plan 030 — CI: cache the build

**Status:** Approved as written (2026-11-04), so D1 to D5 stand at the defaults below. Rob approved pushing this plan's
commits to `main` and linking the plan from #37. Steps 1 and 2 are done and measured (the Outcome has the numbers); Steps 3
and 4 are not.
**Design source:** flywheel's `docs/plans/ci_build_cost_reduction_plan.md` and its
`.github/workflows/cmake-single-platform.yml` (a private repository). This plan applies that design and restates only
what differs. The main difference: flywheel's `_deps` cache does not work here as written (measured below).
**Closes:** [GitHub issue #37](https://github.com/tomlin256/cryptocogs/issues/37) — "Improve CI bill by using cache". The
issue reads: *"Look at flywheel to see how its done"*.

## Problem

`.github/workflows/cmake-single-platform.yml` caches nothing: every push clones seven dependencies and compiles
everything on a fresh runner, serially (`cmake --build --preset` runs with no `-j`).

### What I measured (2026-11-04)

Run `41852041317` (`e9ad24f`, green): image `ubuntu-26.04`, GCC 15.2.0, CMake 4.4.4, workspace
`/home/runner/work/cryptocogs/cryptocogs`.

| Step | Time |
|---|---:|
| Install dependencies (apt) | 11 s |
| Configure CMake | 25 s |
| **Build** | **408 s** |
| Test | 9 s |
| Job | 457 s |

Build, attributed the way flywheel's corrected analysis does it: `make` prints a progress line *before* its work, so the
interval after a line belongs to that line.

| | Lines | Time | Share |
|---|---:|---:|---:|
| Compiling this project's sources | 75 | 357 s | **87.6%** |
| Compiling the dependencies (spdlog 15 s, ixwebsocket 14 s, googletest 13 s, backward-cpp 2 s) | 50 | 44 s | 10.8% |
| Linking (14 libraries, 40 executables) | 54 | 6.5 s | 1.6% |

- **The build is compile-bound**, so ccache is the lever: it can serve all 125 compiles. Caching the dependency trees
  touches 44 s of compiling and most of the 25 s of Configure, which is FetchContent (seven clones; nlohmann_json's full
  history alone takes 15 s).
- **The numbers are noisy.** Six recent runs of near-identical work had Build between 314 s and 586 s. A step is judged
  by its own counters (ccache hits, "Cloning into" lines, step durations), not by one job total.
- **The repository is public**, so standard-runner minutes are free. Today the saving is wall-clock; the money saving
  arrives if the repository goes private (flywheel is private, and its minutes have run out before).

### Flywheel's `_deps` recipe does not work here as written

Flywheel caches `build/<preset>/_deps`, keyed on the root `CMakeLists.txt`. I tried that on this project (CMake 4.4.5 on
the Mac, a scratch tree outside the checkout: configure, delete everything except what a cache would restore, configure
again):

| Restored before the second configure | Clones | Configure |
|---|---:|---:|
| nothing | 7 | 26.2 s |
| `_deps` only — flywheel's recipe | **7** | **26.8 s** |
| `_deps` and `CMakeFiles/fc-stamp`, `fc-tmp` | 0 | 1.2 s |
| `_deps` only, with `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` | 0 | 1.2 s |
| the same, every `_deps/*-src/.git` deleted | 0 | 1.2 s; spdlog and `exchange_common` then build |

- **Why.** The floor is CMake 4.0, so CMP0168 is NEW and FetchContent populates inside the main configure. It keeps its
  "already done" stamps in `<tree>/CMakeFiles/fc-stamp` and `fc-tmp`, "so that they are removed by `cmake --fresh`"
  (`FetchContent.cmake:1670`, CMake 4.4.5), not in `_deps`. Flywheel's `*-subbuild` comment describes the older layout.
- **Size.** `_deps` is 473 MB, 436 MB of it full-history `.git` (nlohmann_json's source directory alone is 343 MB).
  Compressed with `zstd -3`: 405 MB with the `.git` directories, **13 MB** without. Nothing reads `.git` on a hit.
- **Two files pin dependencies**, not one: `CMakeLists.txt` (nlohmann_json, toml++, ixwebsocket, googletest,
  googlebenchmark) and `tests/CMakeLists.txt` (backward-cpp, spdlog, CLI11). A key that hashes only the root restores a
  stale spdlog after a bump in the second.
- **Toolchains alternate.** Runs of cryptocogs and flywheel the same afternoon got CMake 4.4.4 and 4.4.5 from
  `ubuntu-latest`, so a key that carries the CMake version misses whenever a run lands on the other one.

## The change

Everything is in `.github/workflows/cmake-single-platform.yml`, apart from Step 3's check. The finished shape of the
cache-related parts (the real file carries comments saying why, as flywheel's does):

```yaml
on:
  push:  { branches: [ "main" ] }
  pull_request:  { branches: [ "main" ] }
  workflow_dispatch:              # a cache is measured by a second run on an unchanged tree

env:
  PRESET: release-linux-x86_64
  CCACHE_MAXSIZE: 500M

# ...steps...
- name: Install dependencies
  run: sudo apt-get update && sudo apt-get install -y libcurl4-openssl-dev libssl-dev zlib1g-dev ccache

- name: Tool versions
  id: toolchain
  run: |
    cmake --version | head -1
    c++ --version | head -1
    echo "id=$(c++ -dumpfullversion)-$(cmake --version | head -1 | tr ' ' '_')" >> "$GITHUB_OUTPUT"

- name: Cache third-party dependencies
  id: deps
  uses: actions/cache@v6
  with:
    path: |
      ${{github.workspace}}/build/${{env.PRESET}}/_deps
      !${{github.workspace}}/build/${{env.PRESET}}/_deps/*-src/.git
    key: deps-${{runner.os}}-${{runner.arch}}-${{env.PRESET}}-${{steps.toolchain.outputs.id}}-${{hashFiles('CMakeLists.txt', 'tests/CMakeLists.txt')}}

- name: Cache ccache
  uses: actions/cache@v6
  with:
    path: ~/.cache/ccache
    key: ccache-${{runner.os}}-${{env.PRESET}}-${{steps.toolchain.outputs.id}}-${{github.sha}}
    restore-keys: |
      ccache-${{runner.os}}-${{env.PRESET}}-${{steps.toolchain.outputs.id}}-
      ccache-${{runner.os}}-${{env.PRESET}}-

- name: Configure CMake
  run: >
    cmake --preset ${{env.PRESET}}
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    ${{ steps.deps.outputs.cache-hit == 'true' && '-DFETCHCONTENT_FULLY_DISCONNECTED=ON' || '' }}

- name: Build
  run: |
    echo "runner cores: $(nproc)"
    ccache -z
    cmake --build --preset ${{env.PRESET}} -j $(nproc)
    ccache -s
```

- `_deps` has no `restore-keys`: a partial match would restore sources of other pins, and with
  `FETCHCONTENT_FULLY_DISCONNECTED=ON` the restored `*-src` directories are trusted completely.
- ccache has them: its entries are content-addressed over compiler, flags and preprocessed source, so a stale entry misses
  and cannot be wrong. The second prefix lets a run that lands on the other CMake of the alternation (above) reuse
  a snapshot whose compiler is the same.
- `CMakePresets.json` is unchanged: "a kind preset sets `CMAKE_BUILD_TYPE` and nothing else; CI adds its own flags"
  (plan 028), and the launcher is CI's own.

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 Scope.** Parallel build, ccache, the `_deps` cache, `workflow_dispatch`. Not `paths:` filters, `concurrency` or
  `timeout-minutes`: flywheel kept those in their own plans (`ci_path_filter_debounce_plan.md` for the first two,
  `mgmt_server_stop_hang_ci_guard_plan.md` for the third), and they cut runs, not compiles. Say if you want any folded in.
  `-j` is not a cache either, but it is Step 1 of flywheel's cost plan, and 88% of this job is compiling.
- **D2 The `_deps` design.** `FETCHCONTENT_FULLY_DISCONNECTED=ON` on a hit, `.git` left out, key over both pin files — not
  flywheel's recipe as written, and not stamp-caching (see "Designs I rejected").
- **D3 One new test**, `test_ci_cache_key` (Step 3). It fails when a file that pins a dependency is missing from the key,
  which is the one way this plan can go silently wrong.
- **D4 Pushes.** Each of Steps 1 to 3 is one commit on `main`, pushed so its run can be measured; Steps 2 and 3 also
  dispatch one run (free on a public repository). Approving the plan approves those pushes, the push of this plan, and a
  comment on #37 that links it (public, since the repository is).
- **D5 `CCACHE_MAXSIZE: 500M`.** Each run saves its own snapshot (flywheel's design), so the size of one is the
  repository's 10 GB divided by it. Step 2 prints the real size; the cap is kept or tuned from that.

## Steps

Each of Steps 1 to 3 is one commit and is measured by CI against the baseline above; a step that does not pay for itself is
reverted, not kept on the theory that it should have helped. Every checkpoint passes a full local build and test
(`cmake --preset release-macos-arm64`, `cmake --build --preset release-macos-arm64`, `ctest --preset
release-macos-arm64`) before it is committed.

### Step 0 — Approval

Rob approves. Commit this plan (`docs(plan): plan 030 approved as written`), push, and comment on #37 with its path.
*Done when:* #37 links the plan and the plan links #37.

### Step 1 — Build in parallel, and the manual trigger

`workflow_dispatch:`, and `-j $(nproc)` with the `runner cores` echo, so the core count is read, not assumed.
*Tests:* none new. A workflow file has no offline test surface; the CI log is the test. Checkpoint: the file parses
(`ruby -ryaml`), and the project, unchanged, still builds and passes.
*Done when:* the run prints `runner cores: N` and Build is shorter than 408 s; both go in the Outcome. A race that the
serial build hid is a bug to fix or raise, not a reason to drop the flag quietly.

### Step 2 — ccache

apt `ccache`; the `toolchain` output on the Tool versions step; the ccache cache step; `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache`;
`ccache -z` and `ccache -s` around the build; `CCACHE_MAXSIZE`.
*Tests:* none new, as in Step 1.
*Done when:* run A (the push, cold) and run B (`gh workflow run cmake-single-platform.yml`, same tree) are green, B's
`ccache -s` shows all 125 compiles as hits (a miss is explained), and B's Build step is a fraction of A's. A's snapshot
size and save time are recorded.

### Step 3 — Cache the fetched dependencies

In this order, red then green:

1. `tests/ci_checks/check_cache_key.cmake` and `test_ci_cache_key` in `tests/CMakeLists.txt`, registered at the top level
   like the other `cmake -P` checks. It fails against the workflow as Step 2 leaves it (no `deps-` key).
2. The `_deps` cache step and the `FETCHCONTENT_FULLY_DISCONNECTED` flag. The test passes.

*Test (new) `test_ci_cache_key`:* `cmake -DSOURCE_DIR=<repo> -P tests/ci_checks/check_cache_key.cmake`. It finds the
`deps-` key in the workflow and its `hashFiles(...)` list, and requires that every `CMakeLists.txt`, `*.cmake` and
`*.cmake.in` outside `build*/`, `cmake-build-*/` and `.git/` that contains `FetchContent_Declare(` is in the list, and that
every listed path exists (`hashFiles` of a missing file is an empty string, not an error). It fails if it finds no key or
no declaring file, so a moved key or a wrong path cannot pass by checking nothing. The script carries its own
`cmake_minimum_required(VERSION 4.0)`, which `test_cmake_floor` holds.
*Hand-made changes*, in a `git archive` copy, each of which must fail naming the culprit: remove `tests/CMakeLists.txt`
from the list; add a `FetchContent_Declare` to `src/CMakeLists.txt`; misspell a listed path; rename the key. The results go
in the Outcome. CLAUDE.md's count of `cmake -P` checks and tests (seven, 503 in ctest) becomes eight, 504, and the table
gets the new row.
*Done when:* run C (the push; `deps` miss) is green and its `Cache saved` size is tens of MB, not hundreds. Run D
(dispatch; hit) has Configure under 5 s, no `Cloning into`, no `Building CXX object _deps/` line, and ccache hits on all 75
project compiles. If D rebuilds the dependencies' objects, the cache path shrinks to the `*-src` directories and stays,
since ccache covers the objects.

### Step 4 — Words, results, close

CLAUDE.md gets a CI subsection under "Build system": what is cached, how each key is formed, the fixed workspace path, and
"a file that gains a `FetchContent_Declare` goes in the `deps-` key; `test_ci_cache_key` fails until it does". The Outcome
table gets the baseline, Step 1, A, B, C, D, and the first real commit after them (100% hits is the floor, not the typical
run: a commit that edits a widely included header recompiles its dependents). Comment the results on #37. The last commit
says `Closes #37`.

## Designs I rejected

- **Flywheel's `_deps` recipe as written** — measured above: seven clones with or without the cache, and a key that misses
  three of the seven pins.
- **Caching the FetchContent stamps** (`CMakeFiles/fc-stamp`, `fc-tmp`) — works (0 clones, 1.2 s), but ties the workflow to
  FetchContent's internal layout, which is the thing that made flywheel's recipe stop working.
- **`GIT_SHALLOW TRUE` on the seven declarations** — cuts clones and cache size at the source with no CI logic, but it
  edits what every consumer of cryptocogs through FetchContent sees, and it is not caching. The fallback if the `.git`
  exclusion below does not take.
- **`hendrikmuhs/ccache-action`** — flywheel's plan names it, but its workflows install ccache from apt and use
  `actions/cache` directly. Same here: one third-party action fewer.
- **Split restore and save** (`actions/cache/restore` and `/save`, as flywheel's `sanitizers.yml`), to save caches after a
  red run — not needed. The next run restores the last green snapshot and recompiles only what the red commit changed.

## Not in this plan

`paths:` filters and `concurrency` with `cancel-in-progress` (flywheel `ci_path_filter_debounce_plan.md`); `timeout-minutes`
(flywheel `mgmt_server_stop_hang_ci_guard_plan.md`); a sanitizer workflow; a macOS leg. One lead for flywheel, not verified:
none of its last six runs hit its `deps-` cache (keys churn), and its floor is also CMake 4.0, so by the table above a hit
may not skip its clones either. No issue is opened for it.

## Self-review — risks and assumptions

- **A stale `_deps` is worse than none.** With `FETCHCONTENT_FULLY_DISCONNECTED=ON`, a pin bump that the key misses builds
  the old version without a word. The key carries the preset, the compiler and CMake versions and a hash of every file
  that pins a dependency; `test_ci_cache_key` holds the last. It sees only the literal `FetchContent_Declare(`: a pin added
  another way (`ExternalProject_Add`, or a wrapper macro whose callers hold the tags) is invisible to it, and Step 4's
  CLAUDE.md paragraph says so.
- **The `!.../_deps/*-src/.git` exclusion is untested until Step 3's first save.** I tested a tree without `.git`, not the
  action's glob. Run C's `Cache saved` size says: tens of MB means it took. If not, the fallback is `GIT_SHALLOW TRUE`.
- **A restore is all or nothing**, so no half-populated `_deps`. A restored `_deps` with a missing `*-src` would fail the
  configure loudly under `FULLY_DISCONNECTED`, not silently.
- **Restored `*-build` directories plus a fresh configure** are assumed to be reused by make (flywheel saw that with
  FetchContent's older layout; I have not measured it under this one). Step 3's Done-when reads it from run D's log.
- **Fixed workspace path.** Hosted runners use `/home/runner/work/cryptocogs/cryptocogs`, and both caches (ccache hashes
  paths too) assume it. A container or self-hosted runner breaks that; a comment in the workflow says so.
- **Cold runs still happen:** after seven idle days (entries are evicted), after a runner image that changes GCC, and once
  per CMake of the alternation for `_deps`. A cold run costs about what Step 1 leaves.
- **Parallel builds** can expose a missing target dependency that a serial build hides. Local builds already use
  `--parallel 8` and pass, so I expect none; the first parallel run is read, not glanced at.
- **I read the unit-test rule as covering code.** The workflow is configuration with no offline surface, so Steps 1 and 2 add
  no test and Step 3 adds the check for the one invariant that can be tested offline. Say if you want something else.
- **Measurement is the "done" condition** and it is noisy (above): counters and step durations, not a single job total.

## Outcome

| Stage | Run | Job | Configure | Build | Test |
|---|---|---:|---:|---:|---:|
| Baseline, `e9ad24f` | 41852041317 | 457 s | 25 s | 408 s | 9 s |
| Step 1, `-j $(nproc)`, `464768b` | 41875534338 | 299 s | 50 s | **221 s** | 11 s |
| Step 2, ccache, run A: cold, `772235a` | 41877531957 | 327 s | 41 s | 258 s | 14 s |
| Step 2, ccache, run B: dispatch, same tree | 41878860746 | **66.5 s** | 25 s | **11 s** | 10 s |

The first three rows are read from the API's step timings. The two ccache runs are read from their job logs (first to last
line, and the gaps between step headers), because GitHub's displayed status of both stayed `queued` long after their logs
ended, and still does: an Actions incident that day (16:05 to 17:01 UTC) had warned of "delayed status updates for their jobs".
Run A was never waiting for a runner: its log starts 3 s after the run was created, and its cache entry was saved 5.5 minutes
later.

**Step 1 — kept.** The run printed `runner cores: 4`, and Build went from 408 s to 221 s: 46% less, 1.85x on 4 cores. That is
below the fastest of the six serial runs measured before (314 s), so the noise (314 to 586 s) does not explain it. The run
was green, with ctest at 503 of 503 and no compiler warning in its log, so the parallel build exposed no missing dependency. Configure
took 50 s against 24 to 35 s in those six runs: the step did not change, and it is the seven clones.

**Step 2 — kept.** Run A, cold, missed 124 of the 125 compiles (the 125th was identical to another and hit). Its Build took
258 s, 17% more than Step 1's 221 s, which the noise allows and the cost of hashing and storing explains; it saved a snapshot
of 12,368,582 bytes (11.79 MiB) in under a second. Run B, a dispatch on the same commit, restored that snapshot by the
primary key and hit **125 of 125**, all direct: Build 11 s, and the job 66.5 s against 457 s at the baseline, 85% less.
ccache's directory is `/home/runner/.cache/ccache`, as the cache path assumes, and `max_size` read 500.0 MB. **D5:** the
snapshot is 2.6% of the cap, so 500M stays as a ceiling and there is nothing to tune. What is left of B's 66.5 s is apt 17 s,
Configure 25 s (the seven clones) and the tests 10 s; Step 3 goes after the 25 s.
