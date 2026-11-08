# CI: do not build on documentation-only changes

**Status:** Done (2026-11-04). Rob approved the plan as written, so D1 to D4 stood at the defaults below, and approved pushing
its commits to `main` and linking it from #40.
**Design source:** the usual `paths:` filter of a GitHub Actions workflow. This plan differs from it in one choice, D1.
**Closes:** [GitHub issue #40](https://github.com/tomlin256/cryptocogs/issues/40) — "Build will trigger on doc updates
pointlessly". The issue reads: *"You can see that in the recent CI runs for example
https://github.com/tomlin256/cryptocogs/actions/runs/41875368976"*.
**Follows:** [plan ci-cache](030-ci-cache.md), which left `paths:` filters out (its D1: "Say if you want any folded in") and
edits the same workflow file, in other hunks.

## Problem

The workflow runs on every push and pull request to `main`, whatever changed. The issue's example, run `41875368976`, is the
push of commit `1e4028c`, which added `docs/plans/030-ci-cache.md` and nothing else. It had used 467 s of runner time
(apt 156 s, configure 31 s, build 272 s) when it was cancelled, to build and test a project that the commit could not have
changed.

### What I measured (2026-11-04)

- **What is documentation.** 201 files are tracked. `docs/` holds 40, all markdown (the 37 plans and three other
  documents). Outside it the markdown is `README.md` and `CLAUDE.md`.
- **What a build or test reads.** No CMake file, no C++ test and no `cmake -P` check reads anything under `docs/`, or
  `CLAUDE.md`: the C++ tests touch only `temp_directory_path()`, and the checks get the checkout as `-DSOURCE_DIR` and read
  named files. One of those files is documentation: `test_cmake_floor` reads `README.md` and fails unless it says "CMake 4.0
  or later". So a README edit can turn a test red, and has to run CI.
- **GitHub's rule** (workflow syntax, `paths` and `paths-ignore`): with `paths-ignore` the workflow does not run when *every*
  changed path matches a pattern, and runs when any one does not. A push is judged on the two-dot diff between its old and new
  head, a pull request on the three-dot diff. `workflow_dispatch` is not filtered. `paths` and `paths-ignore` cannot be used
  together for one event.
- **`main` has no branch protection and no ruleset**, so a skipped run cannot leave a required check pending, which is what
  blocks a pull request when GitHub never starts the workflow.

## The change

In `.github/workflows/cmake-single-platform.yml`, the triggers (the real file carries the comment):

```yaml
on:
  push:
    branches: [ "main" ]
    # Documentation builds nothing and tests nothing, so a change to only these paths does not run CI (#40).
    # README.md is not listed on purpose: test_cmake_floor reads it. test_ci_paths holds this list, which
    # must be the same under pull_request.
    paths-ignore:
      - 'docs/**'
      - 'CLAUDE.md'
  pull_request:
    branches: [ "main" ]
    paths-ignore:
      - 'docs/**'
      - 'CLAUDE.md'
  workflow_dispatch:
```

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 A deny-list (`paths-ignore`), not an allow-list** (`paths:` with `'!**/*.md'` last). With a deny-list, anything
  not named runs CI, a new top-level directory included, so forgetting an entry costs one wasted run. With an allow-list,
  anything not named is skipped, so forgetting an entry costs a change nobody built. An allow-list suits a repository that
  pays for its minutes; here minutes are free. A trailing `'!**/*.md'` would also skip a README edit, which
  `test_cmake_floor` reads.
- **D2 The list is `docs/**` and `CLAUDE.md`.** README.md keeps running CI (above). Not listed, since the issue is about
  documentation: `LICENSE`, `.gitignore`, `.clangd`, `.vscode/**`. Nothing reads them either; say if you want them.
- **D3 One new test**, `test_ci_paths` (Step 1), as plan ci-cache has `test_ci_cache_key`. It fails when the list drifts onto
  something that a build or a check reads.
- **D4 Pushes.** The filter is one commit on `main`, pushed to be seen working. Approving the plan approves that push, the
  push of this plan, and a comment linking it from #40 (public, since the repository is).

## Steps

### Step 0 — Approval

Rob approves. Commit this plan (`docs(plan): plan 031 approved as written`), push, and comment on #40 with its path.
*Done when:* #40 links the plan and the plan links #40.

### Step 1 — The check, then the filter

In this order, red then green:

1. `tests/ci_checks/check_paths.cmake` and `test_ci_paths` in `tests/CMakeLists.txt`, registered at the top level like the
   other `cmake -P` checks. It fails against the workflow as it stands (no `paths-ignore`).
2. `paths-ignore` under `push` and `pull_request`. The test passes.

*Test (new) `test_ci_paths`:* `cmake -DSOURCE_DIR=<repo> -P tests/ci_checks/check_paths.cmake`. It reads the `paths-ignore`
list of each trigger in the workflow, and fails unless:

- both lists are present, non-empty and equal;
- each entry is a file name or `<dir>/**`; anything else is refused, so the check never guesses what a pattern matches;
- each entry matches at least one file in the checkout (a misspelt entry ignores nothing);
- no entry covers a file that a check reads, found as `${SOURCE_DIR}/<name>` in `tests/**/*.cmake` (today README.md,
  CMakeLists.txt, CMakePresets.json, `cmake/cryptocogs_platform.cmake` and the workflow), nor a `CMakeLists.txt`, `*.cmake`,
  `*.cmake.in`, `*.cpp`, `*.hpp` or `*.inl` file.

It fails if it finds no list or no file that a check reads, so a moved key cannot pass by reading nothing.
*Hand-made changes*, in a `git archive` copy, each of which must fail naming the culprit: add `README.md` to both lists; add
`'**/*.md'`; add `include/**`; misspell `CLAUDE.md`; drop `CLAUDE.md` from `pull_request` only. The results go in the Outcome.
CLAUDE.md's count of `cmake -P` checks and ctest tests moves up by one, and its table gets the new row.
*Checkpoint:* a full local build and test.
*Done when:* this push's own run starts and is green (the workflow file is not ignored), and the next documentation-only
push starts no run at all (`gh run list` has no entry for its sha) while the next push that changes anything else does.

### Step 2 — Words, results, close

CLAUDE.md gets a line in its CI notes: what is skipped, and why README.md is not. The Outcome records the observations of
Step 1. Comment the results on #40. The last commit says `Closes #40`.

## Designs I rejected

- **An allow-list** — D1.
- **`paths-ignore: ['**/*.md']`** — it includes README.md.
- **`[skip ci]` in commit messages** — every documentation commit would have to remember it, and this plan's own would not.
- **Dropping the README clause from `test_cmake_floor`**, so that README.md can be ignored — then the README can claim a floor
  the project does not have, which is what plan cmake4-cxx23 added the clause to stop.
- **A first job that diffs, with the others `needs:` it** — it still starts a runner to find out. The filter starts nothing.

## Not in this plan

`concurrency` with `cancel-in-progress` (a newer push cancels the run in flight) and `timeout-minutes`, as in plan ci-cache. The files of D2 that are not documentation.

## Self-review — risks and assumptions

- **The silent hole is an ignore entry that a build or a check reads.** Today there is none. `test_ci_paths` holds it for an
  entry added later (README.md, a CMake file, a C++ source).
- **A skipped run proves nothing about the documents it skipped.** CLAUDE.md states test counts; nothing reads CLAUDE.md, so
  nothing re-checked them when CI ran either.
- **A push is judged on its net diff.** A push of several commits that ends with only documentation changed starts no run,
  even if an intermediate commit touched code and a later one reverted it. There is nothing left to build.
- **A branch protection added later** that requires this check would leave a documentation-only pull request waiting for a
  run that GitHub never starts. Then the answer is the job-level skip, rejected above only for today.
- **It can only be seen working on GitHub.** The Done-when is read from `gh run list`, after a real documentation-only push.
- **I read the unit-test rule as covering code**, as in plan ci-cache: the workflow is configuration, and `test_ci_paths` holds the
  one invariant of it that can be tested offline. Say if you want something else.

## Outcome

**Step 1.** `test_ci_paths` was red against the workflow of plan ci-cache ("found no paths-ignore list under the push trigger") and
green once `paths-ignore` was added. It reports two entries, `docs/**` and `CLAUDE.md`, covering 42 files, none of which a
check reads or the build compiles, and 8 files that the checks read. Hand-made changes in a scratch copy of the working tree
each failed naming the culprit:

| Change | Result |
|---|---|
| `README.md` added to both lists | `README.md` covers `README.md`, which `tests/cmake_checks/check_cmake_floor.cmake` reads |
| `'**/*.md'` added to both | refused: neither a file name nor `<dir>/**` |
| `include/**` added to both | covers 44 CMake or C++ source files that the build reads, such as `include/exchange/binance/auth.hpp` |
| `CLAUDE.md` misspelt in both | `CLAUDE.mdd` matches no file in the checkout |
| `CLAUDE.md` dropped from `pull_request` only | the lists differ |
| `paths-ignore` removed from `pull_request` | found no `paths-ignore` list under the `pull_request` trigger |

Three extra cases behaved as they should: `.vscode/**` passed (so a dot directory is enumerated), `LICENSE` passed (nothing
reads it), and a `!docs/keep.md` entry was refused. My first attempt at four of these cases edited the workflow with a perl
one-liner whose quoting broke on `*` and `!`, so those copies were unchanged and the check passed them; the table is from the
second attempt, with an `awk` helper, and each of its cases was checked to have changed the file.

**The pushes.** Push `8860993` changed the workflow and the tests, so it was not ignored: run 41883029251 was created 148 s
after the commit (committed 17:59:54Z, created 18:02:22Z) and was green, with ctest at 505 of 505 in CI, the two new checks
included. Push `c334d88`, plan ci-cache's results, changed only `CLAUDE.md` and a plan, and **started no run**:
`gh api …/actions/runs?head_sha=c334d88…` found none for 300 s after the push, and the newest run overall stayed run
41883029251 of `8860993`. A window of 300 s is about twice the worst creation lag measured that afternoon, which was 148 s
(105 s for `8b629bd`, and 4 s for `b3af7d9` before the lag began), because Actions was still catching up after its incident.
So it was asked again at 18:11:36Z, 6 min 23 s after the push: the query's `total_count` was still 0. That is the
Done-when, in both directions.

**Not exercised:** `pull_request`. No pull request was opened, because commits go straight to `main`. It carries the same list
and `test_ci_paths` holds the two lists equal, and GitHub's rule for it is the one quoted under "What I measured".

**The words** landed with plan ci-cache's Step 4, which created the CI notes in CLAUDE.md: its CI subsection ends with a bullet on
what is skipped and why `README.md` is not. CLAUDE.md's count of `cmake -P` checks and tests moved with Step 1 (nine, and 505
in ctest) and has the `test_ci_paths` row.
