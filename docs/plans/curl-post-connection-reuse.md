# `CurlHttpClient::perform`: a POST or a DELETE reuses a connection, and is still written at most once

**Status:** Done (2026-11-22), except that the CI runs of the push had not finished when Step 3 was recorded: their numbers are in
the *Outcome*. Rob approved the plan as written, so D1 to D4 stood at their defaults below. Steps 0 to 3 are recorded in the
*Outcome*, with what differed from the plan.

Closes [#59](https://github.com/tomlin256/cryptocogs/issues/59) and [#68](https://github.com/tomlin256/cryptocogs/issues/68), the
HTTP/2 finding of the *Problem*.

**Follows:** [plan curl-post-sent-once](curl-post-sent-once.md) (#58), which gave every POST and DELETE a connection of its own (its D1)
and opened #59 for what that costs (its D2). This plan revises that D1.
**Not here:** the words of the other failures that follow a sent request
([#57](https://github.com/tomlin256/cryptocogs/issues/57): D3); an HTTP/2 server in the test tree
([#69](https://github.com/tomlin256/cryptocogs/issues/69): D2); a release (D4).

## Problem

Since v2.1.0 `perform` sets `CURLOPT_FRESH_CONNECT` on every POST and DELETE, so that libcurl can never write one twice. Each such call
makes a connection of its own, a TCP connect and a TLS handshake, however many are open. #59 is that cost for the calls that only
read: every Kraken and Crypto.com private endpoint is a POST. It proposes a flag on `HttpRequest` that an adapter sets by hand, and
says of itself that a wrong flag on a state-changing endpoint brings #58 back.

Rob asked whether the answer is a pool of connections. libcurl already keeps one: its connection cache, which a GET uses today. What
plan curl-post-sent-once took from a POST is the use of it, because libcurl writes a request again when a pooled connection dies under
it. So the question is how to make the pool safe for a POST, and the measurements below say it can be made safe for every POST and
DELETE, an order included, with nothing to mark by hand.

Two things were not known when #59 was written:

- **The cost, measured.** A connection of its own adds about 45 to 65 ms to every POST and DELETE from this desk (table 1). An order
  pays it as well as a poll.
- **The rule of #58 is not true on the wire over HTTP/2**, which all four exchanges speak to libcurl (table 2). When a server refuses
  an HTTP/2 stream, libcurl writes the request again on another connection, `FRESH_CONNECT` or not: four writes of one POST were
  measured with today's options (table 4b). HTTP/2 says a refused request was not processed and "can be safely retried" (RFC 9113,
  8.7), so a server that keeps to the protocol does not act twice, but "sent at most once" then rests on the server's word and not on
  the transport. This is [#68](https://github.com/tomlin256/cryptocogs/issues/68).

## What I checked (2026-11-22, `fc66fd5`)

Scratch programs, not in the repo. macOS arm64 (Apple clang 21.0.0, libcurl 8.7.1, SecureTransport, nghttp2 1.69.0) and Linux aarch64
in `ubuntu-dev` (GCC 13.3, libcurl 8.5.0, OpenSSL 3.0.13, nghttp2 1.59.0; the libcurl that plan curl-post-sent-once recorded for CI's
`ubuntu-latest`). The HTTP/1.1 rows use a copy of `tests/unit/keepalive_server.hpp` with a reset added; the HTTP/2 and TLS rows use a
Node 26 server (`node:http2`, cleartext for table 4b and TLS with a scratch certificate for table 5), which ran on the Mac for both
clients: the container reached it through Docker Desktop's network, so where its rows differ from the Mac's the path differs as well
as libcurl. Tables 1 and 2 are the only ones that talk to an exchange (public, unauthenticated GETs) and the only ones that report a
time; table 5 waits 150 ms so that a close has arrived, and the rest wait for nothing.

### 1. What a connection of its own costs

One warm-up, then six pairs: a GET that may reuse the connection, and one with `CURLOPT_FRESH_CONNECT`, with the options `perform` sets
and 0.4 s between requests. Total time of the call, milliseconds, median and range:

| Host, client | Reused | A connection of its own | Extra |
|---|---|---|---|
| `api.kraken.com`, Mac | 23.1 (21.8 to 24.8) | 72.5 (64.2 to 76.4) | 49 |
| `api.kraken.com`, container | 28.1 (23.2 to 36.0) | 73.0 (64.1 to 103.2) | 45 |
| `api.crypto.com`, Mac | 267.1 (264.2 to 290.4) | 332.0 (322.5 to 378.7) | 65 |
| `api.crypto.com`, container | 279.7 (270.0 to 291.0) | 344.4 (327.9 to 354.7) | 65 |

On the Mac the TCP connect ends at 14 to 17 ms and the TLS handshake at 36 to 48 ms; the reply then takes longer than on a warm
connection. One desk, one afternoon, a round trip of about 14 ms: a host nearer an exchange pays less in milliseconds and the same in
round trips. A GET was timed because no private call can be made without credentials; the connection cost does not depend on the method.

### 2. The four exchanges: the protocol, and how long an idle connection lasts

One GET, then one more after each idle gap, reuse allowed, Mac:

| Host | Protocol | Reused after an idle gap of 5, 20, 45, 70, 100, 110 and 117 s |
|---|---|---|
| `api.kraken.com` | HTTP/2 | every one |
| `api.crypto.com` | HTTP/2 | every one |
| `api.exchange.coinbase.com` | HTTP/2 | every one (its replies were 400: it wants a `User-Agent`, which the probe did not send) |
| `api.binance.com` | HTTP/2 | every one |

libcurl itself stops reusing a connection that has been idle for more than 118 s (`CURLOPT_MAXAGE_CONN`'s default), so within the
range that libcurl reuses, none of the four edges had closed the connection.

### 3. A pool of connections opened ahead of the request

| What was tried | Result, the same on both systems |
|---|---|
| `CURLOPT_CONNECT_ONLY`, then a POST on the same handle | 2 connections: the POST made its own and the one opened ahead carried nothing |
| `CONNECT_ONLY` on one handle, a POST on another, the two sharing one cache (`CURLSH`) | the same |
| a request stopped in the pre-request callback, when it is connected and has sent nothing, then a POST | libcurl closed the first connection (`operation aborted by pre-request callback`, `Closing connection`); the POST made its own |
| a TCP socket connected by the caller and handed over (`CURLOPT_OPENSOCKETFUNCTION`, `CURL_SOCKOPT_ALREADY_CONNECTED`) | used, as a new connection: 1 connection at the server, and no second write when the server closes it |

libcurl's source says why (8.5.0, 8.7.1 and 8.17.0 read): "connect-only or to-be-closed connections will not be reused", and a call that
its pre-request callback stops is ended as a stream error, which closes the connection. So libcurl cannot be handed a connection that
is open, has its TLS session and has carried nothing. A TCP socket can be handed over, which saves the 14 to 17 ms of the connect and
leaves the handshake.

### 4. Stopping the second write: `CURLOPT_PREREQFUNCTION`

libcurl's manual: the callback "gets called by libcurl after a connection has been established or a connection has been reused
(including any SSL handshaking), but before any request is actually made on the connection", and it returns `CURL_PREREQFUNC_ABORT`
"to cause the transfer to fail" (added in 7.80.0). In the source it is the first thing in the state that writes a request, and every
retry goes back to the connect state and so through it again. The probe's callback counts its calls and aborts at the second.

**4a. HTTP/1.1.** An earlier request, then the order, reuse allowed (no `FRESH_CONNECT`). Identical on both systems. A dash is a
case the probe did not run; the two cases of the first dash are rows C and G of plan curl-post-sent-once (read twice).

| The order, and what the server does with it | No callback | With the callback |
|---|---|---|
| POST after a POST; closed with no reply because the connection was used before | returns **200**; read **twice** (`Connection died, retrying a fresh connect`) | fails `Operation was aborted by an application callback`; read **once**; 2 calls of the callback |
| the same, every order closed | fails `Server returned nothing`; read **twice** | fails; read **once** |
| DELETE after a POST; POST after a GET | — | read **once** |
| POST after a POST, a reset (RST) in place of the close | returns 200; read **twice** | read **once** |
| DELETE after a GET, a reset | — | read **once** |
| three POSTs, nothing closed | — | 1 connection; one call of the callback for each POST |
| `FRESH_CONNECT` and the callback, every order closed | — | fails `Server returned nothing`; read once; one call |
| a GET after each of the failed rows | — | 200: the client recovers |

**4b. HTTP/2.** A POST, then the order as a POST, reuse allowed except in the last row. The counts are the server's own, of the
requests it read.

| What the server does with the order | libcurl 8.7.1, no callback | libcurl 8.5.0, no callback | With the callback, both |
|---|---|---|---|
| answers it | once | once | once; one call |
| resets the stream with `REFUSED_STREAM` | **twice** | **twice** | **once**; fails |
| sends GOAWAY with a last stream below the order's, then closes | **twice** | **twice** | **once**; fails |
| resets the TCP connection | **twice** | **twice** | **once**; fails |
| closes the TCP connection, no GOAWAY | once; fails `Error in the HTTP2 framing layer` | **twice** | **once**; fails |
| ends the session after reading (GOAWAY that covers the order) | once; fails the same way | **twice** | **once**; fails |
| **today's options** (`FRESH_CONNECT` on the order): refuses the stream of the first three orders it sees | **four times**, then 200 | **four times**, then 200 | **once**; fails |

The last row is the finding in the *Problem*: `Curl_retry_request` has two conditions, and the one for a refused stream does not ask
whether the connection was reused.

### 5. A connection that the server closed while it was idle

TLS, both protocols, both systems. The server closes the connection after its reply (HTTP/1.1: a TLS shutdown, and a destroyed socket;
HTTP/2: GOAWAY then close, and a destroyed session, 50 ms after the reply); the client waits 150 ms and sends a POST with the callback.
In all eight runs libcurl says `Connection 0 seems to be dead`, makes a new connection, and the POST is read once and answered, with one
call of the callback. So a close that has reached the client costs nothing. Only a close that crosses the request in flight does.

## The change

**1. `src/exchange/common/http_client.cpp`.** The `FRESH_CONNECT` block goes, and in its place:

```cpp
// A POST or a DELETE is written at most once. libcurl writes a request again, on another connection, when the first attempt gets no
// reply: a reused connection that closes or is reset under it, an HTTP/2 stream that the server refuses. It cannot tell a request that
// the server never read from one that it read and acted on, so an order would reach the exchange twice, and no option turns that off.
// libcurl does call this callback each time it is about to write the request, on a new connection or a reused one: the callback lets
// the first write through and ends the call at a second. A GET is safe to repeat and gets no callback. The reset above clears both
// options, so they are set on every call; `once` lives until curl_easy_perform returns, and the next call resets the handle first.
SendOnce once;
if (http.method != M::GET) {
    curl_easy_setopt(curl, CURLOPT_PREREQFUNCTION, &allow_one_send);
    curl_easy_setopt(curl, CURLOPT_PREREQDATA, &once);
}
```

`SendOnce` is a count, and `allow_one_send` returns `CURL_PREREQFUNC_OK` for the first call and `CURL_PREREQFUNC_ABORT` after it; both
are in the file's unnamed namespace. `!= M::GET`, as today, so that a method added later is safe until someone decides otherwise. After
`curl_easy_perform`, `CURLE_ABORTED_BY_CALLBACK` (which only this callback can cause: the write callback takes everything it is given)
throws a `std::runtime_error` with its own text, which ends with the sentence a timeout already uses:

```
curl_easy_perform failed: no reply came to the first attempt, and the request was not sent a second time. The request may have been
received and acted on: its outcome is unknown, so check the exchange before sending it again.
```

`CurlHttpClient` gains no member, so no public type changes its layout.

**2. `include/exchange/common/http_client.hpp`.** The paragraph on `perform` about a POST or a DELETE is rewritten: it may reuse a
connection, as a GET does; it is written at most once, whatever the protocol and whichever connection it is on; if the first attempt
gets no reply `perform` throws the text above and the request may have been acted on; a GET may still be sent again.

**3. libcurl 7.80.0 is the floor.** `find_package(CURL 7.80.0 REQUIRED)` in the root `CMakeLists.txt`,
`find_dependency(CURL 7.80.0)` in `cmake/cryptocogsConfig.cmake.in`, and "libcurl 7.80.0 or later" in the dependency table of
`README.md`. Below it the callback does not exist and `http_client.cpp` does not compile; the two lines say so at configure time, to
whoever builds the library and to whoever finds the installed package. A new check, `test_curl_floor`
(`tests/cmake_checks/check_curl_floor.cmake`, in the manner of `test_cmake_floor`), holds the three to one version, and
`test_curl_floor_fixtures` gives it one accept case and three reject cases: no version at the root, another version in the package
config, a README that does not name it.

**4. Tests**, all in `tests/unit/test_http_client.cpp`, in the `CurlHttpClientPerform` fixture, against `KeepAliveServer` (unchanged):

1. `APostThatTheServerClosesWithoutAReplyIsSentOnce` and 2. `ADeleteThat…`: kept. Each gains two checks: the text says `was not sent a
   second time` and `outcome is unknown`.
3. `APostOrADeleteNeverRidesAConnectionAnEarlierRequestUsed` becomes `APostOrADeleteOnAUsedConnectionThatClosesIsSentOnce`. The same
   four rows (POST and DELETE, each after a POST and after a GET) and the same rule (the server closes an order only on a connection
   that has carried a request). Each row now: `perform` throws the text above; the server read two requests, the order **once**, on
   the earlier request's connection (`served_before == 1`); and a GET afterwards returns 200.
4. `GetsKeepSharingOneConnection`: unchanged.
5. `AMixOfMethodsGivesPostAndDeleteAConnectionEach` becomes `AMixOfMethodsSharesOneConnection`: GET, POST, GET, DELETE, GET make **1**
   connection, each request on a connection that has carried all the ones before it. It also holds the count to one call: a count kept
   across calls would stop the DELETE.
6. New, `AGetOnAUsedConnectionThatClosesIsSentAgain`: a GET, then a GET that the server closes on a used connection: `perform` returns
   200 and the server read it twice. A GET keeps libcurl's retry, which nothing held until now.

Tests 3 and 5 are rewritten because the behaviour they describe is the one this plan changes; the rule they stood for, that an order
is read once, is still asserted by 1, 2 and 3.

**5. Docs** (Step 2): `README.md` (*REST — connections*, the dependency table), `CLAUDE.md` (the last bullet of *Coding conventions*),
the header comment of `test_http_client.cpp`, one line in the status of plan curl-post-sent-once that points here, and this plan's
*Outcome*.

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 Every POST and DELETE may reuse a connection, an order included.** It removes the 45 to 65 ms from every such call, it needs no
  mark on any request, on these four exchanges or on a later one, and it changes no public type. What it gives up: plan
  curl-post-sent-once's D1 kept an order off a used connection so that a stale one could never fail it. With this plan an order whose
  connection dies between libcurl's check and the reply fails with the text above (never a second write) and has to be looked for at
  the exchange, as after a timeout. Tables 2 and 5 are why I think that is rare: the four edges keep an idle connection for as long as
  libcurl will reuse it, and a close that has arrived is noticed before the request is written.
  *Alternative, "reads only":* keep `FRESH_CONNECT` for a POST or a DELETE unless the adapter marks the request as one that only reads,
  which is #59 as proposed. `HttpRequest` gains `bool read_only{false}`; Kraken's `make_private_request` sets it from one list of 12
  paths and Crypto.com's `build_private` from one list of 5 methods, so a new endpoint is unmarked until its path is added; the
  callback above stays on every POST and DELETE, so a wrong mark costs an order its own connection and never a second write. It costs a
  new member of a public type (a major release by the repo's rule), 17 marks and a decision for each later endpoint, and an order
  keeps paying the 45 to 65 ms. Say "D1: reads only" and I rewrite *The change* and Step 1 to that before starting.
- **D2 HTTP/2 stays measured by hand.** The test tree has no HTTP/2 server, so tests 1 to 6 hold the callback on HTTP/1.1 and table 4b
  is a scratch measurement, repeated in Step 1 with the fixed source. Holding it in a test needs an HTTP/2 server in the test tree
  (TLS, or a private seam in `CurlHttpClient` to ask for cleartext HTTP/2) and is larger than the fix. *Default:* an issue for it,
  [#69](https://github.com/tomlin256/cryptocogs/issues/69), opened in Step 0. *Alternative:* build it in this plan, as a step before
  Step 1.
- **D3 #57 stays separate.** This plan words the one failure it creates. The failures that already exist after a sent request
  (`Server returned nothing`, `Failure when receiving data from the peer`, `Error in the HTTP2 framing layer`) keep libcurl's words.
- **D4 No release in this plan.** It would be `v2.2.0`, a minor: no public type changes, the behaviour of POST and DELETE does, and the
  libcurl floor is new. With D1's alternative it would be `v3.0.0`. A tag and a release are yours to say.

## Steps

Commits go to `main`. No commit message carries a closing keyword: the issues are closed by hand in Step 2. Counts are measured in
Step 1 and recorded in the *Outcome*.

### Step 0 — Approval

You approve or edit. Then: open two issues, each text audited first (label and milestone: the repo has no milestones) — a `bug`,
"over HTTP/2 a POST on a connection of its own is written again when the server refuses the stream"
([#68](https://github.com/tomlin256/cryptocogs/issues/68)), which this plan closes, and an `enhancement` for D2
([#69](https://github.com/tomlin256/cryptocogs/issues/69)); put their numbers in this plan; commit it
(`docs(plan): curl-post-connection-reuse approved as written`); push (documentation only: no CI run); comment on #59 with the plan's
path. *Done when:* #59 and the plan link each other and you have the two issue numbers.

### Step 1 — The change: tests red, then green

*Start from green.* `debug-macos-arm64` with the clang-tidy gate on: build and full ctest, the result recorded. Anything red stops the
plan there.

*Tests:* 1 to 6 above, `test_curl_floor` and `test_curl_floor_fixtures`.

*Red first,* the new test file over `HEAD`'s `http_client.cpp`, each test alone under a 10 s alarm. Expected: 1 and 2 fail on the text
(`HEAD` throws `Server returned nothing`); 3 fails in all four rows (`HEAD` answers the order on a connection of its own); 5 fails (3
connections, not 1); 4 and 6 pass. `test_curl_floor` fails over `HEAD` (no version at the root). Not committed.

*Change:* items 1 to 3 of *The change*.

*Hand-made changes,* each in the tree with its object deleted first, each test alone under the alarm, the source compared with the
fix afterwards. Expected:

| Change | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| (a) no callback and no `FRESH_CONNECT` | red | red | red | green | green | green |
| (b) the callback set before the reset, which clears it | red | red | red | green | green | green |
| (c) the callback on POST only | green | red | red (the DELETE rows) | green | green | green |
| (d) the callback on every method | green | green | green | green | green | red |
| (e) the count kept across calls | red | red | red | green | red | green |
| (f) two writes allowed | red | red | red | green | green | green |
| (g) `FRESH_CONNECT` kept beside the callback (D1's alternative, unmarked) | red (the text) | red (the text) | red | green | red | green |
| (h) the abort reported in libcurl's words | red | red | red | green | green | green |
| the fix restored | green | green | green | green | green | green |

*Repeated:* each of the six 200 times (`ctest --repeat until-fail:200`), on the Mac and in `ubuntu-dev`.

*HTTP/2 again:* the rows of table 4b with the fixed `http_client.cpp` compiled into the probe (one option added to a scratch copy, so
that it asks for cleartext HTTP/2), on both systems: every order read once.

*Checkpoint:* full build and ctest of `debug-macos-arm64` with the gate on, no warning; in `ubuntu-dev`, `debug-linux-arm64` built and
run in full and `release-`, `asan-` and `tsan-linux-arm64` built (GCC 13.3); the three configurations built with GCC 15.2 in the
throwaway container of `docs/dev-recipes.md`. Commit. *Done when:* red and green as stated, the table as stated, the repeats clean,
table 4b's last column reproduced, and every build and run above green.

### Step 2 — Docs, close

`README.md`: *REST — connections* says that every request reuses an open connection, that a POST or a DELETE is written at most once
and what the text of the failure means, and that a GET may be sent again; the dependency table names the floor. `CLAUDE.md`: the last
*Coding conventions* bullet states the new rule in place of the `FRESH_CONNECT` one (the file stays under its budget). The other items
of *Docs* above. Full ctest (`test_cmake_floor` reads the README, `test_claude_md_budget` and `test_plan_docs` read the rest). Commit,
`git fetch`, and one push for Steps 1 and 2, so that the two workflows run once and not twice. *Done when:* pushed, and #59 and #68
closed by hand, each with a comment (audited) that names the commits and the measurements. #59's comment says that the flag was not
built and why.

### Step 3 — After the push

Not blocking. `asan-macos-arm64` and `tsan-macos-arm64`, full suites, with the options in `CLAUDE.md`. The push's two CI runs
(`release-linux-x86_64`, `clang-tidy`) and the next nightly `Sanitizers` run are read once they have finished, not waited for or
dispatched. *Done when* the results are in the *Outcome*; a finding gets its own issue.

## Designs I rejected

- **A pool of connections opened ahead of the request.** libcurl will not give one to a request (table 3). The one thing it accepts,
  a TCP socket, saves a third of the cost and needs our own name resolution and socket lifetimes.
- **Our own HTTP over `curl_easy_send` and `curl_easy_recv`** on connect-only connections: a second HTTP stack in the order path, and
  HTTP/1.1 only.
- **Requests sent to keep spare connections warm.** Calls the caller never made, counted against its rate limit.
- **#59 as written: a marked request keeps libcurl's retry.** Then "at most once" depends on every mark being right. D1's alternative
  keeps the mark and not that dependence.
- **An idle limit for a POST or a DELETE** (`CURLOPT_MAXAGE_CONN` set for one request: measured, it works). No test can hold it
  without waiting whole seconds, and table 2 gives it nothing to prevent.
- **Forcing HTTP/1.1.** It narrows what was measured and gives up what the exchanges serve.
- **The callback with `FRESH_CONNECT` kept for everything.** It closes the HTTP/2 finding and leaves #59's cost: D1's alternative
  without its marks.

## Self-review — risks and assumptions

- **An order can now meet a dying connection.** A connection that the server closes while the request is in flight, or that an edge
  retires under it, fails the call with "outcome is unknown" where today the order would have had a fresh connection. libcurl would
  have sent it again; this plan does not, even when HTTP/2 says the request was refused and so not processed, because the callback
  cannot see why libcurl is retrying. The caller handles it as it handles a timeout. How often is not measured: it needs a close in a
  window one round trip wide.
- **A caller whose requests are spaced like a server's idle limit** would meet that window on purpose. None of the four edges closes
  within the 118 s that libcurl reuses (table 2, one afternoon); a proxy between the caller and the exchange, or a change at an edge,
  could. A GET has had this exposure since plan curl-handle-reset, with libcurl's retry to hide it.
- **A middlebox that forgets an idle flow without closing it** leaves a connection that looks alive: a POST written to it gets no
  reply until the total timeout (30 s by default), with the timeout's "outcome is unknown" text. Today only a GET can meet that.
- **The guarantee is libcurl's callback contract,** measured on 8.5.0 and 8.7.1 and read in 8.17.0. A libcurl that wrote a request
  without calling it would turn tests 1 to 3 red on HTTP/1.1; nothing in the suite would see it on HTTP/2 (D2, #69).
- **`CURLE_ABORTED_BY_CALLBACK` is taken to be the count's doing.** No other callback of `perform` can abort. One added later must not
  share that code path without a way to tell them apart; the comment at the throw says so.
- **Two tests of plan curl-post-sent-once are rewritten,** not weakened: what they asserted (a connection each) is the behaviour that
  D1 changes, and "read once" stays asserted in the same scenarios.
- **The floor is stated, not proved.** `test_curl_floor` holds the three statements to one version; that 7.80.0 is the true minimum
  rests on libcurl's manual, since no libcurl older than 8.5.0 is to hand. Below the true minimum the source does not compile. In a
  scratch project on the Mac the `find_package` line accepts 8.7.1 and, raised to 99.0, refuses it (`Could NOT find CURL: Found
  unsuitable version "8.7.1"`).
- **A redirect would be stopped.** libcurl calls the callback again for a redirect it follows. `perform` does not turn redirects on;
  if it ever did, a POST or a DELETE that was redirected would fail with this plan's text.
- **The exchanges were measured with GETs, once.** POSTs to private endpoints need credentials and are not made here.
- **Not measured:** a proxy from `https_proxy`; HTTP/3 (libcurl does not choose it unless asked); libcurl between 7.80.0 and 8.5.0;
  the retry after a write that fails on a reused connection, which goes back through the same state as the ones that were.
- **A GET can still be sent twice.** Deliberate, as before, and test 6 now holds it.
- **A libcurl at run time that is older than the one at build time** (found in Step 1). It refuses the option, and nothing would
  hold a POST to one write. Closed by `detail::require_option`: see *What differed from the plan*.
- **A reply of 417 to `Expect: 100-continue`** (found in Step 1). libcurl answers it by sending the request again without the header,
  and the callback stops that as well, with this plan's text, which is then wrong to say that no reply came. libcurl sends the header
  only over HTTP/1.1 and only for a body over 1 MB (`EXPECT_100_THRESHOLD`, read in 8.5.0, 8.7.1 and 8.17.0), which no request this
  library builds comes near. Read in the source, not measured.
- **Time:** the six tests are a few milliseconds each.

## Outcome

Steps 0 to 3 were done on 2026-11-22. Rob approved the plan as written, so D1 to D4 stood at their defaults.

| Step | Commit | What |
|---|---|---|
| 0 | `12204ed` | The plan, approved. [#68](https://github.com/tomlin256/cryptocogs/issues/68) (`bug`) and [#69](https://github.com/tomlin256/cryptocogs/issues/69) (`enhancement`) opened, with no milestone (the repo has none); a comment on #59 with the plan's path |
| 1 | `e519531` | `perform` sets the pre-request callback on every request that is not a GET, and no `FRESH_CONNECT`; the libcurl floor in three places; `test_curl_floor` and its fixtures; the tests |
| 2 | `0716dfd` | `README.md`, `CLAUDE.md`, the status line of plan curl-post-sent-once, this *Outcome*; #59 and #68 closed by hand after the push |
| 3 | the commit that adds *Step 3* below | the record of the sanitizer runs on the Mac |

### Step 1

**Start from green** (`12204ed`, `debug-macos-arm64`, the gate on): 678 of 678, no warning.

**Red first**, the new tests over `HEAD`'s `http_client.cpp`, each alone under a 10 s alarm, the same on the Mac and in `ubuntu-dev`:

| Test | Over `HEAD` | Why |
|---|---|---|
| 1 `APostThatTheServerClosesWithoutAReplyIsSentOnce` | red | read once, but the text is libcurl's `Server returned nothing (no headers, no data)` |
| 2 `ADeleteThatTheServerClosesWithoutAReplyIsSentOnce` | red | the same |
| 3 `APostOrADeleteOnAUsedConnectionThatClosesIsSentOnce` | red, all four rows | nothing thrown: the order has a connection of its own and is answered |
| 4 `GetsKeepSharingOneConnection` | green | |
| 5 `AMixOfMethodsSharesOneConnection` | red | 3 connections |
| 6 `AGetOnAUsedConnectionThatClosesIsSentAgain` | green | |
| `test_curl_floor` | red | "looks CURL up without a minimum version, so the project states no libcurl floor" |
| `test_curl_floor_fixtures` | green | it tests the check, not the checkout |

**Green:** with the change, all of them, on both systems.

**Hand-made changes**, in a scratch copy of the working tree, configured offline against the real tree's fetched sources (not in the
tree, as Step 1 said: a copy cannot leave a change behind). For each: the fixed file copied in, the change applied (its old text
asserted to occur once), the object, the archive and the test binary deleted, the target built, each test run alone under a 10 s
watchdog. Tests 7 and 8 are the two `CurlHttpClientOptions` tests (see *What differed from the plan*).

| Change | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| (a) no callback and no `FRESH_CONNECT` | red | red | red | green | green | green | green | green |
| (b) the callback set before the reset, which clears it | red | red | red | green | green | green | green | green |
| (c) the callback on POST only | green | red | red (the DELETE rows) | green | green | green | green | green |
| (d) the callback on every method | green | green | green | green | green | red | green | green |
| (e) the count kept across calls | red | red | red | green | red | green | green | green |
| (f) two writes allowed | red | red | red | green | green | green | green | green |
| (g) `FRESH_CONNECT` kept beside the callback | red (the text) | red (the text) | red | green | red | green | green | green |
| (h) the abort reported in libcurl's words | red | red | red | green | green | green | green | green |
| (i) a refused option ignored | green | green | green | green | green | green | red | green |
| (j) the text without the sentence about the outcome | red | red | red | green | green | green | green | green |
| (k) the first write refused as well | red | red | red | green | red | green | green | green |
| the fix, before and after | green | green | green | green | green | green | green | green |

Rows (a) to (h) are as Step 1 predicted. The first run showed (h) with test 5 red, which is (g)'s row: the archive had not been
rebuilt (Apple's make compares times in whole seconds), so the binary still held the change before it. The harness now deletes the
archive as well and reads the build log for the compile, the archive and the link. Every row above is from a run where all three
happened, made again on the sources of `e519531`.

**Repeated:** the six tests and the two new ones, 200 times each (`ctest --repeat until-fail:200`): 1600 of 1600 on the Mac (15.9 s)
and 1600 of 1600 in `ubuntu-dev` (6.0 s).

**HTTP/2 again.** The fixed `http_client.cpp`, with one line added to a scratch copy (`CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE`), compiled
into a scratch program that uses `CurlHttpClient` itself: a POST, the order, a GET. Seven server behaviours (the six of table 4b, and
the refusal of the first three orders), each with the order as a POST and as a DELETE: 14 runs on each system.

| | libcurl 8.7.1 (Mac) | libcurl 8.5.0 (`ubuntu-dev`) |
|---|---|---|
| the server read the order once | 14 of 14 | 14 of 14 |
| the POST before it and the GET after it answered 200 | 14 of 14 | 14 of 14 |
| the order answered (the server answers it) | 2 | 2 |
| `perform` throws this plan's text | 8: refused stream, GOAWAY, TCP reset, the first three refused | 12: those, and TCP close and session end |
| `perform` throws `Error in the HTTP2 framing layer` | 4: TCP close and session end, where 8.7.1 does not try again | 0 |

**The package config.** The library installed to a scratch prefix (Release, Mac): a consumer that calls `find_package(cryptocogs 2)`
and links `cryptocogs::exchange_http` configures (`Found CURL … (found suitable version "8.7.1", minimum required is "7.80.0")`),
builds and runs. With the installed config's version raised to 99.0 the consumer's configure fails with `Could NOT find CURL: Found
unsuitable version "8.7.1", but required is at least "99.0"`, and the call stack names the config's line.

**Checkpoint.**

| Tree | Result |
|---|---|
| `debug-macos-arm64`, Apple clang 21.0.0, libcurl 8.7.1, the gate on | built with no warning; **683 of 683** (678 before: three gtests and two `cmake -P` checks are new), 595 without the `clang_tidy_` tests |
| `debug-linux-arm64` in `ubuntu-dev`, GCC 13.3, libcurl 8.5.0 | built with no warning; **595 of 595** |
| `release-`, `asan-` and `tsan-linux-arm64` in `ubuntu-dev` | built with no warning; in the asan tree the transport's 27 tests and the two floor checks pass with CI's options, `detect_leaks=1` included |
| the three configurations with GCC 15.2 (a throwaway `ubuntu:26.04`, CMake 4.2.3, libcurl 8.18.0) | built with no warning; the same 29 pass in the Release tree, which makes a third libcurl for tests 1 to 6 |

Each configure names the floor: `Found CURL … (found suitable version "8.18.0", minimum required is "7.80.0")`.

### What differed from the plan

- **`perform` refuses to send a POST or a DELETE when libcurl refuses the option** (`detail::require_option`). Found while writing the
  change, and not in the plan as approved. The floor is checked when the library is built and when the package is found, but a program
  can be run with a libcurl older than the one it was built with. That libcurl answers `CURLE_UNKNOWN_OPTION` to
  `CURLOPT_PREREQFUNCTION`, and `perform` did not read the answer: with `FRESH_CONNECT` gone nothing would then hold a POST to one
  write, and nothing would say so. Now the call throws before anything is sent, and the text names the option, the libcurl in use and
  the one the library was built with. Cost: two comparisons for each POST or DELETE. Tests 7 and 8 hold the function and change (i)
  the refusal. The call from `perform` is read, not run: no libcurl that has the option refuses it, so no test can reach it.
- **The hand-made changes were made in a scratch copy,** not in the tree, and there are three more of them: (i), (j) and (k).
- **The header comment of `test_http_client.cpp` changed in Step 1,** with the tests it describes: left for Step 2, it would have been
  false in Step 1's commit.
- **`test_curl_floor_fixtures` has two accept cases and nine reject cases,** not one and three. Accepted: the three files agreeing on
  7.80.0; and agreeing on another version, with comments that show other lookups (the check knows no number, and a comment is not a
  call). Refused: the root with no version, with a range, with two lookups; the package config with another version, with none, with
  no lookup; a README that names another version, and one that names none; and the package config and the README both wrong, where
  the error names both.
- **Tests 1 to 3 check the two phrases through one helper,** `expect_it_says_the_request_was_not_sent_again`.

### Step 3

After the push of `e519531` and `0716dfd`, on the Mac (Apple clang 21.0.0, libcurl 8.7.1), with the options in `CLAUDE.md`:

| Tree | Result |
|---|---|
| `asan-macos-arm64` (AddressSanitizer and UBSan) | built with no warning; **598 of 598**; no report |
| `tsan-macos-arm64` | built with no warning; **597 of 597**; no report |

The clang-tidy gate is off again in `build/debug-macos-arm64`, as it was found (595 tests).

**Not read yet.** The push started two CI runs, `CMake on a single platform` (45069355290) and `clang-tidy` (45069355279). Both were
still running when this was written, three minutes after the push, and were not waited for. The nightly `Sanitizers` run after this
date is the first on these commits. None of the three is called green here.
