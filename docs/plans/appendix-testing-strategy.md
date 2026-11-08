# Appendix to multi-exchange-abstraction — Testing Strategy

Companion to [multi-exchange-abstraction.md](multi-exchange-abstraction.md).

The Binance test suite mirrors the existing Kraken suite one-to-one. No new test *techniques* are introduced — every Binance test has a direct Kraken precedent listed below. All tests are network-free: REST via an injected HTTP performer, WebSocket via `MockWsConnection`. Framework is GoogleTest (as the existing C++ suite uses), driven by `ctest`.

## The captured-fixture pattern (the thing to replicate)

The Kraken WS tests do **not** hand-write JSON inline. They capture real frames once into a fixture header (`tests/unit/ws_client_example_json.hpp`, namespace `kraken::ws::test`, one `inline constexpr const char*` per message) and every parse test consumes those constants. Comments mark which frames are real vs synthetic. This keeps `from_json` honest against the actual wire format.

Binance gets the same treatment, with the captured JSON sourced from [appendix-binance-message-formats.md](appendix-binance-message-formats.md). Four fixture headers (split by surface so each test binary only pulls what it needs):

| Fixture header | Namespace | Contents (from the appendix) |
|---|---|---|
| `tests/unit/binance_rest_example_json.hpp` | `binance::rest::test` | ping, time, exchangeInfo, depth, trades, klines, ticker/24hr, ticker/price, bookTicker, plus an error frame |
| `tests/unit/binance_account_example_json.hpp` | `binance::rest::test` | account, order ACK/RESULT/FULL, cancel, cancel-all (array), openOrders, allOrders, myTrades |
| `tests/unit/binance_ws_stream_example_json.hpp` | `binance::ws::test` | subscribe ack, combined-wrapper frames for aggTrade, trade, kline, 24hrTicker, miniTicker, bookTicker, partial depth, diff depth |
| `tests/unit/binance_ws_api_example_json.hpp` | `binance::ws::test` | WS API success reply, error reply |

Each constant carries a one-line comment noting whether it is a real captured frame (public market data — real) or synthetic (anything requiring credentials — order responses, account, WS API replies — synthetic but matching the documented shape), exactly as the Kraken fixture does.

## The four test categories (per Kraken precedent)

| Category | Kraken file(s) | Binance file(s) | What it asserts |
|---|---|---|---|
| **1. Auth / signature** | `test_signature.cpp` | `test_binance_auth.cpp` | HMAC output byte-identical to a known reference vector. Kraken checks against the legacy `KAPI::signature`; Binance checks the lowercase-hex HMAC-SHA256 against Binance's published worked example (key/secret/params → expected signature). Also asserts header injection (`X-MBX-APIKEY`) and that the signed payload is `query+body` concatenated. |
| **2. Request building** | `test_rest_requests.cpp` | `test_binance_rest_requests.cpp` | `build()` produces the right path, HTTP method, and query string for each request type. e.g. `BinanceKlinesRequest{symbol="BTCUSDT",interval="1m"}` → `GET /api/v3/klines?symbol=BTCUSDT&interval=1m`. DELETE methods checked for cancel endpoints. |
| **3. Response parsing** | `test_rest_responses.cpp`, `test_ws_responses.cpp` | `test_binance_rest_responses.cpp`, `test_binance_ws_client.cpp` | `from_json(parse(fixture))` then assert **every field**. Mirrors `test_ws_responses.cpp`'s field-by-field style (`EXPECT_DOUBLE_EQ` on prices, `EXPECT_EQ` on ids/strings). Covers the tricky bits: string→double on prices, int64 ms timestamps, positional array parsing (klines/depth), nested `k` kline object, `fills[]` only-present-in-FULL, array-typed top-level responses (myTrades/openOrders), single-vs-array ticker responses. |
| **4. Client lifecycle (end-to-end, mocked)** | `test_client.cpp` (REST), `test_ws_client.cpp` (WS) | extend with Binance cases | REST: inject a performer via `make_test_client`, assert the request that went out and that the response normalises into `RestResponse<T>` (incl. an error frame → `ok=false`, `errors` populated, **non-2xx status** path). WS: `MockWsConnection` drives the three-phase subscribe and the execute round-trip. |

## `identify_message` tests (per `test_ws_responses.cpp` IdentifyMessage block)

`test_ws_responses.cpp` has one `TEST(IdentifyMessage, X)` per `MessageKind`. Binance gets the equivalent against `BinanceStreamIdentifier` and `BinanceWsIdentifier`, asserting the full `FrameDescriptor` (kind + correlation_id/route_key) for each frame in the dispatch table:

- subscribe ack `{"result":null,"id":1}` → `{MethodResponse, correlation_id:"1"}`
- combined push `{"stream":"btcusdt@aggTrade","data":…}` → `{PushMessage, route_key:"btcusdt@aggTrade"}`
- `bookTicker` (no `e` field) → still routes by `route_key` from the wrapper — explicit test that the ambiguous bare payload is handled
- WS API reply `{"id":…,"status":200,…}` → `{MethodResponse, correlation_id:str(id)}`
- WS API error `{"id":…,"status":400,"error":…}` → `{MethodResponse}` with `ok=false` derived downstream

## `MockWsConnection` lifecycle tests (per `test_ws_client.cpp`)

Reuse the existing `MockWsConnection` (it implements the exchange-agnostic `IWsConnection`, so it works unchanged for Binance once the generic client is in place). Binance-specific cases:

- **Stream subscribe**: `fire_open()` → call `subscribe(BinanceSubscribeRequest{aggTrade})` → assert the outbound `{"method":"SUBSCRIBE","params":["btcusdt@aggtrade"],"id":N}` in `sent_messages` → `inject_message` the `{"result":null,"id":N}` ack → handle becomes active → `inject_message` a wrapped aggTrade push → callback fires with a parsed `BinanceAggTradeEvent` → `handle.cancel()` sends UNSUBSCRIBE and is idempotent.
- **Pre-connection queue**: subscribe *before* `fire_open()`, assert the SUBSCRIBE is queued then flushed on open (mirrors the Kraken pre-connection-queue test).
- **WS API execute**: `execute(BinanceWsNewOrderRequest{…})` → assert outbound `order.place` frame with signed params → inject `{"status":200,"result":…}` matched by id → `WsResponse<…>.ok==true`, fields parsed. Then the `status:400` path → `ok==false`, `error` set.
- **Timeout**: execute with a short timeout and no injected reply → `WsResponse.ok==false` (mirrors the existing Kraken timeout test).

## Determinism

Per the project C++ guidelines: no `sleep_for`, no wall-clock waits. All WS tests drive state synchronously via `fire_open()` / `inject_message()` / `fire_close()`. The one place timing appears — `execute()` timeout — is tested by injecting *no* reply and using a short timeout bound, asserting the failure result rather than racing a real clock (same approach as the existing Kraken timeout test).

## Example programs (runnable demos, not just unit tests)

Beyond the mocked unit tests, the deliverable includes two **runnable client examples**, one-to-one analogs of the existing Kraken pair (`tests/examples/rest_client_example.cpp`, `tests/examples/ws_client_example.cpp`). They are public-endpoint only (no credentials), so they double as live smoke tests of the real transport that the mocked unit tests deliberately avoid.

| Binance example | Kraken analog | Demonstrates |
|---|---|---|
| `examples/binance_rest_client_example.cpp` | `rest_client_example.cpp` | `BinanceRestClient` against every public REST endpoint, one CLI11 subcommand each (`ping`, `time`, `exchangeinfo`, `ticker`, `book`, `klines`, `trades`); `curl_global_init`/`cleanup` lifecycle; spdlog field logging. |
| `examples/binance_ws_client_example.cpp` | `ws_client_example.cpp` | `make_binance_stream_client(url)` + typed `TypedStreamSubscribeRequest` subscriptions, one CLI11 subcommand per stream; push-callback logging; `SubscriptionHandle::cancel()`; **multi-stream-on-one-connection** demo (the Binance-natural form of the Kraken connection-reuse demo). |

Both are wired into `tests/CMakeLists.txt` exactly like their Kraken counterparts (Step 8): the REST example links `binance spdlog CLI11 example_backward`; the WS example additionally links `ixwebsocket`. They are covered by the build (CI compiles all examples); the WS example is *not* part of `ctest` since it opens a live socket — same treatment as `ws_client_example`.

## Per-step deliverables (summary)

| Plan step | New test files | New fixtures | New example |
|---|---|---|---|
| 3 (auth) | `test_binance_auth.cpp` | — | — |
| 4 (public REST) | `test_binance_rest_requests.cpp`, `test_binance_rest_responses.cpp` | `binance_rest_example_json.hpp` | `binance_rest_client_example.cpp` |
| 5 (private REST) | (extend the Step 4 files + `test_client` cases) | `binance_account_example_json.hpp` | — (examples are public-only) |
| 6 (WS streams) | `test_binance_ws_client.cpp` | `binance_ws_stream_example_json.hpp` | `binance_ws_client_example.cpp` |
| 7 (WS API) | (extend `test_binance_ws_client.cpp`) | `binance_ws_api_example_json.hpp` | — |

Every step ends with a full build + full `ctest` run, both green, before the checkpoint commit (project guideline).
