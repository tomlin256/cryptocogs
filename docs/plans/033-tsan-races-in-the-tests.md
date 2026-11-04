# Plan 033 — Fix the two ThreadSanitizer races in the tests

**Status:** Approved (2026-11-04). Rob approved the plan as written, so D1 to D5 stand at the defaults below. Step 0 is done;
nothing else is implemented.

Closes [#27](https://github.com/tomlin256/cryptocogs/issues/27) and [#28](https://github.com/tomlin256/cryptocogs/issues/28).

**Follows:** [plan 032](032-sanitizer-option-and-presets.md). Its Step 6 ran the `tsan` job in CI and its Step 8 ran the `tsan` tree
on the Mac: on both, 507 of 509 tests pass and exactly these two fail, with nothing else. Its Step 9 (`tsan` joins the nightly,
and #30 closes) waits for this plan.
**Not here:** the wall-clock polling in `test_ws_reconnect_session.cpp` ([#42](https://github.com/tomlin256/cryptocogs/issues/42)),
which needs a seam in the library, and any other finding.

## Problem

Two data races, both in the tests and neither in the library, are everything ThreadSanitizer finds in the suite (plan 032, Step 8:
Mac, Apple clang 21.0.0; Step 6: CI, GCC 15.2.0).

- **#27, `Execute.BlocksUntilResponse`** (`tests/unit/test_ws_client.cpp:260-284`). The injector thread sleeps 50 ms (line 274) and then
  reads `conn_ref->sent_messages[0]` (line 275) to learn the request id, while the main thread is inside `client->execute(req, 2000ms)`,
  which calls `MockWsConnection::send` (`tests/unit/mock_ws_connection.hpp:34-36`) and `push_back`s to the same vector. The write is
  under the client's mutex; the read has no lock. The test orders the two accesses by a wall-clock delay, which the memory model does
  not see. CI reports it at `test_ws_client.cpp:275`.
- **#28, `WsReconnectSession.BackoffResetsAfterSuccessfulReconnect`** (`tests/unit/test_ws_reconnect_session.cpp:222-252`). `int
  failsRemaining = 2;` (line 227) is decremented by the connect callback on the session's worker thread (line 233) and read by the test
  thread in the `waitFor` predicate (line 244). Only `connects` is a `std::atomic<int>`. CI reports it at line 233, the Mac at line 244.

### What I measured (2026-11-04)

- `sent_messages` is used at 91 sites in four test files (`test_ws_client.cpp` 35, `test_binance_ws_client.cpp` 26,
  `test_cryptocom_ws_client.cpp` 20, `test_coinbase_ws_client.cpp` 10) and at three places in the mock. Every one is a read: `[i]` 44,
  `.size()` 33, `.empty()` 7, `.at()` 4, `.back()` 2. None binds a reference to an element, iterates, takes an address or mutates it,
  so an accessor that returns a copy is a drop-in. Two documents name the member: `CLAUDE.md` (line 1357) and
  `docs/agent-add-exchange.md` (line 276).
- The injector of `Execute.BlocksUntilResponse` is the only `std::thread` in the four WebSocket test files, and its `sleep_for` the only
  sleep there.
- The test presets set no timeout, so a test that blocks forever holds a job until the job's own ceiling (20 minutes for the sanitizer
  workflow).

## The change

**#28.** One line: `std::atomic<int> failsRemaining{2};`. `connects` is already atomic. The polling that surrounds it stays; it is #42's.

**#27.** `MockWsConnection` becomes safe to use from a second thread, and the test stops sleeping:

```cpp
class MockWsConnection : public exchange::ws::IWsConnection {
public:
    void send(const std::string& msg) override;      // takes the lock, appends, wakes the waiters
    std::vector<std::string> sent() const;           // a copy, taken under the lock
    bool wait_for_sent(std::size_t count);           // blocks until `count` messages have been sent: true;
                                                     // or until release_waiters() has been called: false
    void release_waiters();                          // wakes every waiter, now and later
    // ... unchanged: connect, disconnect, set_on_*, inject_message, fire_*
private:
    mutable std::mutex       mu_;
    std::condition_variable  cv_;
    std::vector<std::string> sent_;                  // was the public sent_messages
    bool                     released_{false};
    // ... unchanged members
};
```

`wait_for_sent` has no timeout and no polling: it waits on the condition variable for its predicate. The 91 sites read
`conn->sent_messages[i]` and become `conn->sent()[i]`: a temporary vector that lives to the end of the full expression. In
`Execute.BlocksUntilResponse`:

```cpp
std::thread injector([&] {
    if (!conn_ref->wait_for_sent(1)) return;         // the client has sent the request; no sleep
    const int64_t id = json::parse(conn_ref->sent()[0])["req_id"].get<int64_t>();
    conn_ref->inject_message(make_add_order_response(id, true, "ORDER-SYNC"));
});
auto resp = client->execute(req, 2000ms);
conn_ref->release_waiters();                         // a failing run must not hang in join()
injector.join();
```

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 Private member and accessors, not a public vector plus a new method.** With `sent_messages` private the race cannot be written
  again: the compiler refuses an unlocked read. The price is 91 mechanical edits in four files, in one commit, covered by all four
  suites. *Alternative:* keep the public vector, lock only `send()`, and add `wait_for_sent`; one test changes, and the next unlocked
  cross-thread read is again the nightly's to find.
- **D2 One plan, two fixes, two closing commits** (`Closes #28`, then `Closes #27`), as both issues say "for its own plan" about the
  same thing and share one proof, the `tsan` tree.
- **D3 #28's fix is the atomic only.** The polling and the two `sleep_for(20ms)` are #42, because a deterministic version needs a seam
  in `WsReconnectSession::workerLoop`.
- **D4 The test presets time out after 120 s** (`execution.timeout`, as flywheel's do). This plan adds the first blocking waits to the
  tests; a test that blocks forever when broken then fails after two minutes and not at the job's ceiling. `test_presets` (R6) gains
  the rule. *Alternative:* a deadline inside `wait_for_sent`, which puts a wall-clock number into the test code.
- **D5 The evidence that a TSan fix holds** is each fixed test run 200 times in the `tsan` tree (`ctest --repeat until-fail:200`),
  and five consecutive green `tsan` dispatch runs in CI, which is plan 032's gate for the nightly. TSan reports a race only when it
  sees both accesses, so neither is a proof; a new report is a new issue (plan 032, D6).

## Steps

Every checkpoint is a full build and ctest of `debug-macos-arm64` (all green, no warning beyond the known #24 and #26), plus the
full suites of `asan-macos-arm64` and `tsan-macos-arm64`; counts are what I expect and are replaced by the real ones in the Outcome.
Commits go straight to `main`, one per step, pushed once the checkpoint is green.

### Step 0 — Approval

You approve or edit. Commit this plan (`docs(plan): plan 033 approved as written`), push, and comment on #27 and #28 with its path.
*Done when:* both issues link the plan and the plan links both.

### Step 1 — The test timeout (D4)

Red then green: `check_presets.cmake` first (R6: every test preset sets `execution.timeout` to 120), then `CMakePresets.json`.
*Test (modified) `test_presets`.* Hand-made changes in a scratch copy, each of which must fail naming R6: one test preset without a
timeout, one with 60.
*Checkpoint:* 507 of 507. *Done when:* red then green, mutations red, pushed.

### Step 2 — #28

*Red:* `ctest --preset tsan-macos-arm64 -R BackoffResetsAfterSuccessfulReconnect` fails today (plan 032, Step 8). *Change:* the atomic.
*Test:* none new. The existing test is the regression test, and the `tsan` tree, one command since plan 032, is its oracle.
*Checkpoint:* the default tree 507 of 507, `asan` 510 of 510, `tsan` 508 of 509 (the other race is still there); the test passes
200 times in a row in the `tsan` tree.
*Done when:* the test is green under `tsan` 200 times; the commit says `Closes #28`; pushed.

### Step 3 — #27

*Tests (new) `test_mock_ws_connection`* (`tests/unit/test_mock_ws_connection.cpp`, linked with `exchange_common` and `GTest::gtest_main`),
five tests, none with a sleep, since every wait is on a predicate and so the order of the threads does not matter:

- `SentReturnsTheMessagesInOrderAsACopy`;
- `WaitForSentReturnsAtOnceWhenAlreadySatisfied`;
- `WaitForSentWakesWhenAnotherThreadSends`;
- `ReleaseWaitersWakesWithFalse`, now and for a waiter that comes later;
- `ConcurrentSendsLoseNothing` (four threads, a hundred sends each; 400 kept, and the mutex proven under `tsan`).

Red first: the tests do not build before the API exists. *Change:* the mock, the 91 sites, `Execute.BlocksUntilResponse`, and the two
documents. *Hand-made changes* in a scratch copy: `send()` without the lock (`ConcurrentSendsLoseNothing` red under `tsan`),
`release_waiters` that sets no flag, and a `send()` that wakes nobody (each of the last two fails by the 120 s timeout of Step 1).
*Checkpoint:* the default tree 512 of 512 (five new tests), `asan` 515 of 515 (a dangling reference from a site I missed would be
caught here), `tsan` 514 of 514; `Execute.BlocksUntilResponse` passes 200 times in a row in the `tsan` tree. `CLAUDE.md` counts
twenty-six test executables and the new row.
*Done when:* all three trees are green, the commit says `Closes #27`, pushed.

### Step 4 — Five green `tsan` dispatch runs

`gh workflow run sanitizers.yml --ref main -f which=tsan`, five times, one at a time (a newer dispatch of the same `which` cancels the
one in flight; a run is about 6.5 minutes). *Done when:* five consecutive runs are `completed success`, and their ids are in the
Outcome. Then plan 032's Step 9 runs: the nightly gains `tsan`, and #30 closes.

## Designs I rejected

- **Keeping the public vector and locking only `send()`** — D1.
- **A deadline inside `wait_for_sent`** — D4.
- **Reading the vector under the client's mutex from the test** — the test cannot reach it, and it would couple the mock to the client.
- **`execute_async` and an injection from the main thread** — the test is about the blocking `execute`, which needs a second thread.
- **Fixing #42 here** — D3.

## Not in this plan

#42, and any change to `WsReconnectSession`. A suppression file. Any other finding of the five runs (a new issue, plan 032 D6). A tag:
the change is test-only and touches no header under `include/`.

## Self-review — risks and assumptions

- **A copy makes a kept reference dangle.** None of the 91 sites keeps one (no binding, iteration, address or mutation found by grep),
  and the `asan` tree is in the checkpoint for the one I missed.
- **`wait_for_sent` blocks forever if the client never sends.** `release_waiters()` after `execute` returns covers the failing path of
  the one test that waits across threads, and the 120 s timeout of D4 is the net for the rest.
- **TSan is not deterministic.** 200 repeats and five CI runs are evidence, not proof (D5). The Mac and CI report each race from
  opposite sides (#28: line 244 on the Mac, line 233 in CI; #27: inside libc++'s `std::string` and `std::vector` on the Mac, line 275
  in CI), so the fix is checked on both runtimes.
- **The mock is shared by four adapters' tests.** The 91 edits are one commit, read-only forms only, and every suite runs in three
  trees at the checkpoint.
- **A mutex in `send()` changes the timing the other tests see.** They are single-threaded, so it changes nothing they can observe.
- **I read the unit-test rule as covering the mock's new API**, which is test code with five tests of its own, and `test_presets` for
  the timeout; the atomic needs none beyond the existing test, whose oracle is the sanitizer tree.

## Outcome

Not started.
