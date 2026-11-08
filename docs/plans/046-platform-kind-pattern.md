# A kind starts with a letter: the platform guard agrees with flywheel-dag's copy

**Status:** Done (2026-11-07). Rob approved the plan as written, so D1 stood at its default (the flywheel-dag issue is opened). Steps 0
to 2 are recorded in the *Outcome*.

Closes [#64](https://github.com/tomlin256/cryptocogs/issues/64).

**Follows:** [plan platform-build-dirs](028-platform-build-dirs.md) (the module, a copy of flywheel-dag's: D3) and
[plan sanitizer-option-and-presets](032-sanitizer-option-and-presets.md) (rule 2).
**Not here:** any edit of flywheel-dag's checkout (D1).

## Problem

`cmake/cryptocogs_platform.cmake` and flywheel-dag's `cmake/flywheel_dag_platform.cmake` are documented as the same table and the
same three rules, to be changed together. They differ in two places. Rob's instruction is "adopt flywheel-dag style": this copy
moves to flywheel-dag's way in both (the first bullet of the self-review says how I read that).

1. **The kind in rule 1.** This copy writes it `[a-z0-9]+` (lines 69, 71 and 73: the shape test, the platform match, and the read of
   the kind for the error text); flywheel-dag's writes it `[a-z][a-z0-9]*`. A name whose kind starts with a digit
   (`2025-macos-arm64`) has the shape of a platform tree here, so on a Linux host it is refused as "for macos-arm64"; there it is a
   free-form name and is accepted.
2. **The system name.** This copy lower-cases it and then maps `darwin` to `macos`, so `darwin` and `DARWIN` give `macos-arm64`;
   flywheel-dag's maps only the exact `Darwin`, so those two give `darwin-arm64`. CMake always names the host `Darwin`, so no real
   host is affected.

### What I checked (2026-11-07; this checkout at `830ae38`, flywheel-dag at `6bf1442`; both read-only)

A throwaway harness in the scratchpad (not in the repo) includes both modules and gives both the same rows: three hosts
(`linux-arm64`, `linux-x86_64`, `macos-arm64`) × three remembered platforms (none, `linux-arm64`, `macos-arm64`) × five values of
the sanitizer option (none, `asan`, `tsan`, `ASAN`, `OFF`) = 45 rows per name. It compares which rule refuses a row, or that none
does; the error texts are each copy's own and are not compared.

| Group | Rows | Differ | Which |
|---|---|---|---|
| 40 names: every name in either `check_platform.cmake` table, plus five whose kind starts with a digit | 1800 | 150 (85 are accepted by one copy and refused by the other) | all five digit-led names, 30 rows each |
| 15 boundary names around the kind pattern (`x-macos-arm64`, `gcc13-linux-x86_64`, `asan2-linux-arm64`, `2025-linux`, …) | 675 | 135 | `2025-linux`, `9-macos`, `9lives-linux`, 45 rows each; the other twelve agree |
| the tag: 7 spellings of the system name × 8 of the machine | 56 | 16 | `darwin` and `DARWIN`, with every machine |

Nothing else differs. The issue's own run (80 of 1665 rows, all digit-led names) is the same finding with other names.

**What adopting costs.** `2025-macos-arm64` on a Linux host is no longer refused: it is not examined, as `scratch` is not. Rule 3
still binds it once it has been configured on another platform. No preset and no row of either table has a digit-led kind, so
nothing that exists changes; `darwin` and `DARWIN` stop giving `macos` for a spelling CMake never produces (`check_presets.cmake`
passes `Darwin`).

## The change

**1. `cmake/cryptocogs_platform.cmake`.**
- The three patterns `[a-z0-9]+-` become `[a-z][a-z0-9]*-`, the kind pattern of flywheel-dag's copy.
- `cryptocogs_platform`: `if(system_name STREQUAL "Darwin")` is `macos`, else the lower-cased name (flywheel-dag's form), then the
  machine as now.
- Comments say what is true now: the kind is a letter and then letters and digits (header, line 8); the function's description of
  the system name; rule 1 (header and in the function) gains "a kind starts with a letter, so `2025-macos-arm64` is not a platform
  tree". The last sentence of the header, which names the difference, is removed; "change both together" stays.

**2. `tests/tree_checks/check_platform.cmake`** (16 new rows; nothing removed):
- tag: `darwin` and `DARWIN` with `arm64` give `darwin-arm64` (only CMake's spelling is macOS).
- accepted: five digit-led names (`2025-macos-arm64`, `2025-linux-x86_64`, `9lives-linux-arm64`, `9lives-macos-x86_64`, `2025-linux`)
  on `linux-arm64` and on `macos-arm64`; and `gcc13-linux-arm64` on `linux-arm64` (a kind may hold digits after its first letter).
- refused: `gcc13-linux-x86_64` and `x-macos-arm64` on `linux-arm64` (a kind of letters and digits, and of one letter, is still a
  kind). The text must contain the whole phrase, `is for linux-x86_64, but this host is linux-arm64`: the name itself, quoted in the
  text, already contains `linux-x86_64`, so the platform alone would not show that the claim was read.
- refused by rule 3: `2025-macos-arm64` on `linux-arm64` that remembers `macos-arm64`; the text contains "first configured on
  macos-arm64" (a name that is not examined still remembers its platform).

**3. Unchanged:** `test_tree_name` (the guard passes the directory name to the same function), `test_presets`, `CLAUDE.md`,
`README.md`. `CLAUDE.md` says "one lower-case word", which stays true.

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1. flywheel-dag gets an issue, not an edit.** Its `tests/check_platform.cmake` has no row for a digit-led kind or for `darwin`
  (its module already passes them: measured above), so a change to its module that undid either would pass its tests. *Default:*
  after the push, open an issue there (label `enhancement`, priority low in the body) that names the rows and links this plan. Plan
  platform-build-dirs D3 says a table changes in both repos; the rule since is that a sibling gets an issue. *Alternative:* none opened.

## Steps

Commits go straight to `main`, one per step, pushed once the checkpoint is green. No commit message carries a closing keyword: #64 is
closed by hand in Step 2. Counts are what I expect and are replaced by the measured ones in the *Outcome*.

### Step 0 — Approval

You approve or edit. Commit this plan (`docs(plan): 046 approved as written`, status line changed), push (documentation only: no CI
run), and comment on #64 with the plan's path (the text audited with `check_public_leaks.py --tree` first). *Done when:* #64 links the
plan and the plan links #64.

### Step 1 — Rows red, then the change, then green

*Start from green.* `cmake --preset debug-macos-arm64`, build, full ctest; the count is recorded. *Done when* all is green; if not I
stop and tell you.

*Tests (modified):* `check_platform.cmake` only, the 16 rows above.

*Red first.* The new rows against `HEAD`'s module: `cmake -DSOURCE_DIR=. -P tests/tree_checks/check_platform.cmake`. Expected 11
failing rows: the two `darwin` tag rows, eight of the ten digit-led accepted rows (`9lives-linux-arm64` on `linux-arm64` and
`2025-macos-arm64` on `macos-arm64` pass, because the old pattern also accepts them), and the rule-3 row (its text says "is for
macos-arm64"). The other five new rows pass at `HEAD`: they are there for the mutants. Not committed: every commit builds and passes.

*Change.* The module, as above.

*Hand-made changes* (a scratch copy of the changed tree made with `rsync -a --exclude 'build*' --exclude .git`, `df -h /` first,
deleted once recorded; the old text asserted to occur once before each edit; the copy diffed against the tree after; run as
`cmake -DSOURCE_DIR=<copy> -P tests/tree_checks/check_platform.cmake`, an unmutated control kept). Expected:

| Change to the changed module | `test_platform_name` |
|---|---|
| control: unmutated | green |
| (a) the shape test (line 69) back to `[a-z0-9]+` | red: the digit-led accepted rows and the rule-3 row |
| (b) lines 71 and 73 back to `[a-z0-9]+`, line 69 as changed | green: equivalent. Both are reached only by a name that passed the shape test, and its kind starts with a letter |
| (c) all three `[a-z]+-` (no digit in a kind) | red: `gcc13-linux-x86_64` |
| (d) only line 71 `[a-z]+-` | red: `gcc13-linux-arm64` accepted |
| (e) only line 73 `[a-z]+-` | red: the phrase `is for linux-x86_64, but …` of `gcc13-linux-x86_64` |
| (f) all three `[a-z][a-z0-9]+-` (a kind of two characters at least) | red: `x-macos-arm64` |
| (g) the system name lower-cased before it is compared with `darwin` | red: the two `darwin` tag rows |
| (h) no `Darwin` branch | red: the existing `Darwin` tag rows |

(b) is in the table because the three spellings are kept the same as flywheel-dag's: no row can tell lines 71 and 73 from the change,
and none should.

*Checkpoint.* Full build and ctest of `debug-macos-arm64`: all green, no warning. Commit
`build: a kind starts with a letter and only Darwin is macos (plan 046, step 1)`, push. CI's `release-linux-x86_64` runs the same
three tests (`test_platform_name`, `test_tree_name`, `test_presets`) on Linux; I look at the run and do not wait for it.
*Done when:* red as stated, then green; (a) to (h) as stated; the checkpoint green; pushed.

### Step 2 — Measure, record, close

The harness again over the committed module and flywheel-dag's: expected **0 of 1800, 0 of 675 and 0 of 56** rows differ (150, 135
and 16 before). This plan's *Outcome* (commits, the red-then-green record, the mutant table as measured, the two harness runs); a
documentation-only commit and push. A comment on #64 that lists the commits and the measurement, and #64 closed by hand (`completed`).
The issue of D1, if confirmed; I tell you its number. *Left for later:* a run of the Linux tree in `ubuntu-dev`.
*Done when:* #64 is closed with its comment, and the Outcome holds the measured figures.

## Designs I rejected

- **flywheel-dag adopts `[a-z0-9]+`** (the issue's second way). It edits a sibling checkout, and a kind is "one lower-case word"
  (the module, `CLAUDE.md`, plan platform-build-dirs D1); a word starts with a letter.
- **The pattern written once, in a variable.** It would end the three sites and the two equivalent mutants, but the module would then
  differ in shape from flywheel-dag's, whose header says the two are changed together. If wanted, it is a change to both.

## Self-review — risks and assumptions

- **"Adopt flywheel-dag style" is read as both differences** (the issue closes both in one change). If only the pattern was meant, the
  `Darwin` hunk and its two tag rows come out; the divergence on `darwin` then stays and needs a line in the header.
- **A digit-led name is no longer examined.** Measured above: nothing that exists is affected. A person who names a tree
  `2025-macos-arm64` on Linux gets a tree; rule 3 refuses it only after it has been configured elsewhere.
- **The comparison is of verdicts, not texts.** The header says the error texts are each copy's own; the harness does not look at the
  words, and the rule-3 row looks at one phrase of this copy's.
- **The equivalent mutant (b).** The argument is that a name reaching lines 71 and 73 already passed the shape test with a letter-led
  kind. The mutant run checks it; if (b) goes red, the argument was wrong and I stop and tell you.
- **The module is two pure functions**, so a Mac run says the same on Linux; CI's `release-linux-x86_64` is the Linux evidence.
- **The harness is not kept.** A test that reads a sibling checkout would break this repository's standing alone (`CLAUDE.md`). The
  figures are recorded here; the rows that hold the behaviour are in `check_platform.cmake`.
- **flywheel-dag may move** between the measurement (`6bf1442`) and Step 2; Step 2 records the commit it was run against.

## Outcome

Steps 0 to 2 are done (2026-11-07). Counts below are measured.

### Commits

| Step | Commit | What |
|---|---|---|
| 0 | `5adedea` | this plan, approved as written |
| 1 | `eef75ef` | the kind pattern, the `Darwin` spelling, the comments, 16 rows; `See #64` and no closing keyword |
| 2 | the commit that adds this section | the status line and this *Outcome*; #64 closed by hand after the push |

### Red, then green

**Start from green** (`debug-macos-arm64`, Apple clang, before any edit): 588 of 588, 7.33 s of tests, nothing rebuilt.

**Red first.** The 16 rows against `HEAD`'s module: **11 rows failed**, as planned: the two `darwin` tag rows; eight of the ten digit-led
accepted rows (`9lives-linux-arm64` on `linux-arm64` and `2025-macos-arm64` on `macos-arm64` pass at `HEAD`); and the rule-3 row, whose
text said "is for macos-arm64". The other five rows passed. With the change: `test_platform_name: ok`.

### Hand-made changes

A scratch copy of the changed tree (`rsync` without `build*` and `.git`; the old text asserted to occur the stated number of times
before each edit; the module restored from a saved copy between rows; the copy deleted afterwards), each run as
`cmake -DSOURCE_DIR=<copy> -P tests/tree_checks/check_platform.cmake`. Every row is what the plan predicted.

| Change to the changed module | `test_platform_name` |
|---|---|
| control, before the rows and after the last restore | green (0 failing rows) |
| (a) the shape test (line 69) back to `[a-z0-9]+` | red, 11 rows: the ten digit-led accepted rows and the rule-3 row |
| (b) lines 71 and 73 back to `[a-z0-9]+`, line 69 as changed | green: equivalent, as argued |
| (c) all three `[a-z]+-` | red, 1 row: `gcc13-linux-x86_64` accepted |
| (d) only line 71 `[a-z]+-` | red, 1 row: `gcc13-linux-arm64` refused |
| (e) only line 73 `[a-z]+-` | red, 1 row: the phrase `is for linux-x86_64, but this host is linux-arm64` of `gcc13-linux-x86_64` |
| (f) all three `[a-z][a-z0-9]+-` | red, 1 row: `x-macos-arm64` accepted |
| (g) the system name lower-cased before it is compared with `darwin` | red, 2 rows: the two `darwin` tag rows |
| (h) no `Darwin` branch | red, 2 rows: `Darwin` with `arm64` and with `x86_64` |

### The two modules, measured again

The harness of the *Problem*, over this checkout at `eef75ef` and flywheel-dag at `6bf1442` (unchanged since the first run):

| Group | Rows | Differ before | Differ after |
|---|---|---|---|
| 40 names | 1800 | 150 (85 accepted by one copy, refused by the other) | **0** |
| 15 boundary names | 675 | 135 | **0** |
| the tag | 56 | 16 | **0** |

### Counts

| Tree | Result |
|---|---|
| `debug-macos-arm64`, after Step 1 | 588 of 588, 7.14 s of tests; nothing rebuilt (a CMake-script change); no warning from our code (two CMake deprecation notices from fetched dependencies, as before) |
| CI `CMake on a single platform` (`release-linux-x86_64`, GCC 15.2), `eef75ef`, run 42409586779 | 588 of 588, 20.22 s of tests, the job took 50 s; `test_platform_name`, `test_tree_name` and `test_presets` pass on Linux |
| CI `clang-tidy`, `eef75ef`, run 42409586785 | 95 of 95, 140.4 s of tests, the job took 3 min 26 s |

### Opened outside this plan

- [tomlin256/flywheel-dag#73](https://github.com/tomlin256/flywheel-dag/issues/73) (enhancement, priority low in the body): D1. Its table
  has no row for a digit-led kind or for `darwin`. Measured read-only at `6bf1442` on scratch copies: its own 56 cases stay green when
  the kind pattern is `[a-z0-9]+` at its three sites, and when `darwin` is matched after the lower-casing; the 16 rows of this plan,
  written in its macros and added to a copy of its table, hold against its module as it is (72 cases) and fail 9 and 2 rows against the
  two changes. Nothing in its checkout was edited.

### What differed from the plan

- **The ten digit-led accepted rows extend the existing loop** over the names that are not platform trees (its comment and its list of
  names) instead of a loop of their own. Two existing lines are edited; no row is removed.
- **D1 was measured more strictly than planned.** The plan relied on the verdict comparison to say flywheel-dag's module passes the
  rows; the rows were also run in flywheel-dag's own macros against its module, and its table was shown not to notice the two changes.

*Left for later:* a run of the Linux tree in `ubuntu-dev`; CI's Linux run above covers the three tests that read the module.
