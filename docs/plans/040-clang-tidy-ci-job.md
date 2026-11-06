# Plan 040 — A clang-tidy job in CI

**Status:** Approved as written (2026-11-06), so D1 to D5 stand at the defaults below. Step 0 is the commit that adds this file.

Closes [#49](https://github.com/tomlin256/cryptocogs/issues/49) — "CI: add a clang-tidy job now that the gate is green (plan 035 left it out until then)".

**Follows:** [plan 035](035-clang-tidy-opt-in-test-step.md) (the gate; its D4 left CI out until the findings were gone),
[plan 037](037-clear-clang-tidy-findings.md) (cleared them), [plan 031](031-ci-skip-docs-only.md) (the `paths-ignore` lists) and
[plan 032](032-sanitizer-option-and-presets.md) (a workflow of its own, and no caches).
**Design source:** the `clang-tidy` job of flywheel-dag's `ci.yml` (flywheel-dag#60), for the install recipe. Its `ubuntu-latest` leg
is green today (run 42239573531, 2026-11-06). This plan differs from it in the points under *Decisions to confirm*.

**Not here:** a macOS leg (D5); a dependency cache (D1; [#54](https://github.com/tomlin256/cryptocogs/issues/54) is the cheaper way to
the same end, raised while planning this); a nightly run (D2); wider checks (plan 035, D3); pinning clang-tidy to a point release; a
rule that makes the job required (`main` has no branch protection); any change under `include/` or `src/`, so no tag and no version.

## Problem

The gate is green: 88 of 88 in a default tree and 95 of 95 with `CRYPTOCOGS_BUILD_BENCHMARKS=ON` (the seven `bench_*.cpp` units), on the
Mac (clang-tidy 23.1.4); plan 037 showed it green in the `ubuntu-dev` container too (23.1.6). Nothing runs it but a person turning
`-DCRYPTOCOGS_CLANG_TIDY` on by hand, as plans 038 and 039 each did in their checkpoints. So a new finding lands unseen, and is found by
whoever next runs the gate.

### What I checked (2026-11-06)

- **The recipe works on the runner.** flywheel-dag's `clang-tidy (ubuntu-latest, relwithdebinfo-linux-x86_64)` job, run 42239573531:
  install of clang-tidy 23 from apt.llvm.org 10 s, configure 29 s, `ctest -R '^clang_tidy_' -j4` 65 s, job 1 min 49 s. So apt.llvm.org
  serves the runner's Ubuntu codename today, and clang-tidy 23 runs over a GCC-built compile database on that image. It does not show
  that it parses *this* project's headers (curl, OpenSSL, ixwebsocket, gtest, nlohmann/json, toml++): the first run does.
- **What the install does and does not need.** `find_program` finds `clang-tidy-23` by name (apt puts it on `PATH`), and
  `clang_tidy_version` passes on `LLVM version 23.`; both are what the container already does. libcurl's development files are not on
  the image (#41: 27 packages, 6.4 MB, the step's variance is the mirror); `libssl-dev` and `zlib1g-dev` are.
- **What the main workflow costs, for scale.** Run 42249330660 (the last push): job 75 s, apt 10 s, configure 25 s, build 15 s, test
  15 s. That configure was a **dependency-cache miss** (the commit had edited `tests/CMakeLists.txt`, so it saved a new 15 MB entry) and
  its 25 s is the seven clones. Plan 030 measured a hit at 1.7 s.
- **What the cache would save the lint job: about 23 s of roughly 2.5 minutes.** And what it would cost, below (D1).
- **Shallow clones** (raised as #54, not done here): the seven pins are 495 MB cloned in full and 51 MB with `--depth 1`; nlohmann_json
  alone 14.9 s against 0.9 s. A cold configure would drop to a few seconds without any cache.
- **The checks that read the workflow.** `test_ci_paths` reads `cmake-single-platform.yml` only: it holds its `paths-ignore` lists equal
  under `push` and `pull_request` and away from every file a check or the build reads. A second workflow with lists of its own is not
  held by it, so it could drift unseen. `test_ci_cache_key` reads the one `deps-` key line of the same file; the lint workflow (D1) has
  no cache, so it has nothing for that test to hold.
- **What I did not check:** that the benchmark option configures in a Linux Debug tree under CI (no workflow has ever turned it on).
  It is a configure of a fetched dependency (google benchmark v1.9.5) that the Mac does every day; the first run is the evidence.

## The change

### 1. `.github/workflows/clang-tidy.yml`

A workflow of its own (D2, like `sanitizers.yml`, so a red lint run cannot mask whether the normal gate is green). The real file carries
the comments; this is its shape:

```yaml
name: clang-tidy

on:
  push:
    branches: [ "main" ]
    paths-ignore:            # the same two lists as cmake-single-platform.yml; test_ci_paths holds them equal
      - 'docs/**'
      - 'CLAUDE.md'
  pull_request:
    branches: [ "main" ]
    paths-ignore:
      - 'docs/**'
      - 'CLAUDE.md'
  workflow_dispatch:

permissions:
  contents: read

env:
  LLVM_MAJOR: "23"           # written once; test_ci_clang_tidy holds it to CRYPTOCOGS_CLANG_TIDY_MAJOR
  PRESET: debug-linux-x86_64

jobs:
  clang-tidy:
    runs-on: ubuntu-latest
    timeout-minutes: 15
    steps:
    - uses: actions/checkout@v6

    - name: Install dependencies
      run: sudo apt-get update && sudo apt-get install -y libcurl4-openssl-dev libssl-dev zlib1g-dev

    # apt.llvm.org's key is checked against the fingerprint LLVM publishes before apt is told to trust it
    # (the same fingerprint as flywheel-dag's job and docker-ubuntu's Dockerfile). No fallback to another major.
    - name: Install clang-tidy
      run: |
        . /etc/os-release
        curl -fsSL -o "$RUNNER_TEMP/llvm.asc" https://apt.llvm.org/llvm-snapshot.gpg.key
        found=$(gpg --show-keys --with-colons "$RUNNER_TEMP/llvm.asc" | awk -F: '/^fpr/ { print $10; exit }')
        test "$found" = 6084F3CF814B57C1CF12EFD515CF4D18AF4F7421 || { echo "apt.llvm.org's key is $found" >&2; exit 1; }
        sudo install -D -m 0644 "$RUNNER_TEMP/llvm.asc" /etc/apt/keyrings/apt.llvm.org.asc
        echo "deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] https://apt.llvm.org/$VERSION_CODENAME/ llvm-toolchain-$VERSION_CODENAME-$LLVM_MAJOR main" | sudo tee /etc/apt/sources.list.d/llvm.list
        sudo apt-get update
        sudo apt-get install -y --no-install-recommends clang-tidy-$LLVM_MAJOR

    - name: Tool versions
      run: |
        cmake --version | head -1
        c++ --version | head -1
        clang-tidy-$LLVM_MAJOR --version | head -2

    # Nothing is built: clang-tidy reads <tree>/compile_commands.json. The benchmarks are on so that their seven units are held too.
    - name: Configure CMake
      run: cmake --preset ${{env.PRESET}} -DCRYPTOCOGS_BUILD_BENCHMARKS=ON -DCRYPTOCOGS_CLANG_TIDY=clang-tidy-$LLVM_MAJOR

    # The test preset carries --output-on-failure, so a red unit prints clang-tidy's findings, and fails a run that selects no test.
    - name: clang-tidy
      run: |
        ctest --preset ${{env.PRESET}} -N -R '^clang_tidy_' | tail -1
        ctest --preset ${{env.PRESET}} -R '^clang_tidy_' -j $(nproc)
```

No cache and no `concurrency` group: every push keeps its own result, so a red one names its commit.

### 2. `test_ci_clang_tidy` (new)

`tests/ci_checks/check_clang_tidy_workflow.cmake`, run as `cmake -DSOURCE_DIR=<repo> -P …` and registered at the top level beside
`test_ci_paths`, so it runs in every tree. It reads the workflow, `cmake/cryptocogs_clang_tidy.cmake` and `CMakePresets.json`, and fails
(naming what is wrong) unless:

| Rule | Why it is a rule |
|---|---|
| `LLVM_MAJOR` is set once and equals `CRYPTOCOGS_CLANG_TIDY_MAJOR` | A major moved in one place only otherwise shows after the push, as a red `clang_tidy_version`. |
| the install, the apt source and the configure name it as `$LLVM_MAJOR` and no line has a literal `clang-tidy-<digits>` or `llvm-toolchain-…-<digits>` | One place to move. |
| the configure line has `-DCRYPTOCOGS_CLANG_TIDY=` and `-DCRYPTOCOGS_BUILD_BENCHMARKS=ON` | Dropping the second lowers the gate from 95 units to 88 and everything stays green. |
| the test line is `ctest … -R '^clang_tidy_'` | The prefix is the gate's. |
| nothing in the file says `continue-on-error` or ends a line in `\|\| true` | A lint step that cannot fail gates nothing. |
| `PRESET` names a configure preset and a test preset in `CMakePresets.json` | A misspelt preset is otherwise a red run after the push. |

It reads each file or fails; it never passes by reading nothing.

### 3. `test_ci_paths` reads every workflow (modified)

`check_paths.cmake` takes every `.github/workflows/*.yml` that has a `push` or `pull_request` trigger (`sanitizers.yml` has neither and
is skipped, and said so in the status line). Each of them must carry a non-empty `paths-ignore` under **both** triggers, as the main one
does today, and all the lists must be equal, across triggers and across workflows. The entries are then held to the checkout as now. A
workflow whose `on:` is not in the block layout the parser reads fails and says so, instead of being skipped. Finding no workflow with
a `push` or `pull_request` trigger at all is an error, so the check cannot pass by reading nothing.

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 No dependency cache. This departs from the issue**, which says to reuse the main workflow's. The saving is about 23 s of a job of
  roughly 2.5 minutes. Reusing the entry would need:
  - the tree `release-linux-x86_64`, since the entry's path and key are that tree's, so the gate would run over the Release compile
    commands and not the Debug ones it was proven on (the Mac, the container, and `.clangd`);
  - the benchmarks off: the entry holds no google benchmark source, and `FETCHCONTENT_FULLY_DISCONNECTED=ON` (which a hit needs) would
    refuse to fetch it. The gate would be 88 units and the seven benchmark files unheld;
  - a copy of the key line and of the toolchain-id step, with `test_ci_cache_key` taught to hold two files (it wants exactly one `deps-`
    line today), and restore-only so the lint job never writes the entry.

  `sanitizers.yml` has no caches by design (plan 032, D7). #54 (shallow clones) would shorten the cold configure for both. *Alternative:*
  the cache, with the four things above; it is a self-contained step to add later if the measured cold configure disappoints.
- **D2 Triggers: every push and pull request to `main` that changes more than documentation (the same lists as the main workflow), and
  on demand. Not nightly.** A nightly finds a finding a day after it landed, and the job is two or three minutes, so the answer can come
  with the push. The price of no nightly is that toolchain drift (a new clang-tidy 23.x, an image roll) shows on the next unrelated push;
  the log prints the versions. *Alternative:* add `schedule` (one line; `test_ci_paths` reads only the push and pull-request lists).
  It lands in two commits, as plan 036 landed its flags: the workflow first with `workflow_dispatch` only, read, and the push and
  pull-request triggers second, so the toolchain's opinion is read before it can put a red mark on `main`.
- **D3 The tree is `debug-linux-x86_64` and the benchmarks are on: 95 tests.** Debug is the kind the gate was proven on and the one the
  editor uses (`.clangd`). The only difference to Release for clang-tidy is `-DNDEBUG`, and no project file uses `assert` today, but a
  gate should look at the code with its assertions in.
- **D4 The two checks of *The change*, 2 and 3.** *Alternative:* none; `clang_tidy_version` goes red after a push when the major
  drifts, and `test_ci_paths` stays on one workflow.
- **D5 Linux only.** No macOS leg until #29 adds one. The Mac half of the gate (the SDK taken from `xcrun`) is exercised by hand only until
  then; flywheel-dag's Mac leg needs `brew install llvm` (a bottle of about 460 MB, 26 s there).

## Steps

Commits go straight to `main`, one per step, pushed once the checkpoint is green. No commit message carries a closing keyword (a
`closes #49` in a message closes the issue on push, before the result comment): #49 is closed by hand in Step 4. Counts are expected ones
and are replaced by measured ones in the Outcome. Every checkpoint is a full build and a full ctest of `debug-macos-arm64` (nothing
under `src/` changes, so the build is a no-op, but it is run), plus the main workflow's run on the commit.

### Step 0 — Approval

*Tests:* none (documentation). You approve or edit. Commit this plan (`docs(plan): plan 040 approved as written`), push (documentation
only, so CI starts no run), and comment on #49 with the plan's path. *Done when:* #49 links the plan, and the plan links #49 (it does).

### Step 1 — `test_ci_clang_tidy` and the workflow, dispatch only (test first), one commit

*Start from green.* `ctest --preset debug-macos-arm64` on a full build: expected 565 of 565 (gate off). *Done when* the baseline is
recorded; if anything is red I stop and tell you.

*Test (new):* `test_ci_clang_tidy` and its `add_test` in `tests/CMakeLists.txt`. *Red first:* the check with no workflow, expected red
("does not exist"). Then the workflow, with `on: workflow_dispatch` only, and green. The workflow is linted with actionlint (none is
installed; its Docker image, with the file mounted read-only).

*Hand-made changes* (a scratch copy made with `git archive`, not `rsync`, so no `build*` is copied), each through the real check:

| | Change | Expected |
|---|---|---|
| a | `LLVM_MAJOR: "22"` | red |
| b | the module's `CRYPTOCOGS_CLANG_TIDY_MAJOR` 23 → 24 | red |
| c | a literal `clang-tidy-23` in the install line | red |
| d | delete `-DCRYPTOCOGS_BUILD_BENCHMARKS=ON` | red |
| e | delete `-DCRYPTOCOGS_CLANG_TIDY=…` | red |
| f | `-R 'clang_tidy_'` (no `^`) | red |
| g | add `continue-on-error: true`; then end the ctest line in `\|\| true` | red, each |
| h | `PRESET: debug-linux-x86-64` | red |
| i | delete the workflow | red |
| j | the control, nothing changed | green |

*Checkpoint.* Full build; ctest of `debug-macos-arm64`: expected 566 of 566 (one new test). *Done when:* red then green as stated; a-i
red and j green; the checkpoint green; pushed; the main workflow's run of the commit green (`release-linux-x86_64`, GCC 15.2, `-Werror`,
566 of 566). The lint workflow does not run (no automatic trigger yet).

### Step 2 — Read the toolchain's opinion: dispatch

*Tests:* none new; the run is the test. `gh workflow run clang-tidy.yml --ref main`, then read the log. Record: the tool versions, the
step times (apt, install, configure, tests), the `Total Tests:` line (expected 95), the result. A second dispatch for the spread.
*Done when:* both runs are green with 95 of 95, or I have stopped and told you why. A red run is handled by the rule in `CLAUDE.md`: a
finding in the project's own code is fixed in a commit of its own if it is small; a parse error in a system header, or anything that is
not small, I bring to you before going on (and a clang-tidy 23.x false positive is the closed-list question there, which is yours).
An infrastructure failure (apt.llvm.org, a clone) is re-run once and recorded.

### Step 3 — The triggers, and `test_ci_paths` for every workflow (test first), one commit

*Test (modified):* `check_paths.cmake` as in *The change*, 3. First the check alone, against the real tree (still one filtered
workflow): expected green, with the status line changed. *Hand-made changes* (scratch copy; the stand-in is the workflow with each
change):

| | Change | Expected |
|---|---|---|
| a | the lint workflow with the real triggers and lists (the end state) | green, two workflows read, `sanitizers.yml` skipped |
| b | its `push` list without `CLAUDE.md` | red, the lists differ |
| c | its `pull_request` list deleted | red, names `clang-tidy.yml` and the trigger |
| d | `README.md` added to both of its lists | red, the lists differ, and README is read by `test_cmake_floor` |
| e | a `push` trigger and no `paths-ignore` | red |
| f | `on: [push, pull_request]` in the flow layout | red, says it needs the block layout |
| g | a third workflow with only `schedule` | green, skipped |
| h | the main workflow's `pull_request` list deleted | red, as today |
| i | no workflow files | red |

Then the triggers added to `clang-tidy.yml`, and the one-line comment above the main workflow's `paths-ignore` (it names the second
list). *Checkpoint.* Full build and ctest: expected 566 of 566. *Done when:* a-i as stated; pushed; the push's own lint run
(it is a push that changes more than documentation) green with 95 of 95, and the main run green.

### Step 4 — Docs, follow-ups, close

*Tests:* none new. `CLAUDE.md`: the *clang-tidy (opt-in)* section (the last bullet, "A CI job is #49", becomes a description of the
job); *CI* (a third workflow: when, what, the cost as measured, `gh workflow run clang-tidy.yml --ref main`, how to move the LLVM major:
`CRYPTOCOGS_CLANG_TIDY_MAJOR` and `LLVM_MAJOR`, which `test_ci_clang_tidy` holds together); *Running tests* (seventeen `cmake -P` checks,
566 in ctest, the Crypto.com-only 158, and the two rows: `test_ci_paths` now reads every workflow, and the new one). `README.md`: one
sentence that CI runs the gate. This file's *Outcome*. Turn the gate off again in the Mac tree if it was on. All numbers measured.
The README commit is not documentation-only (`test_cmake_floor` reads it), so it runs both workflows; the *Outcome* commit is, and
**must start no lint run**: that is the check of D2's ignore lists on the real thing (`gh run list --workflow clang-tidy.yml`).
Compare with flywheel-dag's job, read-only, and raise an issue there for what is better here (the single `LLVM_MAJOR`, the check that
holds it, the `paths-ignore` lists) — #54 too applies to it if it clones the same way. Comment on #29 that a Mac leg of the lint can
join when it lands. *Done when:* pushed; #49 closed by hand with a comment that lists the commits, the runs, the red-then-green record and
the counts.

## Designs I rejected

- **A second job in `cmake-single-platform.yml`.** It would share the cache and the toolchain step for free, and a red lint would turn
  the workflow's one status red. #49 asks for a workflow of its own for that reason.
- **The cache of the main workflow** (D1). Roughly 23 s, for the Release tree, no benchmarks and a second key to police.
- **A nightly instead of push** (D2). The finding would be found a day late. **A nightly as well:** a red mail at 03:41 for no commit; one
  line to add if drift bites.
- **`release-linux-x86_64`** (D3), only to share the cache.
- **A matrix of kinds** (debug and release). The two compile the same code for a front end that does not optimise.
- **Pinning the package version** (`clang-tidy-23=23.1.6-…`). The project holds one major (plan 035, D2), not one build; the Mac and the
  container already differ (23.1.4, 23.1.6).
- **LLVM's `llvm.sh` installer.** It installs more than clang-tidy and cannot check the key against a fingerprint first.
- **A third-party action to install LLVM** (`KyleMayes/install-llvm-action`). It adds a dependency that plan 030 avoided for ccache, for a
  recipe that is eight lines.
- **`Acquire::Retries` or a retry loop** on the apt steps. No failure has been seen, only slowness (#41). Added when one is.
- **Failing the job on a count** (`Total Tests: 95`). It would need an edit with every new source file; `clang_tidy_units` already holds
  the registered units to the compile database.
- **`concurrency` with `cancel-in-progress`.** A burst of pushes would leave the earlier commits with no lint result.

## Self-review — risks and assumptions

- **The first run may be red for a reason that is the toolchain's.** Hence Step 2: a dispatch-only workflow cannot put a red mark on
  `main`. flywheel-dag's green leg is the evidence that the pairing (GCC 15.2 headers, clang-tidy 23) works; this project's own headers are
  the unknown.
- **The job becomes the slowest check.** About 2.5 to 3.5 minutes against the main workflow's 75 s, so a code push waits about a minute and
  a half longer for the last green; a documentation push, none. Estimated, not measured: Step 2 measures it.
- **apt.llvm.org can be down, slow, or without the runner's codename after an image roll.** The job is then red for no code reason, with a
  clear failure at `apt-get update` or the fingerprint; there is no fallback to another major, on purpose (flywheel-dag says the same).
  A re-run answers a blip.
- **LLVM could rotate its signing key.** The fingerprint step fails loudly; the fix is one line here and one in docker-ubuntu's
  `Dockerfile`, which pins the same value.
- **Drift shows on an unrelated push.** A 23.x point release, a new libstdc++ in the image. Accepted (D2); the versions are in the log.
- **Memory.** Four clang-tidy processes at about 0.5 GB on a 16 GB runner.
- **The `paths-ignore` lists are now written twice.** `test_ci_paths` holds them (Step 3). Written down so nobody removes the check.
- **Benchmarks on, in CI, for the first time.** One more clone (google benchmark) and its configure; a failure to clone is a red run to
  re-run. The benchmark executables are not built, so nothing of theirs is compiled.
- **The 95 is the Mac's number.** Linux has the same targets; Step 2 reads the real one from `ctest -N`.
- **`test_ci_clang_tidy` reads three files by name**, and `test_ci_paths` already holds that no `paths-ignore` entry covers a file a check
  reads; none of the three is under `docs/` or is `CLAUDE.md`. Editing `tests/CMakeLists.txt` misses CI's dependency cache once.
- **YAML I cannot run locally.** Only actionlint and the real runner see the workflow; Step 1 lints it, Step 2 runs it.
- **Behaviour change:** a new workflow, a stricter `test_ci_paths`, one new test. No library file, no public header.
- **I assume** you want the benchmarks held too (95, not 88), the Debug tree, and no cache, as above. Each is a one-line change in the
  file if you say otherwise.
