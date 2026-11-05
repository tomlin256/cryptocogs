# Plan 034 — Transport callbacks must not outlive what they call

**Status:** Done (2026-11-05). Rob approved the plan as written, so D1 to D8 stood at the defaults below. The first draft covered #39
only; #43 was folded in at his request before approval. Steps 0 to 5 are done: #39 and #43 are fixed and closed, the Release
`ws_client_example` exits 0 five times in five, and every sanitizer tree, local and in CI, is green.

Closes [#39](https://github.com/tomlin256/cryptocogs/issues/39) and [#43](https://github.com/tomlin256/cryptocogs/issues/43).

**Raised along the way:** [#44](https://github.com/tomlin256/cryptocogs/issues/44), a valid frame whose `method` is not a string makes
`HeartbeatResponder` throw on the I/O thread (found with the #43 reproducer; its own issue); and
[#45](https://github.com/tomlin256/cryptocogs/issues/45), why the CI sanitizers did not spot either bug (the follow-up Rob asked for
when he approved this plan).
**Not here:** #44 and #45; the self-join when the I/O thread drops the last reference to an `IxWsConnection` (documented in
`ix_ws_connection.hpp`, unchanged by this plan); the Binance examples' use of the URL factory.

## Problem

Two classes install callbacks on a transport that goes on calling them from its I/O thread after the callbacks' owner has gone.

### #39 — `IxWsConnection` destroys its callbacks before it stops its socket

`ws_client_example ticker BTC/USD` fails at exit on an ixwebsocket I/O thread in every unpatched Release run I made (3 of 3 today; 33 of
33 in #39). #39's hypothesis is **confirmed**. `IxWsConnection` declares `ws_` before `msg_cb_`, `open_cb_`, `close_cb_` and `error_cb_`
and has no destructor (`include/exchange/common/ix_ws_connection.hpp:100-106`). Members die in reverse order, so the four
`std::function`s are destroyed first, and only then does `~ix::WebSocket` call `stop()` and join the I/O thread. For that whole window
the I/O thread still enters the `[this]` lambda of `connect()` (line 58) and calls a destroyed `std::function`. libc++ does not clear a
destroyed `std::function`, so `if (msg_cb_)` is still true and the call lands in a destroyed lambda. Its captured
`weak_ptr<ExchangeWsClient>` points to a `make_shared` block that was freed when the last of the four weak references died, so
`weak_self.lock()` reads freed memory.

### #43 — `HeartbeatResponder` leaves a `[this]` callback on the inner connection

`HeartbeatResponder::set_on_message` (`src/cryptocom/heartbeat_connection.cpp:29-31`) installs `[this](...) { handle_inbound(raw); }` on
the inner connection, and nothing removes it. The Crypto.com factories give the client the only reference to the responder
(`src/cryptocom/ws.cpp:336-337`) while the caller usually keeps the inner connection. Release the client with the connection open and
the next frame calls `handle_inbound` on a destroyed object. The documented teardown ("release the client, then `disconnect()`",
`ix_ws_connection.hpp:139-140`) leaves exactly that window. The Crypto.com example calls `disconnect()` first and does not hit it.

### What I measured (2026-11-05, macOS arm64, Apple clang 21, Release unless said)

**#39**

- **The system crash report** of one baseline run (`~/Library/Logs/DiagnosticReports/ws_client_example-2026-11-05-094805.ips`, outside
  the repo). Main thread: `IxWsConnection::~IxWsConnection → ix::WebSocket::~WebSocket → std::thread::join`. I/O thread:
  `ix::WebSocket::run → … → ExchangeWsClient::init()::$_0 (on_message) → on_raw_message + 1172 → std::__throw_bad_function_call`.
  So the main thread was already past the callbacks when the I/O thread called one, and `lock()` handed back a bogus `self`.
- **Why it is near-certain, not a race with a frame in flight.** Every orderly `stop()` of an open socket ends with ixwebsocket
  delivering a `Close` message to the user callback on the I/O thread (`IXWebSocketTransport.cpp:209-225`, `setReadyState(CLOSED)` →
  `_onCloseCallback` → `WebSocket`'s constructor lambda → `_onMessageCallback`), after the callbacks are gone. A scratch reproducer
  (loopback `ix::WebSocketServer`, no network) drops an open `IxWsConnection` without `disconnect()`: **3 of 3 runs** ran the Close
  callback *after* all four callbacks were destroyed. The same program with one line added to the header
  (`~IxWsConnection() override { ws_.stop(); }`, a scratch copy of the header) ran it *before*: **6 of 6** (5 drops, 1 `disconnect()`
  first). Without the destructor, `disconnect()` first is already fine, which fits the Crypto.com and Coinbase examples (both call
  `disconnect()`) exiting 0 in #39.
- **When the freed block is touched.** Only when the connection outlives the client, as in the second half of `run_ticker`
  (`raw_conn` is dropped after `client2`): the four lambdas then hold the last weak references. With the URL factory the client is the
  sole owner and its control block stays alive until `~ExchangeWsClient` returns, so `lock()` returns null; that is my reading of why the
  Binance example exited 0 in #39's one run, not something I measured.
- **The failure is chaotic because it is heap corruption.** Today, with the unpatched Release binary: exit 137, exit 137, and one
  **hang** (killed by me after 6 minutes: main thread in `join`, one I/O thread in `__throw_bad_function_call`, another blocked on the
  malloc lock). The same example linked against the patched header scratch copy: **3 of 3 exit 0**. #39 saw 134, 137 and 139.
- **#39's SIGKILL (exit 137) is explained.** The report says `EXC_BREAKPOINT … BUG IN CLIENT OF LIBPLATFORM: Trying to recursively lock
  an os_unfair_lock`. A fault inside `malloc_type_realloc` (the terminate handler was demangling its backtrace on a corrupted heap)
  ran backward-cpp's signal handler, which `malloc`s, so the kernel killed the process. It is a consequence of the corruption, not a
  second bug of ours; backward's handler is not async-signal-safe and I leave it alone. A probe program with the same handler and a clean
  heap exits 134 as designed.
- **ASan is not an oracle for this bug.** One live run of the `asan-macos-arm64` example exited 0 with no report. My reading of why (not
  tested): `weak_ptr::lock()` runs inside the uninstrumented libc++ dylib, and ASan's quarantine keeps the freed block's strong count at
  -1, so `lock()` returns null. A deterministic test needs its own probe.
- **History.** The callbacks have been declared after `ws_` since the class was added (`41fd609`, 2026-03-03, then in
  `kraken_ix_ws_connection.hpp`), so there is no regression to bisect; whether an old build "passed" would be heap-layout luck. I did
  not build any.
- **Linux** was not run (CI never runs the examples). The new test runs in CI's `release-linux-x86_64` job and in the nightly sanitizers.

**#43** (a scratch program on `MockWsConnection`, no network, built at `-O1 -g`; the project's trees may fail differently, and Step 2
records what they do). A `HeartbeatResponder` is destroyed while its inner connection lives, then a frame arrives:

| Case | Unfixed, plain build | Unfixed, ASan |
|---|---|---|
| `make_shared` responder (as the factories build it), normal frame | the frame is lost (the destroyed forward callback reads as empty) | heap-use-after-free in `handle_inbound` |
| same, heartbeat frame | SIGSEGV | heap-use-after-free in `handle_inbound` |
| stack responder (as the unit tests build it), normal frame | the destroyed forward callback is called | stack-use-after-scope in `handle_inbound` |

With a scratch copy of the relay below, all three cases are right in both builds, and the inner connection expires when its owner lets
go (no reference cycle). Unlike #39, ASan *is* an oracle here: `handle_inbound` is our code, compiled with the instrumentation.

## The change

**1. `IxWsConnection`** (`include/exchange/common/ix_ws_connection.hpp`, header-only as plan 016 D1 keeps it):

```cpp
    // Stops the socket, which joins its I/O thread, before any member is destroyed: the callbacks below are
    // still alive for every call the thread makes, including the final on_close that stop() delivers.
    ~IxWsConnection() override { ws_.stop(); }
```

After the destructor body no thread of this object runs. A final `on_close` may therefore run on the I/O thread *while the destructor
runs*, exactly as it does in `disconnect()`; whatever the callbacks capture must outlive the connection. `stop()` is idempotent
(`IXWebSocket.cpp:187`: nothing joinable ⇒ only `close()`), so `disconnect()` followed by destruction, which every example does, stays
as it is.

**2. `HeartbeatResponder`** (`heartbeat_connection.{hpp,cpp}`). The callback installed on the inner connection owns what it touches, so
the responder can be destroyed first:

```cpp
// .cpp; the header keeps only `struct Relay;` and `inner_` (forward_ and handle_inbound() go)
struct HeartbeatResponder::Relay {
    std::weak_ptr<IWsConnection> inner;     // weak: the callback lives inside inner, so a strong reference is a cycle
    MessageCb                    forward;
    void on_message(const std::string& raw) const;      // handle_inbound's body, moved and not changed (see #44)
};

void HeartbeatResponder::set_on_message(MessageCb cb) {
    const auto relay = std::make_shared<const Relay>(Relay{inner_, std::move(cb)});
    inner_->set_on_message([relay](const std::string& raw) { relay->on_message(raw); });
}
```

For as long as the inner connection keeps the callback, heartbeats are still answered and other frames still reach the callback that was
registered through the responder (for `ExchangeWsClient` a `weak_self` lambda, a no-op once the client is gone). The relay is immutable.
Public API unchanged; the private part of the class changes, which a static library's consumers recompile.

**3. Hardening** (`src/exchange/common/ws_client.cpp:93`). `identifier_(j)` is the one `std::function` call in `on_raw_message` outside
`safe_invoke`; a throwing `MessageIdentifier` terminates the process on the I/O thread. Wrap it like the handlers: report through
`error_handler_->on_malformed_frame(raw, e)` and drop the frame. This would not have prevented #39.

**4. The example** (`tests/examples/ws_client_example.cpp`). A small local `Session` (conn + client; destructor: `conn->disconnect()`,
then release the client and the conn) replaces `make_exchange_ws_client(url, …)` and the bare `raw_conn` in all five `run_*` functions, so
every return path follows the teardown of D5, including the early `return` on a failed ack.

**5. A shared test helper**, `tests/unit/callback_probe.hpp` (header-only, like `mock_ws_connection.hpp`): finds a callback that is called
after it was destroyed. A destroyed `std::function` is still callable in practice (libc++ and libstdc++ do not clear it), so nothing in
the type system or a sanitizer says so (see the ASan note under #39). Each callback the probe hands out owns a token whose destructor
counts one destruction, and every call records how many destructions had happened by then; a call that sees one is a call after
destruction. The callbacks read nothing they capture, because on the unfixed code they run after they were destroyed, so the state is
static and a test makes one probe at a time (gtest runs a binary's tests one after another).

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 A destructor, not a member reorder.** Declaring `ws_` after the callbacks would also work (it is destroyed first). The destructor
  states the intent and survives the next member someone adds below `ws_`. *Alternative:* reorder, one comment, no destructor.
- **D2 Callbacks may run until the destructor returns**, including a final `on_close` (same as `disconnect()`). *Alternative:* silent
  destruction, which needs a lock around every callback invocation and so a lock in the I/O thread's hot path; rejected below.
- **D3 The `identifier_` hardening is in this plan** (Step 3, its own commit and test) because #39 lists it. *Alternative:* its own issue.
- **D4 All five `run_*` functions use `Session`**, not just `run_ticker` as #39 says: the other four use the URL factory with streaming
  callbacks, the pattern the header advises against, and the helper makes it five lines each. *Alternative:* `run_ticker` only.
- **D5 The documented teardown changes to "`disconnect()` first, then release the client and the connection in either order".** The
  header says "release the client, then `disconnect()`". After `disconnect()` returns no callback can run, so nothing can drop a last
  reference on the I/O thread; the old order leaves that window open. With the two fixes either order no longer corrupts memory, so this
  is wording, not a repair. *Alternative:* keep the wording and add the destructor guarantee only.
- **D6 No tag.** The change touches two public headers, so v1.0.1 is plausible, but a tag is outward-facing: say so if you want it.
- **D7 Evidence order.** The checkpoint is the full build and ctest of `debug-macos-arm64`. The `asan` and `tsan` trees, the live runs
  and CI come **after the push** (Step 5), as you asked on plan 029 (get the mechanical change pushed, test more after). Plan 033 put the
  sanitizer trees in every checkpoint; say so if you want that here.
- **D8 The shape of #43's fix: a relay the callback owns**, with a weak reference to the inner connection, and the behaviour above (the
  callback goes on working while the inner connection keeps it). *Alternative:* `enable_shared_from_this` and `weak_from_this()` in the
  responder, the idiom `ExchangeWsClient` and `CoinbaseStreamClient` use; rejected below because the unit tests build the responder on the
  stack.

## Steps

Commits go straight to `main`, one per step, pushed once the checkpoint is green. Counts are what I expect and are replaced by the real
ones in the Outcome.

### Step 0 — Approval

You approve or edit. Commit this plan (`docs(plan): plan 034 approved as written`), push, and comment on #39 and #43 with its path (on
#43 also that it is now reproduced, with the table above). *Done when:* both issues link the plan, and the plan links both and #44.

### Step 1 — #39: the destructor, with its test

*Tests (new)* `test_ix_ws_connection` (`tests/unit/test_ix_ws_connection.cpp`, linked with `exchange_common`, `ixwebsocket` and
`GTest::gtest_main`; builds in every flag configuration) and the probe helper. A loopback `ix::WebSocketServer` on `127.0.0.1` (a port
from the dynamic range, retried until `listenAndStart()` succeeds; it sends `hello` on Open), and four tests with no sleep and no
timeout, every wait being a `std::promise` the I/O thread sets:

- `DeliversAFrameToTheMessageCallback`: the harness works (it opens and `hello` arrives), so the next test cannot pass vacuously;
- `DroppingAnOpenConnectionStopsItBeforeAnyCallbackIsDestroyed`: the regression test. All four callbacks come from the probe; asserts
  exactly one `on_close` and that no call of any callback followed a destruction;
- `DisconnectThenDropDeliversOneClose`: `disconnect()`, then drop: one `on_close`, no call after a destruction, nothing from the
  destructor's second `stop()`;
- `DroppingAConnectionThatWasNeverConnectedReturns`: no server, no thread; the destructor returns and no callback runs.

*Red first:* on the current header the regression test fails (the scratch reproducer's result, 3 of 3) and the other three pass.
*Change:* the destructor. *Hand-made changes in a scratch copy*, each configured and built (the object file deleted first: Apple's
GNU Make 3.81 compares one-second timestamps): no destructor (red), destructor present but `ws_.close()` instead of `stop()` (expected
red, since nothing joins and the Close arrives after the destruction; that one is a race, so several runs), members reordered with no
destructor (must be green: the test checks the property, not the mechanism).
*Checkpoint:* the default tree 516 of 516 (four new). *Done when:* red then green, mutations as stated, pushed.

### Step 2 — #43: the relay

*Tests (new)* in `test_cryptocom_ws_client.cpp`, group `CryptoComHeartbeat`, `MockWsConnection` only, no sleeps, the probe for the
forward callback:

- `ForwardsAFrameAfterTheResponderIsDestroyed` (`make_shared`, as the factories build it): the frame reaches the callback once, and the
  callback was not destroyed before it;
- `ForwardsAFrameAfterAStackResponderIsDestroyed` (as the other tests in the file build it);
- `AnswersAHeartbeatAfterTheResponderIsDestroyed`: the reply is in the inner connection's log and the callback sees nothing;
- `TheInstalledCallbackDoesNotKeepTheInnerConnectionAlive`: a `weak_ptr` to the mock expires once its owner lets go. It guards the weak
  reference in the relay and passes on the unfixed code.

*Red first:* the first three on the unfixed responder (today: a lost frame, a call after destruction, a SIGSEGV; the checkpoint tree may
differ and the Outcome records it). *Change:* `Relay`, `set_on_message`, the header's private part and a lifetime comment. *Hand-made
changes in a scratch copy*, object file deleted first: the relay holds `inner` strongly (the cycle test red), the callback captures `this`
again (three red), the relay drops a heartbeat's reply (the heartbeat test red).
*Checkpoint:* the default tree 520 of 520 (four new); the two existing `CryptoComHeartbeat` tests unchanged and green.
*Done when:* red then green, mutations as stated, the commit says `Closes #43`, pushed.

### Step 3 — A throwing `MessageIdentifier` is contained

*Test (new)* `WsErrorIsolation.ThrowingIdentifierIsContained` in `test_ws_client.cpp`: an identifier that throws on one frame;
`EXPECT_NO_THROW(conn->inject_message(…))` (the mock calls back synchronously, so an escape reaches the test), the handler's
`on_malformed_frame` is called once with that frame and the exception text, and a later normal frame still dispatches.
*Red first:* the escape fails `EXPECT_NO_THROW`. *Change:* the `try`/`catch` in `on_raw_message`.
*Checkpoint:* the default tree 521 of 521. *Done when:* red then green, pushed.

### Step 4 — The example and the words

*Tests:* none new. The example needs a live exchange and has no unit test in this repo (Step 5 runs it); the claims in the header
comments are the ones Steps 1 and 2 test. *Change:* `ws_client_example.cpp` (D4); the comments of `ix_ws_connection.hpp` (the destructor
and its guarantee, D5, the self-join caveat kept) and `heartbeat_connection.hpp` (the lifetime); `CLAUDE.md` (twenty-seven test
executables, 510 tests, 521 in ctest, the new row in both tables, the new `CryptoComHeartbeat` tests in the breakdown, a short
"Connection lifetime" paragraph under *Connection abstraction*, the Crypto.com-only count recomputed from a tree as in plan 033).
*Checkpoint:* the default tree 521 of 521, examples build. *Done when:* pushed; the commit says `Closes #39`.

### Step 5 — After the push: measure

1. **Live Kraken.** Five Release runs of `ws_client_example ticker BTC/USD` **one at a time**, 3 s apart (Kraken answers HTTP 429 per IP
   after a burst, #39). *Done when:* five exits of 0; stop and report at the first 429 or non-zero exit. Baseline today: 137, 137, hang.
2. **Live Crypto.com.** One run of `cryptocom_ws_client_example ticker BTC_USD --seconds 35`: long enough for the server's heartbeat, so
   the relay answers one on the real transport (the server drops a client that does not). *Done when:* exit 0 and ticker lines after
   second 30.
3. **Sanitizer trees.** `asan-macos-arm64` and `tsan-macos-arm64` full suites with the options of `CLAUDE.md` (*Sanitizers*), and
   `ctest --repeat until-fail:50` on `test_ix_ws_connection` in the `tsan` tree. This is the first test in the suite that runs ixwebsocket
   threads under TSan; a report inside ixwebsocket is a new issue (plan 032 D6), not a reason to weaken the test. The three #43
   "after destruction" tests are the ones ASan judges.
4. **CI.** The main workflow on the pushed commit, then `gh workflow run sanitizers.yml --ref main -f which=asan` and, when it is done,
   `which=tsan` (one at a time). This is the Linux run of the new tests.

## Designs I rejected

- **Reordering the members only**: D1.
- **Silent destruction** (clear the callbacks, then stop): replacing a `std::function` the I/O thread may be calling is itself a data race;
  making it safe puts a lock around every callback, on the hot path of every frame.
- **`enable_shared_from_this` and `weak_from_this()` in `HeartbeatResponder`** (D8): the existing tests build `HeartbeatResponder hb{inner};`
  on the stack, where `weak_from_this()` is empty and the callback would silently forward nothing; and the I/O thread's `lock()` could
  make it the last owner of the responder and, through `inner_`, of the connection (a self-join).
- **Unregistering in `~HeartbeatResponder`** (`inner_->set_on_message(nullptr)`): replaces a `std::function` the I/O thread may be calling,
  the same data race as silent destruction.
- **A raw back-pointer to the inner connection in the relay**: valid in practice, since the relay lives inside the connection, but a raw
  non-owning pointer is what the C++ guidelines forbid; the `weak_ptr` costs one `lock()` per heartbeat.
- **A transport seam** (an `IWebSocket` interface so a test can fake ixwebsocket): invasive, and plan 016 D1 keeps this header free of
  anything but ixwebsocket on purpose. The loopback server tests the real class.
- **A sanitizer as the oracle for #39**: measured silent (see above). For #43 it is one oracle among two.
- **A live-network regression test**: needs the network, is rate-limited (429), and fails on heap luck.
- **A public RAII session type** that owns client and connection and enforces the teardown: a worthwhile API, a bigger change than a fix;
  the example's local `Session` is its prototype.
- **Fixing only the example**: leaves the hazard for every downstream consumer (the header cites flywheel #83).

## Not in this plan

#44 (the same relay body, moved unchanged, so the fix stays one small commit of its own). The self-join when the I/O thread drops the
last reference to an `IxWsConnection` (still a `std::system_error` from a `noexcept` destructor, then `std::terminate`, as before).
backward-cpp's non-async-signal-safe handler. The Binance examples (URL factory, no `disconnect()`); safe on the main-thread path after
this plan, unproven on the self-join path. A tag (D6).

## Self-review — risks and assumptions

- **#39's regression test shows red on libc++ only.** On unfixed code it calls a destroyed `std::function`, which libc++ leaves callable;
  libstdc++'s destructor also does not clear its manager pointer, so I expect the same, but I have not run the test on Linux against the
  unfixed header (I did not try the Linux container; Docker was down yesterday). On Linux it will only ever be seen green, in CI.
- **#43's red states depend on the allocator** (a lost frame, a SIGSEGV, a call after destruction, as measured at `-O1`). Each of the
  three tests fails or crashes on the unfixed code on this Mac, and ASan is the uniform oracle on every platform, which is why the asan
  run in Step 5 is part of the evidence and not an extra.
- **The probes call a destroyed callback on purpose, on the unfixed code only.** The bodies read no captures, so the call is benign in
  practice; after the fixes nothing calls a destroyed object.
- **A real socket in a unit test.** Loopback only, no sleeps, ports retried; a hung wait is stopped by the 120 s test timeout (plan 033
  D4). A sandbox with no loopback would fail it; GitHub's runners and this Mac have one.
- **TSan may report inside ixwebsocket** (Step 5.3). That would be a finding, not a flake.
- **Destruction now waits for the close handshake in the destructor body** (at most `kClosingMaximumWaitingDelayInMs`, 300 ms, per
  connection) instead of in `~WebSocket`. Same wait, earlier; no new latency.
- **Callbacks that capture something destroyed before the connection** can now be called during destruction. That was undefined before
  (a call on a destroyed `std::function`), and `disconnect()` already behaves this way. Likewise, frames that reach a relay after its
  responder is gone are forwarded, to a callback that may capture references which died with the caller: the contract of any callback
  registered on a connection, and for the factories that callback is a `weak_self` lambda.
- **Answering a heartbeat holds a strong reference to the inner connection for the length of one `send`.** If the caller drops its last
  reference at that moment, the I/O thread destroys the connection and joins itself: the documented hazard, a window of microseconds once
  per heartbeat. A raw pointer would remove it and break the guideline (rejected above).
- **Five live runs are weak evidence** against a heap-layout bug (baseline: 3 of 3 bad). The deterministic tests are the proof; the
  runs are the user-visible symptom.
- **I read the unit-test rule as covering library code.** The example has no test for want of a network, and says so (Step 4).

## Outcome

All six steps are done and pushed, with every default of D1 to D8 standing. Measured on the Mac (Apple clang 21.0.0, CMake 4.4.5,
Unix Makefiles, 12 cores) and on `ubuntu-latest` (GCC 15.2.0). #39 and #43 were closed by the commits that say `Closes`.

**Step 0** (`d0ba872`). Approved as written. #39 and #43 link the plan, and the plan links both and #44. At approval you also asked
for a follow-up on why the CI sanitizers had not spotted either bug: that is [#45](https://github.com/tomlin256/cryptocogs/issues/45),
which collected the evidence below (the free-fill ASan run, and this plan's tests run against the unfixed sources).

**Step 1** (`60a9788`). `test_ix_ws_connection` was red on the old header (the regression test failed **300 of 300** runs, the other
three passed) and green after the destructor (**600 of 600** runs of the whole binary). Two hand-made changes to the header, 300 runs
each: `close()` instead of `stop()` red 300 of 300; `ws_` declared last with no destructor green 300 of 300. The first Linux run was
green (CI run 42013031900, 516 of 516, GCC 15.2.0).

**Step 2** (`f6521dc`). Red first in the debug tree: `ForwardsAFrameAfterTheResponderIsDestroyed` and
`AnswersAHeartbeatAfterTheResponderIsDestroyed` died with a SegFault, `ForwardsAFrameAfterAStackResponderIsDestroyed` failed (the
callback was destroyed with the responder, then called), and the guard passed, as planned. Green after. Hand-made changes, three runs
each: the relay holding the connection strongly turned the guard and the `make_shared` forward test red; a relay that drops the
heartbeat reply turned the heartbeat test and the existing `AnswersAndSwallows` red. The default tree: 520 of 520.

**Step 3** (`161cff2`). `WsErrorIsolation.ThrowingIdentifierIsContained` was red (the `std::runtime_error` escaped `inject_message`,
and the error handler saw nothing) and green after the `try`/`catch`. 521 of 521.

**Step 4** (`ec7c8b4`). `ws_client_example` builds a `Session` per connection in all five `run_*` functions; the header says
`disconnect()` first; `CLAUDE.md` says twenty-seven executables, 510 tests, 521 in ctest. The Crypto.com-only count was taken from a
tree built for the purpose (offline, from the fetched sources) and then removed: **120 gtests, 131 in ctest, all green**; the
document had 112 and 123, before the four `IxWsConnection` tests (they build in every tree) and the four new Crypto.com ones.

**Step 5.**

- **Live Kraken.** Five Release runs of `ws_client_example ticker BTC/USD`, one at a time, 3 s apart: **five exits of 0**. Each log has
  `Subscribed`, ticker lines on both connections, and `Reused-connection subscription cancelled`, so none was a silent 429. Baseline the
  same morning: exit 137, exit 137, a hang.
- **Live Crypto.com.** `cryptocom_ws_client_example ticker BTC_USD --seconds 35`: exit 0, 57 ticker lines over 34.5 s. The stream
  went on past 30 s, where the server drops a client that has not answered its heartbeat; the heartbeat frame itself is swallowed by
  the responder, so I did not see it.
- **Sanitizer trees, after the push.** `asan-macos-arm64` **524 of 524** with CI's options and no report; `tsan-macos-arm64` **523 of
  523** and no report; the eleven new or touched tests (`IxWsConnectionTest`, `CryptoComHeartbeat`, `ThrowingIdentifierIsContained`)
  passed `ctest --repeat until-fail:50` under TSan with `halt_on_error=1`, the first ixwebsocket threads in that tree.
- **The tests against the unfixed sources, under ASan with CI's options** (for #45): the three #43 tests abort (heap-use-after-free,
  stack-use-after-scope, heap-use-after-free); the #39 test fails 20 of 20 runs through the probe with **no ASan report at all**.
- **CI.** The main workflow was green for every push (`60a9788`, `f6521dc`, `161cff2`, `ec7c8b4`). The sanitizers workflow, dispatched
  one mode at a time on `ec7c8b4`: `asan` run 42015292214 green, **524 of 524** in 7 min 29 s; `tsan` run 42016393346 green, **523 of
  523** in 6 min 23 s, no report in either; the new tests ran in both. That is the Linux run of #39's test under both sanitizers.

**What the plan got wrong.**

- **"Every orderly `stop()` ends with a `Close`" is false.** `stop()` sets a flag and then closes the socket; an I/O thread that sees
  the flag first leaves its loop with no handshake and no `Close`. The first full ctest run caught it: `DisconnectThenDropDeliversOneClose`
  failed, and 600 runs of the first version of the test measured **46 failures, about 4 in 100 per test**. The window is where a test that
  has just waited on a gate opened by a callback puts the thread. The tests no longer assert on a `Close`, and the regression test repeats
  the attempt until one had a `Close` (it is what makes a late call possible); the header comment and `CLAUDE.md` say "normally". My first
  count of the failures was `runs_with_a_failure=1`, wrong because a `declare -A` in a script failed under macOS's bash 3.2 and ended the
  loop at the first failure; the second count, 46 of 600, is the real one.
- **Steps 1 and 2 said "builds with no warnings".** True of the log, but those builds did not recompile `test_ws_client.cpp`, whose six
  known `-Wunused-result` warnings (#24) appear whenever it is rebuilt, as Step 3's build showed. No step added a warning.
- **The #43 red states.** At `-O1` the scratch program lost the frame of a `make_shared` responder; in the debug tree the same case
  crashes (SegFault). Both are "red"; the plan said they depend on the allocator.
- **The plan said the `close()` mutation was a race, "so several runs".** It was red 300 of 300 once the test repeated until a `Close`.

**Left for later.** [#44](https://github.com/tomlin256/cryptocogs/issues/44) (a non-string `method` throws out of the responder) and
[#45](https://github.com/tomlin256/cryptocogs/issues/45) (why the sanitizers were silent, and what closes the gap) are open. The red
state of #39's test was not shown on Linux (I did not try the container). No tag (D6). The self-join and the Binance examples are as
they were.
