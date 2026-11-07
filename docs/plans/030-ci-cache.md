# Plan 030 — CI: cache the build

**Status:** Done (2026-11-04). Rob approved the plan as written, so D1 to D5 stood at the defaults below, and approved pushing
its commits to `main` and linking it from #37. The Outcome has the numbers, and the plan marks where a step changed it
(*Amended*).
**Design source:** the common recipe for CI builds that use FetchContent: ccache, a parallel build and a cache of the `_deps`
tree. This plan applies it and restates only what differs. The main difference: caching `_deps` alone does not work here as
written (measured below).
**Closes:** [GitHub issue #37](https://github.com/tomlin256/cryptocogs/issues/37) — "Improve CI bill by using cache".

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

Build, attributed this way: `make` prints a progress line *before* its work, so the interval after a line belongs to that
line.

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
  arrives if the repository goes private (a private repository's minutes are an allowance, and it can run out).

### Caching `_deps` alone does not work here as written

The usual recipe caches `build/<preset>/_deps`, keyed on the root `CMakeLists.txt`. I tried that on this project (CMake 4.4.5 on
the Mac, a scratch tree outside the checkout: configure, delete everything except what a cache would restore, configure
again):

| Restored before the second configure | Clones | Configure |
|---|---:|---:|
| nothing | 7 | 26.2 s |
| `_deps` only — the usual recipe | **7** | **26.8 s** |
| `_deps` and `CMakeFiles/fc-stamp`, `fc-tmp` | 0 | 1.2 s |
| `_deps` only, with `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` | 0 | 1.2 s |
| the same, every `_deps/*-src/.git` deleted | 0 | 1.2 s; spdlog and `exchange_common` then build |

- **Why.** The floor is CMake 4.0, so CMP0168 is NEW and FetchContent populates inside the main configure. It keeps its
  "already done" stamps in `<tree>/CMakeFiles/fc-stamp` and `fc-tmp`, "so that they are removed by `cmake --fresh`"
  (`FetchContent.cmake:1670`, CMake 4.4.5), not in `_deps`.
- **Size.** `_deps` is 473 MB, 436 MB of it full-history `.git` (nlohmann_json's source directory alone is 343 MB).
  Compressed with `zstd -3`: 405 MB with the `.git` directories, **13 MB** without. Nothing reads `.git` on a hit.
- **Two files pin dependencies**, not one: `CMakeLists.txt` (nlohmann_json, toml++, ixwebsocket, googletest,
  googlebenchmark) and `tests/CMakeLists.txt` (backward-cpp, spdlog, CLI11). A key that hashes only the root restores a
  stale spdlog after a bump in the second.
- **Toolchains alternate.** Runs the same afternoon got CMake 4.4.4 and 4.4.5 from
  `ubuntu-latest`, so a key that carries the CMake version misses whenever a run lands on the other one.

## The change

Everything is in `.github/workflows/cmake-single-platform.yml`, apart from Step 3's check. The finished shape of the
cache-related parts (the real file carries comments saying why):

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
    path: ${{github.workspace}}/build/${{env.PRESET}}/_deps
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

- name: Test
  run: ctest --preset ${{env.PRESET}}

# Amended at Step 3 (see the Outcome): the last step, so that the entry the cache's post action saves has no .git
- name: Drop the dependencies' .git directories
  if: steps.deps.outputs.cache-hit != 'true'
  run: rm -rf ${{github.workspace}}/build/${{env.PRESET}}/_deps/*-src/.git
```

- *(Amended at Step 3.)* The `.git` directories are deleted by the last step. The first design left them out with a `!`
  pattern in `path` (`!…/_deps/*-src/.git`), and run C saved 407 MB with it: the action globs `path` with
  `implicitDescendants: false` and gives tar the result as a `--files-from` list, so `…/_deps` is one directory entry and
  tar archives everything under it, whatever the pattern says. A job's post actions run after its last step, so deleting
  them there is what shrinks the entry.
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
  `timeout-minutes`: they cut runs, not compiles. Say if you want any folded in. `-j` is not a cache either, but 88% of this
  job is compiling.
- **D2 The `_deps` design.** `FETCHCONTENT_FULLY_DISCONNECTED=ON` on a hit, `.git` left out, key over both pin files — not
  a bare `_deps` cache, and not stamp-caching (see "Designs I rejected").
- **D3 One new test**, `test_ci_cache_key` (Step 3). It fails when a file that pins a dependency is missing from the key,
  which is the one way this plan can go silently wrong.
- **D4 Pushes.** Each of Steps 1 to 3 is one commit on `main`, pushed so its run can be measured; Steps 2 and 3 also
  dispatch one run (free on a public repository). Approving the plan approves those pushes, the push of this plan, and a
  comment on #37 that links it (public, since the repository is).
- **D5 `CCACHE_MAXSIZE: 500M`.** Each run saves its own snapshot, so the size of one is the
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
2. The `_deps` cache step and the `FETCHCONTENT_FULLY_DISCONNECTED` flag. The test passes. *(Amended: and a last step that
   deletes the `.git` directories, which the first push of this step showed to be needed. That is a second commit of the
   step, with its own runs, C2 and D2.)*

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

- **The bare `_deps` recipe as written** — measured above: seven clones with or without the cache, and a key that misses
  three of the seven pins.
- **Caching the FetchContent stamps** (`CMakeFiles/fc-stamp`, `fc-tmp`) — works (0 clones, 1.2 s), but ties the workflow to
  FetchContent's internal layout, which is the thing that makes the bare `_deps` recipe fail here.
- **`GIT_SHALLOW TRUE` on the seven declarations** — cuts clones and cache size at the source with no CI logic, but it
  edits what every consumer of cryptocogs through FetchContent sees, and it is not caching. Still the fallback, if deleting
  the `.git` directories at the end ever stops being enough.
- **A `!` pattern in the cache `path`**, to leave the `.git` directories out — this plan's first design, and it does not
  work: tar archives whatever is under a listed directory (see "The change", and the Outcome).
- **`hendrikmuhs/ccache-action`** — installing ccache from apt and using `actions/cache` directly is one third-party action
  fewer.
- **Split restore and save** (`actions/cache/restore` and `/save`), to save caches after a
  red run — not needed. The next run restores the last green snapshot and recompiles only what the red commit changed.

## Not in this plan

`paths:` filters, which became [plan 031](031-ci-skip-docs-only.md) (#40) after Rob asked for them while this plan ran;
`concurrency` with `cancel-in-progress`; `timeout-minutes`; a sanitizer workflow; a macOS leg; the slow and variable apt
step (#41).

## Self-review — risks and assumptions

- **A stale `_deps` is worse than none.** With `FETCHCONTENT_FULLY_DISCONNECTED=ON`, a pin bump that the key misses builds
  the old version without a word. The key carries the preset, the compiler and CMake versions and a hash of every file
  that pins a dependency; `test_ci_cache_key` holds the last. It sees only the literal `FetchContent_Declare(`: a pin added
  another way (`ExternalProject_Add`, or a wrapper macro whose callers hold the tags) is invisible to it, and Step 4's
  CLAUDE.md paragraph says so.
- **The `!.../_deps/*-src/.git` exclusion was untested until Step 3's first save, and failed it.** I had tested a tree
  without `.git`, not the action's glob. Run C's `Cache saved` size said so (407 MB), and the fix is the last step of the
  job. Its own risk: it deletes from the tree the post action is about to archive, so the saved size (run C2's) is the proof.
- **A restore is all or nothing**, so no half-populated `_deps`. A restored `_deps` with a missing `*-src` would fail the
  configure loudly under `FULLY_DISCONNECTED`, not silently.
- **Restored `*-build` directories plus a fresh configure** are assumed to be reused by make (I have not
  measured it under this layout). Step 3's Done-when reads it from run D's log.
- **The `deps-` key hashes whole files**, and `tests/CMakeLists.txt` also registers every test, so each new test misses the
  dependency cache once (run E: 32.5 s of Configure). Hashing only the `GIT_REPOSITORY` and `GIT_TAG` lines would avoid it,
  at the price of moving the key out of `hashFiles` and into a step; not done, because a miss costs seconds and a wrong key
  costs a wrong build.
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
| Step 3, first push, run C: `deps` miss, `b3af7d9` | 41879448286 | 139 s | 27 s | 11 s | 9 s |
| Step 3 amended, run C2: `deps` miss, CMake 4.4.5, `8b629bd` | 41880931962 | 80.6 s | 26.7 s | 11.3 s | 8.6 s |
| D2a, dispatch: CMake 4.4.4, `deps` miss | 41881271159 | 57.8 s | 27.1 s | 3.6 s | 8.8 s |
| D2b, dispatch: CMake 4.4.4, `deps` miss | 41881275977 | 81.8 s | 35.0 s | 11.8 s | 12.1 s |
| **D3, dispatch: CMake 4.4.4, `deps` hit** | 41881778529 | **35.0 s** | **1.7 s** | 3.9 s | 12.1 s |
| A real commit, plan 031 Step 1, which edits `tests/CMakeLists.txt`: `deps` miss, `8860993` | 41883029251 | 69.1 s | 32.5 s | 4.7 s | 12.9 s |

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

**Step 3, first push (run C) — the mechanism held, the size did not.** Run C landed on CMake 4.4.5, where A and B had run
4.4.4, so both keys missed. ccache's second restore-key took A's 4.4.4 snapshot and **hit 125 of 125**, which proves that
fallback; the run then saved a snapshot of its own (12,353,921 bytes). Configure took 26.7 s (the seven clones, 7 `Cloning
into` lines), Build 10.6 s, and ctest passed 504 of 504, the new check included. The job took 139 s only because the apt step
took 82.6 s, a mirror at 85.5 kB/s (raised as #41). The run saved the `deps-` entry as **427,056,792 bytes (407 MB), not tens
of MB**: the `!` pattern in `path` had not left out the `.git` directories, because the action gives tar the `_deps`
directory itself (`resolvePaths` in `@actions/cache` globs with `implicitDescendants: false`, and `tar` is run with
`--files-from` and without `--no-recursion`). That fails Step 3's Done-when. A mock tree showed that deleting the
`*-src/.git` directories in a last step does what the pattern was meant to, and that is the amendment above; the runs that
prove it are C2 and D2, below.

**Step 3 — kept, after one amendment.** Run C2, with the `.git` directories deleted by the last step, saved the `deps-`
entry at **14,890,315 bytes (14.2 MiB)** against 427,056,792, and ccache restored by the key of its own toolchain and hit
125 of 125. The next two dispatches, D2a and D2b, both landed on CMake 4.4.4 when only the 4.4.5 entry existed: both missed,
and raced to save the same key, so D2b logged `Unable to reserve cache with key …, another job may be creating this cache`,
a warning and not a failure. D2a's save (14,661,489 bytes) made the 4.4.4 entry. That is the alternation costing one cold run
per CMake, as the self-review said it would.

**D3**, a dispatch on 4.4.4 with both entries present, was the hit. The cache restored 14 MB in 1.7 s. The Configure command
carried `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` and took **1.7 s with no `Cloning into` line**, against 25 s. **No dependency
was compiled**: 75 compile lines, all the project's, against 125. ccache hit 75 of 75, ctest passed 504 of 504, and both
caches ended with `Cache hit occurred on the primary key, not saving cache`. The job took **35.0 s**, 92% less than the
baseline's 457 s: apt 10.9 s, Configure 1.7 s, Build 3.9 s, Test 12.1 s. Step 3's Done-when is met, and so is the question
the self-review left open: make reuses the restored `*-build` directories, since no `_deps/` object was built.

D3's 35 s is the floor for an unchanged tree. A commit that edits a widely included
header recompiles its dependents, and one that edits `CMakeLists.txt` or `tests/CMakeLists.txt` misses the `deps-` key once
(about 25 s of clones), because the key hashes whole files and `tests/CMakeLists.txt` also registers every test.

**The first real commit after them (run E).** Plan 031's Step 1 added a test, so it edited `tests/CMakeLists.txt`, which is in the
`deps-` key: the key missed once, Configure took 32.5 s with its seven clones, and the dependencies were compiled through
ccache. ccache itself hit all 125 compiles, because the edit changed no compile, and Build took 4.7 s. The job took 69.1 s and
ctest passed 505 of 505, the two new checks included. A commit that edits a widely included header would recompile its
dependents; none has been measured, and the cost would be those translation units' compile time, with the rest cached.

**Overall.** An unchanged tree takes **35 s** against 457 s at the baseline (92% less), and a commit that edits one of the two
dependency-pinning files about 69 s (85% less). A ccache snapshot is 12 MB and a dependency entry 14 MB; after these runs the
repository holds 8 entries, 101 MiB, of its 10 GB. Every run saves a ccache snapshot (the commit is in its key), so that grows
by about 12 MB a run until entries unused for seven days age out or the limit evicts the least recently used. The repository
is public, so no minutes were billed before or after: what changed is the wait for a result.
