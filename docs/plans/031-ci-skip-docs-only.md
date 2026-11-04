# Plan 031 — CI: do not build on documentation-only changes

**Status:** Approved as written (2026-11-04), so D1 to D4 stand at the defaults below. Rob approved pushing this plan's
commits to `main` and linking the plan from #40. Nothing below is implemented yet.
**Design source:** flywheel's `.github/workflows/cmake-single-platform.yml` (its `paths:` filter) and
`docs/plans/ci_path_filter_debounce_plan.md` (a private repository). This plan differs from it in one choice, D1.
**Closes:** [GitHub issue #40](https://github.com/tomlin256/cryptocogs/issues/40) — "Build will trigger on doc updates
pointlessly". The issue reads: *"You can see that in the recent CI runs for example
https://github.com/tomlin256/cryptocogs/actions/runs/41875368976"*.
**Follows:** [plan 030](030-ci-cache.md), which left `paths:` filters out (its D1: "Say if you want any folded in") and
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

- **D1 A deny-list (`paths-ignore`), not flywheel's allow-list** (`paths:` with `'!**/*.md'` last). With a deny-list, anything
  not named runs CI, a new top-level directory included, so forgetting an entry costs one wasted run. With an allow-list,
  anything not named is skipped, so forgetting an entry costs a change nobody built. Flywheel took the allow-list because its
  deny-list forgot a directory and its private minutes ran out; here minutes are free. Its `'!**/*.md'` would also skip a
  README edit, which `test_cmake_floor` reads.
- **D2 The list is `docs/**` and `CLAUDE.md`.** README.md keeps running CI (above). Not listed, since the issue is about
  documentation: `LICENSE`, `.gitignore`, `.clangd`, `.vscode/**`. Nothing reads them either; say if you want them.
- **D3 One new test**, `test_ci_paths` (Step 1), as plan 030 has `test_ci_cache_key`. It fails when the list drifts onto
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

- **Flywheel's allow-list** — D1.
- **`paths-ignore: ['**/*.md']`** — it includes README.md.
- **`[skip ci]` in commit messages** — every documentation commit would have to remember it, and this plan's own would not.
- **Dropping the README clause from `test_cmake_floor`**, so that README.md can be ignored — then the README can claim a floor
  the project does not have, which is what plan 029 added the clause to stop.
- **A first job that diffs, with the others `needs:` it** — it still starts a runner to find out. The filter starts nothing.

## Not in this plan

`concurrency` with `cancel-in-progress` (a newer push cancels the run in flight; flywheel `ci_path_filter_debounce_plan.md`)
and `timeout-minutes`, as in plan 030. The files of D2 that are not documentation.

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
- **I read the unit-test rule as covering code**, as in plan 030: the workflow is configuration, and `test_ci_paths` holds the
  one invariant of it that can be tested offline. Say if you want something else.

## Outcome

Not started.
