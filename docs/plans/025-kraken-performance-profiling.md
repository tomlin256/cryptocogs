# Kraken performance profiling: benchmark harness + hot-spot findings

**Status:** Done
**Depends on:** none (targets the Kraken adapter as of v0.5.3)

## Motivation

Rob is worried that the Kraken adapter's heavy use of stack-allocated value types
and return-by-value (`exchange::kraken::RestResponse<T>`, `HttpRequest`,
`TickPrice`, the intermediate `std::string`s inside `KrakenCredentials::sign()`,
etc.) is a performance problem. That's a reasonable thing to check, but not
something to act on by intuition: C++17 makes copy elision for prvalues
mandatory and most compilers apply NRVO to named locals, so "returns a struct by
value" is frequently free already — and for the REST path, a real libcurl
network round-trip (milliseconds) will dwarf any local allocation (nanoseconds)
regardless. Whether that holds here, and whether anything *else* (JSON
parsing being the prime suspect — nlohmann::json is heap/node-heavy) is
actually the bottleneck, is an empirical question. This plan's job is to
measure, not guess: build a benchmark harness, run it against every plausible
hot path, corroborate with a sampling profile, and produce a ranked findings
list. It deliberately stops short of writing fixes — see Scope.

## Scope

- **In scope:** the Kraken adapter only (REST + WS), per Rob's "starting with
  kraken." A gated Google Benchmark harness; micro-benchmarks for every
  candidate hot path found by reading `rest_client.inl`, `auth.cpp`,
  `ws_client.cpp`, and `tick_price.cpp`; one coarse sampling-profiler pass to
  catch anything the chosen micro-benchmarks miss (lock contention, unexpected
  calls); a findings write-up ranking each candidate by measured cost with a
  negligible / measurable-but-not-worth-it / worth-optimizing verdict.
- **Out of scope:** implementing any optimization. If Step 4's findings show
  something genuinely worth fixing, that becomes a fresh, separately-approved
  plan informed by real numbers (see Self-review) — not bundled in here.
  Also out of scope: Binance/Coinbase/Crypto.com (candidates for an identical
  follow-up plan later, since they share `exchange_common`, but not committed
  to here); and network-layer tuning (curl connection reuse, keep-alive), which
  is a different concern from allocation cost.

## Design decisions (confirm at approval)

- **Google Benchmark v1.9.5**, FetchContent'd exactly like the existing
  gtest/spdlog/CLI11 deps, with `BENCHMARK_ENABLE_TESTING` and
  `BENCHMARK_ENABLE_INSTALL` forced `OFF` (same "don't pollute install/CI"
  treatment already applied to `JSON_Install` / `IXWEBSOCKET_INSTALL` /
  `INSTALL_GTEST`).
- **New top-level option `CRYPTOCOGS_BUILD_BENCHMARKS` (default `OFF`)**,
  independent of `CRYPTOCOGS_BUILD_TESTS` (benchmarks need the `kraken` lib and
  some shared test fixtures/headers, not gtest). Fetched and wired in a new
  block in the top-level `CMakeLists.txt`, guarded by
  `CRYPTOCOGS_BUILD_BENCHMARKS AND CRYPTOCOGS_BUILD_KRAKEN`. Default build,
  install, and test behavior is completely unchanged when the flag is off — no
  version bump needed for this plan.
- **New directory `tests/benchmarks/`** (sibling of `tests/unit/` and
  `tests/examples/`), one binary `kraken_benchmarks` linking `kraken` +
  `benchmark::benchmark_main`. Reuses existing captured fixtures —
  `tests/unit/ws_client_example_json.hpp` (`kTickerSnapshotJson`,
  `kBookSnapshotJson`, `kAddOrderResponseJson`, ...) and
  `tests/unit/mock_ws_connection.hpp` (`MockWsConnection`) — instead of
  inventing synthetic payloads, so benchmark inputs match what tests already
  assert is realistic wire data.
- **Candidate hot paths to benchmark**, each picked from a concrete file/line
  read during this plan's research, not speculation:
  1. `KrakenCredentials::sign()` (`src/kraken/auth.cpp`) — builds five
     intermediate `std::string`s and a hand-rolled base64 encode/decode per
     call.
  2. `AddOrderRequest::build()` (`src/kraken/rest_api.cpp`) — request
     construction/serialization allocation on the order-placement path.
  3. `parse_rest_response<OpenOrdersResult>` — JSON parse + deserialize of a
     multi-order payload (the "large response" case), sized via Google
     Benchmark's range API (1 / 10 / 100 orders) to see how cost scales.
  4. `kraken_frame_descriptor()` + `TickerMessage::from_json()` /
     `BookMessage::from_json()` against the captured ticker and 10-level-book
     fixtures — the highest-*frequency* real-world path (market data ticks).
  5. `ExchangeWsClient::on_raw_message()` end-to-end via `MockWsConnection` —
     the full per-message dispatch cost (`json::parse` + mutex + map lookup +
     `std::function` copy + callback invoke), not just the parse in isolation.
  6. `TickPrice::from()` / `TickPrice::str()` round trip
     (`src/exchange/common/tick_price.cpp`) — small, but called once per price
     field on every hot message if a caller re-derives it.
- **Unreserved vector growth (added per Rob's review comment).** Grepped every
  `push_back`/`emplace_back` site in `src/kraken/rest_api.cpp` and
  `src/kraken/ws_api.cpp`: **zero** call `.reserve()` first, even though every
  site iterates a JSON array whose `.size()` is known before the loop starts
  (`for (const auto& item : j["..."])  { v.push_back(...); }`). Concrete sites:
  `BookMessage::from_json` (`ws_api.cpp:395,399` — bids/asks, potentially the
  deepest real vectors in the adapter), `TickerMessage`/`TradeMessage`/
  `OHLCMessage`/`InstrumentMessage`/`ExecutionsMessage`/`BalancesMessage`
  (`ws_api.cpp:383,411,436,465,511,514,569,590`), `BatchAddResponse`/
  `BatchCancelResponse`-style order arrays (`ws_api.cpp:148,208,233`), and REST
  `OHLCResult::candles`, `RecentTradesResult::trades`, `AddOrderBatchResult`,
  `DepositMethodsResult`, `DepositAddressesResult` (`rest_api.cpp:191,213,247,
  520,551,663,679`). This is a distinct candidate-fix bucket from JSON-parse
  cost: mechanical, low-risk (`.reserve(j.at("...").size())` before the loop),
  and directly benchmarkable as a before/after pair rather than a vague
  "might help."
- **One coarse sampling profile**, to corroborate the micro-benchmark ranking
  and catch anything a targeted micro-benchmark can't see (e.g. lock
  contention that only shows up under sustained load). Rather than write a
  bespoke stress-test binary, run macOS `sample` against the already-built
  `kraken_benchmarks` process while it loops a long
  (`--benchmark_min_time=10s`) run of the `OnRawMessage` benchmark, and save
  the extracted top-N-by-self-time text output. `sample`'s availability/
  permissions on this specific macOS build are unconfirmed — see Self-review
  for the fallback if it's unavailable.
- **No fixes in this plan.** Step 4 ends in a findings table, not code
  changes — see Scope.

## Steps

Each step ends with a checkpoint: full `cmake --build build` **and**
`ctest --output-on-failure` from `build/` both green (confirms the new,
default-off harness hasn't broken the existing library or its test suite),
then a commit.

### Step 1 — Benchmark harness scaffolding
- Top-level `CMakeLists.txt`: add `CRYPTOCOGS_BUILD_BENCHMARKS` option;
  FetchContent `google/benchmark` v1.9.5; `add_subdirectory(tests/benchmarks)`
  guarded as described above.
- `tests/benchmarks/CMakeLists.txt`: `add_executable(kraken_benchmarks
  bench_smoke.cpp)` linking `kraken` and `benchmark::benchmark_main`.
- `tests/benchmarks/bench_smoke.cpp`: one trivial `BM_Noop` benchmark, purely
  to prove the harness wires up end-to-end; kept permanently afterward as a
  cheap canary (not deleted in a later step).
- **Unit tests:** none new — this step is build-infra only, no library
  behavior changes. The checkpoint's full `ctest` run is the regression guard.
- **Done:** `cmake -B build -DCRYPTOCOGS_BUILD_BENCHMARKS=ON && cmake --build
  build` succeeds; `./build/bin/kraken_benchmarks` runs and reports `BM_Noop`;
  a default build (flag untouched, i.e. off) is unaffected and `ctest` is
  still fully green.

### Step 2 — Signing + REST request/response benchmarks
- `tests/benchmarks/bench_kraken_rest.cpp`: `BM_KrakenSign` (fixed
  key/nonce/body), `BM_AddOrderRequest_Build`, `BM_ParseOpenOrdersResponse`
  (ranged over order count via a small helper that synthesizes an N-order
  `OpenOrdersResult`-shaped JSON body, mirroring the inline single-order
  fixture literals already used in `tests/unit/test_rest_responses.cpp` — Kraken's
  REST fixtures are inline `R"(...)"` strings there, not a shared header like
  the WS side's `ws_client_example_json.hpp`; `RangeMultiplier`/
  `Complexity(benchmark::oN)` enabled to see how cost scales with N. Note:
  `OpenOrdersResult::open` is a `std::map<std::string, OrderInfo>`
  (`OpenOrdersResult::from_json`, `rest_api.cpp:305-311`), populated via
  `r.open[k] = ...` — a tree insert, not a `push_back`, so this benchmark is
  *not* a reserve candidate (`std::map` has no `reserve()`); it measures
  JSON-parse-plus-O(N log N)-map-insertion cost on its own terms, distinct
  from the vector-growth story below). Add to `kraken_benchmarks`' sources.
- **Unit tests:** none new — benchmarks call already-tested `build()`/
  `parse_rest_response<T>` paths; no new library code is introduced.
- **Done:** binary builds and runs; ns/op captured for all three benchmarks at
  default `--benchmark_min_time`; full checkpoint (build + ctest) green.

### Step 3 — WS dispatch + reserve-ceiling benchmarks
- `tests/benchmarks/bench_kraken_ws.cpp`: `BM_JsonParse_TickerRaw` (raw-bytes-
  to-DOM parse in isolation — added once `BM_OnRawMessage_TickerPush`'s total
  turned out far larger than `BM_TickerFromJson`'s pre-parsed-DOM cost alone
  could explain; quantifies the gap instead of leaving it as an inference),
  `BM_TickerFromJson`,
  `BM_BookFromJson` (ranged book depth — 10/50/100/500 levels, same
  `Complexity(benchmark::oN)` treatment as Step 2's order parse, since
  `BookMessage::from_json`'s bids/asks loops are the deepest unreserved
  vectors found in the design-decisions grep), `BM_FrameDescriptor_Ticker`,
  `BM_OnRawMessage_TickerPush` (end-to-end through a real
  `exchange::kraken::ws::KrakenWsClient` wired to `MockWsConnection`, reusing
  `tests/unit/mock_ws_connection.hpp` directly rather than duplicating it).
- Also add an isolated **reserve-ceiling** pair, independent of JSON
  entirely: `BM_VectorPushBack_NoReserve` vs `BM_VectorPushBack_WithReserve`,
  both filling a `std::vector<exchange::kraken::ws::BookEntry>` (the real
  `BookMessage` bid/ask element type — `{double price, double qty}`, same
  layout as `std::pair<double,double>`) at the same depths as
  `BM_BookFromJson` (10/50/100/500, via matching `->Arg(...)` calls rather
  than `RangeMultiplier` so the N values line up exactly) — one via bare
  `push_back` in a loop (mirrors every site found in the grep), the other
  pre-sized with `.reserve(n)` first. This isolates exactly what `.reserve()`
  can save at each realistic size, decoupled from parse cost, so Step 4 can
  judge whether it's a meaningful fraction of `BM_BookFromJson`'s total or
  noise.
- **Unit tests:** none new — same rationale as Step 2 (benchmarks exercise
  already-tested paths; the reserve-ceiling pair is a standalone
  `std::vector` measurement, not new library code).
- **Done:** binary builds and runs; ns/op captured for all six benchmarks;
  checkpoint green.

### Step 4 — TickPrice benchmarks, sampling profile, and findings write-up
- `tests/benchmarks/bench_tick_price.cpp`: `BM_TickPrice_From`,
  `BM_TickPrice_Str`.
- Run `./build/bin/kraken_benchmarks --benchmark_filter=OnRawMessage
  --benchmark_min_time=10s` in the background, `sample` its PID for ~8s, and
  extract the top self-time symbols. If `sample` (or an `xctrace record
  --template 'Time Profiler'` fallback) is unavailable or blocked in this
  environment, note that explicitly in the findings and rely on the
  micro-benchmark ranking alone — this doesn't block the step.
- Append a **Findings** section to this plan: a table of every benchmarked
  path with ns/op and a verdict (negligible / measurable-but-dominated-by-
  network-or-call-frequency / worth-optimizing), plus — only for anything
  landing in "worth-optimizing" — a short list of concrete candidate fixes,
  explicitly deferred to a follow-up plan for implementation (see Scope). Call
  out the reserve-ceiling result specifically: state what fraction of
  `BM_BookFromJson`'s (and `BM_ParseOpenOrdersResponse`'s) total cost the
  isolated reserve win represents, and — if non-trivial — list every grepped
  `push_back`/`emplace_back` site from Design decisions as a candidate
  `.reserve()` fix, since all of them share the same "array size known
  upfront" shape and a fix proven on one generalizes to the rest.
- **Unit tests:** none new.
- **Done:** all four benchmark source files build and run; findings section
  written with real numbers (and the sampling-profile outcome, success or
  skipped-with-reason); checkpoint green; plan status updated to **Done**.

## Findings

All numbers below are from `kraken_benchmarks` built `RelWithDebInfo`
(`build-bench/`, `-DCRYPTOCOGS_BUILD_BENCHMARKS=ON`) on the machine this plan
was executed on; re-run locally before trusting absolute numbers on different
hardware, though the *relative* story is expected to hold anywhere.

| Benchmark | Result | Verdict |
|---|---|---|
| `BM_TickPrice_From` | 2.1 ns | Negligible |
| `BM_TickPrice_Str` | 18.1 ns | Negligible |
| `BM_FrameDescriptor_Ticker` | 44 ns | Negligible |
| `BM_KrakenSign` | 1.23 µs | Negligible vs. network |
| `BM_AddOrderRequest_Build` | 2.82 µs | Negligible vs. network |
| `BM_TickerFromJson` (DOM→struct only) | ~0.29 µs | Small |
| `BM_ParseOpenOrdersResponse` (1/10/100 orders) | 4.0 / 35.2 / 369 µs | Negligible vs. network; scales ~N log N (map insert) as expected |
| `BM_JsonParse_TickerRaw` (raw bytes→DOM) | **2.34 µs** | **Worth-optimizing bucket, if message rate warrants it — see below** |
| `BM_BookFromJson` (10/50/100/500 levels) | 0.84 / 3.0 / 5.6 / 26.2 µs | Same parse-dominated story, scales ~linearly |
| `BM_OnRawMessage_TickerPush` (full dispatch) | **2.78 µs** | 84% of this is `BM_JsonParse_TickerRaw` alone (see below) |
| `BM_VectorPushBack_NoReserve` vs `_WithReserve` (100 / 500) | 199→64 ns / 495→300 ns | Real, modest — see reserve callout below |

**Headline finding: `json::parse()` (raw bytes → DOM), not stack allocation or
return-by-value, is the dominant cost on the WS hot path.** Of
`BM_OnRawMessage_TickerPush`'s 2.78 µs total, `BM_JsonParse_TickerRaw` alone
accounts for 2.34 µs (84%); `TickerMessage::from_json` (DOM→struct, already
counted separately as `BM_TickerFromJson`) is 0.29 µs (10%); routing
(`kraken_frame_descriptor`) is 44 ns (1.6%); the remaining ~100 ns (3.6%) is
mutex/map-lookup/`std::function`-copy/callback overhead. This was
corroborated independently by an 8253-sample macOS `sample` capture of a 25 s
run of `BM_OnRawMessage_TickerPush` (`RelWithDebInfo`, PID confirmed via exact
binary-name match, not the launching shell — the first attempt accidentally
sampled `zsh` and was discarded): roughly 55-65% of self-time samples land
directly inside nlohmann's lexer/parser/DOM-construction machinery
(`lexer::scan_string`/`scan`/`scan_number`, `sax_parse_internal`,
`json_value::destroy`, `std::__tree::__emplace_unique_key_args` +
`__tree_balance_after_insert` — nlohmann's default object type is
`std::map<std::string, json>`, so every `{...}` in the payload pays a
red-black-tree insert), another ~15-20% is the C allocator (`_xzm_free`,
`_xzm_xzone_malloc`, `malloc_type_malloc`) backing the many small per-node
allocations a DOM parse creates and then immediately tears down each call,
and `_platform_memcmp` alone was 11.6% of samples (key/literal comparisons in
the lexer and tree lookups). Only 25 of 10253 top-of-stack samples (0.24%)
landed inside `TickerMessage::from_json` itself, and no lock/mutex symbol
exceeded 22 samples (0.2%) — the Self-review risk of sustained-load lock
contention did not materialize.

**Is 2.3 µs/message actually a problem?** At the full 2.78 µs/message
dispatch cost, one core can process roughly 360,000 ticker messages/second
before this becomes the bottleneck. A live Kraken WS ticker/book/trade feed
for a realistic number of subscribed symbols is very unlikely to approach
that rate — so on the evidence gathered here, this is real but not
necessarily *worth* fixing without knowing Rob's actual production message
rate. Flagged as a judgment call for Rob, not a recommendation either way.

**Reserve-ceiling callout.** `.reserve()` on `BookData`'s bids/asks vectors is
a real, cheap, low-risk win: at depth 100, reserving cuts one vector's build
from 199 ns to 64 ns (saves ~135 ns/vector, ~270 ns for bids+asks combined) —
about **4.8%** of `BM_BookFromJson/100`'s 5.6 µs total. At depth 500 it saves
~390 ns combined against a 26.2 µs total — about **1.5%**, proportionally
*smaller* because parse cost grows faster than the reserve win as depth
increases. Verdict: worth doing (mechanical, near-zero risk, generalizes to
every grepped site in Design decisions) but it is a minor contributor next to
the parse cost above, not a fix for the headline finding.

**REST-side confirms the Motivation's network-dominance hypothesis directly:**
even parsing a 100-order `OpenOrdersResult` (369 µs) or signing+building an
order (1.23 + 2.82 µs) is one to three orders of magnitude below typical
network RTT (tens to hundreds of milliseconds) — none of it is worth touching
on its own.

**Candidate fixes, in priority order (implementation deferred — see Scope):**
1. **`.reserve()` at every grepped `push_back`/`emplace_back` site** (Design
   decisions list) — cheap, mechanical, no behavior change, ~1.5-5% win where
   it applies. Lowest-risk, do-it-regardless-of-message-rate candidate.
2. **Only if Rob confirms real subscribed message rates approach the
   hundreds-of-thousands/sec region**: investigate reducing `json::parse`
   cost on the highest-frequency channels specifically — e.g. a SAX-based
   targeted extractor for `ticker`/`trade` pushes that skips full DOM
   construction, or evaluating a faster parser for just that path. This is a
   materially bigger, riskier change than #1 (touches the parse strategy, not
   just a call site) and this plan does not recommend committing to it
   without a confirmed production rate that makes it matter.
3. **Not recommended:** anything targeting stack allocation or return-by-value
   in the adapter's own code (`RestResponse<T>`, `HttpRequest`, `TickPrice`,
   the signing intermediate strings) — every one of those benchmarked at
   nanosecond-to-low-microsecond cost, confirming the Motivation's C++17
   copy-elision assumption held. This was Rob's original worry; the evidence
   says it isn't where the time goes.

## Self-review — risks, assumptions, follow-on

- **This plan produces evidence, not code changes.** If Step 4 finds nothing
  worth optimizing, the answer to Rob's original worry is "measured, and it's
  fine" — that's a valid, complete outcome, not a reason to invent work.
- **Risk — macOS sampling profiler availability/symbols.** `sample`'s exact
  behavior on this Darwin version is unconfirmed, and a fully-optimized
  Release binary can have poor symbol resolution. Mitigation: build
  `kraken_benchmarks` as `RelWithDebInfo` (optimized, but with frame pointers
  and symbols) rather than plain `Release`, and treat the sampling pass as
  corroborating evidence, not the sole evidence — the micro-benchmarks stand
  on their own either way.
- **Assumption — network I/O dominates real REST latency.** This plan only
  benchmarks the mocked/local portion of REST calls (signing, building,
  parsing) — it deliberately does not attempt to benchmark real
  `curl_easy_perform()` network round-trips, since that's environment/network
  noise, not library cost. The findings write-up will call this out explicitly
  so the REST numbers aren't misread as "this is how fast placing a real order
  is."
- **Assumption — return-by-value cost doesn't need isolating from the rest of
  each function.** Every benchmark measures a whole realistic operation
  (sign/build/parse), not the return statement alone. If the whole operation
  is already fast, whatever the return mechanics cost is bounded by that same
  number — no separate proof needed that copy elision "worked."
- **Scope creep guard:** Binance/Coinbase/Crypto.com and network-layer tuning
  are explicitly out of scope (see Scope) — flagged as possible follow-ups
  only if Kraken's findings turn out to generalize (they share
  `exchange_common`), not assumed.
- **Follow-on:** any real fix Step 4 identifies gets its own plan number,
  written after this one lands, so it can cite actual before/after benchmark
  numbers rather than a prediction.
