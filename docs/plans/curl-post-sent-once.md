# `CurlHttpClient::perform` never lets libcurl send a POST or a DELETE twice

**Status:** Done (2026-11-06), released as [v2.1.0](https://github.com/tomlin256/cryptocogs/releases/tag/v2.1.0) (2026-11-06). Rob
approved the plan as written, so D1 to D4 stood at the defaults below (no release), and then asked for the release. Steps 0 to 4 are
recorded in the *Outcome*.

Closes [#58](https://github.com/tomlin256/cryptocogs/issues/58).

**Follows:** [plan curl-handle-reset](curl-handle-reset.md), which keeps connections across the handle reset (that is how a client comes to have a
connection to reuse), and [plan curl-timeouts](curl-timeouts.md), whose check of the failures other than a timeout found this.
**Not here:** the words of the error when a connection closes with no reply ([#57](https://github.com/tomlin256/cryptocogs/issues/57),
which this plan makes more pressing: D3); a way for a read-only POST to keep connection reuse (D2); a release (D4).

## Problem

`CurlHttpClient` keeps connections open between calls: plan curl-handle-reset kept them across `curl_easy_reset` on purpose. When libcurl writes a
request on a reused connection and the connection ends before a single byte of reply, it takes the connection for one that went stale
while idle, makes a new one and writes the same request again. Its own log says so: `Connection died, retrying a fresh connect (retry
count: 1)`. `perform` returns the second attempt's reply, and nothing in the result says there were two. The server may have received and
acted on the first. For a POST that places an order that is a possible double submit; for a DELETE that cancels one, the reply that comes
back is the second's, typically an "unknown order" error, although the first cancel went through. libcurl has no option that turns the
retry off.

It needs a connection that the server, or a load balancer in front of it, closes (or resets) after reading the request and before
answering: a crash or restart after processing, a proxy that gives up on its upstream. An order sent after a quiet spell, on a connection
the server is about to time out, is the request most exposed. Whether an exchange rejects a replay of the same signed request (a nonce
that must increase, a client order id) is not checked here, as in the issue.

### What I checked (2026-11-06, `db66a0a`)

A scratch program, not in the repo: a raw-socket HTTP/1.1 keep-alive server on 127.0.0.1 (one `poll()` thread) that records every request
it reads, with the connection it came on and how many requests that connection had carried, and that either answers 200 `ok` and keeps the
connection, or closes it with no reply after reading the whole request; `src/exchange/common/http_client.cpp` of `HEAD` compiled into it
with one block of options inserted per variant. macOS arm64 (Apple clang 21.0.0, libcurl 8.7.1, SecureTransport) and Linux aarch64 in
`ubuntu-dev` (GCC 13.3, libcurl 8.5.0, OpenSSL 3.0.13; CI's `ubuntu-latest` has the same libcurl). On Linux also a TLS version of the
server (OpenSSL, a self-signed certificate; a drop is a `close()` with no `close_notify`).

| Row | The client sends; the server closes…  | `HEAD` | `FRESH_CONNECT` on POST and DELETE |
|---|---|---|---|
| A (the issue) | `POST /first`, `POST /order`; `/order` with no reply if the connection has carried a request before | `perform` returns **200**; the server read `/order` **twice**, on connection 1 and then on 2 | returns 200; read **once**, on connection 2 |
| B | the same, `/order` closed on every connection | throws `Server returned nothing (no headers, no data)`; read **twice** | throws; read **once** |
| C | `POST /first`, `DELETE /order/1`, as A | returns 200; read **twice** | read **once** |
| G | `GET /a`, `POST /order`, as A | returns 200; read **twice**: the POST rode the GET's connection | read **once**, on a new connection |
| D | `GET /first`, `GET /order/g`, as A | returns 200 after libcurl re-sent the GET | unchanged: a GET keeps its retry |
| E | `GET` three times | 1 connection | 1 connection |
| F | `GET`, `POST`, `GET`, `DELETE`, `GET` | 1 connection | 3: the GETs share one, the POST and the DELETE have one each |
| H | ten `POST`s | 1 connection | 10 made, 5 still open at the end |
| I | after B, a `GET` | 200 | 200 (the client recovers) |

The rows are the same on Linux, plain and TLS (TLS: A to E and G), and with a reset (RST) in place of a clean close (A, B, C, G on the Mac):
a reset ends `perform` with `Failure when receiving data from the peer` instead, and is re-sent on `HEAD` and sent once with the option.
The variant block is the only difference between the programs behind the `HEAD` and the fixed column.

| Option on the request | A, B, C | G | E (GETs share a connection) |
|---|---|---|---|
| `FRESH_CONNECT` on POST and DELETE | once | once | yes |
| `FORBID_REUSE` on POST and DELETE | once | **twice**: the POST rides the GET's cached connection | yes |
| both, on POST and DELETE | once | once | yes; the order's connection is closed at once (ten POSTs leave 0 open) |
| `FRESH_CONNECT` on every method | once | once | **no**: three GETs make three connections |
| `FORBID_REUSE` on every method | once | once | **no**: the same |

- **The retry needs a reused connection.** With the option libcurl's log has no `Re-using existing connection` line and ends `Empty reply
  from server`; without it the line is there, then `upload completely sent off`, then `Connection died, retrying a fresh connect`, a new
  connection, and the same POST again. libcurl's manual says `CURLOPT_FRESH_CONNECT` makes "the next transfer use a new (fresh)
  connection by force instead of trying to reuse an existing one", and `CURLOPT_FORBID_REUSE` makes libcurl "explicitly close the
  connection when done with the transfer": it acts *after* a transfer, which is why it cannot stop a request from riding a connection
  that an earlier GET left.
- **The option is not sticky.** `curl_easy_reset` clears it, and row F shows it: the GETs after a POST and a DELETE still reuse a
  connection. It is set on each call, like the timeouts (plan curl-timeouts).
- **Connections do not pile up.** Ten POSTs leave 5 connections open, on both systems: libcurl's cache limit (`CURLOPT_MAXCONNECTS`,
  default 5, and at the limit "curl closes the oldest one in the cache").
- **Deterministic.** Rows A, B, C, E, F and G, 100 runs each per variant on the Mac: one distinct output per variant (600 scenario runs
  each), the log of requests and connections compared, nothing timed.
- **The cost of the option** is a TCP connect and a TLS handshake on each POST or DELETE, which libcurl resumes from its session cache when
  the server allows: an order that took one round trip on a warm connection takes about three on a fresh one (TLS 1.3). That is arithmetic,
  not a measurement against an exchange: nothing here talks to one.

## The change

**1. `src/exchange/common/http_client.cpp`.** After the method-specific options, on each call:

```cpp
// A POST or a DELETE must reach the server once. libcurl sends a request again, on a new connection, when a reused one closes before any
// byte of a reply ("Connection died, retrying a fresh connect"), and `perform` could not tell (#58). The retry happens only on a reused
// connection and libcurl cannot be told not to, so these requests never use one. Their connection stays cached and a GET may use it: a GET
// can be sent twice without harm, so it keeps libcurl's reuse and its retry. The reset above cleared this option, so it is set here, on each
// call.
if (http.method != M::GET)
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
```

`!= M::GET` and not `== M::POST || == M::DELETE`, so that a method added to `HttpRequest::Method` later is safe until someone decides
otherwise. `FORBID_REUSE` is not set (D1).

**2. `include/exchange/common/http_client.hpp`.** The comment on `perform` gains: a POST or DELETE is sent at most once, because it never
rides a connection that an earlier request used; each makes a connection (a TCP connect and a TLS handshake); if that connection closes
with no reply `perform` throws, and the request may have been received and acted on; a GET keeps reuse.

**3. Tests.** A new helper, `tests/unit/keepalive_server.hpp` (like `silent_listener.hpp`): `KeepAliveServer`, the scratch server as a
class. One `poll()` thread over a stop pipe, the listener and the open connections, with no timer (`poll` waits for ever and the
destructor wakes it); it reads each request in full (head, then `Content-Length` bytes), appends a `ReadRequest{connection, served_before,
method, target, body}` to a list under a lock, and then asks the test's `DropRule` what to do: `true` closes the connection with no reply
(the request was read in full, so the client sees a clean close and not a reset); `false` answers 200 `ok` and keeps the connection. The
test reads `requests()` and `connections()` (copies, under the lock) after `perform` has returned. The kernel picks the port. `SIGPIPE` is
guarded (`MSG_NOSIGNAL`, `SO_NOSIGPIPE`). Five new tests in `tests/unit/test_http_client.cpp`, in the existing `CurlHttpClientPerform`
fixture (libcurl initialised once per suite):

1. `APostThatTheServerClosesWithoutAReplyIsSentOnce`: the rule closes every `/order…`; `POST /first` (answered), then `POST /order`:
   throws `std::runtime_error` (the type, not the words: D3), and the server read `/order` exactly once. This is the issue's test.
2. `ADeleteThatTheServerClosesWithoutAReplyIsSentOnce`: the same, with `DELETE /order/1`.
3. `APostOrADeleteNeverRidesAConnectionAnEarlierRequestUsed`: a table, POST and DELETE each after a POST and after a GET; the rule closes
   `/order…` only on a connection that has carried a request before (row A above). Each row: `perform` returns 200, the server read two
   requests, and the second is on a connection other than the first's, with `served_before == 0`. The after-a-GET rows are what
   `FORBID_REUSE` alone would fail.
4. `GetsKeepSharingOneConnection`: three GETs, `connections() == 1`. Passes on `HEAD`; it holds plan curl-handle-reset's reuse against a fix that
   gives up on it.
5. `AMixOfMethodsGivesPostAndDeleteAConnectionEach`: `GET`, `POST`, `GET`, `DELETE`, `GET`: `connections() == 3`, and the POST and the
   DELETE each have `served_before == 0`. It does not say which cached connection a GET reuses, which is libcurl's choice.

The header comment of `test_http_client.cpp` gains a paragraph on connections; the seven tests that use `ix::HttpServer` are unchanged,
because that server says `Connection: close` and so never offers a connection to reuse.

**4. Docs** (Step 2): `README.md`, `CLAUDE.md`, and this plan's *Outcome*.

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 `CURLOPT_FRESH_CONNECT` on POST and DELETE; a GET keeps reuse.** It makes the guarantee by construction, in one line, and it
  removes more than the duplicate: a connection made for the order is not stale, so the "closed with no reply" failures that a stale idle
  connection causes, each of them an order of unknown outcome, stop happening to orders. The cost is the handshake.
  *Alternatives:* (i) `FORBID_REUSE` as well, which closes the order's connection at once (nothing idle is left by orders; a GET after an
  order cannot use its connection); it changes nothing about the guarantee, so I left it out, and you can say otherwise. (ii)
  `FORBID_REUSE` instead: rejected by row G. (iii) The option on every method, or reuse off: rejected by row E, since it undoes plan curl-handle-reset
  for market-data polling. (iv) Keep reuse for orders and veto libcurl's second connection once the request has gone out
  (`CURLOPT_OPENSOCKETFUNCTION` and a debug callback): saves the handshake, but it needs `CURLOPT_VERBOSE` to learn that the request went
  out, depends on libcurl calling the two callbacks in that order, was not measured, and turns every stale-connection race into a failed
  order of unknown outcome instead of avoiding it.
- **D2 Read-only POSTs pay the handshake too.** Kraken's private endpoints are all POST, and so are Crypto.com's, so a Kraken `Balance`
  or `OpenOrders` poll makes a connection each time; the transport cannot tell a query from an order. *Default:* accept it, and open a
  follow-up issue in Step 0 (label `enhancement`, priority low in the body): a per-request flag so an adapter can mark a read-only POST as
  repeatable and keep reuse for it. *Alternative:* do the flag now. It needs every Kraken and Crypto.com request classified by hand, and a
  wrong "read-only" mark on a state-changing endpoint brings this bug back, so the safe default should land first.
- **D3 #57 stays separate.** After this fix a connection that closes with no reply gives `Server returned nothing (no headers, no data)`
  (or `Failure when receiving data from the peer` for a reset), which says nothing about whether the exchange acted on the request, and
  the retry that used to hide some of these is gone. So #57 matters more, but its words are its own change; the tests here check the
  exception type and the number of times the server read the request, so #57 can reword the text. *Alternative:* fold #57 into this plan.
- **D4 No release in this plan; I recommend that #58 ride in the same `v2.1.0` as #51 and #56**, none of which is released (`v2.0.0` is the
  latest tag). It is a behaviour change (a handshake per order). A tag and a release are outward-facing, so yours to say: yes adds Step 4.

## Steps

Commits go straight to `main`, one per step, pushed once the checkpoint is green. No commit message carries a closing keyword: #58 is
closed by hand in Step 2. Counts are what I expect and are replaced by the measured ones in the *Outcome*.

### Step 0 — Approval

You approve or edit. Commit this plan (`docs(plan): plan 043 approved as written`, with the status line changed), push (documentation only:
no CI run), comment on #58 with the plan's path, and open the follow-up issue of D2. *Done when:* #58 links the plan, the plan links #58,
and the new issue's number is in your hands.

### Step 1 — The fix: tests red, then green

*Start from green.* `cmake --preset debug-macos-arm64 -DCRYPTOCOGS_CLANG_TIDY=clang-tidy`, build, full ctest: expected **669 of 669**.
*Done when* the baseline is recorded; if anything is red I stop and tell you.

*Tests (new)* `tests/unit/keepalive_server.hpp` and tests 1 to 5 above; the fourteen `CurlHttpClient` tests that exist are unchanged.

*Red first.* The new test file against `HEAD`'s `http_client.cpp`. Each test alone, as ctest runs it, under `perl -e 'alarm 10; exec @ARGV'`.
Expected: 1, 2, 3 and 5 fail by assertion (the server read `/order` twice; the POST was on connection 1 with `served_before == 1`; one
connection where three were wanted); 4 passes. Nothing hangs: `HEAD` returns or throws. Not committed: every commit builds and passes.

*Change.* `http_client.cpp` and the header comment, as above.

*Hand-made changes* (in the tree, the object deleted before each build because Apple's Make 3.81 compares one-second timestamps, the
number of objects compiled printed, the source compared with `HEAD` after), each test run alone under the alarm. Expected:

| Change | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| (a) no option (`HEAD`) | red | red | red | green | red |
| (b) the option set before the reset, which clears it | red | red | red | green | red |
| (c) `== M::POST` (DELETE left out) | green | red | red (the DELETE rows) | green | red |
| (d) `FORBID_REUSE` in place of `FRESH_CONNECT`, on POST and DELETE | green | green | red (the rows after a GET) | green | red |
| (e) `FRESH_CONNECT` on every method | green | green | green | red | red |
| (f) `FORBID_REUSE` on every method | green | green | green | red | red |
| (g) both options on POST and DELETE | green | green | green | green | green |
| the fix restored (byte-identical to the committed source) | green | green | green | green | green |

(g) is D1's alternative (i): the guarantee is the same and no test can tell them apart, nor should one.

*Repeated.* Each new test 200 times in a row (`ctest --repeat until-fail:200 -R <name>`), on the Mac and in `ubuntu-dev`: none fails.

*Checkpoint.* Full build and ctest of `debug-macos-arm64` with the gate on: **674 of 674** (669 and 5); no warning. The whole tree built in
`ubuntu-dev` (`debug-linux-arm64`, GCC 13.3, `-Werror`) and ctest there: **586 of 586** (581 and 5); it is also the Linux red-then-green
evidence for the five tests (libcurl 8.5.0). The new header is held by the clang-tidy gate in both.
*Done when:* red as stated, then green; (a) to (g) as stated; both checkpoints green; repeats clean; pushed; and the `release-linux-x86_64`
and `clang-tidy` runs of the commit green (GCC 15.2, clang-tidy 23).

### Step 2 — Docs, counts, close

`README.md`: a `### REST — connections` subsection after *timeouts*: a POST or DELETE makes its own connection, so an order is never sent
twice by the transport; the cost; and that a call which fails after the request went out may have been acted on. `CLAUDE.md`: the
structure tree (`keepalive_server.hpp`; the `http_client.cpp` and `test_http_client.cpp` lines), *Running tests* (563 → 568 gtests, 581 →
586 in ctest, the Crypto.com-only line 153 → 158 gtests and 170 → 175, 14 → 19 `CurlHttpClient` tests), the *Test suite breakdown* row,
and the *Coding conventions* bullets: plan curl-timeouts's last sentence (which says a POST can reach the server twice, #58) becomes a pointer, and a
new bullet records what was done. Counts are **measured** (`ctest -N` in the default tree and a Crypto.com-only tree with the gate on); the
gate's 95 units should not change (no new `.cpp`). This plan's *Outcome*. The gate turned off in the tree again. Full ctest, because
`test_cmake_floor` reads the README. *Done when:* pushed, and #58 closed by hand (`completed`) with a comment that lists the commits, the
red-then-green record and the counts. `README.md` is not in the workflows' `paths-ignore`, so this push runs both.

### Step 3 — After the push: measure

Not blocking the push. `asan-macos-arm64` and `tsan-macos-arm64`, full suites, with the options in `CLAUDE.md` (*Sanitizers*): TSan is the
one to watch, since the server thread and the test thread share the request list. Then `gh workflow run sanitizers.yml --ref main -f
which=asan`, then `tsan`, one at a time. *Done when* the results are in the *Outcome*; a finding gets its own issue.

### Step 4 — Release (only if D4 is yes)

Bump the version in `CMakeLists.txt` and the `GIT_TAG` example in `README.md`, full ctest, commit, push, wait for CI, an annotated
`v2.1.0` tag, and `gh release create v2.1.0` with notes that name #51, #56 and #58.

## Designs I rejected

- **`FORBID_REUSE` on POST and DELETE.** Closes a connection after the transfer, so it does not stop a POST from riding one that a GET
  left (row G measured: sent twice).
- **The option on every method, or no reuse at all.** A GET is harmless to repeat, and plan curl-handle-reset kept connections for it (row E).
- **A short `CURLOPT_MAXAGE_CONN` for POST and DELETE.** It narrows the window in which a reused connection is old; it does not close it.
- **Detecting the retry instead of preventing it** (`CURLINFO_NUM_CONNECTS`, or counting `CURLINFO_HEADER_OUT` events in a debug callback).
  It can only report after the second send, and the duplicate is the harm. And `NUM_CONNECTS` cannot tell: measured, the retried POST of
  row A reports 1, as does a POST on a connection of its own.
- **Vetoing the second connection** (D1 alternative iv): more code, unmeasured, and it keeps the stale-connection race.
- **An idempotency key at the HTTP layer.** What a replay does is each exchange's rule, not the transport's.

## Self-review — risks and assumptions

- **Latency.** Each POST or DELETE pays a TCP connect and a TLS handshake: about three round trips for an order that took one, no figure
  measured against an exchange. WebSocket order entry (Kraken, Binance) does not pay it.
- **Read-only POSTs pay it too** (D2): the poll rate of Kraken's private queries is the exposure.
- **A failure is no longer hidden.** A stale idle connection under an order used to be hidden by the retry (and the order sent twice);
  now the order has a fresh connection, so that failure mostly does not occur. What remains is a real close after the request, which now
  throws, with words that do not yet say the outcome is unknown (D3, #57).
- **Connections held.** libcurl keeps at most five idle connections (measured: ten POSTs leave five), so a burst of orders does not
  accumulate sockets; the server's idle timeout closes the five that remain. D1 alternative (i) closes them at once.
- **The mechanism is libcurl's, measured on 8.7.1 and 8.5.0.** A libcurl that retried on a fresh connection too would make tests 1 and 2
  red, which is what they are for. The retry paths that exist all need a reused connection (libcurl's log says `Re-using` first); I
  measured the no-reply path, as a clean close and as a reset, and not the send-error path (`Re-used connection seems dead`), which
  needs a reused connection too and cannot be provoked without a race.
- **Not measured:** HTTP/2 (the scratch servers speak HTTP/1.1; a new h2 connection per POST is what the option does, and a server's
  `GOAWAY` or refused stream is safe to retry by protocol); a proxy from `http_proxy`; TLS on the Mac (SecureTransport), where the retry
  logic is above the TLS layer; libcurl newer than 8.7.1.
- **The helper is new socket code in the test tree.** It reads a request in full before it closes, so the client sees a FIN and not a
  reset (unread bytes would turn the close into an RST, and `perform` would fail with other words). It has no timer: `poll` with no
  timeout, woken by a stop pipe in the destructor, then joined. Shared state is behind one mutex, which TSan checks in Step 3.
  `-Werror` and the clang-tidy gate apply to it (`bugprone-*` and `performance-*`): no namespace-scope strings, `std::ignore` for
  discarded results, the destructor cannot throw.
- **The tests lean on the kernel and libcurl in two ways:** a server that never closes an idle connection gets it reused (rows E and F:
  100 of 100 runs on the Mac, one run in the container), and a connection closed after a full read is a clean close (rows A to C on both).
- **A GET can still be sent twice.** That is deliberate. A signed GET carries the same timestamp the second time; whether an exchange
  takes it twice is not checked here, and a read does no harm either way.
- **Platforms:** the helper uses POSIX sockets, as `silent_listener.hpp` does; the build guard names macOS and Linux only.
- **Time:** each test is a few milliseconds; the five add under a second to the suite.

## Outcome

Steps 0 to 4 are done (2026-11-06); Steps 3 and 4 are at the end of this section. D4 stood at no release; Rob then asked for it
(*"Do the release"*), and `v2.1.0` was released for [#48](https://github.com/tomlin256/cryptocogs/issues/48),
[#51](https://github.com/tomlin256/cryptocogs/issues/51), [#56](https://github.com/tomlin256/cryptocogs/issues/56) and #58 together.

### Commits

| Step | Commit | What |
|---|---|---|
| 0 | `03d2d94` | this plan, approved as written |
| 1 | `1a34a3e` | `perform` sets `CURLOPT_FRESH_CONNECT` on every request that is not a GET, on each call, after the reset; `KeepAliveServer`; five tests |
| 2 | `7ae151c` | `README.md`, `CLAUDE.md` and this *Outcome*; #58 closed by hand after the push |
| 3 | `a46fbf8` | the record of the sanitizer runs |
| 4 | `cec63b5` | `chore: bump version to 2.1.0`; the annotated tag `v2.1.0` on it and the GitHub release (Latest) |
| 4 | the commit that adds the last section below | the record of the release, and the status lines of plans curl-header-list-append-failure, curl-handle-reset and curl-timeouts |

### Red, then green

**Step 1** (`debug-macos-arm64`, Apple clang 21.0.0, libcurl 8.7.1). Each test alone, as ctest runs it, under a 10 s alarm. "Without" is
the new test file over `HEAD`'s `http_client.cpp`, which is change (a) below; it was not committed. Nothing hung: without the fix
`perform` returns or throws.

| Test | Without | With |
|---|---|---|
| 1 `APostThatTheServerClosesWithoutAReplyIsSentOnce` | fails by assertion: the server read `/order` 2 times, not 1 (the issue's evidence) | passes |
| 2 `ADeleteThatTheServerClosesWithoutAReplyIsSentOnce` | fails by assertion: `/order/1` read 2 times | passes |
| 3 `APostOrADeleteNeverRidesAConnectionAnEarlierRequestUsed` | fails in all four rows: 3 requests read where 2 were wanted ("the order was written more than once") | passes |
| 4 `GetsKeepSharingOneConnection` | passes | passes |
| 5 `AMixOfMethodsGivesPostAndDeleteAConnectionEach` | fails: 1 connection where 3 were wanted; the POST on a connection with `served_before == 1`, the DELETE with `served_before == 3` | passes |

The same in `ubuntu-dev` (GCC 13.3, libcurl 8.5.0; the incremental `debug-linux-arm64` build, the object, the archive and the binary
deleted before each build, 1 object compiled each time): red, red, red, green, red without the fix with the same values as above, and
all five green with it.

### Hand-made changes

**Step 1**, to `perform` in `http_client.cpp`. Each in the tree with its object, the archive and the test binary deleted first (Apple's
Make 3.81 compares whole seconds), the number of objects compiled printed (1 every time, no warning), the source compared with a saved
copy of the fix afterwards (byte-identical), and each test run alone under the 10 s alarm. `red` is a failed assertion; no row hung or
crashed. Every row is what the plan predicted.

| Change | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| (a) no option (`HEAD`) | red | red | red | green | red |
| (b) the option set before the reset, which clears it | red | red | red | green | red |
| (c) `== M::POST` (DELETE left out) | green | red | red | green | red |
| (d) `FORBID_REUSE` in place of `FRESH_CONNECT`, on POST and DELETE | green | green | red | green | red |
| (e) `FRESH_CONNECT` on every method | green | green | green | red | red |
| (f) `FORBID_REUSE` on every method | green | green | green | red | red |
| (g) both options on POST and DELETE | green | green | green | green | green |
| the fix restored (byte-identical to the committed source) | green | green | green | green | green |

(d) is why test 3 has rows after a GET: `FORBID_REUSE` closes a connection after its transfer, so a POST that follows a GET still rides the
GET's, and only those rows (and the sequence of test 5) see it. (e) and (f) are why test 4 exists: without it, an option on every method
would pass tests 1 to 3 and undo plan curl-handle-reset for market-data polling. (g) is D1's alternative (i): the guarantee is the same and
no test can tell the two apart, nor should one. The nine `CurlHttpClientPerform` tests that existed before this plan were not run row by
row; the full suite is the checkpoint below.

### Repeated

Each new test 200 times in a row, `ctest --repeat until-fail:200`, as ctest runs it: `debug-macos-arm64` 1000 of 1000 passes (10.9 s),
`ubuntu-dev` `debug-linux-arm64` 1000 of 1000 (3.7 s). None failed.

### Counts (measured)

| Tree | Result |
|---|---|
| `debug-macos-arm64`, gate on, before Step 1 | 669 of 669 |
| `debug-macos-arm64`, gate on, after Step 1 | 674 of 674 (586, and the 88 `clang_tidy_*`); no warning; 17 objects compiled |
| `debug-macos-arm64`, gate off, after Step 1 | 586 tests (`ctest -N -E '^clang_tidy_'`): 568 gtests, 17 `cmake -P` checks and `test_kraken_example` |
| Crypto.com-only (the other three exchanges off), gate on, after Step 1 | built with no warning and run: 210 of 210, 35 gate tests and 175 others, of which 158 gtests, 19 of them `CurlHttpClient` (14 `CurlHttpClientPerform`, 5 `CurlHttpClientTimeouts`) |
| `ubuntu-dev` `debug-linux-arm64` (GCC 13.3, `-Werror`), after Step 1 | 586 of 586; no warning; 18 objects compiled |
| `ubuntu-dev` `debug-linux-arm64`, gate on (clang-tidy 23.1.6) | the 88 `clang_tidy_*` pass, `clang_tidy_tests_unit_test_http_client` (which includes the new header) among them; the gate switched off in the tree again |
| CI `release-linux-x86_64` (GCC 15.2), `1a34a3e` | 586 of 586, 17.8 s of tests; the job took 6 min 4 s, of which the apt step took 3 min 57 s ([#41](https://github.com/tomlin256/cryptocogs/issues/41)) |
| CI `clang-tidy`, `1a34a3e` | 95 of 95, 214.5 s of tests; the job took 4 min 42 s (clang-tidy 23.1.6) |

The gate's units did not change, as planned: no `.cpp` was added (88 in a default tree, 95 with the benchmarks, 35 in a Crypto.com-only
one). `CLAUDE.md` carries the new numbers: 563 → 568 gtests and 581 → 586 in ctest; the Crypto.com-only line 153 → 158 gtests and 170 →
175; 14 → 19 `CurlHttpClient` tests; and, for the structure tree, the `http_client.cpp` and `test_http_client.cpp` lines and the new
`keepalive_server.hpp`, the `test_http_client` rows of the two test tables, and the *Coding conventions*: plan curl-timeouts's bullet no longer says
that a POST can reach the server twice, and a new bullet records this plan. `README.md` has a `### REST — connections` subsection after
*timeouts*.

### Opened outside this plan

- [#59](https://github.com/tomlin256/cryptocogs/issues/59) (enhancement, priority low in the body): a per-request flag so that an adapter
  can mark a request that only reads, and `perform` keeps connection reuse for it (D2). Every Kraken and Crypto.com private endpoint is a
  POST and now pays a connect and a handshake; a wrong "read-only" mark on a state-changing endpoint would bring #58 back, which is why
  it is separate from this plan.

### What differed from the plan

- **The Linux red half was run on its own.** The plan called the full 586-of-586 run in `ubuntu-dev` "also the Linux red-then-green
  evidence"; a green run is only the green half. The five tests were run against the transport without the fix in the same container
  (the table above), then with it.
- **The Crypto.com-only tree was run, not only listed**: 210 of 210, where plan curl-timeouts recorded such a tree as built and listed with
  `ctest -N`.
- **The first version of `KeepAliveServer::serve` had two identical branches** that each closed the connection (the editor's clang-tidy,
  `bugprone-branch-clone`); they became one condition, `drop_(request) || !send_all(...)`, before the first build.

### After the push (Step 3)

Run on `7ae151c`, which has the code of `1a34a3e`. No finding, so no issue.

| Tree | Result |
|---|---|
| `asan-macos-arm64` (ASan and UBSan, Apple clang 21.0.0) with `ASAN_OPTIONS=detect_stack_use_after_return=1:strict_string_checks=1` and `UBSAN_OPTIONS=print_stacktrace=1` | 589 of 589, 27.8 s of tests; no report; no warning; 18 objects compiled |
| `tsan-macos-arm64` with `TSAN_OPTIONS=second_deadlock_stack=1` | 588 of 588, 84.2 s of tests; no report; no warning; 18 objects compiled |
| CI `Sanitizers`, `which=asan` (Linux, GCC 15.2, LeakSanitizer on), run 42295809369 | 589 of 589, 33.4 s of tests; no report; the job took 8 min 22 s |
| CI `Sanitizers`, `which=tsan`, run 42297040688 | 588 of 588, 35.0 s of tests; no report; the job took 5 min 31 s |
| CI on the documentation commit `7ae151c`: `release-linux-x86_64`, `clang-tidy` | 586 of 586 (16.1 s of tests; the job took 43 s), 95 of 95 (125.6 s of tests; 2 min 59 s) |

The 589 and 588 are the 586 and the sanitizer's own tests (the canaries and the coverage test). The two Mac runs were serial, as the
commands in `CLAUDE.md` are written, which is why their times are longer than the parallel runs of plan curl-timeouts. TSan was the one the plan
watched: the server thread of `KeepAliveServer` and the test thread share the request list and the connection count, behind one mutex,
and every test that uses it passed under TSan on the Mac and in CI. Each of the five new tests passes under both sanitizers on the Mac.
Under TSan on the Mac every `CurlHttpClientPerform` test takes about 2.1 s, the nine that existed before this plan included; the
`CurlHttpClientTimeouts` tests take 0.03 s to 0.06 s. That was not looked into. LeakSanitizer, which the Mac cannot run, ran the new
tests in CI and reported nothing.

### Release (Step 4)

Rob asked for it (*"Do the release"*), so D4's "yes" ran, with the version that plans curl-timeouts and curl-post-sent-once named: `v2.1.0`,
a minor release, for the fixes of plans curl-header-list-append-failure (#48), curl-handle-reset (#51), curl-timeouts (#56) and this one
(#58), none of which was in a tag. The bump commit `cec63b5` changes `project(CRYPTOCOGS VERSION …)` in `CMakeLists.txt` and the `GIT_TAG`
example in `README.md`, and nothing else.

| Check | Result |
|---|---|
| `debug-macos-arm64` and `release-macos-arm64` after the bump | both build with no warning (0 and 21 objects compiled) and pass 586 of 586 |
| an install of the release tree to a scratch prefix | `find_package(cryptocogs 2)`, `2.1` and `2.1.0` find 2.1.0; `1`, `3` and `2.2` are refused with "The version found is not compatible with the version requested" |
| CI `release-linux-x86_64` (GCC 15.2), `cec63b5` | 586 of 586, 12.9 s of tests; the job took 55 s |
| CI `clang-tidy`, `cec63b5` | 95 of 95, 143.9 s of tests; the job took 6 min 25 s |
| the tag | annotated, `v2.1.0` on `cec63b5`, pushed after both runs were green; the GitHub release is not a draft and is Latest |

- **The notes name more than the plan said.** Step 4 named #51, #56 and #58. The notes also name #48, which was fixed after v2.0.0 (plan
  curl-header-list-append-failure) while v2.0.0's own notes list it as still open, and, under *Also in this tag*, #49, #50 and #52 (CI and
  an example, not the library).
- **The layout.** `CurlHttpClient` grows from 40 to 56 bytes (Apple clang 21, libc++, arm64) because it holds its `HttpTimeouts`. The four
  REST clients keep their size (48, 48, 48 and 56): they hold the transport through a `std::shared_ptr`. Plan curl-timeouts's D6 chose a minor
  version for "a new defaulted constructor parameter" and did not measure this, and v2.0.0 was a major release for a layout change (the 14
  enums). The notes say it and tell a consumer to rebuild what includes the headers, as v2.0.0's did. The number stands, since a tag is not
  moved; `CurlHttpClient` is the one public class whose size changed, and the REST clients construct it for the caller.
- **Two tests were left out of the local runs.** While this was done the working tree held changes that are not part of this plan and
  were not committed with it: `tests/doc_checks/` and an edit of `tests/CMakeLists.txt` (a size budget for `CLAUDE.md`, plan claude-md-trim, #60).
  Its `test_claude_md_budget` fails by design on today's `CLAUDE.md` (159,987 characters against 38,000). The two budget tests were
  excluded from the local ctest runs above (588 tests with them, 586 without, one red), and the bump commit was made with
  `git commit -o CMakeLists.txt README.md`, so it holds none of that work. CI ran on the commit alone.
