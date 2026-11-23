# `CurlHttpClient::perform`: a test holds that a refused `CURLOPT_PREREQFUNCTION` or `CURLOPT_PREREQDATA` ends the call before anything is sent

**Status:** Steps 0 to 2 done (2026-11-23). Rob approved the plan as written, so D1 stood at its default (no release). Step 3, after
the push, is recorded in the *Outcome* once it has run.

Closes [#70](https://github.com/tomlin256/cryptocogs/issues/70).

**Follows:** [plan curl-post-connection-reuse](curl-post-connection-reuse.md) (#59, #68), which added `detail::require_option` in v2.2.0
and recorded that the call to it from `perform` is "read, not run".
**Not here:** an HTTP/2 server in the test tree ([#69](https://github.com/tomlin256/cryptocogs/issues/69)); the words of the other
failures that follow a sent request ([#57](https://github.com/tomlin256/cryptocogs/issues/57)); a release (D1).

## Problem

Since v2.2.0 a POST or a DELETE is held to one write by a pre-request callback: `perform` sets `CURLOPT_PREREQFUNCTION` and
`CURLOPT_PREREQDATA` before every such call. A libcurl that is older at run time than the one the library was built with answers
`CURLE_UNKNOWN_OPTION`. `perform` hands both answers to `detail::require_option`, which throws before anything is sent, with a text
that names the option and both libcurls. Without that check nothing would hold the request to one write, and nothing would say so:
an order could reach the exchange twice.

`require_option` has two tests of its own (`CurlHttpClientOptions`). The call from `perform` has none. No libcurl that has an option
refuses it, so no test can make `perform` meet a refusal, and the plan of v2.2.0 records the call as "read, not run". A change that
dropped the check from `perform` should therefore leave every test green; Step 1 begins by showing it. A defect that a known design
can close is closed and not parked as a known gap: this plan is that design, with its cost.

### What I checked (2026-11-23, `8f28558`)

A scratch harness, not in the repo: Apple clang 21.0.0, libcurl 8.7.1, `-std=c++23 -O2 -Wall -Wextra -Wpedantic`, no warning. It
makes the two `curl_easy_setopt` calls that `perform` makes for a POST or a DELETE, each answer compared with `CURLE_OK`, directly
and through a parameter of the type `CURLcode (*)(CURL*, CURLoption, ...)` that the optimiser cannot see through (a volatile
pointer, a function that is not inlined). Nine alternating rounds of 3 000 000 pairs; nanoseconds per pair of calls, median and range:

| | per pair |
|---|---|
| directly, as `perform` does | 5.37 (5.32 to 8.03) |
| through the pointer | 5.02 (4.93 to 6.71) |
| extra | −0.36 |

No cost shows: the difference is inside the spread. In `perform` the compiler can inline the function and make the call direct, so
this is the worst case. The call it sits beside takes milliseconds (23 ms for a request on a reused connection to a Kraken host from
this desk: table 1 of plan curl-post-connection-reuse). `&curl_easy_setopt` converted to the pointer type with no cast and no warning.

## The change

**1. `include/exchange/common/http_client.hpp`**, in `detail::` after `require_option`:
- `struct SendOnce { int writes{0}; };`, moved from `http_client.cpp` with its comment.
- `using SetoptFn = CURLcode (*)(CURL*, CURLoption, ...);`: the type of `curl_easy_setopt`.
- `void hold_to_one_write(CURL* curl, SendOnce& once, SetoptFn setopt);`: sets `CURLOPT_PREREQFUNCTION` (the callback that stays in
  `http_client.cpp`) and `CURLOPT_PREREQDATA` (`once`) through `setopt` and hands each answer to `require_option`, so it throws
  `std::runtime_error` before anything is sent. Not part of the supported surface, like its neighbours.
- The comment of `require_option` says what is true then: `perform` reaches it through `hold_to_one_write`, and a test hands in a
  refusal because no libcurl that has an option refuses it.

**2. `src/exchange/common/http_client.cpp`:** `hold_to_one_write` is defined in `detail` after `require_option`. In `perform` the two
`require_option` lines become `detail::hold_to_one_write(curl, once, &curl_easy_setopt);` for a POST or a DELETE, and the comment
above them is cut to match.

**3. `tests/unit/test_http_client.cpp`:** three tests in `CurlHttpClientOptions`, each with plain functions of the type `SetoptFn`
that stand for a libcurl:
1. one that refuses `CURLOPT_PREREQFUNCTION`: `hold_to_one_write` throws `std::runtime_error`, the text names
   `CURLOPT_PREREQFUNCTION` and says that the request was not sent;
2. one that refuses `CURLOPT_PREREQDATA`: the same, naming `CURLOPT_PREREQDATA`;
3. one that takes both: nothing is thrown; each option is offered once, in either order; the data offered is the `SendOnce` passed
   in; and the callback offered, called with that data, lets the first write through (`CURL_PREREQFUNC_OK`) and ends the second
   (`CURL_PREREQFUNC_ABORT`). The fake keeps what it was offered in a plain struct, reset at the start of the test (an allocating
   object at namespace scope is `bugprone-throwing-static-initialization`).

**4. Unchanged:** the six connection tests of `CurlHttpClientPerform`, `CMakeLists.txt`, `CLAUDE.md` (its paragraph says what `perform`
does, and that does not change) and the README. No class gains a member: nothing that a caller can see changes.

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1. No release.** The public header gains three `detail::` declarations, which are not supported surface, and no class changes
  layout. *Default:* they ship with the next release. *Alternative:* v2.2.1 after Step 2.

## Steps

Commits go straight to `main`. No commit message carries a closing keyword: #70 is closed by hand in Step 2. Counts are measured in
Step 1 and recorded in the *Outcome*.

### Step 0 — Approval

You approve or edit. Then: commit this plan (`docs(plan): curl-prereq-refusal-test approved as written`, status line changed),
push (documentation only: no CI run), and comment on #70 with the plan's URL (the text audited with `check_public_leaks.py --tree`
first). *Done when:* #70 and the plan link each other.

### Step 1 — The seam: tests red, then green

*Start from green.* `debug-macos-arm64` with the clang-tidy gate on: build and full ctest, the count recorded. Anything red stops
the plan there.

*The gap, shown.* In a scratch copy (`rsync -a --exclude 'build*' --exclude .git`, `df -h /` first, deleted once recorded): the two
`require_option` wrappers of `perform` removed, so that the answers are ignored. Expected: `test_http_client` green. That is the
problem, measured.

*Tests:* 1 to 3 above.

*Red first:* `hold_to_one_write` without its two `require_option` calls (not committed). Expected: 1 and 2 fail ("no exception"),
3 passes.

*Change:* items 1 and 2 of *The change*, comments included, so that no commit leaves a false one.

*Hand-made changes* (the scratch copy of the changed tree; the old text asserted to occur once before each edit; the copy diffed
against the tree after; each test alone under a 10 s alarm; an unmutated control kept). A cell says red when at least one test of
its column fails; the run records which. Expected:

| Change | 1 | 2 | 3 | POST and DELETE connection tests | GET connection tests |
|---|---|---|---|---|---|
| control: unmutated | green | green | green | green | green |
| (a) the `CURLOPT_PREREQFUNCTION` answer ignored | red | green | green | green | green |
| (b) the `CURLOPT_PREREQDATA` answer ignored | green | red | green | green | green |
| (c) the two option names swapped in the calls to `require_option` | red | red | green | green | green |
| (d) `nullptr` offered as the data | green | green | red | red (the callback reads it) | green |
| (e) `CURLOPT_PREREQFUNCTION` not offered | red | green | red | red | green |
| (f) `CURLOPT_PREREQDATA` not offered | green | red | red | red | green |
| (g) two writes allowed (`writes > 2`) | green | green | red | red | green |
| (h) `perform` does not call `hold_to_one_write` | green | green | green | red | green |
| (i) `perform` calls it for a GET too | green | green | green | green | red |

*Repeated:* each new test 200 times (`ctest --repeat until-fail:200`).

*Checkpoint:* full build and ctest of `debug-macos-arm64` with the gate on, no warning; in `ubuntu-dev`, `debug-linux-arm64` built and
run in full and `release-`, `asan-` and `tsan-linux-arm64` built (GCC 13.3); the three configurations built with GCC 15.2 in the
throwaway container of `docs/dev-recipes.md`. Commit. *Done when:* the gap shown, red as stated, then green, the table as stated, the
repeats clean, and every build and run above green.

### Step 2 — Record, close

The "read, not run" bullet of plan curl-post-connection-reuse (*What differed from the plan*) gets one sentence and a link to this
plan. The harness again, with the committed `hold_to_one_write` in place of its stand-in: expected inside the spread of the direct
pair. This plan's *Outcome* (commits, the red-then-green record, the table as measured, the figures). Full ctest (`test_plan_docs`
reads the plans). Commit, `git fetch`, and one push for Steps 1 and 2, so that the two workflows run once. A comment on #70 (audited)
that names the commits and the figures, and #70 closed by hand (`completed`). *Done when:* pushed, #70 closed with its comment, and the
*Outcome* holds the measured figures.

### Step 3 — After the push

Not blocking. `asan-macos-arm64` and `tsan-macos-arm64`, full suites, with the options in `CLAUDE.md`. The push's two CI runs
(`release-linux-x86_64`, `clang-tidy`) are read once they have finished, not waited for or dispatched. *Done when* the results are in the
*Outcome*; a finding gets its own issue.

## Designs I rejected

- **A real libcurl older than 7.80.0 at run time:** the test binary run against one (`LD_LIBRARY_PATH`). It needs an old libcurl built
  and kept and a leg of CI for it, to hold the same few lines that the seam holds for one function-pointer argument.
- **A replacement `curl_easy_setopt` linked into the test binary.** The function is variadic: a wrapper has to forward a `...` it
  cannot read, so it guesses each option's argument type, and symbol interposition differs between Mach-O and ELF.
- **A setter held by `CurlHttpClient`** (an interface, a pointer or a template parameter): it changes the layout of a public type, and
  a layout change is a major release.
- **`perform` as a template over the setter**, every `curl_easy_setopt` of the function going through it: the body moves to an `.inl`
  for the two calls that can be refused.
- **Leave it as "read, not run".** A defect that a known design can close is closed.

## Self-review — risks and assumptions

- **The seam is `curl_easy_setopt`'s own type.** It converted without a cast or a warning on Apple clang 21 with libcurl 8.7.1. GCC
  13.3 and 15.2 and libcurl 8.5.0 and 8.18.0 are known only from Step 1's checkpoint. If a header declares the function differently
  there, the fall-back is a one-line function of the same type that calls it, and nothing else changes.
- **Cost.** Measured above for the worst case (no inlining): none that shows.
- **A test pins what it needs and no more.** The order of the two options is not pinned, so offering the data first stays a free
  refactor; that the callback is armed at all is held by the connection tests.
- **File-scope state in test 3.** A plain struct reset at the start of the test; gtest runs one test of an executable at a time. If
  tests ever ran in parallel inside one process it would have to be `thread_local`.
- **C-style variadics in the tests.** `.clang-tidy` has `bugprone-*`, `performance-*` and `cppcoreguidelines-pro-type-member-init`, and
  no check of variadic functions; the gate in Step 1 settles it. A `NOLINT` would need Rob's OK.
- **What stays read, not run:** that `perform` passes `curl_easy_setopt` itself, and only for a POST or a DELETE. (h) and (i) show that
  the tests notice the second half. For the first, the library has no other function of that type, and the only way to run it is one of
  the designs above.
- **The other options.** `perform` does not read the answers to its other `curl_easy_setopt` calls. In `curl.h` their numbers are 156 or
  less and the two pre-request options' are 312 and 313: those two are the newest of the options `perform` sets, because libcurl numbers
  a new option after the last one. Not examined further here.
- **A mutant that survives gets a test in this plan,** not a note: if (h) or (i) is green in its column, the connection tests do not
  hold what plan curl-post-connection-reuse says they hold.

## Outcome

Steps 0 to 2 are done (2026-11-23). Counts and figures are measured. Step 3 has not run yet.

### Commits

| Step | Commit |
|---|---|
| 0 | `367e387` `docs(plan): curl-prereq-refusal-test approved as written`; pushed, and #70 commented with the plan's URL |
| 1 | `6aad06c` `test: hold_to_one_write is handed a libcurl that refuses a pre-request option` |
| 2 | the commit that holds this record |

### Start from green, and the gap shown

`debug-macos-arm64`, Apple clang 21.0.0, libcurl 8.7.1, the clang-tidy gate on, at `367e387`: built with no warning, **683 of 683**.

The gap: a scratch copy of `367e387` (298 files, an offline tree named `scratch`) in which the two `require_option` wrappers of `perform`
were removed, so that the answers are ignored. 42 objects built, no warning. `test_http_client`: **22 of 22 green**, and the options
test and the three POST and DELETE connection tests green when run alone. Nothing in the suite notices: the problem of #70, measured.

### Red, then green

The seam with the two `require_option` calls left out of `hold_to_one_write` (not committed): built with no warning; tests 1 and 2
**fail** (`<nothing thrown>`) and test 3 passes. With the two calls: all three pass.

### Hand-made changes, as measured

A scratch copy of the changed tree. The old text of each change was asserted to occur once, the copy was diffed against the tree, and
the objects of the two units and the test binary were deleted before each build (two objects built each time, no warning). Each test
ran alone under a 10 s watchdog. A cell says red when at least one test of its column fails.

| Change | 1 | 2 | 3 | POST and DELETE connection tests (4) | GET connection tests (2) |
|---|---|---|---|---|---|
| control: unmutated | green | green | green | green | green |
| (a) the `CURLOPT_PREREQFUNCTION` answer ignored | **red** | green | green | green | green |
| (b) the `CURLOPT_PREREQDATA` answer ignored | green | **red** | green | green | green |
| (c) the two option names swapped in the calls to `require_option` | **red** | **red** | green | green | green |
| (d) `nullptr` offered as the data | green | green | green | **red** (4 crash: the callback reads the counter) | green |
| (e) `CURLOPT_PREREQFUNCTION` not offered | **red** | green | **red** | **red** (3) | green |
| (f) `CURLOPT_PREREQDATA` not offered | green | **red** | **red** | **red** (4 crash) | green |
| (g) two writes allowed (`writes > 2`) | green | green | green | **red** (3) | green |
| (h) `perform` does not call `hold_to_one_write` | green | green | green | **red** (3) | green |
| (i) `perform` calls it for a GET too | green | green | green | green | **red** (`AGetOnAUsedConnectionThatClosesIsSentAgain`) |
| (j) the handle is not passed on (`nullptr`) | green | green | **red** | **red** (4) | green |

Every change is killed by at least one test. Two cells differ from the plan: (d) and (g) are green in test 3 (see *What differed from
the plan*) and red in the connection tests. `AMixOfMethodsSharesOneConnection` is the one POST and DELETE test that stays green in
(e), (g) and (h), as test 5 did in plan curl-post-connection-reuse: nothing in it closes a connection.

*Repeated:* each of the three new tests 200 times (`ctest --repeat until-fail:200`) on the Mac: clean.

### Checkpoint

| Tree | Result |
|---|---|
| `debug-macos-arm64`, Apple clang 21.0.0, libcurl 8.7.1, the gate on | 16 objects built, no warning; **686 of 686** (683 before: three tests are new) |
| `debug-linux-arm64` in `ubuntu-dev`, GCC 13.3, libcurl 8.5.0 | 18 objects built, no warning; **598 of 598** |
| `release-`, `asan-` and `tsan-linux-arm64` in `ubuntu-dev` | built with no warning (18 objects each); in the asan tree the 25 `CurlHttpClient` tests pass with CI's options, `detect_leaks=1` included |
| the three configurations with GCC 15.2.0 (a throwaway `ubuntu:26.04`, CMake 4.2.3, libcurl 8.18.0) | built with no warning and no error (132, 134 and 133 objects); the 25 `CurlHttpClient` tests pass in the Release tree |

After Step 2's edits the gate is off again in `build/debug-macos-arm64`, as it was found: full ctest **598 of 598**, `test_plan_docs` and
`test_claude_md_budget` included.

### The cost, measured again over the committed function

The harness of *What I checked* with `hold_to_one_write` as committed, called from another translation unit so that the call cannot be
made direct, against the two `curl_easy_setopt` calls with a call of `require_option` for each (what `perform` made before). Apple clang
21.0.0, `-O2`, libcurl 8.7.1, nine alternating rounds of 3 000 000 pairs, ns per pair, median (range):

| | per pair |
|---|---|
| directly | 6.69 (6.68 to 9.17) |
| through the pointer, `hold_to_one_write` | 6.45 (6.44 to 7.72) |
| extra | −0.24 |

The pointer is not slower. The medians differ by 0.24 ns and the ranges overlap, so the sign is not a gain either. Against the 23 ms
of a request on a reused connection (plan curl-post-connection-reuse, table 1) it is nothing.

### What differed from the plan

- **Test 3 reads no value.** The plan had it check that the data offered is the `SendOnce` passed in, and that the callback offered,
  called with it, lets one write through and ends the second. Reading the value of an option needs `va_start` on the parameter of type
  `CURLoption`, and clang's `-Wvarargs` (on, with `-Werror`) refuses it: an enumeration is not the type that its promotion gives. A
  warning is fixed and not silenced, so test 3 counts what was offered and for which handle. (d) and (g) are therefore green in test 3
  and red in the connection tests, which run the real callback and the real counter over a real libcurl. No change escapes.
- **The three tests are in `CurlHttpClientPerform`,** not `CurlHttpClientOptions`: they use its fixture, which initialises libcurl
  before the first handle and cleans up after the last, as `CLAUDE.md` says. A `TEST` and a `TEST_F` cannot share a suite.
- **One change more, (j),** and the last column of the table is split in two (the four POST and DELETE tests, the two GET tests).
- **The header comment of `test_http_client.cpp` changed in Step 1,** with the tests it describes.

No issue was opened outside this plan.
