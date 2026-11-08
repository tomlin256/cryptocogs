# CLAUDE.md holds rules and traps, not reference: 162 KB to 20 KB, with a size test

**Status:** Done (2026-11-06). Rob approved the plan as written, so D1 to D5 stood at the defaults below. The steps and their results are
in the *Outcome*.

Closes [cryptocogs#60](https://github.com/tomlin256/cryptocogs/issues/60).

**Follows:** [plan curl-post-sent-once](curl-post-sent-once.md), which landed while this was prepared: its rule about the connection of
a POST or a DELETE is carried into the new file.
**Not here:** the other stale files (the README's test paragraph and dependency table, [cryptocogs#31](https://github.com/tomlin256/cryptocogs/issues/31)
and [cryptocogs#35](https://github.com/tomlin256/cryptocogs/issues/35)) and the two bugs the old text got wrong
([cryptocogs#21](https://github.com/tomlin256/cryptocogs/issues/21), [cryptocogs#42](https://github.com/tomlin256/cryptocogs/issues/42)).

## Problem

`CLAUDE.md` was 161,709 bytes (1,730 lines) and Claude Code loads the root one in full at the start of every session in this repo. It
grew from 94 KB to 157 KB between 2026-11-04 and 2026-11-06, because the docs step of nearly every plan appended to it (51 commits touch
it). What it held, from a read of the 157 KB version against the tree:

- About 61 KB of venue API, shared-type and architecture reference (Kraken, Binance, Coinbase and Crypto.com endpoint tables, signing,
  the WebSocket layer, "adding a ..." how-tos). Every venue quirk in it is also written in the comment block at the top of the matching
  `include/exchange/*/*.hpp`, with the code; the tables copy `rest_api.hpp`, `ws_api.hpp` and the README.
- About 42 KB of file tree, build-output and test-executable tables, a per-test breakdown, counts and timings. `git ls-files`, `ctest -N`
  and the header of each `tests/*_checks/*.cmake` script give them, and every plan's docs step had to rewrite the counts.
- About 34 KB on the build, sanitizer, clang-tidy, warning and CI mechanism, written out beside the CMake modules and workflows that
  implement it, with run ids, timings and finding counts; and 13 KB of coding conventions, a quarter of them the `CurlHttpClient`
  options that `http_client.cpp` already explains.
- Sixteen statements that were stale or false. For example: "the install prefix is cryptocogs-only in any config" (`backward.hpp` is
  installed, [cryptocogs#21](https://github.com/tomlin256/cryptocogs/issues/21)); "`test_ws_reconnect_session` is deterministic, no
  sleeps", said four times ([cryptocogs#42](https://github.com/tomlin256/cryptocogs/issues/42)); the Kraken auth section (`KrakenAuth`
  exists and the `detail::` helpers are in the public header); a file tree that left out 8 of 11 `.inl` files; "seven headers include the
  C++23 guard" (eight do); "any of the three libraries" (six). A copy of the code is not a safe single source.

## The change

**1. `CLAUDE.md`** keeps rules, commands and traps, 20,114 bytes (126 lines):

- the layout in a few lines, the scope facts no header states (Coinbase and Crypto.com place orders over REST only, FIX is deferred, the
  Coinbase feed has no correlation id, Kraken has two frame classifiers), and the instruction to read the comment block at the top of an
  adapter header before editing it;
- build, test, options, install and build trees; sanitizers, clang-tidy, compiler warnings and CI, each with its commands, its test
  names and the rules a test enforces on an edit;
- the test rules (no network, no sleeps, `ASSERT_HAS_VALUE`, no allocation at namespace scope, an example's `main()`), the coding
  conventions that are not in a header, and the `CurlHttpClient` rule of plan curl-post-sent-once;
- a *Releasing* section (the version-bump commit, the annotated tag, the GitHub Release), a sentence saying the repository stands alone,
  and *Keeping this file short*, which says what belongs here and names the size test.

What was deleted is derivable from a place that stays and is named: the venue sections from the header comment blocks and the README; the
tree, tables and counts from `git ls-files`, `ctest -N` and the check scripts' headers; the CI cost and run-id history from the workflow
comments and the fourteen plans from ci-cache to curl-post-sent-once (in the order of the table in unnumbered-plan-docs); the `CurlHttpClient`, `ASSERT_HAS_VALUE`, `CallbackProbe` and `MockWsConnection` prose from `http_client.cpp`,
`http_client.hpp`, `assert_has_value.hpp`, `callback_probe.hpp` and `mock_ws_connection.hpp`.

**2. `docs/dev-recipes.md`** (new, not loaded automatically): the GCC 15 container, the three compiler differences the tree has met, and the
proof that a test catches a leak. The leak recipe runs a script with `docker exec -i ubuntu-dev bash -s < script`, and puts the files in
through stdin, so no `docker cp` leaves a root-owned file in the sticky `/tmp` of the container.

**3. `README.md`**: the sentence that sent readers to `CLAUDE.md#binance-adapter-reference` points at the headers and at
`docs/plans/appendix-binance-message-formats.md`; the sentence "`CLAUDE.md` says what each [clang-tidy test] proves" points at
`cmake/cryptocogs_clang_tidy.cmake`, whose header does.

**4. `docs/agent-add-exchange.md`**, which the new `CLAUDE.md` points at for a new venue, is corrected where it was wrong: four adapters
and not two; two common libraries and one peer per adapter; the crypto helpers and the WebSocket factories are declared in the header and
defined in `src/<name>/` since plan declarative-headers; the link line includes `exchange_http`. The checklist rows that said "header-only" follow.

**5. `docs/plans.md`** is deleted. It stopped at plan remaining-adapters-reserve-vector-growth and said the opposite of the convention since
then, that the issue is the index. No file asks for it any more: only the twenty-six plans from step-6-binance-rest-private to cmake4-cxx23
(the order is in the table of unnumbered-plan-docs), which are history, mention it.

**6. `test_claude_md_budget`** and **`test_claude_md_budget_fixtures`**, `tests/doc_checks/`, registered at the top level in
`tests/CMakeLists.txt`:

- `check_claude_md_budget.cmake` fails when the root `CLAUDE.md` is over 21000 bytes, when any `CLAUDE.md` in the checkout is over 38000
  characters, or when there is no root `CLAUDE.md`. 21000 is the file plus room for one rule; Claude Code keeps only the start of an
  on-demand `CLAUDE.md` past 40000 characters, and 38000 leaves a margin. The root is counted in bytes (what it costs) and every file in
  characters (what the cut-off counts): an em dash is three bytes and one character, and `string(LENGTH)` counts bytes, so the script
  reads the file as hex and counts the bytes that are not UTF-8 continuation bytes. Build trees, `.git`, `.cache` and `.claude` are skipped.
- `check_claude_md_budget_fixtures.cmake` makes small checkouts at run time and runs the check over each in a child run: both limits
  exactly (accepted) and one over (refused, naming the file, its size and the limit), multi-byte text that is over in bytes and under in
  characters (accepted nested, refused as the root), decoys in a build tree, `.git`, `.cache`, `.claude` and a CLion tree (ignored), and a
  checkout with no root file (refused). The fixtures are not kept in the tree: a 21 KB file named `CLAUDE.md` would be one more file for
  Claude Code to load.

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 How hard to cut.** (1) Delete what a header comment, a CMake or script header, a workflow comment, a test header or a plan already
  says, and name where each block survives. (2) Keep compressed copies of the mechanism. *Default:* 1. With 1 the header comments are the
  only home of what was deleted, which is why the new file says to read them and why a plan's docs step now updates the comment, not this file.
- **D2 Where venue knowledge lives.** (1) In the header comment blocks, with a pointer. (2) A file per venue under `docs/`. (3) A nested
  `include/exchange/<venue>/CLAUDE.md`. *Default:* 1, because the quirks are all there (checked venue by venue). 3 is out:
  `install(DIRECTORY include/exchange/<venue> …)` has no `FILES_MATCHING`, so the file would ship in every install prefix.
- **D3 A size test.** (1) `test_claude_md_budget` with the limits above and a meta-test. (2) None, and rely on the rule. *Default:* 1; the
  file grew 63 KB in two days under a rule that nobody broke on purpose.
- **D4 `CLAUDE.md` stays in the `paths-ignore` lists.** (1) Yes: a push that changes only documentation starts no run, as before. The cost is
  that a push that changes only `CLAUDE.md` skips `test_claude_md_budget`; the next run finds a file over its limit, and the ctest of every
  checkpoint finds it sooner. (2) Take it out of the four lists: the test then runs for every edit, and every edit starts a build.
  *Default:* 1. Edits will be rare now that a plan's docs step does not touch the file.
- **D5 Delete `docs/plans.md`.** *Default:* yes.

## Steps

Each step ends with a build and the whole suite.

- **Step 0 — Approval.** [cryptocogs#60](https://github.com/tomlin256/cryptocogs/issues/60) and this plan. Done when the plan is approved.
- **Step 1 — The test, red then green.** `check_claude_md_budget_fixtures.cmake` first, run with no check script (red: it names the missing
  file); then `check_claude_md_budget.cmake` (green); then the real check on the old `CLAUDE.md` (red: 161,709 bytes). *Done when* the
  fixtures pass, the real check fails on the old file, and a hand-made change to the check script fails the fixtures.
- **Step 2 — The files.** `CLAUDE.md`, `docs/dev-recipes.md`, `README.md`, `docs/agent-add-exchange.md`, `docs/plans.md`, and the two tests
  registered. *Done when* the real check passes and a search of every changed file for the names of other projects (the stands-alone rule) finds nothing.
- **Step 3 — Whole suite.** `cmake --build --preset debug-macos-arm64` and `ctest --preset debug-macos-arm64`; the two scripts in the Linux
  container; `test_ci_paths` with the new scripts in the tree. *Done when* all of it passes.
- **Step 4 — Push.** `README.md` and `tests/CMakeLists.txt` are not in the ignore lists, so the push starts the workflows; the dependency
  cache key hashes `tests/CMakeLists.txt`, so that run is cold once.

## Designs I rejected

- **A check that names `CLAUDE.md`** (`${SOURCE_DIR}/CLAUDE.md`). `test_ci_paths` refuses an ignored path that a check reads by name, so it
  would force `CLAUDE.md` out of the ignore lists (D4, option 2). The check finds `CLAUDE.md` files by their name wherever they are, as
  `test_cmake_floor` finds `CMakeLists.txt`, and its header says what that costs.
- **Counting bytes for every file.** The cut-off counts characters; bytes would refuse a file that Claude Code loads whole.
- **Committed fixtures.** See above: size, and a file named `CLAUDE.md` is loaded.
- **Compressed copies of the venue sections in a `docs/` file.** They would be a second copy of the headers to keep in step, and the headers
  are what a session reads when it edits the code.

## Self-review — risks and assumptions

- **Information loss.** The cuts are deep. Each deleted block names the place it survives; those places were opened before the block went.
  The old file stays in `git`. A header comment that is wrong is now a wrong source of truth: the ones the new text relies on were read
  against the code (the venue headers, `http_client.hpp`/`.cpp`, `ix_ws_connection.hpp`, `heartbeat_connection.hpp`, the test helpers).
- **A push that changes only `CLAUDE.md` skips the budget test** (D4). Each plan's checkpoint runs the whole suite locally, which runs it.
- **The dependency cache.** `tests/CMakeLists.txt` is in the key of the `deps-` cache, so the first run after this change configures cold
  (about half a minute more).
- **Links.** `docs/plans/multi-exchange-abstraction.md` links two anchors that no longer exist (`#websocket-message-dispatch-low-level`,
  `#namespace-layout`). It is history and is left as it is.
- **Not touched, and still stale.** Most line numbers cited in `docs/agent-add-exchange.md` §2 drifted (the citations of `auth.hpp:91`,
  `rest.hpp:43` and `auth.hpp:66` were removed with the edit; `rest.hpp:32` and `:57`, `rest_api.hpp:55` and `:119` remain and point at the
  wrong lines); its §4 still mentions a compat shim that plan remove-compat-shim removed. Neither is wrong about structure; both are in one issue.
- **Assumption.** The 40000-character cut-off for an on-demand `CLAUDE.md` is as Claude Code's memory documentation states it; the root
  file is loaded whole, and its 21000-byte budget is a choice, not a cap.

## Outcome

### Bytes

| File | Before | After |
|---|---|---|
| `CLAUDE.md` | 161,709 | 20,114 |
| `docs/dev-recipes.md` | — | 4,551 |
| `README.md` | 26,898 | 27,184 |
| `docs/agent-add-exchange.md` | 17,339 | 18,682 |
| `docs/plans.md` | 3,731 | deleted |
| `tests/CMakeLists.txt` | 25,881 | 27,321 |
| `tests/doc_checks/check_claude_md_budget.cmake` | — | 5,403 |
| `tests/doc_checks/check_claude_md_budget_fixtures.cmake` | — | 7,120 |

### Red, then green

- With no check script, `check_claude_md_budget_fixtures.cmake` stopped: `found no …/check_claude_md_budget.cmake`. With the script it
  passes (0.2 s).
- The real check on the old file failed: `CLAUDE.md is 159987 characters; a CLAUDE.md is held to 38000` and `161709 bytes; the root
  CLAUDE.md is held to 21000`. On the new one it passes: 20,114 bytes, 20,090 characters.

### Hand-made changes

Twenty-six changes to a scratch copy of the check script, each of which the fixtures must fail on, and did: the root limit and the file
limit one over and one under; `>` made `>=` for either; the nested limit counted in bytes and the root in characters; characters taken as
bytes; the continuation-byte class cut to `[89a]`, `[9ab]`, `[8ab]`, `[89b]` and widened to `[89abc]` (the fixtures' four-character unit
has a continuation byte with every high nibble from 8 to b for this); each of `build*`, `cmake-build-*`, `.git`, `.cache` and `.claude`
no longer skipped; the no-root check removed; the root never found; `GLOB_RECURSE` made `GLOB`; the problems never reported; and three
messages without the size or the limit. One change first survived (a per-file report without the byte count: the headline matched the
words the case expected), so the accept cases now look for the per-file line.

### Whole suite

`ctest --preset debug-macos-arm64`: 588 of 588 passed (586, and the two new tests); `test_ci_paths`, `test_cmake_floor` and
`test_ci_cache_key` among them. Both new scripts also pass in `ubuntu-dev` (Linux aarch64, CMake 4.4.5).

### The leak recipe

Dry-run in `ubuntu-dev`: a copy of `HEAD` through `git archive | docker exec -i … tar -x` (all files owned by the container user), an
old file put over a new one through `tee`, then the script through `docker exec -i ubuntu-dev bash -s`: an offline configure of the
`asan-linux-arm64` preset with the seven `FETCHCONTENT_SOURCE_DIR_*` options, a build of `test_tick_price`, and a run with
`ASAN_OPTIONS=detect_leaks=1` (clean). The step that needs an old, leaking source was not run; the recipe is the one plan
curl-header-list-append-failure used with `docker cp` replaced.

### What differed from the plan

The root is 20,114 bytes, not the 19,258 of the first draft: *Releasing*, the plan curl-post-sent-once rule, the "stands alone" sentence and *Keeping
this file short* are in it, and the guard paragraph, the clang-tidy test list and the CI notes were cut to make room. The naming rule for
build trees is written out, because the repository is public and a reader of it may not have the guide that states it. *Releasing* follows the bump
commit of v2.1.0, which landed meanwhile: it changes `project(CRYPTOCOGS VERSION …)` in `CMakeLists.txt` and the `GIT_TAG` example in
`README.md`.

The commits are those whose subject ends `(plan 044, step N)`.
