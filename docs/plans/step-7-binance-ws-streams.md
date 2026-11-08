# Step 7: Binance WebSocket market streams

**Status**: Done — implemented in commits `7d461fc` (7.1), `5409a69` (7.2), `da8eda5` (7.3), `b833587` (7.4), `195d482` (7.5); suite 255 → 277 green; example live-verified against `stream.binance.com`
**Parent**: [Plan multi-exchange-abstraction, Step 7](multi-exchange-abstraction.md#step-7--binance-websocket-market-streams)
**Branch**: `feature/multi-exchange-abstraction`
**Message formats**: [appendix-binance-message-formats.md §3 + §5](appendix-binance-message-formats.md)
**Scaffold contract**: [Plan multi-exchange-abstraction §A2](multi-exchange-abstraction.md#a2-websocket-request-scaffold--typedwsrequestr-and-typedsubscriberequest)

Implements plan multi-exchange-abstraction Step 7: a Binance market-stream client built entirely on
the existing `exchange::ws::ExchangeWsClient` — Binance contributes only a
frame descriptor, push message types, a subscribe-request scaffold, and a
factory. No changes to the generic client, transport, or reconnect machinery.

This is the first real proof of the §A2 claim that `subscribe_async` is
exchange-agnostic: the same dispatch loop that drives Kraken channels must
drive Binance streams with zero client changes.

---

## Design decisions

1. **One header, `include/exchange/binance/ws_streams.hpp`, namespace
   `exchange::binance::ws`, header-only.** Mirrors Kraken's split: re-exports
   of the common client types, URL constant, event types, ack,
   `binance_stream_frame_descriptor`, `TypedStreamSubscribeRequest<PushMsg>`,
   aliases, and the conn-based factory all live here. No new `.cpp` enters
   `binance` (everything is inline, like Kraken's `ws_api.hpp`), and the
   header does **not** include `ix_ws_connection.hpp` — so includers don't
   inherit an ixwebsocket dependency.

2. **Factory shape mirrors `make_kraken_ws_client` exactly** — conn-based:

   ```cpp
   inline std::shared_ptr<BinanceStreamClient>
   make_binance_stream_client(std::shared_ptr<IWsConnection> conn,
                              std::shared_ptr<IWsErrorHandler> error_handler = nullptr);
   ```

   with `using BinanceStreamClient = exchange::ws::ExchangeWsClient;` and
   `STREAM_URL = "wss://stream.binance.com/stream"` (combined endpoint).
   The URL-based flow is spelled
   `exchange::ws::make_exchange_ws_client(url, binance_stream_frame_descriptor, eh)`
   via the existing common overload in `ix_ws_connection.hpp` — same as
   Kraken, where the URL overload deliberately lives in the common layer so
   only callers that want the real transport pull in ixwebsocket. (Plan multi-exchange-abstraction's
   "`make_binance_stream_client(url)`" line is satisfied by this pair; adding
   a URL overload *inside* `ws_streams.hpp` would leak the ixwebsocket
   include into every user, which Kraken's layout deliberately avoids.)

3. **`BinanceStreamAck` derives `exchange::ws::BaseWsResponse`.** The generic
   `detail::make_ws_response` derives `ok` from `success`/`error` for
   `BaseWsResponse` subtypes (verified in `ws_client.inl`), so the ack maps:
   success ack `{"result":null,"id":N}` → `success=true`; error frame
   `{"error":{"code":C,"msg":M},"id":N}` → `success=false`,
   `error="M"`. This is plan multi-exchange-abstraction §A2's "ok from result-present / no-error",
   implemented through the existing type-dispatch — no client change.

4. **Push `from_json` unwraps the combined-stream wrapper.**
   `ExchangeWsClient::on_raw_message` hands the **whole frame** to the push
   callback (`cb(j)` — verified in `ws_client.cpp`), so on the combined
   endpoint every event type receives `{"stream":"…","data":{…}}`. A single
   helper `detail::stream_payload(const json& j)` returns
   `j.contains("data") ? j.at("data") : j`, and every event `from_json`
   parses through it — accepting both wrapped frames (what dispatch
   delivers) and bare payloads (what the documented fixtures are).

5. **Dispatch is descriptor-only — no Binance `identify_message`/
   `MessageKind` classifier.** Plan multi-exchange-abstraction is explicit that the frame descriptor
   is the non-negotiable piece and the richer caller-facing classifier is
   optional. Nothing needs one yet (`bookTicker`'s missing `"e"` field makes
   bare-payload classification ambiguous anyway — on the combined endpoint
   the wrapper's `"stream"` value is the identity). YAGNI; revisit if a
   raw-frame consumer appears.

   `binance_stream_frame_descriptor` (appendix §5 table):
   - `"stream"` key present → `PushMessage`, `route_key = stream value`.
   - else `"id"` present **and non-null** with `"result"` or `"error"` →
     `MethodResponse`, `correlation_id = to_string(id)`.
   - else → `Unknown` (includes error frames with `id:null`, which cannot be
     correlated — the pending handler times out; noted in self-review).

6. **`TypedStreamSubscribeRequest<PushMsg>` is the §A2 sketch verbatim** —
   stores an open-form `stream` string, `route_key()` returns it,
   `to_json()`/`unsubscribe_json()` emit
   `{"method":"SUBSCRIBE|UNSUBSCRIBE","params":[stream],"id":req_id}`.
   **One stream per request** — the params array always has exactly one
   element, matching the one-callback-per-`route_key` dispatch model.
   (Binance allows batching several streams in one SUBSCRIBE; with a single
   shared ack there'd be no per-stream success signal — out of scope.)

7. **Stream-name helper functions own the case convention.** Binance REST
   symbols are uppercase but stream names require lowercase symbols with
   mixed-case suffixes (`btcusdt@aggTrade`) — a foot-gun worth one helper per
   stream: `agg_trade_stream("BNBBTC")` → `"bnbbtc@aggTrade"`, plus
   `trade_stream`, `kline_stream(symbol, interval)`, `ticker_stream`,
   `mini_ticker_stream`, `book_ticker_stream`, `depth_stream` (diff),
   `partial_depth_stream(symbol, levels)`. ASCII-lowercase only (symbols are
   `[A-Z0-9]`). The request structs stay dumb (store `stream` verbatim), so
   exotic streams (`@depth@100ms`, `!ticker@arr`) remain reachable by
   passing a raw string; helpers cover the documented defaults. `interval`
   stays a raw string, consistent with REST `BinanceKlinesRequest`.

8. **`BinanceBookLevel` hoists from `rest_api.hpp` to
   `exchange/binance/types.hpp`** — the one non-additive touch. The WS depth
   payloads (`@depth` diff and `@depth<levels>` partial) use exactly the
   positional `["price","qty"]` row the REST order book already parses;
   duplicating the struct or making `ws_streams.hpp` include all of
   `rest_api.hpp` are both worse. Move the struct unchanged into
   `exchange::binance::` (types.hpp, which both layers already include) and
   leave `using exchange::binance::BinanceBookLevel;` inside
   `exchange::binance::rest` so every existing spelling keeps resolving.
   The 255-test suite (which field-asserts REST depth parsing) is the
   regression net.

9. **Field mapping is faithful-but-not-slavish**: terse keys map to named
   members (`"p"` → `price`), string numbers → `double` via `std::stod`,
   ids/times → `int64_t`, bools native. Documented `"ignore"` fields
   (aggTrade/trade `"M"`, kline `"B"`) are dropped, matching the REST kline
   precedent (field 11 dropped). `BinancePartialDepth` happens to share the
   REST `BinanceOrderBook` shape but gets its own struct in `ws` — aliasing
   the REST type would force the cross-layer include decision 8 exists to
   avoid.

10. **Out of scope**: user-data streams / listenKey (needs REST keepalive
    plumbing — its own step), the WS API trading endpoint (plan multi-exchange-abstraction Step 8),
    SUBSCRIBE batching (decision 6), `!ticker@arr` / `@depth@100ms`-style
    helpers (raw strings reach them), `LIST_SUBSCRIPTIONS`/`SET_PROPERTY`
    methods, a wired-up `WsReconnectSession` resubscribe demo (machinery
    exists; composing it is caller policy — the example stays minimal like
    Kraken's), and ping/pong handling (ixwebsocket auto-pongs).

---

## Sub-step 7.1 — Frame descriptor + ack (`ws_streams.hpp` scaffold)

**Done when**: `binance_stream_frame_descriptor` classifies all three frame
shapes per the appendix §5 table and `BinanceStreamAck` parses success and
error acks with `ok` derived correctly through `detail::make_ws_response`.

### New header: `include/exchange/binance/ws_streams.hpp`

Banner + include `exchange/common/ws_client.hpp` + `exchange/binance/types.hpp`.
Namespace `exchange::binance::ws` with the Kraken-style re-export block
(`IWsConnection`, `IWsErrorHandler`, `RateLimitedWsErrorHandler`,
`WsResponse`, `SubscriptionHandle`, `ExchangeWsClient`, `MessageIdentifier`),
then:

```cpp
inline constexpr std::string_view STREAM_URL = "wss://stream.binance.com/stream";

namespace detail {
// Combined-stream frames arrive as {"stream":…,"data":{…}}; dispatch hands
// the whole frame to push callbacks. Bare payloads (fixtures, raw /ws use)
// have no wrapper. Parse through this so from_json accepts both.
inline const json& stream_payload(const json& j) {
    return j.contains("data") ? j.at("data") : j;
}
} // namespace detail

// Ack for SUBSCRIBE/UNSUBSCRIBE: {"result":null,"id":N} on success,
// {"error":{"code":C,"msg":M},"id":N} on failure. Deriving BaseWsResponse
// lets the generic make_ws_response derive WsResponse::ok.
struct BinanceStreamAck : exchange::ws::BaseWsResponse {
    int64_t id{0};
    static BinanceStreamAck from_json(const json& j);
};

// MessageIdentifier for the combined-stream endpoint (appendix §5):
//   {"stream":…,"data":…}              → PushMessage, route_key = stream
//   {"result"/"error":…, "id":N}       → MethodResponse, correlation_id = str(N)
//   anything else (incl. id:null)      → Unknown
inline exchange::ws::FrameDescriptor
binance_stream_frame_descriptor(const json& j);
```

### Fixtures — new `tests/unit/binance_ws_stream_example_json.hpp`

Banner; namespace `exchange::binance::ws::test`; shapes verbatim from
appendix §3 (documented frames — streams are public, but fixtures are pinned
to the appendix like the REST ones): `kSubscribeAckJson`
(`{"result":null,"id":1}`), `kErrorAckJson`
(`{"error":{"code":2,"msg":"Invalid request: subscription id not provided"},"id":7}`),
`kWrappedAggTradeJson` (`{"stream":"bnbbtc@aggTrade","data":{…aggTrade…}}`).

### Tests — new `tests/unit/test_binance_ws_client.cpp` (+ CMake executable)

`add_executable(test_binance_ws_client test_binance_ws_client.cpp)` linked
`binance GTest::gtest_main` (pattern of the other Binance binaries).

- Descriptor: success ack → `MethodResponse`, `correlation_id=="1"`; error
  ack → `MethodResponse`, `correlation_id=="7"`; wrapped push →
  `PushMessage`, `route_key=="bnbbtc@aggTrade"`; `{}` and
  `{"error":{…},"id":null}` → `Unknown`.
- `BinanceStreamAck::from_json`: success ack → `success==true`, no error,
  `id==1`; error ack → `success==false`, `error=="Invalid request: …"`.

### Checkpoint commit
`feat: step 7.1 — Binance stream frame descriptor + ack`
Full build + `ctest --output-on-failure` green.

---

## Sub-step 7.2 — Flat push event types

**Done when**: the five flat (non-nested) event payloads parse field-by-field
from appendix fixtures, both bare and wrapped.

### `ws_streams.hpp` additions — all with `static X from_json(const json&)` via `detail::stream_payload`

| Type | Stream | Fields (wire → member) |
|---|---|---|
| `BinanceAggTradeEvent` | `<s>@aggTrade` | `E`→`event_time`, `s`→`symbol`, `a`→`agg_trade_id`, `p`→`price`, `q`→`qty`, `f`→`first_trade_id`, `l`→`last_trade_id`, `T`→`trade_time`, `m`→`is_buyer_maker` |
| `BinanceTradeEvent` | `<s>@trade` | `E`, `s`, `t`→`trade_id`, `p`, `q`, `T`, `m` |
| `BinanceTickerEvent` | `<s>@ticker` | `E`, `s`, `p`→`price_change`, `P`→`price_change_pct`, `w`→`weighted_avg_price`, `x`→`prev_close`, `c`→`last_price`, `Q`→`last_qty`, `b`/`B`→`bid_price`/`bid_qty`, `a`/`A`→`ask_price`/`ask_qty`, `o`/`h`/`l`→`open`/`high`/`low`, `v`/`q`→`volume`/`quote_volume`, `O`/`C`→`stats_open_time`/`stats_close_time`, `F`/`L`→`first_trade_id`/`last_trade_id`, `n`→`num_trades` |
| `BinanceMiniTickerEvent` | `<s>@miniTicker` | `E`, `s`, `c`→`close`, `o`/`h`/`l`, `v`/`q` |
| `BinanceBookTickerEvent` | `<s>@bookTicker` | `u`→`update_id`, `s`, `b`/`B`, `a`/`A` — **no `e`/`E` fields** (appendix note) |

### Fixtures
`kAggTradeJson`, `kTradeJson`, `kTickerJson`, `kMiniTickerJson`,
`kBookTickerJson` — §3 verbatim (bare payloads).

### Tests (`test_binance_ws_client.cpp`)
- One field-by-field `from_json` test per type against its bare fixture
  (`price==0.001` via `EXPECT_DOUBLE_EQ`, ids/times as int64, bools).
- `AggTrade_ParsesWrappedFrame`: `kWrappedAggTradeJson` parses identically to
  bare — pins the decision-4 unwrap rule.

### Checkpoint commit
`feat: step 7.2 — Binance flat stream event types`
Full build + `ctest --output-on-failure` green.

---

## Sub-step 7.3 — Structured push event types (kline + depth)

**Done when**: kline's nested `k` object and both depth shapes parse; the
`BinanceBookLevel` hoist lands with the full suite still green.

### `types.hpp` / `rest_api.hpp` — the decision-8 hoist
Move `BinanceBookLevel` (unchanged) from `rest_api.hpp` into
`exchange/binance/types.hpp`; add `using exchange::binance::BinanceBookLevel;`
in `exchange::binance::rest` where the struct used to be.

### `ws_streams.hpp` additions

```cpp
// Candle payload nested under "k" — keyed single-letter fields (the REST
// kline is a positional 12-array; same data, different wire shape).
struct BinanceStreamKline {
    int64_t start_time{0}, close_time{0};      // t, T
    std::string symbol, interval;              // s, i
    int64_t first_trade_id{0}, last_trade_id{0}; // f, L
    double  open{0}, close{0}, high{0}, low{0}; // o, c, h, l
    double  volume{0}, quote_volume{0};        // v, q
    double  taker_buy_base_volume{0}, taker_buy_quote_volume{0}; // V, Q
    int64_t num_trades{0};                     // n
    bool    is_closed{false};                  // x
    static BinanceStreamKline from_json(const json& k);   // bare "k" object
};

struct BinanceKlineEvent {       // E, s, k
    int64_t event_time{0};
    std::string symbol;
    BinanceStreamKline kline;
    static BinanceKlineEvent from_json(const json& j);
};

struct BinanceDepthUpdateEvent { // E, s, U, u, b, a
    int64_t event_time{0};
    std::string symbol;
    int64_t first_update_id{0}, final_update_id{0};
    std::vector<BinanceBookLevel> bids, asks;
    static BinanceDepthUpdateEvent from_json(const json& j);
};

struct BinancePartialDepth {     // lastUpdateId, bids, asks (full-name keys)
    int64_t last_update_id{0};
    std::vector<BinanceBookLevel> bids, asks;
    static BinancePartialDepth from_json(const json& j);
};
```

### Fixtures
`kKlineJson`, `kDepthUpdateJson`, `kPartialDepthJson` — §3 verbatim.

### Tests
- `Kline_FromJson`: every `k` field asserted (incl. `is_closed==false`,
  `interval=="1m"`).
- `DepthUpdate_FromJson`: `U`/`u` ids, one bid `[0.0024, 10]` + one ask
  field-asserted.
- `PartialDepth_FromJson`: `last_update_id==160`, levels asserted.
- The existing REST depth tests re-passing is the hoist's regression check —
  no test text changes.

### Checkpoint commit
`feat: step 7.3 — Binance kline/depth stream events; hoist BinanceBookLevel`
Full build + `ctest --output-on-failure` green.

---

## Sub-step 7.4 — Subscribe scaffold, factory, lifecycle tests

**Done when**: a `MockWsConnection`-driven `ExchangeWsClient` completes the
full Binance subscribe lifecycle — SUBSCRIBE frame out, ack in, wrapped push
dispatched to a typed callback, UNSUBSCRIBE on cancel — with **zero changes
to the generic client**.

### `ws_streams.hpp` additions

`TypedStreamSubscribeRequest<PushMsg>` exactly per §A2 (stores `stream`;
`route_key()`; `to_json()`/`unsubscribe_json()` emitting
`{"method","params",[stream],"id"}`), the decision-7 stream-name helpers, the
eight aliases:

```cpp
using BinanceAggTradeSubscribe     = TypedStreamSubscribeRequest<BinanceAggTradeEvent>;
using BinanceTradeSubscribe        = TypedStreamSubscribeRequest<BinanceTradeEvent>;
using BinanceKlineSubscribe        = TypedStreamSubscribeRequest<BinanceKlineEvent>;
using BinanceTickerSubscribe       = TypedStreamSubscribeRequest<BinanceTickerEvent>;
using BinanceMiniTickerSubscribe   = TypedStreamSubscribeRequest<BinanceMiniTickerEvent>;
using BinanceBookTickerSubscribe   = TypedStreamSubscribeRequest<BinanceBookTickerEvent>;
using BinanceDepthSubscribe        = TypedStreamSubscribeRequest<BinanceDepthUpdateEvent>;
using BinancePartialDepthSubscribe = TypedStreamSubscribeRequest<BinancePartialDepth>;
```

plus `BinanceStreamClient` + `make_binance_stream_client(conn, eh)`
(decision 2).

### Test infrastructure — extract `MockWsConnection`
Move the `MockWsConnection` class from `tests/unit/test_ws_client.cpp` into a
new shared `tests/unit/mock_ws_connection.hpp` (banner; class body unchanged;
`test_ws_client.cpp` includes it). Behaviour-neutral — the Kraken WS suite
re-passing verifies the extraction.

### Tests (`test_binance_ws_client.cpp`)
- `SubscribeRequest_ToJson`: `BinanceAggTradeSubscribe` with
  `stream=agg_trade_stream("BNBBTC")` → exact
  `{"method":"SUBSCRIBE","params":["bnbbtc@aggTrade"],"id":N}` and matching
  `unsubscribe_json()`; `route_key()=="bnbbtc@aggTrade"`.
- `StreamHelpers_LowercaseSymbol`: each helper's output asserted
  (`kline_stream("BTCUSDT","1m")=="btcusdt@kline_1m"`,
  `partial_depth_stream("BNBBTC",5)=="bnbbtc@depth5"`, …).
- `Subscribe_Lifecycle`: `fire_open` → `subscribe_async` → assert SUBSCRIBE
  frame in `sent_messages` → inject `{"result":null,"id":<id>}` → ack
  `ok==true`, handle active → inject wrapped aggTrade frame → typed callback
  fired with field-asserted event → `handle.cancel()` → UNSUBSCRIBE frame
  sent; second `cancel()` sends nothing (idempotent).
- `Subscribe_ErrorAck_NoCallbackInstalled`: inject error ack → `ok==false`,
  handle inactive; a subsequent push frame does **not** fire the callback.
- `Subscribe_BeforeOpen_QueuedAndFlushed`: subscribe before `fire_open` →
  nothing sent; `fire_open()` → SUBSCRIBE flushed.
- `TwoStreamsOneConnection`: two subscriptions (aggTrade + bookTicker) on one
  client; pushes route to the right callbacks by `route_key` — the
  connection-reuse model the example demos live.

### Checkpoint commit
`feat: step 7.4 — Binance stream subscriptions via ExchangeWsClient`
Full build + `ctest --output-on-failure` green.

---

## Sub-step 7.5 — Example program + live verification

**Done when**: `binance_ws_client_example` builds, and each subcommand
streams live frames from `wss://stream.binance.com/stream` (public — no
credentials).

### New: `tests/examples/binance/binance_ws_client_example.cpp`
Direct analog of the Kraken `ws_client_example`: CLI11 app, one subcommand
per stream — `aggtrade <symbol>`, `trade <symbol>`,
`kline <symbol> [--interval 1m]`, `ticker <symbol>`, `miniticker <symbol>`,
`bookticker <symbol>`, `depth <symbol> [--levels N]` (N>0 → partial via
`partial_depth_stream`, else diff) — plus `multi <symbol>`: aggTrade +
bookTicker subscribed on **one** client/socket (two live
`SubscriptionHandle`s), the Binance-natural connection-reuse demo. Each
`run_*` creates the client via
`exchange::ws::make_exchange_ws_client(std::string(STREAM_URL), binance_stream_frame_descriptor)`,
subscribes with the typed request + spdlog-logging callback, streams for a
bounded number of frames/seconds, then cancels the handle. (Wall-clock bounds
are fine here — this is a live example, not a unit test.)

### CMake (`tests/CMakeLists.txt`)
```cmake
add_executable(binance_ws_client_example examples/binance/binance_ws_client_example.cpp)
target_link_libraries(binance_ws_client_example
    binance ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward
)
```
(matches `binance_rest_client_example` + the ixwebsocket link the Kraken WS
example uses).

### Verification
Full build + full `ctest` green, then live runs of every subcommand
(including `multi`) against `stream.binance.com`, confirming parsed frames
log with sensible fields.

### Checkpoint commit
`feat: step 7.5 — Binance WS stream example (live-verified)`

### Wrap-up (after 7.5)
"**Done**:" paragraph at the end of plan multi-exchange-abstraction Step 7 (commit trail, decisions,
test count); flip this plan to Done in `docs/plans.md` — separate
`docs: mark plan 005 done` commit, per the precedent of plans step-5-binance-rest-public and step-6-binance-rest-private.

---

## Self-review

### Risks / assumptions

- **The `BinanceBookLevel` hoist (decision 8) is the only non-additive
  touch.** Mechanical move + a `using` re-export keeps every existing
  spelling valid; the REST depth tests are the regression net. If anything
  unexpected fails at the 7.3 checkpoint, stop and re-plan per the global
  error rule.
- **The whole-frame-to-callback contract is load-bearing for decision 4.**
  Verified against `on_raw_message` (`cb(j)`) before writing this plan; the
  `AggTrade_ParsesWrappedFrame` + lifecycle tests pin it. If the generic
  client ever changes to pre-extract payloads, the unwrap helper makes the
  event types tolerant of both.
- **Error acks with `id:null` are unroutable** (descriptor → `Unknown`): the
  pending subscribe would time out (5 s default) instead of failing fast.
  Binance only omits the id when the request was malformed JSON — which our
  `to_json()` can't produce. Accepted; noted rather than special-cased.
- **`MockWsConnection` extraction touches an existing test file.** Class body
  moves verbatim; the Kraken WS suite re-passing at the 7.4 checkpoint
  verifies it. No assertions change.
- **One-stream-per-SUBSCRIBE (decision 6)** costs N round-trips for N
  streams. Binance's limit is 5 messages/sec inbound — irrelevant at example
  scale; batching is a future optimisation with a real design question
  (single ack vs per-stream failure), deliberately deferred.
- **Stream fixtures are pinned to the appendix, not live captures.** Streams
  are public, so a capture pass is possible — but the appendix shapes are
  Binance's own documented examples, and the live example (7.5) is the
  drift detector. Same stance as plan step-5-binance-rest-public's REST fixtures.
- **Live verification depends on network reachability** of
  `stream.binance.com` from the dev machine (plan step-5-binance-rest-public's live REST runs
  succeeded, so assumed fine). If a subcommand fails live, that's a real
  error — stop and fix, not shrug.
- **No reconnect/resubscribe wiring in this step.** `WsReconnectSession` is
  ready and Binance's ~24 h forced disconnect is real, but composing
  session + resubscribe is caller policy (plan multi-exchange-abstraction assigns it to the
  caller's `ConnectFn`). The example stays minimal, mirroring Kraken's.

### What "done" looks like

- New `include/exchange/binance/ws_streams.hpp` (header-only): descriptor,
  ack, 8 push event types + `BinanceStreamKline`, subscribe scaffold +
  helpers + 8 aliases, `BinanceStreamClient` + factory + `STREAM_URL`.
- `BinanceBookLevel` lives in `types.hpp` (re-exported into `rest`).
- New `tests/unit/binance_ws_stream_example_json.hpp` (11 fixtures),
  `tests/unit/test_binance_ws_client.cpp` (+ CMake executable),
  `tests/unit/mock_ws_connection.hpp` (extracted, shared with the Kraken WS
  test).
- New `binance_ws_client_example` binary, live-verified per subcommand.
- Roughly 25–30 new tests; suite ~255 → ~280+, green at every checkpoint.
- 5 checkpoint commits + 1 docs wrap-up commit on
  `feature/multi-exchange-abstraction`.
