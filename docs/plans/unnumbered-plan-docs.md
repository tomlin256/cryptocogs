# Plan docs carry no sequence number: file names, titles and citations name the plan

**Status:** Done on 2026-11-08, except that Step 3 held back 8 items of GitHub text (see the *Outcome*), so #65 stays open. Rob approved
the plan as written, so D1 to D5 stood at their defaults below. Steps 0 to 4 are recorded in the *Outcome*.

Closes [#65](https://github.com/tomlin256/cryptocogs/issues/65).

**Not here:** commit messages and annotated tags (history: they keep their numbers, D5), and the three files of `docs/` that are not
plans.

## Problem

Rob: "Remove the seq prefix from plan docs in docs/plans as its confusing."

Why the prefix confuses, measured on 2026-11-08 at `cd6b118` (read-only; the counts come from `git grep` and `gh`, and the scripts
are in the scratchpad, not in the repository):

- **It looks like an issue number and is not one.** Plan 037 is #46 and plan 046 is #64, so "plan 046" and "#46" are different
  things, and comments read "(plan 037, #46)". The first issue is #21 and the plan docs record `Closes` from plan 032 on only (15 of
  50), so the issue number cannot take the prefix's place as a name.
- **It is not unique and not simple.** 53 files are in `docs/plans/`: 50 start `NNN-`, three do not (`code-review-findings.md`,
  `code-review-plan.md`, `ws-error-handling-gaps.md`). Five files share `001` (the plan and four appendices), and seven are
  `step-N-…` of that plan under a number of their own: `003-step-5-binance-rest-public.md` carries a 3 and a 5.
- **It is everywhere.** Stripping `NNN-` from the 50 names gives 50 distinct names, none equal to an unnumbered file, so the rename
  is safe. Citing the plans is not: the table below.

| What | Count | Can it change? |
|---|---|---|
| `plan NNN` / `Plan NNN` in tracked files | 629 in 155 files: 404 in 46 plan docs, 225 in 109 other files (107 tests, 57 `src/` and `include/`, 35 CMake and root, 21 workflows, 5 `docs/`) | yes |
| numbers that follow one: "plans 028 and 032", "plans 025 to 027" | 21, of which 6 are ranges | yes (a range is written as the plans it means) |
| bare short citations ("026's Results", "Before (025)", `# 013 — …`) | 91 in 32 files, 11 of them titles | yes, by hand |
| H1 titles that carry the number | all 50 | yes |
| relative links and file names `NNN-name.md` inside plan docs | 90 lines | yes |
| `docs/plans/…` in other files | 16 files (`README.md`, `CLAUDE.md`, 2 workflows, 12 test files); 6 of the paths name no file in full (`plans/027 Design decisions`) | yes |
| commit subjects and bodies, annotated tags | 200 commits, 9 tags | no: history |
| quotes of those in plan docs (`docs(plan): plan 030 approved as written`, `(plan 046, step 1)`) and of the titles of #22, #47, #49 | counted in Step 1 | stay as quoted (D5) |
| issue and comment text | 88 `docs/plans/` links in 31 issues; 117 `plan NNN` in 36 issues | editable, public (D3) |
| release notes | 7 `docs/plans/` links in v1.0.1, v2.0.0, v2.1.0 | editable, public (D3) |

A link to a renamed file does not redirect on GitHub, so the 95 links in issues and releases stop resolving when Step 2 is pushed.

## The change

**Names.** `docs/plans/<name>.md`: the file's name with `NNN-` removed and nothing else changed (D4). 50 renames with `git mv`, so
`git log --follow` still reaches each file's history. The three unnumbered files stay.

**Titles.** The H1 drops its number: `# Plan 046 — A kind starts with a letter: …` becomes `# A kind starts with a letter: …`. An
appendix names its parent, which the shared `001` used to say: `# 001 Appendix — Testing Strategy` becomes
`# Appendix to multi-exchange-abstraction — Testing Strategy`.

**Citations.** A plan is cited by its name, and the issue number stays beside it where it is now:

- `plan 035` → `plan clang-tidy-opt-in-test-step`, `(plan 036, #47)` → `(plan warnings-as-errors, #47)`;
- a link: the text `plan 028` and the target `028-platform-build-dirs.md` become `plan platform-build-dirs` and
  `platform-build-dirs.md`;
- a partial path names the file in full: `plans/027 Design decisions` becomes
  `plans/remaining-adapters-reserve-vector-growth.md Design decisions`;
- a range is spelled out: "plans 025 to 027" → "plans kraken-performance-profiling, kraken-reserve-vector-growth and
  remaining-adapters-reserve-vector-growth".

**History is not rewritten** (D5): commit subjects and tags, and the quotes of them and of the titles of #22, #47 and #49 that plan
docs hold. This plan's *Outcome* holds a table of the 50 old numbers and new names in the order the plans were made, so that a
"plan 046" in a commit subject can be read.

**The convention.** `CLAUDE.md` names the numbered pattern twice (in *Layout* and in its last section); both become
`docs/plans/<name>.md`, with the rule that a plan has no number: it is cited as `plan <name>`, and a commit subject says
`(plan <name>, step N)`. The example in the global guide, `add-auth.md`, already has this shape.

**The guard** (D2). `tests/doc_checks/check_plan_docs.cmake`, run as `test_plan_docs`, a plain `cmake -P` script beside
`check_claude_md_budget.cmake`, which it follows (the files are found by glob and named nowhere, so `test_ci_paths` has no objection;
a push that changes only `docs/**` starts no run and skips it, and each checkpoint's ctest runs it). It fails unless:

1. every file directly in `docs/plans/` (a hidden file such as `.DS_Store` is skipped) is named `[a-z][a-z0-9-]*.md`, and there is at
   least one (a moved directory must not pass by checking nothing);
2. every `docs/plans/<x>` in a file of the checkout is `docs/plans/<x>.md` for a file that exists, and every relative link to a
   `.md` file in a plan doc (with or without a `#fragment`) resolves.

It skips `build*/`, `cmake-build-*/`, `.git/`, `.cache/`, `.claude/` and `tests/doc_checks/` (its own fixtures hold paths that must not
exist). `test_plan_docs_fixtures` makes small checkouts at run time and runs the check over each, as `test_claude_md_budget_fixtures`
does. This plan's text writes its examples so that the check accepts them (`plans/…` without the `docs/`, or `<name>`).

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1. How far the number goes.** *Default:* all the way: file names, titles and every citation in the repository (the table above;
  168 files). *Alternative:* file names and links only (about 65 files): "plan 046" would stay in prose and in titles, pointing at
  files that no longer carry it. I rejected it as the default because those citations would then name nothing that exists, and a
  defect that a known design closes is closed.
- **D2. The guard.** *Default:* add `test_plan_docs` and its fixtures (about 100 lines of CMake and 130 of fixtures; 2 tests).
  *Alternative:* none; the scratch census of Step 1 and the link check of Step 2 are then the only evidence, and nothing stops a
  numbered plan from coming back.
- **D3. Links in GitHub text.** *Default:* edit the 95 `docs/plans/` links in 31 issues, their comments and 3 releases so that they
  resolve, and nothing else in that text. Outward-facing; each edit is audited first, the originals are kept, and GitHub keeps the
  edit history. *Alternative:* leave them: they stop resolving, and the numbers in them are read with the table.
- **D4. The first plan's family.** *Default:* a literal strip, so `001-appendix-compat-shim.md` is `appendix-compat-shim.md` and
  `003-step-5-binance-rest-public.md` is `step-5-binance-rest-public.md`; the appendices' titles name the parent (above), and the step
  docs already say "Step 5 of plan 001" in their text. *Alternative:* a `multi-exchange-` prefix on the four appendices and the seven
  step docs (11 more renames), so that they sort together as the shared number made them.
- **D5. History keeps its numbers.** *Default:* the quotes stay as quoted and so do the titles of #22, #47 and #49. *Alternative:*
  retitle those three issues and update the quotes (3 more edits on GitHub).

## Steps

Commits go straight to `main`, one per step, pushed once the checkpoint is green. No commit message carries a closing keyword: #65 is
closed by hand in Step 4. Counts are what I expect and are replaced by the measured ones in the *Outcome*.

### Step 0 — Approval

You approve or edit. Commit this plan (`docs(plan): unnumbered-plan-docs approved as written`, status line changed), push
(documentation only: no CI run), and comment on #65 with the plan's path (the text audited with `check_public_leaks.py --tree`
first). *Done when:* #65 links the plan and the plan links #65.

### Step 1 — Cite by name (paths unchanged)

Every citation changes and no file moves, so each one still resolves (the name is inside the old file name) and every commit builds.
Doing the text first also keeps Step 2's renames close to pure, so that git follows them.

*Start from green.* `cmake --preset debug-macos-arm64`, build, full ctest; the count is recorded (588 expected). *Done when* all is
green; if not I stop and tell you.

*Tests:* none new or modified. The edits are comments and prose; every test that reads an edited file runs in the full ctest
(`test_claude_md_budget`, `test_cmake_floor`, `test_ci_paths`, `test_ci_clang_tidy`, `test_warning_option`, `test_clang_tidy_*`, …).

*Census, red first.* A script in the scratchpad (not kept) counts the citations of the table above in the tracked files. Before:
those numbers. After: only the quotes of D5 and this plan's own text remain, each listed and read.

*Change.*
1. A table of the 50 numbers and names, made from `ls docs/plans`. A script applies it to `plan NNN`, `Plan NNN`, the numbers that
   follow one, the link text and the 6 partial paths, keeping the leading case.
2. By hand, each read in context: the 6 ranges; the 91 bare citations; the 50 titles (the appendices name their parent);
   possessives and sentence starts ("Plan 027's Binance analog", "Plan 035 added the gate"); and any comment line that would pass 130
   columns, which is re-wrapped.
3. `CLAUDE.md`: its 6 citations and the two plan-file rules (wording above).

*Checks of the rewrite.*
- **Inverse.** The table applied backwards to the new text of every changed file gives the old text, except on the hand-edited lines;
  those are counted and listed in the *Outcome*. A wrong name in the mechanical pass cannot hide.
- **Comments only.** In every changed C++, CMake and YAML file, each changed line is equal before and after up to the start of its
  comment (`//` or `#`); no backslash is added at the end of a comment line.
- **Budget.** `wc -c CLAUDE.md` under 21000 (20273 now; the names are longer than the numbers, about 150 bytes more).

*Checkpoint.* Full build and ctest of `debug-macos-arm64`: all green, no warning. Commit
`docs: plans are cited by name, not number (plan unnumbered-plan-docs, step 1)`, push. The push changes code, workflows and the
README, so CI starts (`release-linux-x86_64` and `clang-tidy`); I look at the runs and do not wait. *Done when:* the census as stated;
the three checks; the checkpoint green; pushed.

### Step 2 — File names, links, convention and the guard

*Start from green* (Step 1's tree, ctest count recorded).

*Tests (new):* `test_plan_docs` and `test_plan_docs_fixtures`, registered in `tests/CMakeLists.txt` beside
`test_claude_md_budget`, at the top level only. The fixtures:
- accepted: a plan doc with a digit inside its name and a link with a fragment; a code comment that cites an existing plan; a
  numbered plan doc under `build/` (skipped, as another project's files are);
- refused: a numbered name (the same file outside `build/`); a mention of a plan that does not exist; a partial path; a link to a
  missing sibling; a `docs/plans/` that is empty, and one that is missing.

*Red first.* The check over Step 1's tree reports 50 numbered names (rule 1) and no broken link (rule 2). Not committed: every commit
builds and passes.

*Change.*
1. `git mv` of the 50 files.
2. In the same commit: the 90 links and file names `NNN-name.md` inside plan docs, the paths in the 16 other files, and
   `CLAUDE.md`'s two rules.
3. The check, its fixtures and their registration.

*Hand-made changes* to the check (a scratch copy of the changed tree made with `rsync -a --exclude 'build*' --exclude .git`, `df -h /`
first, deleted once recorded; the old text asserted to occur once before each edit; run as
`cmake -DSCRATCH_ROOT=<dir> -P tests/doc_checks/check_plan_docs_fixtures.cmake`, an unmutated control kept). Expected:

| Change to the check | `test_plan_docs_fixtures` |
|---|---|
| control: unmutated | green |
| (a) rule 1 allows a name that starts with a digit | red: the numbered name |
| (b) rule 2 does not require `.md`, so a partial path passes | red: the partial path |
| (c) a link's `#fragment` is not removed before the file is looked up | red: the link with a fragment |
| (d) the relative links of plan docs are not read | red: the link to a missing sibling |
| (e) `build*/` is scanned | red: the plan doc in `build/` |
| (f) an empty `docs/plans/` passes | red: the empty directory |

*Checkpoint.* Full build and ctest of `debug-macos-arm64`: all green (590 expected: 588 and the two new), no warning; `git diff -M
--stat HEAD~1` shows 50 renames, and `git log --follow` reaches the history of three of them. Commit
`docs: plan files carry no number; test_plan_docs holds it (plan unnumbered-plan-docs, step 2)`, push. *Done when:* red as stated,
then green; (a) to (f) as stated; no `docs/plans/[0-9]` left in the tree; the checkpoint green; pushed.

### Step 3 — The links in GitHub text (D3)

Right after the Step 2 push, so that the links are dead for minutes and not longer.

1. Fetch the bodies and comments of the 31 issues and the notes of the 3 releases into the scratchpad; those files are the originals.
2. Change the numbered file name in each of the 95 links to the new one (`<NNN>-<name>.md` to `<name>.md`) and nothing else.
3. Audit the new text with `check_public_leaks.py --tree` (exit 0), and read each diff.
4. Apply: `gh issue edit --body-file`, `gh api -X PATCH` for a comment, `gh release edit --notes-file`.
5. Fetch again: no link to a numbered plan file is left, and every plan link in the text names a file that exists on `main`
   (`git cat-file -e main:docs/plans/<name>.md`).

*Tests:* none (no project file changes); step 5 is the check. A denied write is reported with what is left, and I wait for
"try again". *Done when:* step 5 holds.

### Step 4 — Measure, record, close

This plan's *Outcome*: the commits; the census before and after; the table of the 50 old numbers and new names; the hand-edit counts
and the lines listed; the mutants as measured; the ctest counts; the CI runs I saw finish; what differed from the plan. A
documentation-only commit and push, a comment on #65 that lists the commits and the measurements, and #65 closed by hand
(`completed`). *Left for later:* a build in `ubuntu-dev` (the diff of code files is comments only, checked in Step 1; CI builds the
pushed commit on Linux) and the sanitizer trees. *Done when:* #65 is closed with its comment and the Outcome holds the measured
figures.

## Designs I rejected

- **Cite a plan by its issue number.** Plans 001 to 031 have no `Closes` line and the first issue is #21, so 35 of the 50 plans would
  need a different name.
- **The issue number as the file prefix** (`65-unnumbered-plan-docs.md`). It is the confusion again, with a number that is at least
  unique; the request is to remove the prefix.
- **A date prefix.** Still a prefix, and the order is in git (`git log --diff-filter=A`).
- **An index file of old numbers in the repository.** `CLAUDE.md` says the issue is the index and there is no index file. The table
  lives in this plan's *Outcome*, as a record.
- **Stub files at the old paths** to keep links alive. They would keep the numbered names in the directory.

## Self-review — risks and assumptions

- **I read "seq prefix" as the `NNN-` of the names and, with it, the number as an identifier everywhere.** If only the names were meant,
  say so: D1's alternative drops Steps 1 and 3 and most of Step 2.
- **`ls docs/plans` is no longer in the order the plans were made.** The table in the *Outcome* is, and so is
  `git log --diff-filter=A --format=%ad -- docs/plans/<name>.md`.
- **A wrong name in the mechanical pass** is closed by the inverse check. The hand edits (6 ranges, 91 bare citations, sentence
  starts, 50 titles) are not: each is read in context and the diff is read with `git diff --word-diff`.
- **Longer names, longer lines.** Names are 10 to 40 characters against 3. Comment lines past 130 columns are re-wrapped by hand; the
  count is in the *Outcome*.
- **`CLAUDE.md` is 727 bytes under its limit** and gains about 150. Measured in Step 1; if it is over, a citation there is shortened.
- **Quotes keep their numbers** (D5), so the census does not reach zero. The lines that remain are listed, not hidden.
- **Step 3 edits published text.** The originals are kept, the new text is audited, and GitHub keeps each edit's history. Between the
  Step 2 push and Step 3 the 95 links are dead.
- **The code diff is comments only**, checked by script. The GCC build is not run here; CI's `release-linux-x86_64` builds the pushed
  commit and I look at it.
- **Another session's files.** `git status` is read before each commit and commits are by explicit path (`git commit -o <paths>`);
  today the only untracked file is `.DS_Store`.
- **Public repository.** Nothing here names a non-public project; every piece of issue, comment and release text is audited before it
  is posted.
- **This plan quotes old forms** (`plan 035`, `NNN-`), so the Step 1 census skips this file, and its paths are written so that
  `test_plan_docs` accepts them.

## Outcome

Steps 0 to 2 and 4 are done, and Step 3 is done for 50 of its 58 items (2026-11-08). Counts below are measured.

### Commits

| Step | Commit | What |
|---|---|---|
| 0 | `390bd6a` | this plan, approved as written |
| 1 | `efb574d` | every citation by name, in 162 files; no file moved; `See #65` |
| 2 | `5275739` | the 50 renames, the paths that named them, the rule in `CLAUDE.md`, `test_plan_docs` and its fixtures; `See #65` |
| 3 | none | 50 items of GitHub text edited (below) |
| 4 | the commit that adds this section | the status line and this *Outcome*; `See #65`, and #65 stays open (below) |

### The census

`git grep` over the tracked files, this file skipped. Before is `cd6b118`; the other columns are the tree after each step.

| What | Before | After step 1 | After step 2 |
|---|---|---|---|
| `plan NNN` | 629 in 155 files | 28 in 26 files | 29 in 27 files |
| list continuations ("plans 028 and 032", "025 to 027") | 25 | 1 | 1 |
| bare citations ("026's Results", "Before (025)") | 82 | 8 | 8 |
| `plan-NNN` | 7 | 0 | 0 |
| numbered H1 titles | 50 | 0 | 0 |
| `NNN-name.md` lines in plan docs | 90 | 90 | 0 |
| partial paths (`plans/027 Design decisions`) | 6 | 0 | 4 |
| files outside `docs/plans` with a numbered path | 10 | 16 | 1 |

What remains, each read: 24 quotes of commit subjects in inline code (`docs: mark plan 004 done`,
`docs(plan): plan 030 approved as written`, `(plan 046, step 1)`, …), the titles of #22, #47 and #49 (4 tokens: D5), 8 bare numbers in
quoted subjects, and the sentence in `CLAUDE.md` that tells a reader the old subjects say `plan 046`. The 4 partial paths and the 1 file
with a numbered path are the check's own header and fixtures, which hold bad paths on purpose and which the check skips.

### Step 1: the checks of the rewrite

- **Inverse.** The new text of each changed file, turned back into numbers, against the old text, as a word stream with the comment markers
  dropped: the 50 titles, and **104 spots in 31 files** that were edited by hand. By file: `remaining-adapters-reserve-vector-growth` 31,
  `rename-kraken-credentials` 10, `claude-md-trim` 7, `kraken-reserve-vector-growth` 5, `step-8-binance-ws-api` 5, `bench_binance_ws.cpp`,
  `bench_coinbase_ws.cpp` and `bench_cryptocom_ws.cpp` 4 each, `rename-to-cryptocogs` 3, `remove-compat-shim` 3, `curl-post-sent-once`,
  `cmake4-cxx23`, `coinbase-fix-order-entry`, `step-6-binance-rest-private`, `CMakeLists.txt`, `sanitizers.yml` and
  `cmake-single-platform.yml` 2 each, and 1 each in `test_http_client.cpp`, `test_cryptocom_auth.cpp`, `test_coinbase_auth.cpp`,
  `test_binance_auth.cpp`, `check_tree_name.cmake`, `check_clang_tidy_module.cmake`, `bench_tick_price.cpp`, `bench_kraken_ws.cpp`,
  `bench_kraken_rest.cpp`, `code-review-multi-exchange-abstraction.md` and the plans `tsan-races-in-the-tests`,
  `cryptocom-user-trade-channel`, `cmake-install-nits` and `step-7-binance-ws-streams`. They are the ranges, the bare citations,
  possessives ("Plan 027's Binance analog"), the link texts and sentences of the longest plan, the sentences in two workflows and in
  `CMakeLists.txt` that did not read with a name in place of a number, the full path in place of each partial one, five divider lines and
  one `-Werror=deprecated-declarations` that the re-wrap broke. A wrong name in the mechanical pass showed up as none of them.
- **Comments only.** 106 of the 107 changed files that are not Markdown (C++, CMake, YAML and the two dot-files) are equal before and after
  once comments are removed, string literals kept. The one that differs is `CMakeLists.txt`, in two string literals: the help text of
  `CRYPTOCOGS_BUILD_BENCHMARKS` and the doc string of the platform cache variable, both of which cite a plan. No backslash was added at the
  end of a comment line.
- **Width.** 50 comment lines in code and configuration passed 130 columns and were re-wrapped (0 remain); 39 lines of Markdown passed 140
  and were re-wrapped (a table row and the lines that were already wider are left). The re-wrap moves words and nothing else: the
  Markdown files compare equal as word streams before and after it. The step adds 799 lines and removes 758.
- **Budget.** `CLAUDE.md` 20273 bytes at the start, 20370 after Step 1, 20589 after Step 2 (limit 21000).

### Red, then green

**Start from green** (`debug-macos-arm64`, Apple clang): 588 of 588, 24.24 s of tests. After Step 1 the same, 588 of 588, 24.97 s, with
every edited unit rebuilt and no warning from our code (two CMake deprecation notices, from the fetched `backward` and `CLI11`).

**Red first** (Step 2): `check_plan_docs.cmake` over Step 1's tree reports **51 findings**: the 50 numbered names, and the old rule in
`CLAUDE.md` (a numbered pattern for plan files), which the plan had not predicted. No link is dead, as planned. After the change it passes
over 54 plan docs and 254 text files, and the full run is **590 of 590**, 25.12 s, no warning.

`git diff -M --stat` of the commit shows 50 renames, each 81% to 100% similar; `git log --follow` reaches 4 commits of
`clang-tidy-ci-job`, 9 of `warnings-as-errors` and 23 of `multi-exchange-abstraction`.

### Hand-made changes

A scratch copy of the changed tree (`rsync` without `build*` and `.git`; the old text asserted to occur once before each edit; the check
restored from a saved copy between rows; the copy deleted afterwards), each run as
`cmake -DSCRATCH_ROOT=<dir> -P tests/doc_checks/check_plan_docs_fixtures.cmake`. The failing cases are the fixtures' names.

| Change to the check | `test_plan_docs_fixtures` |
|---|---|
| control: unmutated | green |
| (a) a name may start with a digit | red: `numbered_name` |
| (b) a mention may leave out its `.md` | red: `partial_path` |
| (c) a link's `#fragment` stays on, and the link is skipped | red: `missing_with_fragment` |
| (c2) a link's `#fragment` stays on, and the file is looked up with it | red: `accepted` (the link with a fragment to a plan that exists) |
| (d) the links of plan docs are not read | red: `missing_sibling`, `missing_with_fragment`, `missing_parent` |
| (e) `build*/` is scanned | red: `accepted` |
| (f) an empty `docs/plans/` passes | red: `empty_directory`, `only_hidden` |
| not in the plan: a hidden file is a candidate | red: `accepted` |
| not in the plan: `tests/doc_checks/` is scanned | red: `accepted` |
| not in the plan: a URL is read like a path | red: `accepted` |
| not in the plan: the full stop that ends a sentence stays in the name | red: `accepted` |
| not in the plan: links are resolved from the working directory | red: `accepted` |
| not in the plan: a directory in `docs/plans/` is not listed | red: `subdirectory` |
| not in the plan: `cmake-build-*/`, `.git/`, `.cache/` or `.claude/` is scanned (4 rows) | red: `accepted` each |
| not in the plan: no mention is read | red: `missing_plan`, `partial_path` |
| not in the plan: the bare directory `docs/plans/` is refused | red: `accepted` |

The first run of the row "a URL is read like a path" stayed **green**. The fixtures' `write_file` took a function's three named
arguments and ignored the rest, so the README of the accepted case was cut at its first piece and held no URL. The pieces are now joined
with `string(CONCAT)` first, and the row went red.

### The links in GitHub text (Step 3)

Fetched read-only before the Step 2 push and again after it (unchanged): 65 issues, 57 comments, 6 releases. **89 links to numbered plan
files in 58 items**: 13 in 12 issue bodies, 68 in 43 comments, 8 in 3 releases. The plan had counted 95. The new text of each item differs
from the old only in the number that leaves the file name of each link (checked by removing that number from both), every new path
exists on `main`, and the link texts ("plan 043") are as they were.

**Edited: 50 items, 77 links**: 10 issue bodies, 37 comments and the notes of `v1.0.1`, `v2.0.0` and `v2.1.0`. The audit
(`check_public_leaks.py --tree`) exited 0 over exactly those 50 files; each was read back and equals what was sent.

**Held back: 8 items, 12 links** (2 issue bodies and 6 comments). The audit exits 1 on them with 28 findings, because the text they
already publish names a non-public project. They were not edited: D3 allows the links and nothing else, and the audit is the gate. The
ids are not written here, because they would point at the text; Rob has them. Their 12 numbered links are the only ones left in the
95 `docs/plans/` mentions of the GitHub text. The 3 other mentions that name no file here are URLs into flywheel-dag's plan docs.

### Counts

| Tree | Result |
|---|---|
| CI `CMake on a single platform` (`release-linux-x86_64`, GCC 15.2), `efb574d`, run 42501887357 | 588 of 588, 17.80 s of tests, the job took 2 min 2 s |
| CI `clang-tidy`, `efb574d`, run 42501887341 | 95 of 95, 211.42 s of tests, the job took 4 min 35 s |
| CI `CMake on a single platform`, `5275739`, run 42502725328 | 590 of 590, 13.64 s of tests, the job took 1 min 3 s |
| CI `clang-tidy`, `5275739`, run 42502725352 | 95 of 95, 111.35 s of tests, the job took 2 min 37 s |

All four runs finished green when I looked.

### Found outside this plan

Published issue and comment text (8 items, above) names a non-public project, and no check in the repository reads issue text. I opened
no issue for it: a public issue would point at the text. Rob decides whether to scrub it.

### What differed from the plan

- **The counts.** Bare citations were 82 and not 91; list continuations 25 and not 21; and 7 hyphenated forms (`plan-001`) were not
  counted at all. GitHub held 89 numbered links and not 95.
- **The guard** also skips URLs, because 5 of them name `docs/plans/<x>` files of another repository, and its red-first run found the old
  rule in `CLAUDE.md` as well.
- **`CMakeLists.txt`** changed in two string literals, not only comments.
- **The partial paths** were written out in full in Step 1, so that Step 2 renames them like any other.
- **`CLAUDE.md`** grew by 316 bytes and not 150: it also tells a reader that old subjects say `plan 046` and where the table is.
- **Step 3** is not complete (above), so #65 is not closed.
- **Left for later**, as planned: a build in `ubuntu-dev` and the sanitizer trees. CI built both pushed commits with GCC 15.2.

### The 50 old numbers and the names they became

In the order the plans were made. The date is that of the file's first commit. The four appendices were made with plan 001 and name it as
their parent. A commit subject such as `plan 046, step 1` reads through this table.

| Was | Is (`docs/plans/<name>.md`) | Made |
|---|---|---|
| 001 | multi-exchange-abstraction | 2026-06-06 |
| 001 | appendix-binance-message-formats | 2026-06-06 |
| 001 | appendix-compat-shim | 2026-06-06 |
| 001 | appendix-migration-guide | 2026-06-06 |
| 001 | appendix-testing-strategy | 2026-06-06 |
| 002 | step-2b-compat-shim | 2026-06-07 |
| 003 | step-5-binance-rest-public | 2026-06-10 |
| 004 | step-6-binance-rest-private | 2026-06-11 |
| 005 | step-7-binance-ws-streams | 2026-06-12 |
| 006 | step-8-binance-ws-api | 2026-06-12 |
| 007 | step-9-cmake-build-validation | 2026-06-13 |
| 008 | step-10-agent-onboarding-guide | 2026-06-13 |
| 009 | relocate-tickprice-to-common | 2026-06-13 |
| 010 | shared-curl-http-transport | 2026-06-13 |
| 011 | generic-rest-client | 2026-06-13 |
| 012 | install-and-package-config | 2026-06-13 |
| 013 | source-compat-shim | 2026-06-14 |
| 014 | remove-compat-shim | 2026-06-14 |
| 015 | rename-to-cryptocogs | 2026-06-15 |
| 016 | declarative-headers | 2026-06-17 |
| 017 | cmake-install-nits | 2026-06-17 |
| 018 | coinbase-exchange-adapter | 2026-06-19 |
| 019 | coinbase-fix-order-entry | 2026-06-19 |
| 020 | cryptocom-exchange-adapter | 2026-06-20 |
| 021 | credentials-from-file | 2026-06-22 |
| 022 | rename-kraken-credentials | 2026-06-22 |
| 023 | cryptocom-user-trade-channel | 2026-07-13 |
| 024 | cryptocom-create-order-exec-inst | 2026-07-13 |
| 025 | kraken-performance-profiling | 2026-08-31 |
| 026 | kraken-reserve-vector-growth | 2026-09-01 |
| 027 | remaining-adapters-reserve-vector-growth | 2026-09-01 |
| 028 | platform-build-dirs | 2026-11-02 |
| 029 | cmake4-cxx23 | 2026-11-04 |
| 030 | ci-cache | 2026-11-04 |
| 031 | ci-skip-docs-only | 2026-11-04 |
| 032 | sanitizer-option-and-presets | 2026-11-04 |
| 033 | tsan-races-in-the-tests | 2026-11-04 |
| 034 | transport-callback-lifetimes | 2026-11-05 |
| 035 | clang-tidy-opt-in-test-step | 2026-11-05 |
| 036 | warnings-as-errors | 2026-11-05 |
| 037 | clear-clang-tidy-findings | 2026-11-05 |
| 038 | curl-header-list-append-failure | 2026-11-06 |
| 039 | kraken-example-ws-frames | 2026-11-06 |
| 040 | clang-tidy-ci-job | 2026-11-06 |
| 041 | curl-handle-reset | 2026-11-06 |
| 042 | curl-timeouts | 2026-11-06 |
| 043 | curl-post-sent-once | 2026-11-06 |
| 044 | claude-md-trim | 2026-11-06 |
| 045 | scrub-nonpublic-names | 2026-11-07 |
| 046 | platform-kind-pattern | 2026-11-07 |
