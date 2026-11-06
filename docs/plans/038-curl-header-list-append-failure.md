# Plan 038 — `CurlHttpClient` stops when `curl_slist_append` fails

**Status:** In progress. Rob approved the plan as written on 2026-11-06, so D1 to D5 stand at the defaults below. Step 0 is done when this
commit is pushed.

Closes [#48](https://github.com/tomlin256/cryptocogs/issues/48).

**Raised along the way:** [#51](https://github.com/tomlin256/cryptocogs/issues/51), priority **high**: `perform` leaves the previous
call's `CURLOPT_HTTPHEADER` and `CURLOPT_POSTFIELDS` on the reused handle, so a header-less request after a header-ful one crashes and a
body-less DELETE after a POST sends dead-buffer bytes. Found while reading the same function and reproduced with a scratch program
(loopback server, ASan and plain build); its own plan.
**Not here:** #51; the same `chunk = curl_slist_append(chunk, …)` pattern in `tests/examples/kapi.cpp:335-336` (the frozen reference
wrapper that `test_signature.cpp` compares against; say if you want an issue for it); a tag (D4).

## Problem

`CurlHttpClient::perform` builds the header list with (`src/exchange/common/http_client.cpp:54`):

```cpp
chunk.reset(curl_slist_append(chunk.release(), line.c_str()));
```

`release()` gives up ownership before the call. `curl_slist_append` returns `NULL` when it fails and leaves the list it was handed alone
(its man page: "To avoid overwriting an existing non-empty list on failure, the new list should be returned to a temporary variable
which can be tested for NULL before updating the original list pointer"). So when it fails:

1. the nodes built so far are leaked, because nothing owns them any more;
2. `chunk` is empty, so the headers appended before the failure are not sent; and
3. the headers after it start a new list.

`HttpRequest::headers` is a `std::map`, so lines are appended in key order. A Kraken private request has `API-Key`, `API-Sign` and
`Content-Type`: if the append for `API-Sign` fails the request goes out with `Content-Type` only, unsigned; if the last one fails,
`perform` sets no `CURLOPT_HTTPHEADER` at all. (Derived from the code and the man page: the failure cannot be provoked without fault
injection.) It needs an allocation failure inside libcurl, so the priority stays **low**, as in #48.

**After the fix** `perform` throws. Every adapter's `execute()` catches `std::exception` around `perform_` and returns `ok == false`
with `request failed: <what>` (the four `rest_client.inl`), so a caller gets a local, readable error and nothing is sent, instead of a
request that reaches the exchange without its signing headers.

### What I checked (2026-11-06, `545a752`)

- `perform` is the only user of `curl_slist_append` in `src/` and `include/`; the other is the legacy `kapi.cpp`.
- No test mentions `CurlHttpClient` or `http_client`: the adapter tests inject a performer, so the transport has never run under test.
  `debug-macos-arm64` has 556 tests today.
- The man page's example is the shape to copy: append into a temporary, test it, assign on success. It also says the *new* list is
  returned, so the head is not promised to stay: the code adopts what it is given back.

## The change

**1. A function that builds the list, with the append call injectable.** New `include/exchange/common/curl_header_list.hpp` (a public
header that the directory install picks up, so it includes `cxx_standard.hpp` first, which `test_standard_guard` checks) and
`src/exchange/common/curl_header_list.cpp` (in `libexchange_http.a`, next to `http_client.cpp`):

```cpp
namespace exchange::rest::detail {

using Headers     = std::map<std::string, std::string>;                       // HttpRequest::headers
using CurlSlist   = std::unique_ptr<curl_slist, void (*)(curl_slist*)>;       // owns a header list; null when there is none
using SlistAppend = std::function<curl_slist*(curl_slist*, const char*)>;     // curl_slist_append's signature

// "Key: value" for each header, in key order; null for no headers. Throws std::runtime_error if libcurl cannot allocate a node,
// after freeing the nodes made so far. `append` is libcurl's own; a test passes one that fails.
CurlSlist make_curl_header_list(const Headers& headers, const SlistAppend& append = &curl_slist_append);

}
```

```cpp
// .cpp, the loop body (the line-building code is unchanged)
curl_slist* const grown = append(list.get(), line.c_str());   // libcurl leaves `list` alone when it fails, so keep it until it works
if (grown == nullptr)
    throw std::runtime_error("CurlHttpClient: curl_slist_append() failed");
if (grown != list.get()) {                                    // the first line always changes the head
    static_cast<void>(list.release());                        // `grown` heads the same nodes: hand them over, do not free them
    list.reset(grown);
}
```

**2. `perform` uses it.** The loop and the deleter lambda go; the rest of `perform` is untouched, `if (headers)` included (that is #51):

```cpp
const auto headers = detail::make_curl_header_list(http.headers);
if (headers)
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
```

**3. The contract.** `http_client.hpp` says `perform` throws `std::runtime_error` on "a libcurl transport failure"; it becomes "a libcurl
failure: a transport error, or libcurl unable to allocate the header list".

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 `std::runtime_error`, not `std::bad_alloc`.** `perform` is documented to throw `std::runtime_error` for a libcurl failure and the
  constructor already reports `curl_easy_init()` returning `NULL` that way; the adapters fold `what()` into `request failed: …`, which
  then reads `CurlHttpClient: curl_slist_append() failed` and not `std::bad_alloc`. *Alternative:* `std::bad_alloc`, the issue's first
  suggestion and idiomatic for an allocation failure, but a caller that catches the documented `std::runtime_error` would stop catching it.
- **D2 A function with an injectable append, not a class.** One function, one seam, nothing to own once it returns; the tests need
  nothing a class would add. *Alternative:* a `CurlHeaderList` class whose `add()` keeps the earlier lines after a failure, which a test
  could look at. No production path looks at a list after a failure, so that would test a state nothing reaches.
- **D3 Step 2, a loopback test of `perform`, is in this plan.** The changed lines of `perform` are glue with no seam, and glue is
  tested. It is also the first test of `CurlHttpClient` and the harness #51 will need. Its own commit, so it can go without touching
  Step 1. *Alternative:* the function's unit tests only; `perform` is tested with #51.
- **D4 No tag.** An allocation-failure path; a patch release (v2.0.1) is outward-facing, so say so if you want one.
- **D5 Evidence order.** The checkpoint is the full build and ctest of `debug-macos-arm64` with the clang-tidy gate on. The sanitizer
  trees and the Linux evidence come after the push (Step 4), as you asked on plan 029. *Alternative:* the sanitizer trees in every
  checkpoint (plan 033).

## Steps

Commits go straight to `main`, one per step, pushed once the checkpoint is green. No commit message carries a closing keyword (a
`closes #48` in a message closes the issue on push, before the result comment): #48 is closed by hand in Step 3. Counts are what I
expect and are replaced by the real ones in the Outcome.

### Step 0 — Approval

You approve or edit. Commit this plan (`docs(plan): plan 038 approved as written`), push (documentation only, so CI starts no run),
and comment on #48 with the plan's path and on #51 with a link to the plan's *Not here*. *Done when:* #48 links the plan, and the plan
links #48 and #51.

### Step 1 — The function, the fix and its tests (test first)

*Start from green.* Configure `debug-macos-arm64` with the gate on (`-DCRYPTOCOGS_CLANG_TIDY=clang-tidy`), build, and run the whole
ctest: expected 556 of 556 and the 85 `clang_tidy_*` tests. *Done when* the baseline is recorded; if anything is red I stop and tell you.

*Tests (new)* `test_curl_header_list` (`tests/unit/test_curl_header_list.cpp`, linked with `exchange_http` and `GTest::gtest_main`; no
network, built in every flag configuration). Real libcurl lists throughout; the only double is the append function, which forwards to
the real `curl_slist_append` until it is told to fail. Five tests, no sleeps, the list read back by walking it:

- `NoHeadersGiveNoListAndNoCall`: an empty map returns null and never calls the append function.
- `LinesAreKeyColonSpaceValueInKeyOrder`: `{"B":"2","A":"1","C":<200 characters>}` reads back as `A: 1`, `B: 2`, `C: …` (the string is
  copied, the order is the map's).
- `AppendsOntoTheListBuiltSoFar`: the recording double is given `nullptr` first and then the head it returned; the result is that head.
- `AFailureAtAnyPositionThrowsAndStops`: for position 1, 2 and 3 of three headers: `std::runtime_error` naming `curl_slist_append`,
  and the double was called exactly as many times as the position (nothing is appended after a failure).
- `AdoptsTheHeadTheAppendFunctionReturns`: a double that prepends (returns a new head whose `next` is the old one): the list reads back
  in that order and every node is freed once (ASan is the judge of that in the sanitizer trees).

*Red first.* Write the function with the **old** loop body (`list.reset(append(list.release(), …))`) and run: I expect exactly
`AFailureAtAnyPositionThrowsAndStops` red (the old code does not throw and goes on) and the other four green. Then the fix, and five green.

*Change.* The header and source; `exchange/common/curl_header_list.cpp` in `exchange_http` (`src/CMakeLists.txt`); the
`test_curl_header_list` entry in `tests/unit/CMakeLists.txt`; `perform`; the comment in `http_client.hpp`.

*Hand-made changes in a scratch copy* (made with `git archive`, never `rsync` of the checkout with its `build*` trees; the object file
deleted before each build, because Apple's Make 3.81 compares one-second timestamps; the number of objects compiled printed):
(a) the old inline logic: `AFailureAtAnyPositionThrowsAndStops` red; (b) `if (!grown) continue;` (the failure swallowed): the same test
red; (c) adopt only when the list is empty (`if (!list) list.reset(grown)`): `AdoptsTheHeadTheAppendFunctionReturns` red; (d) separator
`":"` without the space: `LinesAreKeyColonSpaceValueInKeyOrder` red.

*Checkpoint.* Full build; ctest of `debug-macos-arm64` 561 of 561 and the `clang_tidy_*` tests all green (87: the new `.cpp` and the new
test are two more units). *Done when:* red then green as stated, the four changes red as stated, the checkpoint green, pushed.

### Step 2 — `perform` under test (D3)

*Tests (new)* `test_http_client` (`tests/unit/test_http_client.cpp`, linked with `exchange_http`, `ixwebsocket` and `GTest::gtest_main`;
built in every flag configuration). A loopback `ix::HttpServer` on `127.0.0.1` records the request it gets (method, uri, headers, body)
and answers `200` with a fixed body. The port is found the way `test_ix_ws_connection` finds one (scan upward from a port derived from
the process id; ixwebsocket cannot report a port it bound for 0). No sleeps: `perform` returns after the reply, and the recorded request
is read after that, handed over through a `std::promise` so TSan has nothing to report. Three tests:

- `SendsEveryHeaderOfAGet`: three headers arrive with their exact values; status 200 and the body come back.
- `SendsTheBodyAndTheHeadersOfAPost`: method POST, the body and the headers arrive.
- `PerformsARequestWithNoHeaders`: a fresh client and no headers: status 200, and none of the names used by the other tests is present.

**One request per client, on purpose:** a second request on the same client is #51 (a header-less one after a header-ful one crashes).
The file says so, and the tests of #51 extend it.

*Characterization, not red-first.* They describe behaviour Step 1 must keep, so they are run on `perform` as it was before Step 1
(a scratch copy at the commit before) and must be green there too, then green after.
*Hand-made changes in a scratch copy:* `perform` never sets `CURLOPT_HTTPHEADER`: the two tests with headers red; the function drops
each value: the same two red.

*Checkpoint.* Full build and ctest of `debug-macos-arm64` 564 of 564 and the gate (88). *Done when:* green on the old `perform` and on
the new one, the two changes red, pushed.

### Step 3 — Docs, push, CI, close

`CLAUDE.md`: the structure tree (the header, the source, the two tests), *Running tests* (the executable count, the test count, the
ctest count and the table rows of the two binaries, the Crypto.com-only count recomputed from a tree as in plan 033, the
`clang_tidy_*` counts), the *Test suite breakdown* rows, and the coding-conventions line that lists what `exchange_http` compiles. All
numbers are measured. This file's *Outcome*. Turn the gate off again in the tree (`-DCRYPTOCOGS_CLANG_TIDY=`). *Done when:* pushed, the
`release-linux-x86_64` run of the last commit is green (it is the GCC 15.2 build and test), and #48 is closed by hand with a comment
that lists the commits, the red-then-green record and the counts.

### Step 4 — After the push: measure

Not blocking the push (push, then measure).
1. **Sanitizer trees.** `asan-macos-arm64` and `tsan-macos-arm64`, full suites, with the options in `CLAUDE.md` (*Sanitizers*). TSan is
   the one that matters for Step 2's server thread; a report inside ixwebsocket is a finding for its own issue (plan 034).
2. **The Linux ASan job.** `gh workflow run sanitizers.yml --ref main -f which=asan`, one dispatch at a time: LeakSanitizer runs only
   there, so it is the oracle of the leak half of #48. *Done when* green. If Docker is up, also build the scratch copy with the old logic
   in the Linux asan tree and see LeakSanitizer report the leak from `AFailureAtAnyPositionThrowsAndStops` (the proof the oracle sees
   it); skipped if Docker is down.

## Designs I rejected

- **The issue's three-line fix inline in `perform`:** untestable without a seam, and the issue itself asks for one.
- **Link-time interposition or a fault-injecting `malloc`:** platform-specific, replaces the real function for the whole test binary,
  and cannot say "fail the third call" without global state.
- **A seam on `CurlHttpClient`** (a constructor parameter or a virtual method): widens a public class for a test, and `perform` still
  needs a server to run.
- **An abstract `IHeaderListBuilder`:** one C function and one double; a `std::function` is the lightest thing that substitutes it.
- **A function pointer instead of `std::function`:** the double would need file-scope state for its counter.
- **A class** (D2) **and `std::bad_alloc`** (D1).
- **Fixing `kapi.cpp` as well:** it is the reference the signature test compares against; its behaviour is not to change.
- **A custom allocator or a free-function hook in the deleter** to make the leak assertable on macOS: a second seam for one assertion
  that LeakSanitizer already makes on Linux.

## Self-review — risks and assumptions

- **One of the five Step 1 tests is red on the old logic.** The other four guard what the old code also did (format, order, the
  arguments, adopting a head) so that a mistake in the move shows. That is the bug's whole observable effect: it no longer throws. The
  leak itself cannot be asserted on macOS.
- **The leak half has its oracle only on Linux.** If the fix regressed to the old shape, the failure test catches it everywhere; the
  `asan` job (Step 4) is the second, independent check of the leak.
- **The seam is a `std::function` built from `&curl_slist_append` on every call.** A function pointer fits its small buffer, so there is
  no allocation, and the cost is far below a network round trip.
- **The production default, the real `curl_slist_append`, is exercised only by Step 2.** If D3 is declined, only the examples and the
  live adapters run it.
- **`static_cast<void>(list.release())`** discards a result that `bugprone-unused-return-value` watches; a cast to `void` is its accepted
  spelling. If clang-tidy 23 disagrees I restructure; a `NOLINT` needs your OK (the closed list in `CLAUDE.md`).
- **Step 2 uses real sockets:** loopback only, scanned ports, no sleeps; a hung wait ends at the 120 s ctest timeout, and a sandbox with
  no loopback would fail it. TSan may report inside ixwebsocket: a finding, not a flake.
- **Step 2 avoids #51 on purpose.** It could read as hiding the bug; the file says why, and #51 is linked and marked high.
- **Behaviour change:** only when libcurl cannot allocate: `perform` throws instead of sending fewer headers. A caller of
  `CurlHttpClient::perform` that bypasses the adapters now sees an exception there, which the header now documents.
- **A new public header under `detail`.** The directory install ships it, but it is not part of the supported surface; it is additive,
  so no major version.
- **I assume libcurl's documented contract:** `NULL` on failure, the list that was passed left alone, the returned pointer is the new
  head. The man page of the libcurl on this Mac (8.7.1) says exactly that.
- **I read the unit-test rule as covering glue.** `perform` has no seam for its failure path: that path is the function's throw (tested)
  propagating through three lines of `perform` (read, not run).
