# Step 8: Binance WebSocket API (bidirectional trading)

**Status**: Done — implemented in commits `398d2a8`, `0201b3f`, `d9592c7`, `b3504b0`, `480eba6`
**Parent**: [Plan multi-exchange-abstraction, Step 8](multi-exchange-abstraction.md#step-8--binance-websocket-api-bidirectional-trading)
**Branch**: `feature/multi-exchange-abstraction`
**Message formats**: [appendix-binance-message-formats.md §4 + §5](appendix-binance-message-formats.md)
**Predecessor**: [Plan step-7-binance-ws-streams](step-7-binance-ws-streams.md) (streams — same client, same test binary)

Implements plan multi-exchange-abstraction Step 8: request/response trading over Binance's WebSocket
API endpoint (`wss://ws-api.binance.com/ws-api/v3`), built entirely on the
existing `exchange::ws::ExchangeWsClient`. There is **no push/channel concept**
on this endpoint — every frame is a method call correlated by `id` — so this
step exercises only the `execute`/`execute_async` half of the generic client
(plan step-7-binance-ws-streams proved the `subscribe` half). Zero changes to the generic client,
transport, or reconnect machinery.

In scope (per plan multi-exchange-abstraction): `ping`, `order.place`, `order.cancel`, per-request
HMAC-SHA256 signing. The logon-session flow (`session.logon`, Ed25519) is
explicitly deferred by plan multi-exchange-abstraction.

---

## Design decisions

1. **One header, `include/exchange/binance/ws_api.hpp`, namespace
   `exchange::binance::ws`, header-only.** Sibling of `ws_streams.hpp` in the
   same namespace — the two surfaces are independently includable and
   co-includable (duplicate `using` re-exports of the same common names are
   legal identical redeclarations; the unit binary includes both and proves
   it). No new `.cpp` enters `binance`; no ixwebsocket include.

2. **Factory mirrors plan step-7-binance-ws-streams decision 2 exactly** — conn-based in the header:

   ```cpp
   using BinanceWsApiClient = exchange::ws::ExchangeWsClient;
   inline constexpr std::string_view WS_API_URL = "wss://ws-api.binance.com/ws-api/v3";

   inline std::shared_ptr<BinanceWsApiClient>
   make_binance_ws_api_client(std::shared_ptr<IWsConnection> conn,
                              std::shared_ptr<IWsErrorHandler> error_handler = nullptr);
   ```

   URL-based construction is spelled via the existing common overload
   (`exchange::ws::make_exchange_ws_client(url, binance_ws_api_frame_descriptor, eh)`
   from `ix_ws_connection.hpp`) so the header never leaks ixwebsocket —
   plan multi-exchange-abstraction's "`make_binance_ws_api_client(url)`" line is satisfied by this
   pair, same as 005.

3. **One envelope base, `BinanceWsApiResponse : exchange::ws::BaseWsResponse`.**
   Every WS API reply shares the `{"id","status","result"|"error","rateLimits"}`
   shell (appendix §4). The base carries `int status{0}`,
   `std::optional<int64_t> id`, `std::optional<int> error_code`,
   `std::vector<BinanceWsRateLimit> rate_limits`; a shared
   `detail::parse_ws_api_envelope(BinanceWsApiResponse&, const json&)` sets
   `success = (status < 400)` and `error = error.msg` — so the generic
   `detail::make_ws_response` derives `ok` with no client change (the same
   `BaseWsResponse` type-dispatch plan step-7-binance-ws-streams used). This is Kraken's
   `BaseResponse` pattern with Binance's shell.

4. **`rateLimits` is parsed, minimally.** Unlike plan step-7-binance-ws-streams's dropped ignore
   fields, `rateLimits` is the API's flow-control feedback for *trading*
   (ORDERS budget consumed per reply) — operationally useful and cheap: one
   5-field `BinanceWsRateLimit` struct + a loop in the envelope parser.
   *Strikeable if you'd rather defer it; nothing else depends on it.*

5. **WS API result payloads reuse the REST response structs.** The appendix
   states the `order.place` result "parses like the REST RESULT order shape",
   and `order.cancel`'s matches `DELETE /api/v3/order`. So:

   ```cpp
   struct BinanceWsNewOrderResponse : BinanceWsApiResponse {
       std::optional<exchange::binance::rest::BinanceNewOrderResponse> order;
       static BinanceWsNewOrderResponse from_json(const json& j);  // envelope + result delegate
   };
   ```

   (cancel analog reuses `rest::BinanceCancelOrderResponse`). No duplicated
   50-line parsers; the existing REST response tests double as the parser
   regression net. Intra-adapter coupling (`ws_api.hpp` includes
   `rest_api.hpp`) is accepted — both halves of one exchange.

6. **`BinanceWsCredentials` is an alias for `rest::BinanceCredentials`.**
   The WS API uses the *same* key material, algorithm (HMAC-SHA256 → lowercase
   hex), and `recvWindow` semantics as REST — only the payload framing differs.
   A distinct struct would duplicate fields for no semantic gain (Kraken needed
   a distinct `WsCredentials` because its WS auth model genuinely differs —
   token vs key/secret; Binance's does not). Plan multi-exchange-abstraction's name survives as the
   alias, re-exported in `exchange::binance::ws`.

7. **Signing is one free helper, `detail::ws_sign_params`, reusing the
   existing crypto primitives.**

   ```cpp
   // Injects apiKey, timestamp, recvWindow (if creds.recv_window_ms > 0) into
   // params, builds the alphabetically-sorted "k=v&k=v" payload, and appends
   // "signature" (HMAC-SHA256 hex, rest::detail::{hmac_sha256,to_hex}).
   void ws_sign_params(json& params, const BinanceWsCredentials& creds,
                       int64_t timestamp_ms);
   ```

   - **Sorting is free**: nlohmann's `json` object is backed by `std::map`, so
     key iteration is already alphabetical — no sort step, no ordering bug
     class.
   - **Value rendering rule**: string values render raw (no quotes), all
     others via `dump()` (`timestamp` → digits). Payload and wire frame are
     produced from the *same* `params` object in the same call, so they cannot
     disagree.
   - Reaching into `rest::detail::` for the HMAC/hex primitives is
     intra-adapter reuse of private helpers, not a public-surface leak — the
     alternative (hoisting them) is churn with no behaviour change.
   - The `id` is top-level, **outside** `params`, so the signature is
     independent of `req_id` assignment order — the client may assign the id
     before or after with no hazard.

8. **Requests carry `creds` + an explicit `timestamp{0}` field; `to_json()`
   signs.** `ExchangeWsClient` assigns `req_id` then calls `to_json()` once —
   that is the only hook, so signing happens there. `timestamp == 0` means
   "use the system clock now" (live callers do nothing); tests set it to a
   fixed value, making the entire frame — signature included — exactly
   assertable. This is the WS analog of `BinanceAuth`'s injectable `ClockFn`.
   `BinanceWsPingRequest` is unsigned and carries neither field.

9. **Request field sets mirror the REST request structs** (`symbol`, `side`,
   `type`, optional `timeInForce`/`quantity`/`price`/`newClientOrderId`/
   `stopPrice`/`icebergQty`/`quoteOrderQty`/`newOrderRespType`; cancel:
   `symbol` + `orderId` | `origClientOrderId` + optional `newClientOrderId`),
   with the same caller-formatted-decimal-string convention (plan step-6-binance-rest-private
   decision 6) and the same `exchange::binance::` enum converters. The structs
   are *not* shared with REST — build mechanics differ (query/body string +
   `IRestAuth` vs sorted-JSON params signed inline) — only the field sets and
   conventions are.

10. **`binance_ws_api_frame_descriptor` is trivial**: any frame with a
    non-null `"id"` is a `MethodResponse` with
    `correlation_id = stringify(id)` (tolerating string ids; we emit int64,
    matching the client's `std::to_string(req_id)` pending key — same
    verified contract as plan step-7-binance-ws-streams); everything else is `Unknown`. No `route_key`,
    no push branch, no Binance `identify_message`/`MessageKind` (plan step-7-binance-ws-streams
    decision 5 carries over).

11. **8.5 adds a `ping`-only live example — a deliberate, flagged deviation
    from the testing-strategy appendix table** (which lists "—" for this
    step). The appendix's *policy* is "examples are public-only"; `ping` on
    the WS API endpoint **is** public, and a ~60-line example live-verifies
    the real handshake, frame correlation, and envelope parsing against
    `ws-api.binance.com` — the part mocks deliberately avoid, and this step's
    only credential-free probe. Signed methods stay example-less, exactly per
    the policy. *Strikeable: drop 8.5 and the plan still satisfies plan multi-exchange-abstraction
    Step 8, whose tests are fully mocked.*

12. **Out of scope**: `session.logon`/Ed25519 session auth (deferred by
    plan multi-exchange-abstraction), `order.test`, `sor.order.place`, OCO/`orderList.*` methods,
    `account.status`/`myTrades` over WS API, `userDataStream.*`, the
    `returnRateLimits` request option, testnet wiring, and a
    reconnect/resubscribe demo (machinery exists; composing it is caller
    policy).

---

## Sub-step 8.1 — Envelope, frame descriptor, ping

**Done when**: `binance_ws_api_frame_descriptor` classifies the appendix §4
frames; `BinanceWsApiResponse` envelope parsing derives `success`/`error`/
`rate_limits` correctly; `BinanceWsPingRequest::to_json()` emits the exact
wire frame.

### New: `include/exchange/binance/ws_api.hpp`
Banner; includes `exchange/binance/rest_api.hpp` (decision 5 — also brings
`types.hpp` converters transitively), `exchange/binance/auth.hpp`
(decisions 6/7), `exchange/common/ws_client.hpp`; common re-exports
(005-style); `WS_API_URL`; `BinanceWsRateLimit`; `BinanceWsApiResponse` +
`detail::parse_ws_api_envelope` (decisions 3–4);
`binance_ws_api_frame_descriptor` (decision 10);
`BinanceWsPingRequest : TypedWsRequest<BinanceWsPongMessage>`
(`to_json() == {"id":req_id,"method":"ping"}`) and
`BinanceWsPongMessage : BinanceWsApiResponse` (envelope only).

### New: `tests/unit/binance_ws_api_example_json.hpp`
Banner; namespace `exchange::binance::ws::test`; appendix §4 frames verbatim
(`kWsApiOrderPlaceSuccessJson`, `kWsApiOrderPlaceErrorJson`) plus
`kWsApiPongJson` (`{"id":1,"status":200,"result":{}}`) and a string-id
variant — real-vs-synthetic commented per the testing-strategy convention.

### Tests (extend `tests/unit/test_binance_ws_client.cpp`)
- `WsApiDescriptor_SuccessReply` / `_ErrorReply`: §4 fixtures →
  `MethodResponse`, `correlation_id == "e2a85d9f-…"` (string id passthrough).
- `WsApiDescriptor_IntId`: `{"id":7,"status":200,"result":{}}` →
  `correlation_id == "7"` (matches the client's pending key).
- `WsApiDescriptor_Unknown`: `{}` and `{"id":null,…}` → `Unknown`.
- `WsApiEnvelope_Success`: pong fixture → `success`, `status==200`, empty
  `rate_limits` handled.
- `WsApiEnvelope_Error`: §4 error fixture → `!success`,
  `error=="Account has insufficient balance…"`, `error_code==-2010`,
  `rate_limits[0]` fields (type/interval/intervalNum/limit/count).
- `WsApiPing_ToJson`: `req_id=42` → exact `{"id":42,"method":"ping"}`.

### Checkpoint commit
`feat: step 8.1 — Binance WS API envelope, frame descriptor, ping`
Full build + `ctest --output-on-failure` green (277 → ~284).

---

## Sub-step 8.2 — Signed params (`BinanceWsCredentials` + `ws_sign_params`)

**Done when**: `detail::ws_sign_params` produces a deterministic, sorted,
correctly-signed params object for fixed creds/timestamp.

### `ws_api.hpp` additions
`using BinanceWsCredentials = exchange::binance::rest::BinanceCredentials;`
re-export (decision 6); `detail::ws_sign_params` + the value-rendering helper
(decision 7).

### Tests
- `WsSignParams_DeterministicSignature`: fixed creds/timestamp/params →
  recompute the expected hex HMAC-SHA256 in the test over the hand-written
  sorted payload string (the `test_binance_client` precedent of plan step-6-binance-rest-private) →
  `params["signature"]` matches; `apiKey`/`timestamp` injected.
- `WsSignParams_SortedPayload`: keys inserted in non-alphabetical order →
  signature equals the one computed over the alphabetically sorted payload
  (pins the `std::map` ordering assumption).
- `WsSignParams_RecvWindow`: `recv_window_ms=5000` → `recvWindow` present and
  inside the signed payload; `recv_window_ms=0` → absent.
- `WsSignParams_ValueRendering`: string param renders unquoted in the payload,
  int param as digits — asserted via signature recomputation.

### Checkpoint commit
`feat: step 8.2 — Binance WS API signed params`
Full build + full ctest green (~288).

---

## Sub-step 8.3 — `order.place` / `order.cancel` pairs

**Done when**: both request types emit exact signed frames for fixed
creds/timestamp, and both response types parse the §4 fixtures (envelope +
reused REST result structs).

### `ws_api.hpp` additions
- `BinanceWsNewOrderRequest : TypedWsRequest<BinanceWsNewOrderResponse>` —
  decision 9 field set + `creds` + `timestamp`; `to_json()` builds wire-named
  params, calls `ws_sign_params`, emits
  `{"id":req_id,"method":"order.place","params":{…}}`.
- `BinanceWsNewOrderResponse : BinanceWsApiResponse` with
  `std::optional<rest::BinanceNewOrderResponse> order` (decision 5).
- `BinanceWsCancelOrderRequest : TypedWsRequest<BinanceWsCancelOrderResponse>`
  (`method:"order.cancel"`; `symbol` + `orderId`|`origClientOrderId`).
- `BinanceWsCancelOrderResponse : BinanceWsApiResponse` with
  `std::optional<rest::BinanceCancelOrderResponse> order`.

### Fixtures
Add `kWsApiCancelSuccessJson` (synthetic — §2's DELETE shape inside the §4
envelope; commented as such).

### Tests
- `WsApiNewOrder_ToJson`: fixed creds/timestamp/req_id, LIMIT GTC order →
  exact full frame including the recomputed signature; asserts enum wire
  strings (`"BUY"`, `"LIMIT"`, `"GTC"`) and that omitted optionals are absent.
- `WsApiNewOrder_ParsesSuccess`: §4 success fixture → `ok` path fields via the
  reused REST struct (`order_id==12510053279`, `status==OrderStatus::New`,
  `transact_time`, prices) + envelope `rate_limits`.
- `WsApiNewOrder_ParsesError`: §4 error fixture → `!success`, msg, code,
  `order == nullopt`.
- `WsApiCancel_ToJson_ByOrderId` / `_ByClientOrderId`: exact frames, exactly
  one id param present each.
- `WsApiCancel_ParsesSuccess`: synthetic fixture → envelope + reused cancel
  struct fields.

### Checkpoint commit
`feat: step 8.3 — Binance WS API order.place / order.cancel`
Full build + full ctest green (~294).

---

## Sub-step 8.4 — Factory + execute-lifecycle tests

**Done when**: a `MockWsConnection`-driven client completes full WS API
round-trips — signed frame out, reply in, typed `WsResponse` resolved —
with zero changes to the generic client.

### `ws_api.hpp` additions
`BinanceWsApiClient` alias + `make_binance_ws_api_client(conn, eh)`
(decision 2).

### Tests (reusing `tests/unit/mock_ws_connection.hpp`)
- `WsApi_PingRoundTrip`: `fire_open` → `execute_async(ping)` → assert sent
  frame → inject pong with matching id → future resolves `ok`.
- `WsApi_OrderPlaceRoundTrip`: signed order out (id captured from
  `sent_messages`) → inject §4 success with that id → `ok`,
  `result->order->order_id` asserted.
- `WsApi_OrderPlaceError`: inject §4 error with matching id → `ok==false`,
  `error` msg surfaced, future resolves (no hang).
- `WsApi_BeforeOpen_QueuedAndFlushed`: execute before `fire_open` → nothing
  sent; `fire_open()` → frame flushed.
- `WsApi_Timeout`: `execute(ping, 10ms)` with no reply → `ok==false`,
  `error=="request timed out"` (pins the blocking-path timeout for this
  adapter).

### CMake
None — `test_binance_ws_client` already links `binance cryptocogs`
(the generic `ExchangeWsClient` impl lives in `libkraken.a` until Step 9's
`exchange_common` extraction).

### Checkpoint commit
`feat: step 8.4 — Binance WS API client factory + lifecycle tests`
Full build + full ctest green (~299).

---

## Sub-step 8.5 — `ping` example + live verification (decision 11)

**Done when**: `binance_ws_api_example ping` exits 0 against the real
`wss://ws-api.binance.com/ws-api/v3`, logging the pong envelope (status,
rate limits).

### New: `tests/examples/binance/binance_ws_api_example.cpp`
Minimal CLI11 app, single `ping` subcommand: client via the common URL
factory + `binance_ws_api_frame_descriptor` (no shim, no direct ixwebsocket
symbols), `execute(BinanceWsPingRequest{})`, log `ok`/`status`/`rateLimits`,
exit `ok ? 0 : 1`. Wall-clock timeout acceptable (live example, not a test).

### CMake (`tests/CMakeLists.txt`)
Same pattern + comment as `binance_ws_client_example`
(`binance cryptocogs ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward`).

### Verification
Full build + full ctest green, then the live `ping` run.

### Checkpoint commit
`feat: step 8.5 — Binance WS API ping example (live-verified)`

### Wrap-up (after 8.5)
"**Done**:" paragraph at the end of plan multi-exchange-abstraction Step 8 (commit trail, decisions,
test count); flip this plan to Done in `docs/plans.md` and the Status header —
separate `docs: mark plan 006 done` commit, per precedent.

---

## Self-review

### Risks / assumptions

- **No local worked signature vector for the sorted-params scheme.** The
  appendix gives the rule but no key/params→hex example (unlike REST, where
  Binance's published vector pins `test_binance_auth`). The 8.2 tests pin
  construction (sortedness, inclusion set, rendering) by *recomputing with the
  same primitive* — they cannot catch a shared misreading of the payload rule.
  The live example is unsigned (`ping`), so **the first live signed call —
  outside this plan — is the true end-to-end validation**; a testnet
  (`ws-api.testnet.binance.vision`) smoke test is the natural follow-up if
  wanted. Accepted and flagged rather than hidden.
- **Payload/wire divergence risk is structurally closed** (decision 7): one
  function renders both from one object; `WsSignParams_ValueRendering` pins
  the rule.
- **Result-struct reuse (decision 5) assumes the WS shapes stay
  REST-identical.** The appendix asserts it today; if live behaviour ever
  diverges, the fix is splitting the struct then — not pre-duplicating now.
- **`id:null` error frames are unroutable** (descriptor → `Unknown`; pending
  times out). Binance omits the id only for malformed JSON, which our
  `to_json()` cannot produce. Same accepted note as plan step-7-binance-ws-streams.
- **`ws_api.hpp` pulls OpenSSL headers** (via `auth.hpp`) into its includers.
  `binance` already links OpenSSL; `ws_streams.hpp` users are unaffected.
  Cosmetic, noted.
- **Namespace co-habitation**: both Binance WS headers re-export the same
  common names into `exchange::binance::ws` — identical using-declarations
  are legal redeclarations; the unit binary includes both headers and is the
  compile-time proof.
- **Zero generic-client changes** is the same claim plan step-7-binance-ws-streams proved for
  subscriptions, now for the execute path — which Kraken already drives in
  production shape; the only novelty is the descriptor and payload signing.
- If anything fails unexpectedly at a checkpoint, stop and re-plan per the
  global error rule — no workarounds.

### What "done" looks like

All five checkpoints committed, suite ~299 green (exact counts recorded per
checkpoint), live `ping` exit 0, plan multi-exchange-abstraction Step 8 marked Done, this plan
flipped to Done in `docs/plans.md`.
