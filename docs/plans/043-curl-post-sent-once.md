# Plan 043 — `CurlHttpClient::perform` never lets libcurl send a POST or a DELETE twice

**Status:** In progress. Rob approved the plan as written on 2026-11-06, so D1 to D4 stand at the defaults below (no release). Step 0 is
done; the rest is recorded in the *Outcome* as it is done.

Closes [#58](https://github.com/tomlin256/cryptocogs/issues/58).

**Follows:** [plan 041](041-curl-handle-reset.md), which keeps connections across the handle reset (that is how a client comes to have a
connection to reuse), and [plan 042](042-curl-timeouts.md), whose check of the failures other than a timeout found this.
**Not here:** the words of the error when a connection closes with no reply ([#57](https://github.com/tomlin256/cryptocogs/issues/57),
which this plan makes more pressing: D3); a way for a read-only POST to keep connection reuse (D2); a release (D4).

## Problem

`CurlHttpClient` keeps connections open between calls: plan 041 kept them across `curl_easy_reset` on purpose. When libcurl writes a
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
  connection. It is set on each call, like the timeouts (plan 042).
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
4. `GetsKeepSharingOneConnection`: three GETs, `connections() == 1`. Passes on `HEAD`; it holds plan 041's reuse against a fix that
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
  `FORBID_REUSE` instead: rejected by row G. (iii) The option on every method, or reuse off: rejected by row E, since it undoes plan 041
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
and the *Coding conventions* bullets: plan 042's last sentence (which says a POST can reach the server twice, #58) becomes a pointer, and a
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
- **The option on every method, or no reuse at all.** A GET is harmless to repeat, and plan 041 kept connections for it (row E).
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

To be written as the steps are done.
