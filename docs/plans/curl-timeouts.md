# `CurlHttpClient::perform` gives up on a server that stops answering

**Status:** Done (2026-11-06), released as [v2.1.0](https://github.com/tomlin256/cryptocogs/releases/tag/v2.1.0) (2026-11-06) with plans
curl-header-list-append-failure, curl-handle-reset and curl-post-sent-once; the record is in plan curl-post-sent-once, Step 4. Rob approved
the plan as written, so D1 to D6 stood at the defaults below (no release), and then asked for the release. Steps 0 to 4 are recorded in the
*Outcome*.

Closes [#56](https://github.com/tomlin256/cryptocogs/issues/56).

**Follows:** [plan curl-handle-reset](curl-handle-reset.md). `perform` now resets its handle first, so every option below is set on each
call, after the reset (the issue and plan curl-handle-reset's *Coding conventions* bullet both say so).
**Not here:** a release (D6); a timeout per request; a structured error type; the same words for the other failures after a request went
out (`Server returned nothing`, `Failure when receiving data from the peer`): a follow-up issue, opened in Step 0.

## Problem

`perform` sets no timeout. libcurl's `CURLOPT_TIMEOUT` defaults to 0 (no limit), so a server that accepts a connection and never answers
blocks the caller's thread forever; one that never completes the TCP handshake blocks it for libcurl's default connect timeout, 300 s.
Every adapter's `execute()` calls `perform` on the caller's own thread, so in a trading loop the thread that hangs is the one that would
cancel orders, and the adapters' `catch` never gets to report `request failed: …`. The issue's reproduction (2026-11-06, `c4780f2`):
`perform` on a plain GET was still blocked when a 6 s alarm fired (exit 142).

### What I checked (2026-11-06)

A scratch program, not in the repo: a listening socket on 127.0.0.1 that **never calls `accept()`** (the kernel completes the handshake
and keeps the bytes, and no reply ever comes), and raw libcurl against it with a fresh listener for every run, 300 runs per row. macOS
arm64 (Apple clang 21.0.0, libcurl 8.7.1, SecureTransport); Linux aarch64 in `ubuntu-dev` (libcurl 8.5.0, OpenSSL 3.0.13).
"Began" is `CURLINFO_PRETRANSFER_TIME_T` > 0: libcurl had reached the point where it sends the request.

| Transfer | `CURLE_OPERATION_TIMEDOUT` (rc 28) | Began, macOS | Began, Linux |
|---|---|---|---|
| `http://`, total timeout 1 ms | 300 of 300, both | 99 of 300 | 300 of 300 |
| `http://`, total 2 ms | 300 of 300, both | 293 of 300 | 300 of 300 |
| `http://`, total 5 ms and 20 ms | 300 of 300, both | 300 of 300 | 300 of 300 |
| `https://`, connect timeout 1 ms | 300 of 300, both | 0 of 300 | 0 of 300 |
| `https://`, connect timeout 20 ms | 300 of 300, both | 0 of 300 | 0 of 300 |

- **A timeout's outcome is deterministic; its phase is not.** At 1 ms every run ended in rc 28 on both systems, but whether the request
  had begun depends on the platform and the load. A test can assert the outcome; it cannot assert the phase of a plain-HTTP timeout. A TLS
  handshake that is never answered is a connect-phase timeout on every run, whatever the limit, and nothing is sent.
- **`CURLINFO_PRETRANSFER_TIME_T` is the marker for "nothing was sent".** It is 0 until libcurl is about to send the request, so 0 proves
  the request was not sent, and above 0 means it may have been (the conservative direction). `CURLINFO_REQUEST_SIZE` is **0 for a request
  that was sent** here (the plain-HTTP row), so it cannot be the marker. libcurl's own wording differs by phase and version (`Operation
  timed out after 303 milliseconds with 0 bytes received`, `SSL connection timeout`, `Connection timed out after 304 milliseconds`), so the
  text is built from the marker and never from the error buffer; `curl_easy_strerror` says `Timeout was reached` in all of them.
- **A connect timeout of 1 ms fires on plain loopback too on macOS** (`Failed to connect to 127.0.0.1 port N after 2 ms: Timeout was
  reached`), so a test that wants the total timer to be the one that fires gives the connect limit a long value, and the other way round.
- **A listener that never accepts has a finite queue.** With a backlog of 16, macOS refuses the 17th connection (`Couldn't connect to
  server`, rc 7); Linux gave rc 28 for all 40 tried. A test makes at most three connections to one listener.

## The change

**1. `include/exchange/common/http_client.hpp`.**

```cpp
struct HttpTimeouts {
    std::chrono::milliseconds connect{10'000};  // name resolution, TCP and TLS: until the request can be sent
    std::chrono::milliseconds total{30'000};    // the whole call, the connect included
};

explicit CurlHttpClient(std::string base_url, HttpTimeouts timeouts = {});   // std::invalid_argument if either is not positive
```

`perform`'s comment says what a timeout throws and what its text promises. At the bottom, in `namespace detail` (outside the supported
surface, as `curl_header_list.hpp` is): `std::string timeout_message(bool request_may_have_been_sent, const HttpTimeouts& limits);`, a
pure function so that both texts can be tested without a timer.

**2. `src/exchange/common/http_client.cpp`.** The three options go straight after the reset, each call:

```cpp
curl_easy_reset(curl);
// Options that hold for every request are set here, on each call, because the reset clears them (#56). NOSIGNAL is libcurl's advice for a
// program with threads that uses timeouts: without it a build with the synchronous resolver enforces a name-lookup timeout with SIGALRM.
curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeouts_.connect.count()));
curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeouts_.total.count()));
```

and after `curl_easy_perform`, before the generic failure:

```cpp
if (rc == CURLE_OPERATION_TIMEDOUT) {
    curl_off_t began_us = 0;
    // "Not sent" is claimed only when libcurl says the transfer never began; if it cannot say, the request may have gone out.
    const bool may_have_been_sent = curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME_T, &began_us) != CURLE_OK || began_us > 0;
    throw std::runtime_error(detail::timeout_message(may_have_been_sent, timeouts_));
}
```

The two texts (the issue asked that a timed-out order placement say its outcome is unknown and not invite a blind retry; the transport
cannot tell an order from a balance query, so the words are about the request, not the endpoint):

- Not sent: `curl_easy_perform failed: Timeout was reached (connect limit 10000 ms, total limit 30000 ms). The request was not sent.`
- May have gone out: `curl_easy_perform failed: Timeout was reached (connect limit 10000 ms, total limit 30000 ms). The request may have
  been received and acted on: its outcome is unknown, so check the exchange before sending it again.`

The adapters already turn it into `RestResponse{ok=false, errors={"request failed: " + what}}`.

**3. The four adapters** (`include/exchange/<ex>/rest_client.hpp`, `src/<ex>/rest_client.cpp`): the production constructor gains a second,
defaulted parameter, `HttpTimeouts timeouts = {}`, and passes it to `std::make_shared<CurlHttpClient>`. Existing callers compile unchanged.

**4. Tests.** A new test helper, `tests/unit/silent_listener.hpp` (like `callback_probe.hpp`): `SilentListener` binds 127.0.0.1 port 0 (the
kernel picks the port; `getsockname` reads it back, so no scanning as in the ix server), listens with a backlog of 16, never accepts, and
closes in its destructor; with a fixture that calls `curl_global_init` once per suite, the library's own rule.

`tests/unit/test_http_client.cpp` (7 new, 14 in all). The first two use limits that make **only the timer under test able to end the
call**: the other limit is an hour, so a missing or misplaced option hangs the test until the preset's 120 s timeout instead of passing.

1. `EveryCallToAServerThatNeverRepliesTimesOut`: `http://`, `{connect 1 h, total 1 ms}`, three calls on one client; each throws
   `std::runtime_error` whose text has `Timeout was reached`, `connect limit 3600000 ms` and `total limit 1 ms`. Three calls because the
   options are set on each call, not once.
2. `ATlsHandshakeThatNeverCompletesTimesOutBeforeAnythingIsSent`: `https://`, `{connect 1 ms, total 1 h}`; the text says `The request was
   not sent.` and not `outcome is unknown`.
3. `ANonPositiveConnectTimeoutIsRefused`, 4. `ANonPositiveTotalTimeoutIsRefused`: 0 ms and −1 ms throw `std::invalid_argument` naming the field.
5. `TheDefaultsAreTenSecondsToConnectAndThirtyInTotal`: `HttpTimeouts{}`.
6. `ATimeoutAfterTheRequestMayHaveGoneOutSaysItsOutcomeIsUnknown`, 7. `ATimeoutBeforeTheRequestWentOutSaysItWasNotSent`:
   `detail::timeout_message(true|false, {2 s, 5 s})` names both limits in ms, and the first says the outcome is unknown and to check before
   sending again and does not say `not sent`; the second says it was not sent and does not say unknown. No timer.

One test per adapter, in its existing client test file (`test_client.cpp`, `test_binance_client.cpp`, `test_coinbase_client.cpp`,
`test_cryptocom_client.cpp`), and the header comment of each stops saying "no network": a client built on a `SilentListener` URL with
`{connect 1 h, total 1 ms}` runs a public time/ping request and returns `ok == false` with `errors[0]` containing `request failed:`,
`Timeout was reached` and `total limit 1 ms`. The last is what shows the adapter forwarded the limits: an adapter that dropped them would
fall back to the 30 s default and print `total limit 30000 ms`.

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 Defaults: connect 10 s, total 30 s.** On a healthy exchange neither fires; they bound the worst case. *Alternative:* shorter (5 s
  and 10 s), which fails slow private endpoints on large accounts and makes more timed-out orders of unknown outcome; longer leaves a
  trading loop blocked longer. Either way a caller can choose.
- **D2 Two texts, chosen by whether the request began, not one hedge for every timeout.** "Not sent" is provable and lets a caller retry
  at once; a single "may have been acted on" would be wrong for the most common timeout, an unreachable host. *Alternative:* the one
  hedge, no `getinfo` and no `detail` function.
- **D3 The four adapters take `HttpTimeouts`** (a defaulted second constructor parameter). Without it a caller of `KrakenRestClient`
  cannot choose anything, which is the point of the issue's "values the caller can choose". *Alternative:* `CurlHttpClient` only.
- **D4 Tests use 1 ms limits and assert only what the timer guarantees** (the outcome, and the TLS case's phase), per the issue's "needs a
  decision". Every one of the 1,200 runs with a 1 ms limit (600 per system) ended in rc 28, with no sleep and no poll. The "may have gone out" text is tested as a pure
  function, so the one thing left untested live is that `perform` passes `true` when libcurl says the transfer began (a mutation that
  always passes `false` survives; inverting it, or always passing `true`, fails test 2). The false claim is the dangerous one, hence the
  conservative `getinfo` above. *Alternative:* a live test that doubles the total timeout from 1 ms until the "may have gone out" text
  appears (at most 64 ms): it closes that gap, and it is a bounded wall-clock loop, which the no-wall-clock rule frowns on. I leave it out
  unless you say so.
- **D5 A non-positive limit throws `std::invalid_argument`.** libcurl reads 0 as "no limit" (total) or 300 s (connect), the hang this plan
  removes, and a caller who writes `0ms` almost certainly means something else.
- **D6 No release in this plan; I recommend one for #51 and #56 together, `v2.1.0`** (a new defaulted constructor parameter, so a minor
  version). Neither fix is released yet (`v2.0.0` is the latest tag). A tag and a release are outward-facing, so yours to say: yes adds
  Step 5.

## Steps

Commits go straight to `main`, one per step, pushed once the checkpoint is green. No commit message carries a closing keyword: #56 is
closed by hand in Step 3. Counts are what I expect and are replaced by the measured ones in the *Outcome*.

### Step 0 — Approval

You approve or edit. Commit this plan (`docs(plan): plan 042 approved as written`, with the status line changed), push (documentation
only: no CI run), comment on #56 with the plan's path, and open the follow-up issue for the other failures after a request went out (label
`enhancement`, priority low in the body). *Done when:* #56 links the plan, the plan links #56, and the new issue number is in your hands.

### Step 1 — The transport: tests red, then the fix

*Start from green.* `cmake --preset debug-macos-arm64 -DCRYPTOCOGS_CLANG_TIDY=clang-tidy`, build, full ctest: expected **658 of 658**.
*Done when* the baseline is recorded; if anything is red I stop and tell you.

*Tests (new)* `tests/unit/silent_listener.hpp` and tests 1 to 7 above in `tests/unit/test_http_client.cpp`; the seven that exist are unchanged.

*Red first.* The test file built against a variant of the new code that has the declarations and none of the behaviour (no options set,
no validation, `timeout_message` returning an empty string). Each test alone, as ctest runs it, under `perl -e 'alarm 10; exec @ARGV'`.
Expected: 1 and 2 are still blocked when the alarm fires (exit 142, the issue's evidence); 3, 4, 6 and 7 fail by assertion; 5 passes (the
defaults are in the struct). Not committed: every commit builds and passes.

*Change.* Header and `http_client.cpp` as above.

*Hand-made changes* (in the tree, the object deleted before each build because Apple's Make 3.81 compares one-second timestamps, the
number of objects compiled printed, the source compared with `HEAD` after), each test run alone under the alarm:
(a) the variant above. (b) `CURLOPT_TIMEOUT_MS` removed: 1 hangs, 2 green. (c) `CURLOPT_CONNECTTIMEOUT_MS` removed: 2 hangs, 1 green.
(d) the three options set before the reset: 1 and 2 hang. (e) options set on the first call only: 1 hangs on its second call.
(f) `may_have_been_sent` inverted: 2 red. (g) the branch texts swapped in `timeout_message`: 6 and 7 red. (h) `CURLOPT_NOSIGNAL`
removed: **all green**, and it stays so: a numeric address does no lookup and every libcurl we build against has the threaded resolver, so
no test can see the option; the comment and libcurl's advice are the only evidence.

*Checkpoint.* Full build and ctest of `debug-macos-arm64` with the gate on: **665 of 665** (658 and 7); no warning. The whole tree built in
`ubuntu-dev` (`debug-linux-arm64`, GCC 13.3, `-Werror`) and `test_http_client` run there, because plan curl-handle-reset's first push was red on a GCC
warning that Apple clang does not report and this one adds code; this is also the Linux evidence for test 2 (OpenSSL).
*Done when:* red as stated, then green; (a) to (h) as stated; both checkpoints green; pushed; and the `release-linux-x86_64` and `clang-tidy`
runs of the commit green (GCC 15.2, clang-tidy 23).

### Step 2 — The adapters

*Tests (new)* the four, one per client test file. *Red first:* each adapter's constructor accepts `HttpTimeouts` and ignores it; each
test fails on the message (`total limit 30000 ms`) after the default 30 s, so the four take about two minutes. Then the forwarding. A
mutation that forwards in three adapters and not the fourth: that adapter's test alone is red.
*Checkpoint.* Full build; ctest of `debug-macos-arm64`, gate on: **669 of 669**; the same build and run in `ubuntu-dev`. *Done when* green
and pushed, and the two runs of the commit green.

### Step 3 — Docs, counts, close

`README.md`: a `### REST — timeouts` subsection under *Usage patterns*, after *placing an order*: the two limits, what a timeout returns,
and that a timed-out order must be checked before it is sent again. `CLAUDE.md`: the structure tree (`silent_listener.hpp`, the
`http_client.cpp` and `test_http_client.cpp` lines), the `KrakenRestClient` constructor shown at line ~877, *Running tests* (the
executable's count, the totals 552 → 563 gtests and 570 → 581, the Crypto.com-only line 145 → 153 gtests and 162 → 170, 7 → 14
`CurlHttpClient::perform` tests), the *Test suite breakdown* rows, and the *Coding conventions* bullet from plan curl-handle-reset, whose "such as a
timeout (#56)" becomes what was done. Counts are **measured**, as in plan curl-handle-reset (`ctest -N` in the default tree and a Crypto.com-only tree
with the gate on); the gate's 95 units should not change (no new `.cpp`). This plan's *Outcome*. The gate turned off in the tree again.
*Done when:* pushed (documentation only), and #56 closed by hand (`completed`) with a comment that lists the commits, the red-then-green
record and the counts.

### Step 4 — After the push: measure

Not blocking the push. `asan-macos-arm64` and `tsan-macos-arm64`, full suites, with the options in `CLAUDE.md` (*Sanitizers*): TSan
shows what the 1 ms limits do under their slowdown (the phase moves; the outcome should not). Then `gh workflow run sanitizers.yml --ref main -f which=asan`, then `tsan`, one at a time. *Done when* the results are in the
*Outcome*; a finding gets its own issue.

### Step 5 — Release (only if D6 is yes)

Bump the version in `CMakeLists.txt:35` and the `GIT_TAG` example in `README.md:354`, full ctest, commit, push, wait for CI, an annotated
`v2.1.0` tag, and `gh release create v2.1.0` with notes that name #51 and #56.

## Designs I rejected

- **Setting the options once, in the constructor.** `perform` resets at the top of its first call, so they would be gone before the first
  request (plan curl-handle-reset).
- **`CURLINFO_REQUEST_SIZE` as the marker:** measured 0 for a request that was sent.
- **Reading libcurl's error buffer** to tell the phases: the wording differs by phase and by version.
- **A structured `HttpTimeout` exception with `request_sent()`.** The adapters fold `what()` into a string, so nothing would read it. It is
  the right shape when `RestResponse` gains error categories.
- **A timeout per request** (`HttpRequest::timeout`), and `CURLOPT_LOW_SPEED_LIMIT`/`LOW_SPEED_TIME`: not asked for; both can follow.
- **Passing 0 through to libcurl** (D5).
- **An `ix::HttpServer` whose callback blocks** for the silent server: it needs a thread per connection and a release flag, which a
  listener with no `accept()` does not, and it is an HTTP parser, not a socket that stays quiet for the TLS case.

## Self-review — risks and assumptions

- **A behaviour change:** a call that took more than 30 s, or whose connection took more than 10 s, used to succeed and now throws. The
  limits are the caller's to change, and nothing about a healthy exchange gets near them.
- **A timed-out POST is not retried by anything here.** The text tells the person reading the log to check; `RestResponse` carries no flag
  a program could read, and the transport cannot tell an order from a query.
- **The marker's meaning rests on libcurl's documentation and two measurements** (8.7.1 and 8.5.0). It is the conservative direction: 0
  proves nothing was sent, above 0 includes the instant before the send. CI's libcurl is the third check, and `CURLINFO_PRETRANSFER_TIME_T`
  needs libcurl 7.61.
- **Tests 1 and 2 depend on the kernel completing a handshake with no `accept()`** (measured on both systems) and on libcurl having TLS (a
  build without it would fail test 2 on the text, not hang). Both listeners see one to three connections.
- **At 1 ms the phase moves with the machine** (99 of 300 on macOS, 300 of 300 on Linux), so no live assertion depends on it. Under TSan
  or a loaded runner the plain-HTTP rows lean further towards "not sent"; tests 1 and 7 do not care.
- **A hang is how a missing option shows**, and the preset's 120 s timeout is what stops it; the hand-made changes use the 10 s alarm.
- **`NOSIGNAL` is untested** (change (h)) and said so above.
- **Time:** each live test is a few milliseconds; the four adapter tests are the same. The red run of Step 2 is slow by design (30 s each).
- **A client with `total` below `connect`** never reaches its connect limit; the header says the total includes the connect, and nothing
  rejects it.
- **`-Werror` and the gate:** the tests set both fields of `HttpTimeouts` (GCC 13's `-Wmissing-field-initializers` on a designated list
  that leaves one out); the raw-socket helper uses `reinterpret_cast`, which `.clang-tidy` does not forbid; the listener's fd is closed on
  every path.
- **Platforms:** `long` is 64 bits on every platform the build guard names (macOS and Linux, arm64 and x86_64), so `count()` fits.

## Outcome

Steps 0 to 4 are done (2026-11-06); Step 4 is at the end of this section. D6 stood at no release; Rob then asked
for it, and `v2.1.0` was released on 2026-11-06 for [#48](https://github.com/tomlin256/cryptocogs/issues/48),
[#51](https://github.com/tomlin256/cryptocogs/issues/51), #56 and [#58](https://github.com/tomlin256/cryptocogs/issues/58) together (plan curl-post-sent-once, Step 4).

### Commits

| Step | Commit | What |
|---|---|---|
| 0 | `3e6f2e8` | this plan, approved as written |
| 1 | `f4d74cf` | `HttpTimeouts`; `perform` sets `CURLOPT_NOSIGNAL`, `CURLOPT_CONNECTTIMEOUT_MS` and `CURLOPT_TIMEOUT_MS` on each call, after the reset; the text of a timeout; `SilentListener`; seven tests |
| 2 | `7359107` | the four REST clients take `HttpTimeouts` and hand it to the transport; four tests |
| 3 | `023fa0b` | `README.md`, `CLAUDE.md` and this *Outcome*; #56 closed by hand after the push |
| 4 | the commit that adds the last section below | the record of the sanitizer runs |

### Red, then green

**Step 1** (`debug-macos-arm64`, Apple clang 21.0.0, libcurl 8.7.1). Each test alone, as ctest runs it, under a 10 s alarm. "Without" is
the test file over a `perform` that has the declarations and none of the behaviour (change (a) below); it was not committed.

| Test | Without | With |
|---|---|---|
| 1 `EveryCallToAServerThatNeverRepliesTimesOut` | blocked until the alarm fired (the issue's evidence) | passes |
| 2 `ATlsHandshakeThatNeverCompletesTimesOutBeforeAnythingIsSent` | blocked until the alarm fired | passes |
| 3 and 4 `ANonPositive…TimeoutIsRefused` | fail by assertion: nothing throws | pass |
| 5 `TheDefaultsAreTenSecondsToConnectAndThirtyInTotal` | passes: the defaults are in the struct | passes |
| 6 and 7 the two texts of a timeout | fail by assertion: the text is empty | pass |

**Step 2.** "Without" is the four REST clients with a constructor that accepts `HttpTimeouts` and ignores it, so the transport has the
defaults. Each test alone, under a 90 s alarm, the four at once.

| Test | Without | With |
|---|---|---|
| `KrakenRestClient.ProductionClientHandsItsTimeoutsToTheTransport` | fails (exit 1) after 30.011 s | passes (0.02 s in ctest) |
| `BinanceClient.ProductionClientHandsItsTimeoutsToTheTransport` | fails (exit 1) after 30.016 s | passes (0.02 s) |
| `CoinbaseClient.ProductionClientHandsItsTimeoutsToTheTransport` | fails (exit 1) after 30.008 s | passes (0.02 s) |
| `CryptoComClient.ProductionClientHandsItsTimeoutsToTheTransport` | fails (exit 1) after 30.011 s | passes (0.02 s) |

Each fails on the message and for no other reason: the call timed out, and the text reads `connect limit 10000 ms, total limit 30000 ms`
where the test wants `total limit 1 ms`. This is what the plan expected, 30 s each.

### Hand-made changes

**Step 1**, to `perform` and `timeout_message` in `http_client.cpp`. Each in the tree with its object deleted first (Apple's Make 3.81
compares whole seconds), the number of objects compiled printed (1 every time), and each test run alone under the 10 s alarm. `hang` is
the alarm (exit 142), `red` a failed assertion. The seven tests that existed before this plan were green in every row.

| Change | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| (a) the declarations and none of the behaviour | hang | hang | red | red | green | red | red |
| (b) `CURLOPT_TIMEOUT_MS` removed | hang | green | green | green | green | green | green |
| (c) `CURLOPT_CONNECTTIMEOUT_MS` removed | green | hang | green | green | green | green | green |
| (d) the three options set before the reset | hang | hang | green | green | green | green | green |
| (e) the options set on the first call only | hang | green | green | green | green | green | green |
| (f) `may_have_been_sent` inverted | green | red | green | green | green | green | green |
| (g) the branch texts swapped in `timeout_message` | green | red | green | green | green | red | red |
| (h) `CURLOPT_NOSIGNAL` removed | green | green | green | green | green | green | green |
| the fix restored (byte-identical to the committed source) | green | green | green | green | green | green | green |

(e) hangs test 1 on its second call: that is what `EveryCall…` is for. (g) turns test 2 red too, because it reads the sentence. (h) is
the one the plan said no test can see: a numeric address does no lookup and the libcurl here has the threaded resolver. D4's gap stands as
written: a `may_have_been_sent` that is always `false` is caught by no test, because the only live assertion of the phase is the TLS case,
which wants `false`; tests 6 and 7 hold the two texts as a function of the flag, not where the flag comes from.

**Step 2**, to the production constructor of one client at a time (it hands the transport `HttpTimeouts{}` and ignores its argument), the
other three forwarding. After each build the four tests were run at once under a 90 s alarm.

| Client that ignores its timeouts | Kraken | Binance | Coinbase | Crypto.com |
|---|---|---|---|---|
| Kraken | red | green | green | green |
| Binance | green | red | green | green |
| Coinbase | green | green | red | green |
| Crypto.com | green | green | green | red |
| all four forward (restored; the checkpoint below) | green | green | green | green |

Each test sees its own client alone, which is what a test of the wiring should do.

### Counts (measured)

| Tree | Result |
|---|---|
| `debug-macos-arm64`, gate on, before Step 1 | 658 of 658 |
| `debug-macos-arm64`, gate on, after Step 1 | 665 of 665 |
| `debug-macos-arm64`, gate on, after Step 2 | 669 of 669 (581, and the 88 `clang_tidy_*`); no warning |
| `debug-macos-arm64`, gate off, after Step 2 | 581 tests (`ctest -N -E '^clang_tidy_'`): 563 gtests, 17 `cmake -P` checks and `test_kraken_example` |
| Crypto.com-only (the other three exchanges off), gate on, after Step 2 | built with no warning and listed with `ctest -N`, not run: 205 tests, 35 gate tests and 170 others, of which 153 gtests, 14 of them `CurlHttpClient` (9 `CurlHttpClientPerform`, 5 `CurlHttpClientTimeouts`) and 7 `CryptoComClient` |
| `ubuntu-dev` `debug-linux-arm64` (GCC 13.3, `-Werror`), after Step 1 | 577 of 577; no warning |
| `ubuntu-dev` `debug-linux-arm64`, after Step 2 | 581 of 581; no warning; the eight gate units of the files that Step 2 touched pass under clang-tidy 23 |
| CI `release-linux-x86_64` (GCC 15.2), `f4d74cf` | 577 of 577, 20.4 s of tests; the job took 2 min 11 s |
| CI `clang-tidy`, `f4d74cf` | 95 of 95, 157.0 s of tests; the job took 3 min 37 s |
| CI `release-linux-x86_64`, `7359107` | 581 of 581, 16.6 s of tests; the job took 4 min 36 s |
| CI `clang-tidy`, `7359107` | 95 of 95, 148.1 s of tests; the job took 3 min 25 s |

The gate's 95 units did not change, as planned: no `.cpp` was added. `CLAUDE.md` carries the new numbers: 552 → 563 gtests and 570 → 581
in ctest; the Crypto.com-only line 145 → 153 gtests (the Crypto.com suite 94 → 95) and 162 → 170; 7 `CurlHttpClient::perform` tests → 14
`CurlHttpClient` tests; and, for the structure tree, the rows of the four clients and `test_http_client`, the `KrakenRestClient`
constructor, the namespace row, and the two *Coding conventions* bullets (the one from plan curl-handle-reset now points at the new one).

### Opened outside this plan

- [#57](https://github.com/tomlin256/cryptocogs/issues/57) (enhancement): the other failures after a request went out
  (`Server returned nothing`, `Failure when receiving data from the peer`) still carry libcurl's words, which say nothing about whether the
  exchange acted on the request.
- [#58](https://github.com/tomlin256/cryptocogs/issues/58) (bug): when the server closes a reused connection after the request was sent
  and before any byte of a reply, libcurl sends the request again on a new connection and `perform` returns the second reply, so a POST can
  reach the server twice. Plan curl-handle-reset keeps connections across the reset, which is how a client comes to have one to reuse.

### What differed from the plan

- **The documentation push is not "documentation only" to CI.** Step 3 said no workflow would run. The `paths-ignore` lists of both
  workflows name `docs/**` and `CLAUDE.md` and not `README.md`, because `test_cmake_floor` reads it; so the push that carries the new
  README subsection runs both. The plan document alone would not.
- **`CLAUDE.md` said all I/O in the tests is mocked**, except two executables. The four adapter tests connect the production client to a
  `SilentListener` on 127.0.0.1, so that sentence, and the header comment of each of the three client test files that said "no libcurl", now
  name them.
- **The adapter tests spell `std::chrono::hours(1)` and `std::chrono::milliseconds(1)`** where the Step 1 tests use the literals `1h` and
  `1ms`. clangd's unused-include hint does not count a user-defined literal as a use of `<chrono>` and flagged the include; naming the
  type removes it.
- **Four mutations in Step 2, one per client**, where the plan said "a mutation that forwards in three adapters and not the fourth": the
  table above is that mutation four times.

### After the push (Step 4)

Run on `023fa0b`, which has the code of `7359107`. No finding, so no issue.

| Tree | Result |
|---|---|
| `asan-macos-arm64` (ASan and UBSan, Apple clang 21.0.0) with `ASAN_OPTIONS=detect_stack_use_after_return=1:strict_string_checks=1` and `UBSAN_OPTIONS=print_stacktrace=1` | 584 of 584, 9.3 s of tests; no report; no warning |
| `tsan-macos-arm64` with `TSAN_OPTIONS=second_deadlock_stack=1` | 583 of 583, 21.7 s of tests; no report; no warning |
| CI `Sanitizers`, `which=asan` (Linux, GCC 15.2, LeakSanitizer on), run 42286334863 | 584 of 584, 39.0 s of tests; no report; the job took 10 min 26 s |
| CI `Sanitizers`, `which=tsan`, run 42287925386 | 583 of 583, 35.0 s of tests; no report; the job took 5 min 31 s |
| CI on the documentation commit `023fa0b`: `release-linux-x86_64`, `clang-tidy` | 581 of 581 (14.5 s of tests; the job took 40 s), 95 of 95 (101.9 s of tests; 2 min 41 s) |

The 584 and 583 are the 581 and the sanitizer's own tests (the canaries and the coverage test). TSan was the one the plan watched: its
slowdown moves the 1 ms limits, and every live test passed under it, on the Mac and in CI (the phase moves, the outcome does not).
LeakSanitizer, which the Mac cannot run, ran the new tests in CI and reported nothing.
