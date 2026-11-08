# `kraken_example`'s WebSocket frames carry the order and the token

**Status:** Done (2026-11-06). Approved as written, so D1 stood at the default below. You asked for it to be done quickly, since it is
only an example: the *Outcome* says what was kept and what was cut. #50 and #52 are closed.

Closes [#50](https://github.com/tomlin256/cryptocogs/issues/50) and [#52](https://github.com/tomlin256/cryptocogs/issues/52) (#52 was
raised while planning #50, and you asked for it to be included).

**Not here:** the unused `creds` parameter of `ws_send` (it stays: it stands for a real transport that would pick the public or the
private endpoint with it); any library change (see *Designs I rejected*); a tag (nothing under `include/` or `src/` changes).

## Problem

`tests/examples/kraken_example.cpp` is a demo with a stubbed transport: `ws_send` prints each frame as `[WS SEND] <json>` and sends
nothing. Two defects in what it prints, read from `build/debug-macos-arm64/bin/kraken_example` at `85a16ec` (2026-11-06):

**#50, the `add_order` frame** (lines 145-150). The comment says "same params, just change the symbol to WS format", and only
`order_type` is copied:

```cpp
op.symbol = "BTC/USD";
ws::AddOrderRequest ws_req;
ws_req.order_type = op.order_type;
ws_req.req_id = 123456;
ws_send(ws_req.to_json(), ws_creds);
```

```
[WS SEND] {"method":"add_order","params":{"order_qty":0.0,"order_type":"limit","side":"buy","symbol":"","token":""},"req_id":123456}
```

`op` holds a limit **buy** of **1.2** at **26500.0** with `order_userref` 100054, and its symbol is `BTC/USD` after the first line. The
frame has none of that but `order_type` and `side`, and those two are right only because `Limit` and `Buy` are the first enumerators of
their enums, which a bare request holds since plan clear-clang-tidy-findings. (Before it `side` was indeterminate: the demo printed `sell`
on the Mac by chance, and the read was undefined behaviour.)

**#52, the token.** `ws_send` ignores its `creds`, and no request is given the token (`"my_ws_token"`, line 66, stands in for the one
the real code would fetch), so no private frame carries one:

| Frame (line of its `ws_send`) | Token as printed today |
|---|---|
| `add_order` (150) | `"token":""` |
| `amend_order` (172) | `"token":""` |
| `cancel_order` (184) | `"token":""` |
| `cancel_after` (196) | `"token":""` |
| `batch_add` (227) | `"token":""` |
| `subscribe`, `executions` (264) | none (`SubscribeRequest::token` is a `std::optional`, left out when unset) |
| `subscribe`, `balances` (270) | none |

The demo's own comment above the last two says "Private channels (token required)".

### What I checked

- `ws::AddOrderRequest` has a member of the same name and type for each field `op` sets (`order_type`, `side`, `order_qty`, `symbol`,
  `limit_price`, `order_userref`) and a `token`, so the copy is plain assignments. `rest::AddOrderRequest` takes the whole struct
  (`rest_req.params = op`) and `ws::BatchAddRequest` holds a `std::vector<OrderParams>`; only the single WS order flattens the fields,
  which is why the demo copies by hand.
- The other four requests have a `std::string token` and `SubscribeRequest` an optional one; `private_ws.cpp:66` sets it the same way
  (`req.token = token;`).
- No test runs an example: `kraken_example` is built and never run (plan clear-clang-tidy-findings, D3, compared its output before and after). The one
  precedent for a `cmake -P` check that runs a built target is `sanitizer_canary_*` (`tests/sanitize_checks/run_canary.cmake`).
- The 11 `[WS SEND]` lines are deterministic: nlohmann::json writes compact, keys sorted, doubles in its own shortest form (the
  `27000.0` of `amend_order` is already there). The REST lines carry a nonce, so the check does not read them.

## The change

**1. The demo.** The `add_order` block, the comment made true:

```cpp
// --- WebSocket: the same order. The pair is in WS format (BTC/USD) and the frame carries the token. ---
op.symbol = "BTC/USD";
ws::AddOrderRequest ws_req;
ws_req.order_type    = op.order_type;
ws_req.side          = op.side;
ws_req.order_qty     = op.order_qty;
ws_req.symbol        = op.symbol;
ws_req.limit_price   = op.limit_price;
ws_req.order_userref = op.order_userref;
ws_req.token         = ws_creds.token;
ws_req.req_id        = 123456;
ws_send(ws_req.to_json(), ws_creds);
```

and one `.token = ws_creds.token;` on each of `ws_amend`, `ws_cancel`, `ws_dms`, `ws_batch` and the two private `SubscribeRequest`s.
`add_order` then prints

```
[WS SEND] {"method":"add_order","params":{"limit_price":26500.0,"order_qty":1.2,"order_type":"limit","order_userref":100054,"side":"buy","symbol":"BTC/USD","token":"my_ws_token"},"req_id":123456}
```

and each of the other six gains `"token":"my_ws_token"`, last in its `params` (keys are sorted). The four public frames do not change.

**2. The check, `test_kraken_example`.** `tests/example_checks/check_kraken_example.cmake`, run as
`cmake -DEXAMPLE=$<TARGET_FILE:kraken_example> -DGOLDEN=<file> -P …` and registered in `tests/CMakeLists.txt` beside the target, inside
`if(CRYPTOCOGS_BUILD_KRAKEN)` (a tree without the Kraken adapter has no example, so it registers none). It runs the example and fails
unless:

- the exit status is `0` (a sanitizer's report ends a run non-zero; stdout and stderr are kept apart, so a report is never read as a frame);
- the `[WS SEND] ` lines, prefix removed, are the lines of `tests/example_checks/kraken_example_ws_frames.txt`, same count and same
  order, frame for frame. A failure names each frame that differs with the expected and the printed line, and a frame more or fewer; the
  file must have at least one frame, so an empty file cannot make an empty run pass;
- **no frame has an empty token** (`"token":""`). The file is the record of today's frames and can be regenerated without being read; this
  is the rule of #50 and #52 and does not depend on the file.

The file is the demo's WS output, one frame a line (11); the check's header gives the one-line command that regenerates it. The
lines are split with `string(FIND)` and not as a CMake list, because JSON has `[` and `]` in it.

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1 The demo's order stays a limit buy.** The file then shows every copy of the `add_order` frame but two: `side` and `order_type`.
  A bare `ws::AddOrderRequest` already holds `Buy` and `Limit`, so deleting those two assignments leaves the frame as it is and the check
  green (the way the original defect hid). Step 1 shows by hand that both copies work. *Alternative:* make the order a limit **sell**
  (`op.side`, and the word in the response fixture `"buy 1.20000000 XBTUSD @ limit 26500.00000"`, which the demo parses and does not
  print), and the check then guards `side` too. `order_type` cannot be guarded either way: `Limit` is the demo's order and the default.

## Steps

Commits go straight to `main`, one per step, pushed once the checkpoint is green. No commit message carries a closing keyword (a
`closes #50` in a message closes the issue on push, before the result comment): #50 and #52 are closed by hand in Step 2. Counts are
expected ones and are replaced by measured ones in the Outcome.

### Step 0 — Approval

*Tests:* none (documentation). You approve or edit. Commit this plan (`docs(plan): plan 039 approved as written`), push (documentation
only, so CI starts no run), and comment on #50 and #52 with the plan's path. *Done when:* both issues link the plan, and the plan links
both (it does).

### Step 1 — The check and the fixes (test first), one commit

*Start from green.* Configure `debug-macos-arm64` with the gate on (`-DCRYPTOCOGS_CLANG_TIDY=clang-tidy`), build, run the whole ctest:
expected 652 of 652 (564 + 88 gate). *Done when* the baseline is recorded; if anything is red I stop and tell you.

*Test (new):* `test_kraken_example`, above: `check_kraken_example.cmake`, `kraken_example_ws_frames.txt` and the `add_test`. No sleeps and no
network (the transport is a stub that prints). The file is written from the frames in *The change* and read against the demo's output
by eye, not generated from the fixed demo.

*Red first.* With the check in place and the demo untouched: expected red, with the seven frames of the table named as differing and the
five printed with `"token":""` named by the second rule. Then the fixes, and green.

*Hand-made changes to the demo* (in the tree; the example's object deleted first, because Apple's Make 3.81 compares one-second
timestamps; the number of objects compiled printed; the file restored and compared with `HEAD` afterwards), each run through the real check:

| | Change | Expected |
|---|---|---|
| a-e | delete one `add_order` assignment: `symbol`, `order_qty`, `limit_price`, `order_userref`, `token` | red, each |
| f, g | delete the `side`, then the `order_type` assignment | **green** (D1: recorded, not hidden) |
| h | `op.side = Side::Sell` with every copy in place | red, the frame reads `"side":"sell"` (the evidence that the `side` copy works) |
| i-n | delete one of the six other token assignments: `amend`, `cancel`, `cancel_after`, `batch_add`, `executions`, `balances` | red, each |
| o, p | delete the `ws_send` of the `add_order` frame; send it twice | red (a frame fewer; a frame more) |
| q | delete the `amend_order` token **and** regenerate the file from that demo | red from the empty-token rule alone (the blind regeneration it is for) |

*Checkpoint.* Full build; ctest of `debug-macos-arm64` with the gate on: expected 653 of 653 (565 + 88: one new test, no new unit), the
example's own `clang_tidy_` unit included, no warning. *Done when:* red then green as stated; a-e and h-q red and f, g green; the
checkpoint green; pushed; the `release-linux-x86_64` run of the commit green (GCC 15.2, `-Werror`).

### Step 2 — Docs, close

*Tests:* none new. `CLAUDE.md`: the structure tree (`tests/example_checks/` and its two files; the `kraken_example.cpp` line), *Running
tests* (the ctest count, and the new check in its own row: the sixteen `cmake -P` checks need no build and this one runs the built
example), the Crypto.com-only count (expected unchanged, 157: the check is registered only with the Kraken adapter; shown by
configuring such a tree and listing its tests with `ctest -N`, and the same in a default tree). All numbers measured. This file's
*Outcome*. Turn the gate off again in the tree (`-DCRYPTOCOGS_CLANG_TIDY=`). *Done when:* pushed, and #50 and #52 closed by hand, each
with a comment that lists the commit, the red-then-green record and the counts.

### Step 3 — After the push: measure

Not blocking the push (push, then measure). `asan-macos-arm64` and `tsan-macos-arm64`, full suites (the check runs the example under
each sanitizer); then `gh workflow run sanitizers.yml --ref main -f which=asan`, and `tsan`, one dispatch at a time. Linux matters
here: GCC 15 at `-O2` under TSan reported `-Wmaybe-uninitialized` for a request local whose `std::optional<std::string>` members are never
engaged (*Compiler warnings* in `CLAUDE.md`), and `ws_req` is that shape. The tests that hit it copied the request and `ws_req` is only
assigned into, but only the dispatch shows. *Done when:* the Mac trees are green with no report and both dispatches are green.

## Designs I rejected

- **Make `ws::AddOrderRequest` hold an `OrderParams`**, as `rest::AddOrderRequest` does, so the demo says `ws_req.params = op`. It
  removes 24 duplicated fields, and it breaks every caller that sets `ws_req.side`: a major release for a demo.
- **A converter in the library**, `ws::AddOrderRequest` from an `OrderParams` and a token. Additive, and it would make the comment true,
  but it is a third place to keep in step with `OrderParams` and the WS struct, and public API for a low-priority demo. If you want it,
  it is its own plan.
- **`ws_send` puts the token into `params`** (#52's second suggestion). It hides the API the demo exists to show (`private_ws.cpp:66`
  sets `req.token`), and the frames printed would not be what the request types build.
- **Extract the demo's order building into a function** that a gtest calls. It bends a demo, which shows its code inline, around a
  test, and the test would no longer say what the demo prints, which is what both issues are about.
- **Compare the WS frame with the REST body.** The demo prints 60 characters of the REST body, with a nonce; printing it all is a bigger
  change to the demo than the bugs.
- **A hand-written case for each of the seven frames this plan changes.** It needs a rule for which frames count, and the reader of the
  file then sees a part of what the demo sends. One file of all 11 frames is read in one place and its diff in review is the change.
- **`string(JSON)` per field in the check.** Better messages, another place to be wrong; a frame is a line and the failure prints both.
- **A command-line option on the demo** to flip the side, so the check could guard `side` (D1). An option on a demo for a test.
- **Run every example in ctest.** The others talk to real exchanges.

## Self-review — risks and assumptions

- **The file pins the demo's WS output.** A deliberate change to the order, the placeholder token or the other frames turns the check
  red and the file is regenerated with the demo (the command is in the check's header; the diff of the file is the review). A cost of
  one file per change, and the point.
- **A regenerated file can be accepted unread.** That is why the empty-token rule does not use the file, and mutation q shows it bites
  alone. What it cannot see is a *missing* token on a new private request: that is the reader of the diff.
- **Two copies are invisible to the check** (D1); f, g and h record that.
- **The check runs a real binary, so it needs the build.** `ctest` in a configured, unbuilt tree fails with a message that says to
  build; every gtest has the same property.
- **Numbers.** `1.2` and `26500.0` come out of nlohmann's own double-to-string, the same on Apple clang and GCC; the Linux CI run is the
  second platform.
- **Sanitizer trees run the example too.** An instrumented example prints the same frames; a report goes to stderr and a non-zero exit
  fails the check.
- **The line splitter is mine.** CMake has no line iterator that is safe for text with `[` in it; the loop is about ten lines and is
  exercised by every red and green run above, including the count mismatches o and p.
- **GCC 15** (Step 3). If it reports, the fix is in the demo's code, not a flag.
- **clang-tidy 23** checks the new lines as part of the example's unit. A finding is fixed, not `NOLINT`ed (the closed list in `CLAUDE.md`).
- **Behaviour change:** the demo's printed frames and nothing else. No library file, no public header, no version.
- **I assume** you want `ws_creds.token` in every private frame, as #50's sketch and #52 say, and that `"my_ws_token"` is the demo's
  placeholder for it (line 66, the real call commented out above it).
- **I read the unit-test rule as covering a demo.** There is no unit to test, so the test is the demo's observable output.

## Outcome

*As built, 2026-11-06.* Every step and every piece of evidence of the plan stood, with the two cuts under *Differences*.

| Step | Commit | `debug-macos-arm64`, clang-tidy gate on | CI |
|---|---|---|---|
| 0 Approval | `f1d17a7`; comments on [#50](https://github.com/tomlin256/cryptocogs/issues/50#issuecomment-6689282235) and [#52](https://github.com/tomlin256/cryptocogs/issues/52#issuecomment-6689282658) | none (documentation) | none: documentation only |
| 1 The check and the fixes | `047d02c` | baseline **652 of 652**; red first; then **653 of 653** (565 + 88 gate), no warning | [run 42249330660](https://github.com/tomlin256/cryptocogs/actions/runs/42249330660) green: `release-linux-x86_64`, GCC 15.2, `-Werror`; 565 of 565, `test_kraken_example` among them |
| 2 Docs | the commit that adds this row | not rebuilt (documentation only); the gate turned off again: **565 of 565** | none: documentation only |
| 3 After the push | none | not run (*Differences*, 1) | the `Sanitizers` run, below |

**Red first (Step 1).** On the demo as it was (a copy of its binary) the check named frames 0, 1, 2, 3, 4, 8 and 9 as differing from the
file and frames 0 to 4 as having an empty token, as predicted. The four frames that the fixes leave alone (the three public subscribes
and the ping) matched the file, which was written by hand from the plan before the demo was touched. With the fixes: green, 11 frames.

**Hand-made changes (Step 1).** One script; the example's object deleted first and 1 object compiled each time; the control first
(green); the source identical to the saved copy of the fixed demo at the end (`cmp`), and rebuilt green:

| | Change | Result |
|---|---|---|
| a-e | delete `add_order`'s `symbol`, `order_qty`, `limit_price`, `order_userref`, `token` | red, frame 0 |
| f, g | delete `side`, `order_type` | **green** (D1, as planned) |
| h | `op.side = Side::Sell`, every copy in place | red, and frame 0 reads `"side":"sell"` |
| i-n | delete the token of `amend_order`, `cancel_order`, `cancel_after`, `batch_add`, `executions`, `balances` | red, frames 1, 2, 3, 4, 8, 9 |
| o | delete the `add_order` `ws_send` | red: frames 0 to 10 named (each is out of place, the last is missing) |
| p | send it twice | red: frames 1 to 11 named (the last is extra) |
| q | delete the `amend_order` token, regenerate the file from that demo | red, with `frame 1 has an empty token` and no frame diff: the rule bites alone |

**Differences from the plan as written.**

1. **The two Mac sanitizer trees were not run.** Step 3 named the full suites of `asan-macos-arm64` and `tsan-macos-arm64`, then the Linux
   dispatches. For an example, as you asked, only the dispatch was done: `sanitizers.yml` with `which=both`, one run (GCC 15.2 on Linux),
   which is where Step 3's risk lives (GCC 15's `-Wmaybe-uninitialized` on a request local) and the only place with LeakSanitizer. What
   is missing is the check running under Apple's sanitizer runtimes.
2. **The Crypto.com-only count was shown, not measured by a full build.** `test_kraken_example` is registered only inside
   `if(CRYPTOCOGS_BUILD_KRAKEN)`, so that tree's 157 cannot change. A configure-only scratch tree with the other three exchanges off
   lists no `test_kraken_example`, and the default tree lists it.
3. **q's counts in my script's results file are off by one.** The script counted lines containing "expected" and "has an empty token", and
   the message's own header ("11 printed, 11 expected), or one has an empty token") contains both. I read the output: one empty-token
   block, no frame diff.
4. **The hand-made changes were compared with a saved copy of the fixed demo, not with `HEAD`**, since the fixes were not committed
   while they ran.

**Found along the way, and not fixed here.** [#53](https://github.com/tomlin256/cryptocogs/issues/53) (documentation, low): the demo's
header comment names `kraken_types.hpp`, which went in v0.1.1; the other mentions are in the historical plans.

**Counts, measured (Step 2).**

| | Before | After |
|---|---|---|
| `debug-macos-arm64`, ctest, gate off | 564 | **565** (548 gtests, 16 `cmake -P` checks, `test_kraken_example`) |
| clang-tidy gate, default tree | 88 | **88** (no new unit: the example's own was already one, and it passed with the new lines) |
| both, gate on | 652 | **653** |
| Crypto.com-only tree, ctest | 157 | 157 (Differences, 2) |

**Step 3, after the push.** The `Sanitizers` workflow, dispatched for both modes at `047d02c` ([run 42249623424](https://github.com/tomlin256/cryptocogs/actions/runs/42249623424)): **green**. `asan` **568 of 568** (the 565, the two canaries and the coverage test) in 6 min 39 s, `tsan` **567 of 567** (the 565, the canary and the coverage test) in 6 min 46 s; `test_kraken_example` passed in both; GCC 15.2 with `-Werror` reported nothing on `ws_req`, which was Step 3's risk; no warning and no sanitizer report in either log.
