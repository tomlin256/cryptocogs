# Benchmark + `.reserve()` vector growth for Binance/Coinbase/Crypto.com

**Status:** Done
**Depends on:** [Kraken performance profiling](kraken-performance-profiling.md),
[Kraken `.reserve()` fix](kraken-reserve-vector-growth.md) (v0.5.3, HEAD `cd521c8`)
**Closes:** [GitHub issue #22](https://github.com/tomlin256/cryptocogs/issues/22) — "Review
Binance/Coinbase/Crypto.com adapters for the same JSON-parse + unreserved-vector patterns as plan 025/026"

## Motivation

Plans kraken-performance-profiling and kraken-reserve-vector-growth profiled the
**Kraken** adapter only and found two things: (1)
`json::parse()` dominates WS message-dispatch cost (~84% of per-message time,
not stack allocation or return-by-value), and (2) every `push_back`/
`emplace_back`-into-`std::vector<T>` loop in `src/kraken/{ws_api,rest_api}.cpp`
grows from empty with no `.reserve()` first, a real but modest win (~1.5–4.8%
of the relevant parse cost, confirmed in isolation regardless of end-to-end
noise — see the Results of plan kraken-reserve-vector-growth). Both plans explicitly scoped out
Binance/Coinbase/Crypto.com. Issue #22 asks to close that gap: grep the three
remaining adapters for the same unreserved-vector pattern, and re-run enough
of the `tests/benchmarks/` harness on their WS hot path to confirm — not
assume — the same parse-dominance conclusion generalizes. This plan does both,
combined (unlike the deliberate split of plans kraken-performance-profiling
and kraken-reserve-vector-growth), because the latter already answered the
one question that made splitting them useful the first time: whether
`.reserve()` is worth doing at all. It is, unconditionally, on its own
low-risk merits (see Design decisions) — so there's no reason to gate the
mechanical fix behind a second round-trip once the benchmark step confirms
issue #22's actual open question (does parse-dominance generalize).

## Scope

- **In scope:** `src/binance/{rest_api,ws_streams,ws_api}.cpp`,
  `src/coinbase/{rest_api,ws_streams}.cpp` (no separate `ws_api.cpp` for
  Coinbase), `src/cryptocom/{rest_api,ws}.cpp` — every `push_back`/
  `emplace_back`-into-`std::vector<T>` loop sourced from a JSON array with a
  known `.size()`, same grep as the Kraken site list of plan
  kraken-reserve-vector-growth (full tables in Design
  decisions — 47 sites total: 19 Binance, 12 Coinbase, 16 Crypto.com). One new
  benchmark binary per adapter (`binance_benchmarks`, `coinbase_benchmarks`,
  `cryptocom_benchmarks`), each with one "high-frequency market tick" and one
  "deepest vector" WS dispatch benchmark, mirroring Step 3 of plan kraken-performance-profiling (its Kraken
  treatment) closely enough to compare, not the full six-benchmark-plus-
  sampling-profile apparatus (issue #22 explicitly scopes this to "a quick
  benchmark check," not a repeat of the full investigation in plan kraken-performance-profiling).
- **Out of scope:** REST-side benchmarks (issue #22 §2 explicitly scopes the
  "confirm it generalizes" check to the **WS hot path** — REST's
  network-dominance conclusion rests on real libcurl RTT, which is identical
  architecture across all four adapters and isn't re-litigated here). A
  sampling-profiler pass (the corroborating evidence for Kraken in plan kraken-performance-profiling; not
  repeated for three more adapters absent a reason to distrust the
  micro-benchmarks). Any adapter-specific optimization beyond `.reserve()`
  (e.g. a faster parser) — candidate #2 of plan kraken-performance-profiling remains explicitly unstarted and
  ungated by this plan, same as it is for Kraken.
- **Out of scope:** `CoinbaseStreamClient::send_queue_.push_back(payload)`
  (`ws_streams.cpp:246`) — a single queued outbound message, not a loop over a
  JSON array; not a reserve candidate.
- **Out of scope:** any `std::map`-keyed result field (none turned up in the
  grep for these three adapters — Binance/Coinbase/Crypto.com's
  open-orders-style endpoints all return arrays, unlike Kraken's map-keyed
  `OpenOrdersResult`/etc. — so unlike plan kraken-reserve-vector-growth there is no map exclusion list to
  write).
- **Out of scope:** request-*building* JSON-array sites. Unlike Kraken (which
  had two: `BatchAddRequest::to_json`, `AddOrderBatchRequest::build`), grepping
  all six in-scope files for `json::array()` found zero request-building
  `push_back` sites — every `json::array()` occurrence is a `.value(key,
  json::array())` response-parsing default or Crypto.com's `result_data()`
  empty-sentinel (see Design decisions). This exclusion category simply
  doesn't apply here; noted so its absence reads as confirmed, not missed.

## Design decisions (confirm at approval)

- **Same core safety rule as plan kraken-reserve-vector-growth:** every `.reserve()` call reuses the
  loop's own already-iterated expression, inserted immediately before the
  loop. If the existing range-for safely iterates that expression, `.size()`
  on the same object immediately before is equally safe — no new failure mode
  is introduced. This is why no new unit tests are needed (below).
- **New wrinkle plan kraken-reserve-vector-growth didn't have — four sites source from
  `j.value(key, json::array())`, not a plain `[]`/`.at()` subscript:**
  `BinanceAccount::from_json`'s `balances`/`permissions`
  (`rest_api.cpp:307,309`), `BinanceNewOrderResponse::from_json`'s `fills`
  (`rest_api.cpp:457`), and `detail::parse_ws_api_envelope`'s `rate_limits`
  (`ws_api.cpp:50`). `json::value(key, default)` returns by **value** (a
  temporary), not a reference, so reserving via a second `.value(...)` call
  would evaluate it twice — safe (no side effects either way) but wastefully
  constructs a redundant default `json::array()` when the key is absent. For
  these four sites only, hoist the expression into a named local **once**,
  then reserve and loop over the local:
  ```cpp
  const json rate_limits = j.value("rateLimits", json::array());
  r.rate_limits.reserve(rate_limits.size());
  for (const auto& el : rate_limits) r.rate_limits.push_back(BinanceWsRateLimit::from_json(el));
  ```
  This is still "the same expression," just bound to a name instead of
  repeated — strictly safer than the naive two-call alternative, not a
  deviation from the core rule.
- **Crypto.com's `result_data(frame)` helper needs no such hoisting.** Defined
  `src/cryptocom/ws.cpp:58` as `const json& result_data(const json& frame)` —
  it returns a real reference (either into `frame`'s tree or a
  `static const json empty` sentinel), so calling it twice is exactly as cheap
  and safe as Kraken's plain `j["key"]` sites. Confirmed by reading the
  function body, not assumed from the name.
- **Full site list — Binance (19 sites):**

  `src/binance/rest_api.cpp` (14):

  | Function | Vector | Loop source |
  |---|---|---|
  | `BinanceTickerPrice::from_json` (array branch only) | `t.entries` | `j` |
  | `BinanceOrderBook::from_json` | `b.bids` | `j["bids"]` |
  | `BinanceOrderBook::from_json` | `b.asks` | `j["asks"]` |
  | `BinanceTradesResult::from_json` | `r.trades` | `j` |
  | `BinanceKlinesResult::from_json` | `r.klines` | `j` |
  | `BinanceSymbolInfo::from_json` | `s.order_types` | `j["orderTypes"]` |
  | `BinanceExchangeInfo::from_json` | `e.symbols` | `j["symbols"]` |
  | `BinanceTicker24hr::from_json` (array branch only) | `t.entries` | `j` |
  | `BinanceAccount::from_json` | `a.balances` | `j.value("balances", json::array())` † |
  | `BinanceAccount::from_json` | `a.permissions` | `j.value("permissions", json::array())` † |
  | `BinanceOpenOrdersResult::from_json` | `r.orders` | `j` |
  | `BinanceMyTradesResult::from_json` | `r.trades` | `j` |
  | `BinanceNewOrderResponse::from_json` | `r.fills` | `j.value("fills", json::array())` † |
  | `BinanceCancelAllResponse::from_json` | `r.orders` | `j` |

  `src/binance/ws_streams.cpp` (4):

  | Function | Vector | Loop source |
  |---|---|---|
  | `BinanceDepthUpdateEvent::from_json` | `e.bids` | `j["b"]` |
  | `BinanceDepthUpdateEvent::from_json` | `e.asks` | `j["a"]` |
  | `BinancePartialDepth::from_json` | `d.bids` | `j["bids"]` |
  | `BinancePartialDepth::from_json` | `d.asks` | `j["asks"]` |

  `src/binance/ws_api.cpp` (1):

  | Function | Vector | Loop source |
  |---|---|---|
  | `detail::parse_ws_api_envelope` | `r.rate_limits` | `j.value("rateLimits", json::array())` † |

  († = hoist-to-local variant above. `TickerPrice`/`Ticker24hr`'s `else`
  branch pushes a single non-looped element — not a reserve candidate.)

- **Full site list — Coinbase (12 sites):**

  `src/coinbase/rest_api.cpp` (9):

  | Function | Vector | Loop source |
  |---|---|---|
  | `CoinbaseProductsResult::from_json` | `r.products` | `j` |
  | `CoinbaseOrderBook::from_json` | `b.bids` | `j.at("bids")` |
  | `CoinbaseOrderBook::from_json` | `b.asks` | `j.at("asks")` |
  | `CoinbaseTradesResult::from_json` | `r.trades` | `j` |
  | `CoinbaseCandlesResult::from_json` | `r.candles` | `j` |
  | `CoinbaseAccountsResult::from_json` | `r.accounts` | `j` |
  | `CoinbaseOrdersResult::from_json` | `r.orders` | `j` |
  | `CoinbaseCancelAllResult::from_json` | `r.order_ids` | `j` |
  | `CoinbaseFillsResult::from_json` | `r.fills` | `j` |

  `src/coinbase/ws_streams.cpp` (3):

  | Function | Vector | Loop source |
  |---|---|---|
  | `CoinbaseL2Snapshot::from_json` | `s.bids` | `j.at("bids")` |
  | `CoinbaseL2Snapshot::from_json` | `s.asks` | `j.at("asks")` |
  | `CoinbaseL2Update::from_json` | `u.changes` | `j.at("changes")` |

- **Full site list — Crypto.com (16 sites):**

  `src/cryptocom/rest_api.cpp` (10):

  | Function | Vector | Loop source |
  |---|---|---|
  | `CryptoComInstrumentsResult::from_json` | `r.instruments` | `j.at("data")` |
  | `CryptoComTickersResult::from_json` | `r.tickers` | `j.at("data")` |
  | `CryptoComOrderBook::from_json` | `b.bids` | `d.at("bids")` |
  | `CryptoComOrderBook::from_json` | `b.asks` | `d.at("asks")` |
  | `CryptoComCandlesResult::from_json` | `r.candles` | `j.at("data")` |
  | `CryptoComTradesResult::from_json` | `r.trades` | `j.at("data")` |
  | `CryptoComOrdersResult::from_json` | `r.orders` | `j.at("data")` |
  | `CryptoComUserBalance::from_json` | `b.position_balances` | `j.at("position_balances")` |
  | `CryptoComUserBalanceResult::from_json` | `r.data` | `j.at("data")` |
  | `CryptoComUserTradesResult::from_json` | `r.trades` | `j.at("data")` |

  `src/cryptocom/ws.cpp` (6):

  | Function | Vector | Loop source |
  |---|---|---|
  | `CryptoComTradeEvent::from_json` | `e.trades` | `result_data(frame)` |
  | `CryptoComBookEvent::from_json` | `e.bids` | `d.at("bids")` |
  | `CryptoComBookEvent::from_json` | `e.asks` | `d.at("asks")` |
  | `CryptoComCandlestickEvent::from_json` | `e.candles` | `result_data(frame)` |
  | `CryptoComUserOrderEvent::from_json` | `e.orders` | `result_data(frame)` |
  | `CryptoComUserBalanceEvent::from_json` | `e.balances` | `result_data(frame)` |

- **Benchmark harness generalizes the existing Kraken-only gate.** Today's
  top-level `CMakeLists.txt` hard-warns-and-skips `tests/benchmarks/` entirely
  if `CRYPTOCOGS_BUILD_BENCHMARKS` is ON but `CRYPTOCOGS_BUILD_KRAKEN` is OFF.
  This plan changes that to mirror the existing four-way library-build
  warning: fetch Google Benchmark and descend into `tests/benchmarks/`
  whenever `CRYPTOCOGS_BUILD_BENCHMARKS` is ON regardless of which adapter
  flags are on, and warn only if *all four* adapter flags are off. Inside
  `tests/benchmarks/CMakeLists.txt`, each adapter's benchmark binary is gated
  by its own `CRYPTOCOGS_BUILD_<ADAPTER>` (the existing `kraken_benchmarks`
  target gets the same `if(CRYPTOCOGS_BUILD_KRAKEN)` wrapper, for symmetry) —
  the same per-adapter gating convention `tests/CMakeLists.txt` already uses
  for unit tests.
- **One representative "high-frequency" push and one "deepest vector" push
  per adapter**, reusing existing captured fixtures for the small/fixed-depth
  ones and a synthetic ranged-JSON generator (mirroring Kraken's
  `make_book_json(depth)`) for the 10/50/100/500 depth sweep, since no
  existing fixture is deep enough on its own:
  - **Binance:** high-frequency = `fixtures::kAggTradeJson` /
    `BinanceAggTradeEvent` / `BinanceAggTradeSubscribe` (`agg_trade_stream`).
    Deepest = `BinanceDepthUpdateEvent` (bids/asks of `BinanceBookLevel`),
    synthetic `make_binance_depth_json(depth)` matching
    `{"e":"depthUpdate",...,"b":[[price,qty],...],"a":[...]}`. Dispatch
    benchmark mirrors `test_binance_ws_client.cpp`'s `Subscribe_Lifecycle`
    (`make_binance_stream_client`, `agg_trade_stream`, `make_ack`, `wrap`).
  - **Coinbase:** high-frequency = `wf::TICKER_JSON` / `CoinbaseTickerEvent` /
    `subscribe_ticker`. Deepest = `CoinbaseL2Snapshot` (bids/asks of
    `CoinbaseL2Level{double price, double size}`), synthetic
    `make_coinbase_snapshot_json(depth)` matching
    `{"type":"snapshot","product_id":...,"bids":[[price,size],...],"asks":[...]}`.
    Dispatch benchmark uses the **bespoke** `CoinbaseStreamClient` directly
    (`make_coinbase_stream_client` + `subscribe_ticker`) — optimistic
    subscribe, no ack round-trip to set up, per
    `test_coinbase_ws_client.cpp`'s existing pattern. This is *not* an
    `ExchangeWsClient` dispatch benchmark like the other two; it measures
    Coinbase's own type-keyed dispatch loop instead.
  - **Crypto.com:** high-frequency = `fx::TICKER_PUSH` / `CryptoComTickerEvent`
    / `CryptoComTickerSubscribe`. Deepest = `CryptoComBookEvent` (bids/asks of
    `CryptoComWsBookLevel{double price, double size, int64_t num_orders}`),
    synthetic `make_cryptocom_book_json(depth)` matching the nested
    `result.data[0].bids/asks` shape `CryptoComBookEvent::from_json` actually
    reads. Dispatch benchmark uses **undecorated**
    `make_exchange_ws_client(conn, cryptocom_frame_descriptor)` (no
    `HeartbeatResponder`) — the decorator's overhead is a separate, orthogonal
    concern from parse/dispatch cost, same isolation principle as benchmarking
    `ExchangeWsClient` alone for Kraken/Binance.
- **The benchmark step (Step 4) is measurement only, no `src/` changes** —
  same discipline as plan kraken-performance-profiling. It exists to answer issue #22's actual open
  question (does parse-dominance generalize), not to gate whether `.reserve()`
  is worth doing — plan kraken-reserve-vector-growth already settled that question generically (real,
  free, zero-behavior-change, worth doing regardless of end-to-end
  visibility). So Steps 5–7 apply all 47 sites unconditionally once Step 4
  lands, rather than making the fix conditional on each adapter's end-to-end
  delta being visible.
- **No new unit tests for the `.reserve()` fixes themselves** — identical
  structural safety argument as plan kraken-reserve-vector-growth (reusing an already-safely-iterated
  expression cannot introduce a new failure mode; `.reserve()` cannot change
  `size()`, values, or ordering). Existing `from_json` field-assertion
  coverage in `test_{binance,coinbase,cryptocom}_rest_responses.cpp` and
  `test_{binance,coinbase,cryptocom}_ws_client.cpp` is the correctness
  regression guard; Step 8's benchmark re-run is the performance regression
  guard.
- **No version bump** — same precedent as plan kraken-reserve-vector-growth (capacity-hint-only change
  to already-shipped adapters, no API/ABI/behavior change).

## Steps

Each step ends with a checkpoint: full `cmake --build build` **and**
`ctest --output-on-failure` from `build/` both green (confirms the default,
benchmarks-off tree is unaffected), then a commit. Steps 1–3 additionally
build+run a `build-bench/` tree (`-DCRYPTOCOGS_BUILD_BENCHMARKS=ON`, same
`RelWithDebInfo` treatment as plan kraken-performance-profiling) as their actual deliverable.

### Step 1 — Binance: harness generalization + benchmarks
- Generalize the top-level `CMakeLists.txt` benchmark block per Design
  decisions (drop the Kraken-only hard warning; warn only if all four adapter
  flags are off).
- `tests/benchmarks/CMakeLists.txt`: wrap `kraken_benchmarks` in
  `if(CRYPTOCOGS_BUILD_KRAKEN)`; add the `CRYPTOCOGS_BUILD_BINANCE`-gated
  `binance_benchmarks` target.
- New `tests/benchmarks/bench_binance_ws.cpp`: `BM_JsonParse_AggTradeRaw`,
  `BM_AggTradeFromJson`, `BM_DepthUpdateFromJson` (ranged 10/50/100/500,
  `Complexity(benchmark::oN)`), `BM_OnRawMessage_AggTradePush` (end-to-end via
  `MockWsConnection`), `BM_VectorPushBack_{No,With}Reserve` for
  `BinanceBookLevel` at the same four depths.
- **Unit tests:** none new — benchmarks exercise already-tested `from_json`/
  dispatch paths.
- **Done:** default `build/` checkpoint green; `build-bench/` builds and runs
  both `kraken_benchmarks` (unaffected) and `binance_benchmarks`.

### Step 2 — Coinbase benchmarks
- `tests/benchmarks/CMakeLists.txt`: add the `CRYPTOCOGS_BUILD_COINBASE`-gated
  `coinbase_benchmarks` target.
- New `tests/benchmarks/bench_coinbase_ws.cpp`: `BM_JsonParse_TickerRaw`,
  `BM_TickerEventFromJson`, `BM_L2SnapshotFromJson` (ranged, same four
  depths), `BM_OnRawMessage_TickerPush` (end-to-end via `CoinbaseStreamClient`
  + `MockWsConnection`, optimistic subscribe), `BM_VectorPushBack_{No,With}Reserve`
  for `CoinbaseL2Level`.
- **Unit tests:** none new.
- **Done:** `build-bench/` also builds and runs `coinbase_benchmarks`; default
  `build/` checkpoint green.

### Step 3 — Crypto.com benchmarks
- `tests/benchmarks/CMakeLists.txt`: add the `CRYPTOCOGS_BUILD_CRYPTOCOM`-gated
  `cryptocom_benchmarks` target.
- New `tests/benchmarks/bench_cryptocom_ws.cpp`: `BM_JsonParse_TickerRaw`,
  `BM_TickerEventFromJson`, `BM_BookEventFromJson` (ranged, same four depths),
  `BM_OnRawMessage_TickerPush` (end-to-end via undecorated
  `make_exchange_ws_client` + `MockWsConnection`), `BM_VectorPushBack_{No,With}Reserve`
  for `CryptoComWsBookLevel`.
- **Unit tests:** none new.
- **Done:** `build-bench/` also builds and runs `cryptocom_benchmarks`;
  default `build/` checkpoint green.

### Step 4 — Findings write-up (confirm/deny parse-dominance)
- Run all three new binaries from `build-bench/bin/`.
- Append a **Findings** section to this plan: per adapter, a table of
  JsonParse-raw / FromJson / OnRawMessage-end-to-end ns, the %-of-total the
  raw parse accounts for, and the ranged `VectorPushBack` no/with-reserve
  deltas at 10/50/100/500 with their %-of-`FromJson`-total. One explicit
  verdict sentence per adapter: does "parse dominates" hold, stated from the
  measured numbers.
- No `src/` changes in this step.
- **Unit tests:** none new.
- **Done:** Findings section written with real numbers for all three
  adapters; three stated verdicts.

### Step 5 — Binance `.reserve()` fixes (19 sites)
- Add the 19 `.reserve()` calls from the Binance site table to
  `src/binance/rest_api.cpp` (14), `src/binance/ws_streams.cpp` (4),
  `src/binance/ws_api.cpp` (1); the four `j.value(key, json::array())` sites
  use the hoist-to-local variant.
- **Unit tests:** none new — `test_binance_rest_responses.cpp` /
  `test_binance_ws_client.cpp` already assert every touched function's
  fields.
- **Done:** all 19 sites patched; full checkpoint green, no test-count
  regression.

### Step 6 — Coinbase `.reserve()` fixes (12 sites)
- Add the 12 `.reserve()` calls to `src/coinbase/rest_api.cpp` (9),
  `src/coinbase/ws_streams.cpp` (3).
- **Unit tests:** none new — `test_coinbase_rest_responses.cpp` /
  `test_coinbase_ws_client.cpp` cover every touched function.
- **Done:** all 12 sites patched; full checkpoint green.

### Step 7 — Crypto.com `.reserve()` fixes (16 sites)
- Add the 16 `.reserve()` calls to `src/cryptocom/rest_api.cpp` (10),
  `src/cryptocom/ws.cpp` (6).
- **Unit tests:** none new — `test_cryptocom_rest_responses.cpp` /
  `test_cryptocom_ws_client.cpp` cover every touched function.
- **Done:** all 16 sites patched; full checkpoint green.

### Step 8 — Re-benchmark + Results write-up + close out
- Rebuild `build-bench/` against the patched sources; re-run all three new
  binaries (filtered to the ranged/reserve benchmarks, as plan kraken-reserve-vector-growth Step 3
  did).
- Append a **Results** section: per-adapter before/after tables (ranged
  `FromJson` total + isolated `VectorPushBack` no/with-reserve), and a verdict
  consistent with (or explaining any divergence from) plan kraken-reserve-vector-growth's own finding
  that the isolated win is real even when swamped by end-to-end noise.
- Update this plan's Status to **Done**; update `docs/plans.md`'s row.
- Comment on and close GitHub issue #22 referencing this plan
  (`gh issue close 22 --comment "..."`).
- Final full default-tree checkpoint (`build/`, `ctest`) green; commit.
- **Unit tests:** none new.
- **Done:** Results section written with real re-measured numbers for all
  three adapters; `docs/plans.md` updated; issue #22 closed; final checkpoint
  green.

## Findings

All numbers below are mean-of-5-repetitions from `binance_benchmarks`/
`coinbase_benchmarks`/`cryptocom_benchmarks` built `RelWithDebInfo`
(`build-bench/`, `-DCRYPTOCOGS_BUILD_BENCHMARKS=ON`) on the machine this plan
was executed on, load average 2.0–2.9 throughout; coefficient of variation
was ≤1.6% for every benchmark except the sub-20ns `WithReserve/10` entries
(3.4–4.6% CV — expected at that absolute scale, where measurement granularity
itself is a larger fraction of the result). Re-run locally before trusting
absolute numbers on different hardware, though the *relative* story (per
plan kraken-performance-profiling) is expected to hold anywhere.

**Per-adapter dispatch breakdown** — mirrors plan kraken-performance-profiling's Kraken table
(`BM_JsonParse_*Raw` / `BM_*FromJson` / `BM_OnRawMessage_*` end-to-end):

| | Binance (aggTrade) | Coinbase (ticker) | Crypto.com (ticker) |
|---|---|---|---|
| `JsonParse` (raw bytes→DOM) | 1126 ns | 2742 ns | 2878 ns |
| `FromJson` (DOM→struct) | 211 ns | 318 ns | 1057 ns |
| `OnRawMessage` (full dispatch) | 1840 ns | 3105 ns | 4147 ns |
| Parse — % of total | **61.2%** | **88.3%** | **69.4%** |
| FromJson — % of total | 11.5% | 10.2% | 25.5% |
| Dispatch/other — % of total | 27.3% | 1.4% | 5.1% |

**Verdict: "parse dominates" holds for all three as the single largest cost
component, but the margin varies — it does not uniformly reproduce Kraken's
84%.** Coinbase (88.3%) reproduces Kraken's profile almost exactly (small
1.4% dispatch remainder, matching Kraken's own ~5.2% frame-descriptor+mutex
overhead in the same ballpark). Crypto.com (69.4%) and Binance (61.2%) still
have parse as the largest single line item — larger than `FromJson` and
dispatch combined in both cases — but with real, adapter-specific secondary
costs:
- **Crypto.com's `FromJson` (1057 ns) is ~3–5× Binance's/Coinbase's** (211/318
  ns) despite a comparably small single-object payload. Plausible cause (not
  investigated further — out of scope, see Scope): `CryptoComTickerEvent::
  from_json`'s helper indirection (`result_obj`/`str_field`/`num_int`, each
  re-walking `.contains()`+`.at()`) plus more fields in the terse-keyed ticker
  payload (13 vs Binance's ~9). Flagged as an observation, not a
  recommendation.
- **Binance's dispatch/other share (27.3%) is ~5–19× the other two's** (1.4%
  Coinbase, 5.1% Crypto.com). Plausible cause: the combined-stream envelope
  unwrap (`{"stream":...,"data":...}`) plus routing by stream-name string
  rather than a short "channel" string. Also flagged as an observation only.

**Ranged `FromJson` (10/50/100/500 levels), ns** — same `Complexity(oN)`
linear-scaling shape as Kraken's `BM_BookFromJson`:

| Depth | Binance `DepthUpdateFromJson` | Coinbase `L2SnapshotFromJson` | Crypto.com `BookEventFromJson` |
|---|---|---|---|
| 10 | 1264 | 1205 | 1824 |
| 50 | 4912 | 4947 | 6724 |
| 100 | 9330 | 9523 | 12764 |
| 500 | 44688 | 45440 | 60670 |

**Reserve-ceiling callout** — isolated `BM_VectorPushBack_NoReserve` vs.
`_WithReserve` (one vector; doubled below for the bids+asks pair the ranged
benchmark actually parses, same treatment plan kraken-performance-profiling used for Kraken's
`BookData`):

| Adapter | Depth | No-reserve | With-reserve | Save (bids+asks) | % of ranged `FromJson` total |
|---|---|---|---|---|---|
| Binance | 10 | 72.4 ns | 17.6 ns | 109.6 ns | 8.7% |
| Binance | 50 | 144 ns | 43.5 ns | 201.0 ns | 4.1% |
| Binance | 100 | 199 ns | 66.8 ns | 264.4 ns | 2.8% |
| Binance | 500 | 511 ns | 323 ns | 376.0 ns | 0.84% |
| Coinbase | 10 | 73.4 ns | 17.0 ns | 112.8 ns | 9.4% |
| Coinbase | 50 | 147 ns | 43.8 ns | 206.4 ns | 4.2% |
| Coinbase | 100 | 202 ns | 66.9 ns | 270.2 ns | 2.8% |
| Coinbase | 500 | 524 ns | 325 ns | 398.0 ns | 0.88% |
| Crypto.com | 10 | 82.1 ns | 16.7 ns | 130.8 ns | 7.2% |
| Crypto.com | 50 | 159 ns | 38.7 ns | 240.6 ns | 3.6% |
| Crypto.com | 100 | 218 ns | 67.1 ns | 301.8 ns | 2.4% |
| Crypto.com | 500 | 627 ns | 365 ns | 524.0 ns | 0.86% |

This reproduces Kraken's shape almost exactly (plan kraken-reserve-vector-growth: 4.8%@100, 1.5%@500) —
all three land in the same 2–3%@100 / <1%@500 range, proportionally smaller
at higher depth because parse cost grows faster than the reserve win. **This
confirms the generic conclusion of plan kraken-reserve-vector-growth holds across adapters: real, cheap,
worth doing, and definitionally not the fix for the headline parse cost.**

**Conclusion for Steps 5–7:** all three adapters confirm issue #22's premise
closely enough — parse is the largest cost component everywhere, and the
`.reserve()` win is real and in the same 1–9% range Kraken showed — to proceed
with the unconditional fix per the Design decisions above. Nothing here
changes the candidate-fix priority order of plan kraken-performance-profiling: `.reserve()` (this plan) is
worth doing regardless of message rate; investigating `json::parse` cost
itself remains gated on a confirmed production message rate, same as Kraken.

## Results

Re-ran `binance_benchmarks`/`coinbase_benchmarks`/`cryptocom_benchmarks`
(`build-bench/`, `RelWithDebInfo`, rebuilt against the patched
`src/{binance,coinbase,cryptocom}/*.cpp`) after Steps 5–7, 5 repetitions each,
filtered to the ranged `FromJson` and `VectorPushBack` benchmarks, and
compared against the Findings section's recorded "before" numbers.

**Ranged `FromJson`** (DOM→struct only — the JSON string is parsed once
outside the timed loop, same shape as Kraken's `BM_BookFromJson` in plan kraken-reserve-vector-growth;
see the Verdict below for why "full end-to-end" would be the wrong label for
either):

| Adapter | Depth | Before | After | Δ |
|---|---|---|---|---|
| Binance `DepthUpdateFromJson` | 10 | 1264 ns | 1068 ns | **−15.5%** |
| | 50 | 4912 ns | 4595 ns | **−6.5%** |
| | 100 | 9330 ns | 8943 ns | **−4.1%** |
| | 500 | 44688 ns | 44140 ns | **−1.2%** |
| Coinbase `L2SnapshotFromJson` | 10 | 1205 ns | 997 ns | **−17.3%** |
| | 50 | 4947 ns | 4597 ns | **−7.1%** |
| | 100 | 9523 ns | 9181 ns | **−3.6%** |
| | 500 | 45440 ns | 44498 ns | **−2.1%** |
| Crypto.com `BookEventFromJson` | 10 | 1824 ns | 1607 ns | **−11.9%** |
| | 50 | 6724 ns | 6315 ns | **−6.1%** |
| | 100 | 12764 ns | 12172 ns | **−4.6%** |
| | 500 | 60670 ns | 59456 ns | **−2.0%** |

Every adapter, every depth: a clean, monotonic improvement, in the same
ballpark as (mostly somewhat larger than) the Findings section's doubled
isolated-reserve prediction (7–9%@10, 2–4%@100, <1%@500). Coefficient of
variation stayed ≤1.1% throughout (one outlier: Coinbase/100 at 2.24%), so
these are real, reproducible deltas, not noise.

**Isolated `BM_VectorPushBack_NoReserve` vs `_WithReserve` (unaffected by the
`src/` change — these benchmark a standalone `std::vector`, not real
`from_json` — included to confirm the mechanism itself is unchanged):**

| Adapter | Depth | Before | After |
|---|---|---|---|
| Binance | 100 | 199 → 66.8 ns | 201 → 66.7 ns |
| Binance | 500 | 511 → 323 ns | 518 → 323 ns |
| Coinbase | 100 | 202 → 66.9 ns | 203 → 66.3 ns |
| Coinbase | 500 | 524 → 325 ns | 526 → 318 ns |
| Crypto.com | 100 | 218 → 67.1 ns | 221 → 67.0 ns |
| Crypto.com | 500 | 627 → 365 ns | 628 → 362 ns |

Reproduces the Findings section's numbers almost exactly (within measurement
noise), confirming `.reserve()`'s isolated effect is unchanged by the fix —
as expected, since these two benchmarks never called the patched code.

**Verdict:** all three adapters show a clean, monotonic, reproducible
improvement at every depth, roughly matching (and at the smaller depths,
modestly exceeding) the doubled-isolated-reserve prediction — no adapter
regressed at any depth. This is a cleaner reproduction than plan kraken-reserve-vector-growth itself
got for Kraken's structurally identical `BM_BookFromJson` (same shape: DOM
parsed once outside the timed loop, only `from_json` timed inside), which
showed a smaller, less consistent improvement and an outright regression at
depth 500 despite similarly low reported CV. Both are measuring the same
kind of small (hundreds-of-ns), noise-susceptible effect, so this plan
doesn't claim to know *why* this session's numbers came out cleaner than
those of plan kraken-reserve-vector-growth — plausibly ordinary cross-session machine-state variance (load
average, allocator arena warm-up, thermal state), the same category of cause
that plan kraken-reserve-vector-growth identified for its own noisier result, rather than anything specific to
these three adapters. The mechanism itself (confirmed unchanged above) is
what actually matters and is not in question either way: `.reserve()` is
real, free, and zero-behavior-change, per the original conclusion of plan kraken-reserve-vector-growth — here it
happens to also show up cleanly end-to-end, which is a bonus, not the basis
for the fix.

## Self-review — risks, assumptions, follow-on

- **Risk — reserve on the wrong expression.** Same class of risk as plan
  kraken-reserve-vector-growth's #1, at ~2.6× the site count (47 vs. 18). Mitigated identically: each
  site is reviewed individually against the tables above, not blind
  find-replace.
- **Risk — the four `j.value(key, json::array())` sites are a pattern plan
  kraken-reserve-vector-growth never had to handle** (Kraken's sites were all plain `[]`/`.at()`
  subscripts returning references). Mitigated by the hoist-to-local variant
  in Design decisions, which is strictly safer than a naive repeated
  `.value()` call, not a weaker version of the core rule.
- **Risk — Coinbase's dispatch path is structurally different** (bespoke
  `CoinbaseStreamClient`, not an `ExchangeWsClient` alias), so its end-to-end
  benchmark can't reuse Kraken/Binance/Crypto.com's dispatch code, only the
  same shape. Mitigated by grounding it in `test_coinbase_ws_client.cpp`'s
  already-tested optimistic-subscribe pattern rather than inventing new
  mechanics.
- **Assumption — one "high-frequency" and one "deepest" message per adapter
  is a reasonable, not exhaustive, proxy** for that adapter's WS hot path —
  the same judgment call plan kraken-performance-profiling made for Kraken (ticker/book), not a claim
  every message type was profiled.
- **Assumption — `.reserve()` stays behavior-free for every touched element
  type.** Verified by inspection, not assumed: `BinanceBookLevel`,
  `CoinbaseL2Level{double,double}`, `CryptoComWsBookLevel{double,double,int64_t}`,
  and the rest are all plain structs of primitives/strings — no custom
  allocator, no reserve-triggered exception path beyond `std::bad_alloc`
  (unhandled everywhere else in this codebase too), same as plan kraken-reserve-vector-growth's
  equivalent assumption for Kraken's types.
- **Scope-creep guard.** REST benchmarks, a sampling-profiler pass, and any
  fix beyond `.reserve()` are explicitly out of scope (see Scope) — issue #22
  itself scoped the benchmark check to the WS hot path only.
- **Follow-on:** none anticipated. This closes issue #22 in full — both the
  reserve-pattern review and the WS-hot-path benchmark check — bringing all
  three remaining adapters to the same state Kraken reached after plans
  kraken-performance-profiling and kraken-reserve-vector-growth. If
  Step 4 surfaces something adapter-specific and materially worse than
  Kraken's 84%/~2.3µs-per-message finding (e.g., a much deeper default
  subscription depth in practice), that's a new issue to file, not scope
  creep into this plan.
