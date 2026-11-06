# Plan 041 — `CurlHttpClient::perform` starts every call from a reset handle

**Status:** Steps 0 to 2 are done (2026-11-06; see the *Outcome*), and the fix is released in
[v2.1.0](https://github.com/tomlin256/cryptocogs/releases/tag/v2.1.0) (2026-11-06) with plans 038, 042 and 043; the record is in plan 043,
Step 4. Rob approved the plan as written, so D1 to D5 stood at the defaults below (no release). Step 3, the sanitizer trees and Linux, was
never written up here: the sanitizer runs of plans 042 and 043 are on code that includes this plan's reset and are clean, and the run
against the old `perform` in `ubuntu-dev` was not done.

Closes [#51](https://github.com/tomlin256/cryptocogs/issues/51).

**Follows:** [plan 038](038-curl-header-list-append-failure.md), which found #51, kept it out of its own change on purpose, and added the
loopback harness (`tests/unit/test_http_client.cpp`) that this plan extends.
**Raised along the way:** [#56](https://github.com/tomlin256/cryptocogs/issues/56) (bug, priority medium): `perform` sets no timeout, so a
server that accepts a connection and never replies blocks the caller forever. Reproduced with the scratch program below (still blocked
after 6 s). Its fix has to set its options after this plan's reset; the issue says so.
**Not here:** #56; a connection-reuse test (D4); a release (D3), which I recommend.

## Problem

`CurlHttpClient` owns one libcurl easy handle and every `perform` reuses it. libcurl options stay set until they are changed or the handle
is reset, and `CURLOPT_HTTPHEADER` and `CURLOPT_POSTFIELDS` are stored as pointers: libcurl copies neither. `perform` sets the first only
when the request has headers and the second only for a POST, or a DELETE with a body, and clears neither (it does clear
`CURLOPT_CUSTOMREQUEST`, for this very reason). So the next call inherits a pointer to memory that `perform` has given back (the header
list, freed as `perform` leaves) or that belonged to a request that no longer exists (the body).

It needs no allocation failure, and the earlier call need not have succeeded: **a call that throws leaves the same state**, so a network
error on a private call followed by a public one is the same bug.

### What I checked (2026-11-06, `c4780f2`; macOS arm64, Apple clang 21.0.0, libcurl 8.7.1)

A scratch program, not in the repo: a raw-socket HTTP/1.1 keep-alive server on 127.0.0.1 that records the bytes of every request, and
`src/exchange/common/http_client.cpp` of `HEAD` compiled into it. Each row is one client and two calls. "Bad URL" is a path with a control
character: libcurl refuses it (`URL using bad/illegal format`), so `perform` throws and nothing is sent.

| Sequence | Today | With `curl_easy_reset(curl);` first in `perform` |
|---|---|---|
| GET with `X-Secret`, then GET with no headers | SIGSEGV in `curl_strnequal` ← `Curl_http_host` (exit 139; ASan: SEGV, exit 134) | the second request has no `X-Secret` |
| POST `symbol=X&secret=1` with `X-First`, then DELETE with no body and `X-Second` | the DELETE goes out with `Content-Length: 17`, `Content-Type: application/x-www-form-urlencoded` and **the POST's body** (the buffer survived here; ASan: `stack-use-after-scope` in `cr_buf_read`) | `X-Second` only: no body, no `Content-Length`, no `Content-Type` |
| POST with a bad URL (throws), then a DELETE with no body and `X-Second` | the DELETE re-sends the POST's body | `X-Second` only |
| GET with `X-Secret` and a bad URL (throws), then a bare GET | SIGSEGV | clean |
| POST then GET; DELETE with a body then GET | a plain GET both times | a plain GET both times, with the `CUSTOMREQUEST` clearing line gone |

- **Connection reuse survives the reset.** GET, POST, GET, DELETE on one client against the keep-alive server: the server accepted **1**
  connection for the 4 requests. libcurl documents it: the reset keeps live connections, the TLS session cache, the DNS cache, cookies,
  shares and the alt-svc cache.
- **Nothing relies on sticky state.** `perform` sets nine options on every call (`URL`, `HTTPHEADER`, `CUSTOMREQUEST`, `HTTPGET`, `POST`,
  `POSTFIELDS`, `POSTFIELDSIZE`, `WRITEFUNCTION`, `WRITEDATA`), the constructor sets none, and `http_client.cpp` is the only file in `src/`
  and `include/` that uses the handle. After a reset the handle is what `curl_easy_init` made.
- **Every release has it.** `CurlHttpClient` is in `v0.1.0` with the same conditional `HTTPHEADER` and `POSTFIELDS` and no reset, and in
  every tag since; `v2.0.0` is the latest.
- **#56, same program:** a server that accepts, reads the request and never replies. `perform` on a plain GET was still blocked when a
  6 s alarm fired (exit 142). `perform` sets no timeout of any kind.
- **`ix::HttpServer`, which the loopback tests use, closes every connection after its reply and sends no `Connection: close`**
  (`Http::sendResponse` writes `Content-Length` and the headers it is given). A second request on one client then makes libcurl try a
  connection that is closed, or notice that it is; timing decides, and libcurl recovers either way. The tests ask for `Connection: close`
  so there is no timing in them. Its connection limit (128) counts live connections and its garbage collector wakes on each close, so the
  sequential requests of the tests do not reach it.
- **ix reads a request body for any method that sends `Content-Length`**, so a DELETE with a body arrives whole.
- **Baseline:** `debug-macos-arm64` has 566 tests with the gate off and 654 with the clang-tidy gate on (88 `clang_tidy_*`).

## The change

**1. `src/exchange/common/http_client.cpp`.** `curl_easy_reset(curl);` becomes the first statement of `perform`. The line that clears
`CURLOPT_CUSTOMREQUEST`, and its comment, go: the reset does it. The rest is untouched, `CURLOPT_HTTPGET` included (GET is the default
after a reset; the line still says what the branch means).

```cpp
HttpResponse CurlHttpClient::perform(const HttpRequest& http) {
    CURL* curl = curl_.get();

    // The handle is reused and libcurl's options stay set until they are changed. HTTPHEADER and POSTFIELDS are pointers, to a list that was
    // freed and to a body that belonged to the previous call, so put every option back to its default before this request sets its own (#51).
    // Live connections and the DNS and session caches are kept: connections are still reused. This is first so that no way out of the previous
    // call, a throw included, leaves anything behind. An option that should hold for every request is set below, on each call.
    curl_easy_reset(curl);
```

**2. `include/exchange/common/http_client.hpp`.** The comment on `perform` gains: each call stands alone, so nothing set for an earlier
request (headers, body, method) reaches the next, one that threw included; connections stay open between calls.

**3. `tests/unit/test_http_client.cpp`** (the executable that exists: no CMake change, and no new unit for the clang-tidy gate).

- The server keeps every request it saw (`requests()`, a copy taken under the lock, as `last()` is; `last()` stays for the three tests
  that exist) and its reply carries `Connection: close`.
- Four new tests, each on **one client over several requests**. The earlier request objects stay alive in the test, so on today's code the
  stale body is a *valid* pointer and test 2 fails by assertion, not by undefined behaviour; the stale header list is freed inside
  `perform` whatever the test does, so test 1 is the one that is undefined behaviour on today's code.
  1. `AGetWithNoHeadersAfterOneWithHeadersSendsNone`: the issue's first sequence. The second request reaches the server with none of the
     first's headers, and the status and body come back.
  2. `ADeleteWithNoBodyAfterAPostSendsNoBody`: the issue's second sequence. The server sees a DELETE with `X-Second` and nothing of the
     POST: empty body, no `Content-Length`, no `Content-Type`, no `X-First`.
  3. `ARequestAfterAFailedOneSendsOnlyItsOwn`: a POST with a body and the header `X-First` to a bad URL; `EXPECT_THROW(...,
     std::runtime_error)` and nothing reached the server; then a DELETE with no body and its own header `X-Second`: no body, no
     `Content-Length`, no `X-First`. (Its own header keeps the header list out of this test, so today's failure is the stale body, an
     assertion; the header half through a throw is in test 4.)
  4. `ARequestSendsOnlyItsOwnWhateverCameBefore`: a table of five kinds that succeed (GET with a header, bare GET, POST with a body and a
     header, DELETE with a body and a header, bare DELETE) and two that throw (the POST and the GET with headers, on a bad URL). Every
     pair of *any kind, then a kind that succeeds*: 35 pairs on one server, a fresh client for each. For the second request: the method,
     target and body are those asked for; each of its headers has its value; no distinctive header of any other kind is present; a
     request with no body has neither `Content-Length` nor `Content-Type`, and one with a body has `Content-Length` equal to its size.
     `SCOPED_TRACE` names the pair. The table is built in a function (a namespace-scope vector allocates before `main`:
     `bugprone-throwing-static-initialization`).
- The file's header comment loses its "one request per client, on purpose" paragraph.

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 One `curl_easy_reset` at the top of `perform`.** The issue's fix sketch and libcurl's documented way to reuse a handle.
  *Alternative:* clear `HTTPHEADER`, `POSTFIELDS`, `POSTFIELDSIZE` and `POST` by hand: a smaller diff with the same effect today, but a
  list that has to grow with every option `perform` ever sets (#56 adds timeouts), and at the bottom of `perform` it is skipped by a
  throw (rows 3 and 4 of the table).
- **D2 Four tests.** The issue's "Test" section asks for the two sequences; tests 3 and 4 make the claim in the header comment, "whatever
  came before, a call that threw included". *Alternative:* tests 1 and 2 only.
- **D3 No release in this plan; I recommend `v2.0.1`.** The bug is in every release and bites ordinary use (a private call, then a public
  one, on one client), so a patch release is warranted. A tag and a release are outward-facing, so it is yours to say: yes adds Step 4
  (bump `project(VERSION)` in `CMakeLists.txt:35` and the `GIT_TAG` example in `README.md:354`, an annotated tag, and `gh release create`,
  because a tag alone drew "i see a tag, but no release on gh" for v1.0.1).
- **D4 No connection-reuse test.** Measured once above (1 connection for 4 requests). `ix::HttpServer` cannot show it, since it closes
  each connection, so a test needs its own keep-alive server (about 80 lines of sockets). A later change that gave each call a fresh
  handle would pass the suite and cost a TCP and TLS handshake per call. *Alternative:* a fifth test with that server, in this plan.
- **D5 Evidence order**, as in plans 029 and 038: the checkpoint is `debug-macos-arm64` with the gate on; the sanitizer trees and Linux
  come after the push (Step 3). *Alternative:* the sanitizer trees in the checkpoint.

## Steps

Commits go straight to `main`, one per step, pushed once the checkpoint is green. No commit message carries a closing keyword (it would
close the issue on push, before the result comment): #51 is closed by hand in Step 2. Counts are what I expect and are replaced by the
measured ones in the *Outcome*.

### Step 0 — Approval

You approve or edit. Commit this plan (`docs(plan): plan 041 approved as written`), push (documentation only: no CI run), and comment on
#51 with the plan's path. *Done when:* #51 links the plan and the plan links #51.

### Step 1 — The tests, red; then the fix

*Start from green.* `cmake --preset debug-macos-arm64 -DCRYPTOCOGS_CLANG_TIDY=clang-tidy`, build, full ctest: expected **654 of 654**.
*Done when* the baseline is recorded; if anything is red I stop and tell you.

*Tests (modified)* `tests/unit/test_http_client.cpp`: the server change and the four tests above. The three that exist are unchanged.

*Red first.* Build the tests without the fix and run each of the four alone, as ctest does. Expected: 1 and 4 crash with SIGSEGV (the
scratch runs did, on this machine); 2 and 3 fail on their assertions (the stale body, `Content-Length: 17`). The three that exist stay
green. This run is not committed: every commit builds and passes.

*Change.* The fix and the header comment, as above.

*Hand-made changes* (in the tree; the object deleted before each build, because Apple's Make 3.81 compares one-second timestamps, and
the number of objects compiled printed; the sources compared with `HEAD` after):
(a) the reset removed, which is today's code: the red above. (b) the reset replaced by clearing `CURLOPT_HTTPHEADER` only: 1 green, 2, 3
and 4 red. (c) replaced by clearing the POST options only (`POSTFIELDS`, `POSTFIELDSIZE`, `CUSTOMREQUEST`, `HTTPGET`): 2 and 3 green, 1
and 4 red. (d) the reset moved to the end of `perform`, after the status is read: 1 and 2 green, 3 and 4 red, which is what *first* buys.
(e) the reset kept and the `CUSTOMREQUEST` line kept as well: all green (the special case was redundant, not wrong).

*Checkpoint.* Full build; ctest of `debug-macos-arm64` with the gate on: **658 of 658** (570 and the 88 `clang_tidy_*`); no warning.
*Done when:* red as stated, then green; (a) to (e) as stated; the checkpoint green; pushed; and the `release-linux-x86_64` and
`clang-tidy` runs of the commit green (GCC 15.2 and clang-tidy 23).

### Step 2 — Docs, counts, close

`CLAUDE.md`: the structure tree's line for `test_http_client.cpp`; *Running tests* (the executable's test count, the totals 548 → 552 and
566 → 570, the table row for `test_http_client`, which loses the "one request per client, because … is #51" sentence, and the
Crypto.com-only line: 141 → 145 gtests, 158 → 162, "3 `CurlHttpClient::perform` tests" → 7); the *Test suite breakdown* row; and a
*Coding conventions* bullet: `perform` resets its handle first, and an option that should hold for every request is set after the reset,
on each call (the note #56 will need). Counts are **measured**: `ctest -N` in the default tree, and a Crypto.com-only tree (the other three
exchanges off) configured, built and run with the gate on, as in plan 038; the gate's registration count, 95 with the benchmarks, should
not change (no new unit). This plan's *Outcome*. The gate turned off in the tree again (`-DCRYPTOCOGS_CLANG_TIDY=`).
*Done when:* pushed (documentation only, so no run), and #51 closed by hand (`completed`) with a comment that lists the commits, the
red-then-green record and the counts.

### Step 3 — After the push: measure

Not blocking the push.
1. `asan-macos-arm64` and `tsan-macos-arm64`, full suites, with the options in `CLAUDE.md` (*Sanitizers*). TSan matters for the server's
   thread and its new vector.
2. `gh workflow run sanitizers.yml --ref main -f which=asan`, then `tsan`, one dispatch at a time.
3. If Docker is up: the four new tests against the old `perform` in `ubuntu-dev` (GCC 13.3, glibc), a scratch copy made with
   `git archive`, to see what the stale pointers do outside macOS: a crash, or a header and a body re-sent. Skipped if Docker is down.

*Done when* the results are in the *Outcome*; a finding gets its own issue.

### Step 4 — Release (only if D3 is yes)

Bump the version in the two places above, run the full ctest, commit, push, wait for CI, then an annotated `v2.0.1` tag and
`gh release create v2.0.1` with notes that name #51 and the crash.

## Designs I rejected

- **Clearing the options by hand** (D1).
- **A scope guard that resets when `perform` leaves.** The handle would hold no dangling pointer at rest, but nothing reads one: every
  call resets first. It costs a second reset per call and a type, for no observable difference.
- **A fresh handle per call** (`curl_easy_init` and `curl_easy_cleanup` inside `perform`): it fixes #51 and loses connection and TLS
  session reuse, a handshake per call on a client meant for trading.
- **`CURLOPT_COPYPOSTFIELDS`:** libcurl would own the body, which fixes half of the bug, and has no equivalent for the header list.
- **A seam on `CurlHttpClient`** (a virtual or a constructor parameter) so a test can look at the options: the defect is on the wire, and
  the loopback server is the oracle.
- **Setting `HTTPHEADER` and `POSTFIELDS` unconditionally** (to the list or the body, or to `nullptr`): the same by-hand list as D1, and
  it still leaves `POST` and any option added later.

## Self-review — risks and assumptions

- **Today's red is undefined behaviour in part.** Test 1 depends on what the freed header list looks like: a SIGSEGV on the Mac
  (measured, plain and ASan), but elsewhere possibly a header re-sent. Both fail the test (a crash, or `X-Secret` present), and the Linux
  check in Step 3 shows which. After the fix nothing is read from freed memory.
- **`curl_easy_reset` resets everything.** An option set on the handle outside `perform` would be lost. None exists today; the
  `CLAUDE.md` bullet and the comment in `perform` say where a new one goes (#56 is the next).
- **Connection and TLS reuse are kept by libcurl's contract and one measurement, not by a test** (D4). The measurement was libcurl 8.7.1
  on the Mac with a keep-alive server, plain HTTP; the TLS session cache is documented, not measured.
- **Test 3 and the table's failing kinds depend on libcurl refusing a control character in the URL.** 8.7.1 here does, with `URL using
  bad/illegal format or missing URL`; I expect the CI image's libcurl to, and the CI run of Step 1 is the check. A libcurl that accepted
  it would fail `EXPECT_THROW` by name, not pass.
- **`Connection: close` takes libcurl's reuse of a closed connection out of the tests.** That is the point (no timing), and it is why D4
  needs a server of its own.
- **The reset's position is covered only through calls that throw** (mutation (d)); the throw inside `make_curl_header_list` (an
  allocation failure) cannot be provoked, and is the same path.
- **Behaviour change:** none for a sequence that worked. In the two that did (POST then GET, DELETE with a body then GET) the bytes on
  the wire were the same with and without the reset in the scratch runs.
- **Test cost:** the new tests make about 65 loopback requests (60 in the table: the 25 pairs that start with a request that succeeds
  send two, the 10 that start with a throw send one), each on a fresh TCP connection; the three that exist took 0.03 s each under ASan
  and 2.1 s under TSan on the Mac (plan 038, mostly start-up). The 120 s ctest timeout bounds a hang.
- **`-Werror` and the gate:** no new warning sources expected; the table is a function-local vector, `curl_easy_reset` returns `void`,
  and the file has no optional to check.
- **Priority:** the issue says high, and a crash in ordinary use of a released version supports it; hence D3.

## Outcome

Steps 0 to 2 are done (2026-11-06). Step 3 is measured after the push and recorded at the end of this section. D3 stood at no release
and recommended `v2.0.1`; the fix went out in `v2.1.0` instead (plan 043, Step 4), with the fixes of plans 038, 042 and 043.

### Commits

| Step | Commit | What |
|---|---|---|
| 0 | `3308832` | this plan, approved as written |
| 1 | `647a9ce` | `curl_easy_reset(curl)` first in `perform` and the `CUSTOMREQUEST` line gone; the comment in `http_client.hpp`; the server keeps every request and says `Connection: close`; the four tests |
| 1 | `ca7c790` | braces round an assertion in an `if` in `expect_only_its_own` (GCC's `-Wdangling-else`; see *What differed*) |
| 2 | the commit that adds this section | `CLAUDE.md` and this *Outcome*; #51 closed by hand after the push |

### Red, then green (Step 1: `debug-macos-arm64`, Apple clang 21.0.0, libcurl 8.7.1)

Each test alone, as ctest runs it. "Without" is the test file of `647a9ce` over the `perform` of `c4780f2` (no reset); "with" is the fix.

| Test | Without the reset | With it |
|---|---|---|
| `AGetWithNoHeadersAfterOneWithHeadersSendsNone` | SIGSEGV (exit 139) | passes |
| `ADeleteWithNoBodyAfterAPostSendsNoBody` | fails (exit 1): the body `symbol=X&secret=1`, `Content-Length: 17` and `Content-Type: application/x-www-form-urlencoded` of the POST reach the server | passes |
| `ARequestAfterAFailedOneSendsOnlyItsOwn` | fails (exit 1) with the same three | passes |
| `ARequestSendsOnlyItsOwnWhateverCameBefore` | SIGSEGV (exit 139) | passes (46 ms for its 35 pairs) |
| the three that existed | pass | pass |

This is what the plan expected: 1 and 4 undefined behaviour on the freed header list (a crash here), 2 and 3 the stale body, by assertion.

### Hand-made changes (Step 1)

Each in the tree with its object deleted first (Apple's Make 3.81), and the number of objects compiled printed: 1 every time. The four new
tests and the three old ones were run alone; `crash` is a signal, `red` a failed assertion.

| Change to `perform` | Test 1 | Test 2 | Test 3 | Test 4 | The three old |
|---|---|---|---|---|---|
| (a) no reset (the code of `c4780f2`) | crash | red | red | crash | green |
| (b) the reset replaced by clearing `CURLOPT_HTTPHEADER` only | green | red | red | red | green |
| (c) replaced by clearing `POSTFIELDS`, `POSTFIELDSIZE`, `CUSTOMREQUEST` and `HTTPGET` only | crash | green | green | crash | green |
| (d) the reset moved to the end of `perform`, after the status is read | green | green | red | crash | green |
| (e) the reset kept and the `CUSTOMREQUEST` line kept as well | green | green | green | green | green |
| the fix restored (the source byte-identical to the one of `647a9ce`) | green | green | green | green | green |

(a) is the record above. Each half of the bug has a test that catches it alone: clearing the header list only (b) still sends the POST's
body, and clearing the POST options only (c) still reads the freed list. (d) is what *first* buys: at the end of `perform` a call that
threw leaves its state behind, and tests 3 and 4 say so. (e) shows the old `CUSTOMREQUEST` line was redundant, not wrong, and no test
tells the two apart, which is why it went.

### Counts (measured)

| Tree | Result |
|---|---|
| `debug-macos-arm64`, gate on, before the change | 654 of 654 |
| `debug-macos-arm64`, gate on, after | 658 of 658 (570, and the 88 `clang_tidy_*`) |
| `debug-macos-arm64`, gate off, after | 570 of 570: 552 gtests, 17 `cmake -P` checks and `test_kraken_example` |
| Crypto.com-only (the other three exchanges off), gate on, after | 197 of 197: 35 gate tests and 162 others, of which 145 gtests and 7 of them `CurlHttpClientPerform` |
| CI `release-linux-x86_64` (GCC 15.2), `ca7c790` | 570 of 570, 17.6 s of tests; the job took 1 min 50 s |
| CI `clang-tidy` (Debug, benchmarks and gate on), `ca7c790` | 95 of 95, as before: no `.cpp` was added or removed; the job took 6 min 22 s |

`CLAUDE.md` carries these: 548 → 552 and 566 → 570, the Crypto.com-only line (141 → 145 gtests, 158 → 162), 3 → 7
`CurlHttpClient::perform` tests, the two rows for `test_http_client`, the structure tree, and a *Coding conventions* bullet on where an
option that must hold for every request goes (after the reset, on each call: the note #56 needs).

### What differed from the plan

- **The first push was red on GCC.** The main workflow on `647a9ce` (run 42269972714) failed in its Build step with
  `test_http_client.cpp:193:16: error: suggest explicit braces to avoid ambiguous 'else' [-Werror=dangling-else]`: a bare `if` over an
  `EXPECT_EQ` in `expect_only_its_own`. Apple clang 21 and clang-tidy 23 do not report it (the `clang-tidy` run on `647a9ce` was green),
  and the plan's Step 1 checkpoint was the Mac only. `CLAUDE.md` already says an assertion under a bare `if` needs braces and that a change that adds
  code is built on GCC before the push; I pushed without the GCC build. `main` was red in that workflow from the push of `647a9ce`
  (18:21:35Z) to the push of `ca7c790` (18:25:07Z). `ca7c790` is the braces; before pushing it I rebuilt and ran on Apple clang and built
  `test_http_client` in `ubuntu-dev` (GCC 13.3, `-Werror`: no warning, 7 of 7 tests pass). Both workflows on `ca7c790` are green, which
  is what Step 1's *Done when* asked of the commit.
- **Nothing else.** The four tests, the 35 pairs, the counts (658, 570, 552, 145, 162) and the five hand-made changes came out as the
  plan said; in (c) and (d) the plan's "red" for test 4 is a crash.
- **Issues opened:** [#56](https://github.com/tomlin256/cryptocogs/issues/56) (bug, priority medium), `perform` sets no timeout. Found
  while reading `perform`; not part of this fix.

### Step 3 — after the push

Not measured when this section was written; the sanitizer trees, the two dispatches of `sanitizers.yml` and the run against the old
`perform` in `ubuntu-dev` are added below.
