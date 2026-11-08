# Pre-size `std::vector` growth in Kraken `from_json` (`.reserve()`)

**Status:** Done
**Depends on:** [Kraken performance profiling](025-kraken-performance-profiling.md) (v0.5.3, HEAD `d844f42`)

## Motivation

Plan kraken-performance-profiling benchmarked every plausible Kraken hot path and found the dominant
WS-dispatch cost is `nlohmann::json::parse()` itself (~84% of
`BM_OnRawMessage_TickerPush`), not stack allocation or return-by-value — but
it also isolated a real, if modest, secondary win: every `push_back`/
`emplace_back` loop in `src/kraken/{ws_api,rest_api}.cpp` grows its
`std::vector` from empty with no `.reserve()` call first, even though the
loop's own JSON array size is known before the loop starts. Plan kraken-performance-profiling's
reserve-ceiling benchmark quantified this at ~4.8% of `BM_BookFromJson/100`'s
total and ~1.5% at depth 500, and its Findings section listed `.reserve()` at
every grepped site as the one concretely-recommended, low-risk fix — deferred
to its own plan so it could cite before/after numbers rather than a
prediction. This is that plan.

## Scope

- **In scope:** add `.reserve(n)` immediately before every `push_back`/
  `emplace_back`-into-`std::vector<T>` loop in `src/kraken/ws_api.cpp` and
  `src/kraken/rest_api.cpp` where the loop's source is a JSON array (`n` =
  that array's `.size()`), using the *exact same* JSON expression the
  existing loop already iterates. No other behavior changes.
- **Out of scope:** the two sites that build a `json` array via
  `json::array()` + `.push_back()` rather than a `std::vector<T>` — nlohmann's
  `basic_json` has no public `.reserve()` (confirmed by grepping the vendored
  header; the only `reserve()` calls in `nlohmann/json.hpp` are on an internal
  destructor-avoidance stack, unrelated). Reaching into the private
  representation via `get_ref<json::array_t&>().reserve(n)` would be
  internal-representation-poking not used anywhere else in this codebase, for
  a saving plan kraken-performance-profiling already showed is small even on the clean `std::vector`
  case. Left alone: `BatchAddRequest::to_json` and
  `AddOrderBatchRequest::build` (batch order placement — bounded by
  exchange-imposed batch size limits, and per plan kraken-performance-profiling's REST findings,
  dominated by network + signing cost regardless).
- **Out of scope:** `json::parse()` cost itself (plan kraken-performance-profiling candidate #2 —
  explicitly not recommended without a confirmed production message rate) and
  anything in Binance/Coinbase/Crypto.com (plan kraken-performance-profiling didn't benchmark them;
  a possible future follow-up, not committed to here).
- **Out of scope:** `OpenOrdersResult`/`ClosedOrdersResult`/
  `QueryOrdersResultWrapper`/`TradesHistoryResult`/`QueryTradesResultWrapper`/
  `LedgersResult`/`QueryLedgersResultWrapper`/`OpenPositionsResult`/
  `AssetInfoResult`/`AssetPairsResult`/`TickerResult`/`AccountBalanceResult`/
  `ExtendedBalanceResult` — all populate a `std::map`, not a `std::vector`
  (`r.open[k] = ...` etc.); `std::map` has no `.reserve()`. Plan kraken-performance-profiling already
  identified this for `OpenOrdersResult` specifically; the same applies to
  every other map-keyed result type by the same reasoning, so none of them
  are reserve candidates.

## Design decisions (confirm at approval)

- **Every reserve call reuses the loop's own already-iterated expression.**
  E.g. for `for (const auto& item : j["data"]) m.data.push_back(...)`, the fix
  is `m.data.reserve(j["data"].size()); for (const auto& item : j["data"])
  m.data.push_back(...);` — same `j["data"]`, not a re-derived path. Since the
  existing loop already safely iterates that expression (it's inside the same
  `if (j.contains(...))` guard, or is the guard itself), calling `.size()` on
  it immediately before cannot introduce a new failure mode: if the range-for
  is safe, `.size()` on the same object is equally safe. This is the key
  safety argument for not adding new tests (see Steps).
- **Full site list — `src/kraken/ws_api.cpp` (12 sites):**

  | Function | Vector | Loop source |
  |---|---|---|
  | `CancelOrderResponse::from_json` | local `v` (→ `orders_cancelled`) | `res["orders_cancelled"]` |
  | `BatchAddResponse::from_json` | local `v` (→ `orders`) | `res["orders"]` |
  | `TickerMessage::from_json` | `m.data` | `j["data"]` |
  | `BookData::from_json` | `b.bids` | `j["bids"]` |
  | `BookData::from_json` | `b.asks` | `j["asks"]` |
  | `BookMessage::from_json` | `m.data` | `j["data"]` |
  | `TradeMessage::from_json` | `m.data` | `j["data"]` |
  | `OHLCMessage::from_json` | `m.data` | `j["data"]` |
  | `InstrumentMessage::from_json` | `m.assets` | `d["assets"]` |
  | `InstrumentMessage::from_json` | `m.pairs` | `d["pairs"]` |
  | `ExecutionsMessage::from_json` | `m.data` | `j["data"]` |
  | `BalancesMessage::from_json` | `m.data` | `j["data"]` |

- **Full site list — `src/kraken/rest_api.cpp` (6 sites):**

  | Function | Vector | Loop source |
  |---|---|---|
  | `OHLCResult::from_json` | `r.candles` | `v` (the per-pair candle array) |
  | `OrderBookResult::from_json` (local `parse` lambda) | local `entries` | `arr` |
  | `RecentTradesResult::from_json` | `r.trades` | `v` (the per-pair trade array) |
  | `AddOrderBatchResult::from_json` | `r.orders` | `j["orders"]` |
  | `DepositMethodsResult::from_json` | `r.methods` | `j` |
  | `DepositAddressesResult::from_json` | `r.addresses` | `j` |

  `OHLCResult`/`RecentTradesResult` reserve inside an outer
  `for (const auto& [k, v] : j.items())` that only populates `candles`/
  `trades` on the one non-`"last"` key Kraken's single-pair REST response
  actually sends — the reserve mirrors that same already-existing
  single-pair assumption, not a new one.
- **No fixes to request-*building* JSON-array sites** (see Scope) — this
  plan only touches response `from_json` deserialization.
- **No new unit tests.** Every touched function already has `from_json`
  field-assertion coverage in `tests/unit/test_ws_responses.cpp` or
  `tests/unit/test_rest_responses.cpp` (per CLAUDE.md's test suite table).
  `.reserve()` is a pure capacity hint — it cannot change `size()`, element
  values, or ordering, so it cannot turn a passing correctness assertion into
  a failing one, and (per the design decision above) the only way it could
  introduce a *new* failure is by evaluating an unsafe expression, which is
  structurally ruled out by reusing the loop's own expression. The existing
  suite is the correctness regression guard; Step 3's benchmark re-run is the
  performance regression guard.
- **Benchmark-verified, not just diffed.** `tests/benchmarks/` (plan kraken-performance-profiling)
  already has `BM_BookFromJson` and `BM_ParseOpenOrdersResponse`... — note
  `BM_ParseOpenOrdersResponse` parses a `std::map`-backed result and is *not*
  expected to move (see Scope); the relevant before/after pair is
  `BM_BookFromJson` (depths 10/50/100/500) plus a new
  `BM_TickerMessageFromJson`/`BM_InstrumentMessageFromJson`-style check is not
  needed — `BM_BookFromJson` alone already isolates the exact vectors (`bids`/
  `asks`) plan kraken-performance-profiling's reserve-ceiling benchmark measured against. Step 3 reruns
  it post-fix and compares against plan kraken-performance-profiling's recorded ceiling
  (~4.8%@100, ~1.5%@500) instead of assuming the prediction held.

## Steps

Each step ends with a checkpoint: full `cmake --build build` **and**
`ctest --output-on-failure` from `build/` both green, then a commit.

### Step 1 — `src/kraken/ws_api.cpp` (12 sites)
- Add the 12 `.reserve()` calls listed above, each immediately before its
  loop, each reusing the loop's own JSON expression.
- **Unit tests:** none new — see Design decisions. `ctest` re-runs
  `test_ws_responses.cpp`'s existing `from_json` assertions (incl. the
  10-level `kBookSnapshotJson` fixture, which is genuinely multi-element)
  against every touched function.
- **Done:** all 12 sites patched; `cmake --build build && ctest
  --output-on-failure` from `build/` fully green (no test count regression
  from plan kraken-performance-profiling's baseline).

### Step 2 — `src/kraken/rest_api.cpp` (6 sites)
- Add the 6 `.reserve()` calls listed above, same rule.
- **Unit tests:** none new — `test_rest_responses.cpp`'s existing assertions
  for `OHLCResult`, `OrderBookResult`, `RecentTradesResult`,
  `AddOrderBatchResult`, `DepositMethodsResult`, `DepositAddressesResult`
  cover every touched function.
- **Done:** all 6 sites patched; same full checkpoint green.

### Step 3 — Benchmark re-verification + results write-up
- Rebuild the existing `build-bench/` tree (`RelWithDebInfo`,
  `-DCRYPTOCOGS_BUILD_BENCHMARKS=ON`, from plan kraken-performance-profiling — no CMake changes needed)
  and re-run `./build-bench/bin/kraken_benchmarks
  --benchmark_filter='BM_BookFromJson|BM_VectorPushBack'`.
- Append a **Results** section to this plan: a before/after table for
  `BM_BookFromJson` at depths 10/50/100/500 (before = plan kraken-performance-profiling's recorded
  numbers), the measured %, and a one-line verdict on whether it landed near
  plan kraken-performance-profiling's predicted ceiling (~4.8%@100 / ~1.5%@500) or diverged (and if
  so, why).
- Final full default-tree checkpoint (`build/`, `ctest`) green; update this
  plan's Status to **Done**; update `docs/plans.md`'s row to `Done`.
- **Unit tests:** none new.
- **Done:** Results section written with real re-measured numbers; final
  checkpoint green; both doc updates made; commit.

## Results

Re-ran `kraken_benchmarks` (`build-bench/`, `RelWithDebInfo`, rebuilt against
the patched `src/kraken/{ws_api,rest_api}.cpp`) after Steps 1–2, 10
repetitions each, and compared against plan kraken-performance-profiling's recorded "before" numbers.

**Full end-to-end `BM_BookFromJson` (json::parse + reserve + push_back
combined):**

| Depth | Before (kraken-performance-profiling) | After (this plan) | Δ | Predicted ceiling |
|---|---|---|---|---|
| 10 | 0.84 µs | 0.772 µs | −8.1% | (not separately predicted) |
| 50 | 3.0 µs | 2.912 µs | −2.9% | (not separately predicted) |
| 100 | 5.6 µs | 5.551 µs | −0.9% | −4.8% |
| 500 | 26.2 µs | 26.834 µs | **+2.4%** | −1.5% |

This does **not** cleanly reproduce the predicted win at 100/500 — depth 500
actually measured slower. Per-run coefficient of variation was ~0.7–1.0%
(10 reps), which explains part of the spread but not all of the depth-500
delta.

**Isolated `BM_VectorPushBack_NoReserve` vs `_WithReserve` (the controlled
comparison, no json::parse involved) — same run:**

| Depth | Before (kraken-performance-profiling) | After (this plan) |
|---|---|---|
| 100 | 199 → 64 ns (saves 135 ns) | 201 → 66.5 ns (saves 134.5 ns) |
| 500 | 495 → 300 ns (saves 195 ns) | 514 → 321 ns (saves 193 ns) |

This reproduces plan kraken-performance-profiling's original numbers almost exactly (within ~1 ns).

**Verdict:** the `.reserve()` mechanism itself behaves exactly as plan kraken-performance-profiling
predicted — the isolated comparison, unaffected by json::parse noise, is
essentially identical run-to-run. The full end-to-end `BM_BookFromJson`
number, however, is dominated by json::parse (plan kraken-performance-profiling's ~84% finding), and
cross-session differences in that dominant term (background load, thermal
state, allocator arena state — this machine's load average was 2.3–3.6
during this run) are large enough to swamp the ~270–390 ns reserve win at
the full-pipeline level. In short: **the fix does exactly what was measured
in isolation; it is not reliably visible in a full end-to-end trace because
it's smaller than the run-to-run noise of the dominant cost it sits next
to.** This doesn't change plan kraken-performance-profiling's own verdict — it reinforces it: reserve()
was correctly triaged as a minor, low-priority-but-free contributor, not a
fix expected to move the headline number. The change is kept (free,
mechanical, zero behavior change, confirmed correct by the full existing
test suite) on its own merits, not on the strength of an end-to-end
benchmark delta.

## Self-review — risks, assumptions, follow-on

- **Risk — reserve on the wrong expression.** The one way this class of
  change can go wrong is reserving against a different JSON node than the one
  actually iterated (e.g. a copy-paste slip between the bids/asks pair in
  `BookData::from_json`). Mitigated by the "reuse the loop's own expression"
  rule in Design decisions and by reviewing each of the 18 sites individually
  against the table above rather than a blind find-replace.
- **Risk — this is a small win being sold as more than it is.** Plan kraken-performance-profiling was
  explicit: the reserve win is real but minor (1.5-4.8%) next to the
  json::parse cost it doesn't touch. This plan doesn't change that
  conclusion — it exists because plan kraken-performance-profiling recommended it as a cheap,
  near-zero-risk cleanup, not because it resolves Rob's original worry (that
  was already resolved, negatively, by plan kraken-performance-profiling itself).
- **Assumption — `.reserve()` is genuinely free of behavior change in this
  codebase.** True for `std::vector<T>` with no custom allocator or exotic
  `T` (`BookEntry`, `TickerData`, etc. are all plain structs of primitives/
  strings) — no reserve-triggered exception paths beyond `std::bad_alloc`,
  which isn't handled specially anywhere else in this codebase either.
- **Follow-on:** plan kraken-performance-profiling's candidate #2 (reducing `json::parse` cost itself,
  e.g. a SAX-based extractor for the highest-frequency channels) remains
  explicitly unstarted and not recommended without Rob confirming a
  production message rate that would justify the larger, riskier change.
