# Multi-Exchange Abstraction

**Goal**: Generalise the cryptocogs request/response scaffold so that Kraken and Binance (and future exchanges) share a single set of client, connection, and response-envelope types. Only authentication schemes, endpoint paths, and message formats differ per exchange.

---

> **MANDATORY — Branch and commit discipline**
>
> All implementation work for this plan **must** be done on a dedicated feature branch. Do **not** commit implementation work directly to `main`.
>
> ```
> git checkout -b feature/multi-exchange-abstraction
> ```
>
> Commit at the completion of every step (and at meaningful sub-step checkpoints within longer steps). Each commit should leave the build and tests green. These commits are not just safety checkpoints — they are the source of reference diffs that will be used when writing the agent onboarding guide in Step 10. A sparse commit history makes that guide impossible to write well.

---

## Companion documents

This file is the architecture overview and step plan. Two companion docs hold the detail that the steps depend on:

- **[001-appendix-binance-message-formats.md](001-appendix-binance-message-formats.md)** — every Binance REST and WebSocket message captured verbatim from the live API docs, annotated with field types and the dispatch routing key. This is the source material for the test fixtures (the Binance analog of `tests/unit/ws_client_example_json.hpp`).
- **[001-appendix-testing-strategy.md](001-appendix-testing-strategy.md)** — the test approach mirrored from the existing Kraken suite: captured-frame fixture headers, `from_json` parse tests asserting every field, request-build tests, `identify_message`/frame-descriptor dispatch tests, and mock-performer / `MockWsConnection` end-to-end tests. Lists every new Binance test file and what it covers.
- **[001-appendix-migration-guide.md](001-appendix-migration-guide.md)** — how existing `cryptocogs` callers move from the `kraken::` / `kraken_*.hpp` surface to the new `exchange::…` / `exchange/…` layout: include-path and namespace mapping tables, function renames, test-harness migration, and worked before/after diffs.
- **[001-appendix-compat-shim.md](001-appendix-compat-shim.md)** — the **shipped** deprecated compatibility shim that lets pre-refactor code compile and run with zero edits (transparent forwarding headers + namespace shim with factory forwarders), its deprecation signalling, CMake opt-out, transparency tests, and the client adoption workflow (drop in → verify → migrate at leisure → flip the shim off).

---

## Motivation

The current library bakes Kraken-specific names into every structural layer:
- Namespaces: `kraken::`, `kraken::rest::`, `kraken::ws::`
- Headers: `kraken_rest_api.hpp`, `kraken_ws_api.hpp`, etc.
- Client classes: `KrakenRestClient`, `KrakenWsClient`

The scaffold underneath — `TypedPublicRequest<R>`, `RestResponse<T>`, `IWsConnection`, the dispatch loop in `KrakenWsClient`, `SubscriptionHandle` — is fully exchange-agnostic. Extracting it as a shared layer lets a Binance (or any future) adapter reuse the entire machinery and only supply:

1. Per-exchange request/response structs (field mapping + JSON serialisation)
2. A per-exchange frame-descriptor function — Kraken's is `kraken_frame_descriptor`, returning `FrameDescriptor` — that `ExchangeWsClient` binds as its `MessageIdentifier` to route inbound frames (see §A's naming note: this is *not* called `identify_message`, which is a separate, optional classifier)
3. A per-exchange auth adapter (signing + header injection)

---

## Proposed Repository Layout

```
include/
  exchange/
    common/
      rest.hpp            # TypedPublicRequest<R>, TypedPrivateRequest<R>,
                          #   RestResponse<T>, HttpRequest, IRestAuth
      ws.hpp              # IWsConnection, IWsErrorHandler, WsResponse<T>,
                          #   SubscriptionHandle, BaseWsResponse, WsRequestBase,
                          #   TypedWsRequest<R>, FrameDescriptor, FrameKind,
                          #   MessageIdentifier — NOT SubscribeResponse (that's
                          #   Kraken's own ack type; see §A2) or MessageFrame
                          #   (never built; superseded by FrameDescriptor)
      ws_client.hpp       # ExchangeWsClient<MsgId> — exchange-agnostic dispatch
      ws_client.inl       # Template bodies (included by ws_client.hpp)
      ix_ws_connection.hpp# IxWsConnection — generic ixwebsocket IWsConnection impl
                          #   + make_exchange_ws_client(url, identifier) factory
      reconnect_session.hpp # WsReconnectSession — generic reconnect/backoff machinery
      types.hpp           # Canonical exchange-agnostic enums:
                          #   Side {Buy, Sell}, OrderType {Limit, Market, …},
                          #   TimeInForce {GTC, GTD, IOC}, OrderStatus
    kraken/
      auth.hpp            # Credentials, sign(), make_nonce()
      types.hpp           # Kraken-specific: PriceType, TriggerReference, StpType,
                          #   FeePreference, TickPrice, OrderParams, Triggers,
                          #   Conditional, OrderDescription, OrderInfo, etc.
      rest_api.hpp        # All Kraken REST request/response structs
      ws_api.hpp          # All Kraken WS request/response structs +
                          #   identify_message / kraken_frame_descriptor
      rest_client.hpp     # KrakenRestClient — standalone, templated execute();
                          #   signs directly via Credentials::sign() (does not
                          #   route through IRestAuth — see Step 2 corrections;
                          #   closed by Step 3, which makes KrakenAuth its
                          #   first implementor)
      ws_client.hpp       # KrakenWsClient alias, PUBLIC_WS_URL/PRIVATE_WS_URL
                          #   consts + make_kraken_ws_client() — binds
                          #   kraken_frame_descriptor over common make_exchange_ws_client
    binance/
      auth.hpp            # BinanceCredentials {api_key, secret_key, Algorithm}
                          #   Algorithm: HmacSha256 | Rsa | Ed25519
      types.hpp           # Binance-specific types and enums
      rest_api.hpp        # Binance REST request/response structs
      ws_api.hpp          # Binance WebSocket API request/response structs +
                          #   binance_ws_api_frame_descriptor
      ws_streams.hpp      # Binance market stream types +
                          #   binance_stream_frame_descriptor
                          #   (listen-key user streams deferred)
      rest_client.hpp     # BinanceRestClient
      ws_client.hpp       # STREAM_URL/WS_API_URL consts + make_binance_stream_client()
                          #   / make_binance_ws_api_client() factories — both expected
                          #   to be inline header-only wrappers, like Kraken's
src/
  exchange/common/
    ws_client.cpp         # ExchangeWsClient non-template impl — exchange-agnostic.
                          #   The ONLY WS .cpp either adapter needs: KrakenWsClient
                          #   is a bare alias whose factory is inline (no .cpp of its
                          #   own), and Binance is expected to follow the same shape.
  kraken/
    rest_client.cpp       # was kraken_rest_client.cpp
    types.cpp             # was kraken_types.cpp
  binance/
    rest_client.cpp       # New
tests/
  unit/
    (existing Kraken tests, updated for new namespaces/headers)
    binance_rest_example_json.hpp      # captured REST fixtures (cf. ws_client_example_json.hpp)
    binance_account_example_json.hpp   # captured account/order fixtures
    binance_ws_stream_example_json.hpp # captured WS stream push frames
    binance_ws_api_example_json.hpp    # captured WS API replies
    test_binance_auth.cpp
    test_binance_rest_requests.cpp
    test_binance_rest_responses.cpp
    test_binance_ws_client.cpp
  examples/
    kraken/
      (existing examples, header paths updated)
    binance/
      binance_rest_client_example.cpp  # analog of rest_client_example.cpp (BinanceRestClient)
      binance_ws_client_example.cpp    # analog of ws_client_example.cpp (Binance stream client)
```

> Fixture headers and the full test matrix are detailed in [001-appendix-testing-strategy.md](001-appendix-testing-strategy.md); their captured JSON comes from [001-appendix-binance-message-formats.md](001-appendix-binance-message-formats.md).

**Naming corrections folded into this layout** (the original draft got both wrong): Kraken's actual `MessageIdentifier` function is `kraken_frame_descriptor`, *not* `identify_message` — the latter is a separate, optional, richer caller-facing classifier Kraken also happens to provide (returns `MessageKind`, for code that bypasses the typed client; see §A's naming note for the full distinction). Binance's analogues should follow the proven `<exchange>_frame_descriptor` naming. Likewise, there is no `kraken/ws_client.cpp` — `KrakenWsClient` is a bare type alias and its factory is `inline`, so the only WS `.cpp` in the tree is the exchange-agnostic `exchange/common/ws_client.cpp`; budget for `binance/ws_client.cpp` only if Binance's two factories end up needing genuinely non-template code (unlikely, given Kraken needed none).

### Namespace layout after migration

| Namespace | Contains |
|---|---|
| `exchange::` | Canonical enums (Side, OrderType, TimeInForce, OrderStatus) |
| `exchange::rest::` | TypedPublicRequest<R>, TypedPrivateRequest<R>, RestResponse<T>, HttpRequest, IRestAuth |
| `exchange::ws::` | IWsConnection, **IxWsConnection** (generic transport), **WsReconnectSession**, WsResponse<T>, SubscriptionHandle, ExchangeWsClient, WsRequestBase, TypedWsRequest<R>, BaseWsResponse, `make_exchange_ws_client()` |
| `exchange::kraken::` | Kraken-specific enums, TickPrice, OrderParams, Triggers, Conditional, OrderInfo, etc. |
| `exchange::kraken::rest::` | All Kraken REST req/resp types, Credentials, KrakenRestClient |
| `exchange::kraken::ws::` | All Kraken WS req/resp types, SubscribeChannel, WsCredentials, `identify_message` + `kraken_frame_descriptor` (the `MessageIdentifier` — see §A's naming note), URL consts, `make_kraken_ws_client()` |
| `exchange::binance::` | Binance-specific types |
| `exchange::binance::rest::` | All Binance REST req/resp types, BinanceCredentials, BinanceRestClient |
| `exchange::binance::ws::` | Binance WS API types, BinanceWsClient |
| `exchange::binance::streams::` | Market/user stream types (listen key sessions, named stream channels) |

**Backwards-compatibility**: a **shipped, deprecated shim** (default-on CMake option) provides the old `include/kraken_*.hpp` paths and `kraken::…` namespaces as transparent forwarders, so existing callers compile and run unchanged and migrate at their own pace. Full design in [001-appendix-compat-shim.md](001-appendix-compat-shim.md); it is removed no earlier than the next major version.

---

## Key Architectural Decisions

### A. `ExchangeWsClient` — runtime-parameterised dispatch

The entire `KrakenWsClient` dispatch loop (pending-handler map, subscription-callback map, pre-connection queue, `SubscriptionHandle`, thread safety) is exchange-agnostic. Only the `MessageIdentifier` — the per-exchange `const json& → FrameDescriptor` function bound at construction — differs; Kraken's is `kraken_frame_descriptor` (see the naming note below — `identify_message` is something else entirely). The refactored client is:

```cpp
// exchange/common/ws_client.hpp
namespace exchange::ws {

enum class FrameKind { MethodResponse, PushMessage, Unknown };

struct FrameDescriptor {
    FrameKind kind;
    // MethodResponse / subscribe-ack: correlates the reply to its request.
    std::optional<std::string> correlation_id;
    // PushMessage: the subscription routing key the callback was registered under.
    std::string route_key;
};

using MessageIdentifier = std::function<FrameDescriptor(const json&)>;

class ExchangeWsClient : public std::enable_shared_from_this<ExchangeWsClient> {
public:
    explicit ExchangeWsClient(std::shared_ptr<IWsConnection> conn,
                             MessageIdentifier identifier);
    void init();
    // execute / execute_async / subscribe / subscribe_async — identical to current
    // KrakenWsClient signatures, but templated on request types that satisfy:
    //   Req::response_type, Req::to_json(), Response::from_json(json)
    // (No exchange-specific types in this class)
};

} // namespace exchange::ws
```

Each exchange provides its own `MessageIdentifier` function and wraps `ExchangeWsClient` in a thin type alias + factory.

> **Naming note (corrects the original draft of this section)**: the `MessageIdentifier` is *not* called `identify_message`. As built, Kraken names it `kraken_frame_descriptor` — `const json& → FrameDescriptor`, the function actually bound into `ExchangeWsClient`. `identify_message` is a *second*, independent, optional function Kraken also provides: `const json& → MessageKind`, a richer caller-facing classifier for code that bypasses `KrakenWsClient` and dispatches raw frames itself (see [CLAUDE.md § low-level dispatch](../../CLAUDE.md#websocket-message-dispatch-low-level)). The two serve different audiences and answer different questions — `kraken_frame_descriptor` is what every adapter *must* supply; `identify_message`/`MessageKind` is a convenience Kraken happens to also offer. Follow `kraken_frame_descriptor` as the naming pattern for `binance_frame_descriptor` (or `binance_stream_frame_descriptor` / `binance_ws_api_frame_descriptor` if Binance's two WS protocols need distinct ones):

```cpp
// exchange/kraken/ws_api.hpp
namespace exchange::kraken::ws {
    exchange::ws::FrameDescriptor kraken_frame_descriptor(const json& j);
}

// exchange/kraken/ws_client.hpp  (NOT ix_ws_connection.hpp — that file doesn't
// exist per-exchange; IxWsConnection is purely a common-layer transport, §E)
using KrakenWsClient = exchange::ws::ExchangeWsClient;

inline std::shared_ptr<KrakenWsClient>
make_kraken_ws_client(std::shared_ptr<IWsConnection>   conn,
                      std::shared_ptr<IWsErrorHandler> error_handler = nullptr) {
    return exchange::ws::make_exchange_ws_client(std::move(conn), kraken_frame_descriptor,
                                                  std::move(error_handler));
}
```

(`IWsErrorHandler` is an optional error-surfacing strategy added during implementation — every `make_*_ws_client` factory threads it through as a defaulted trailing parameter; see §E.)

**Why `correlation_id` is a `std::string`, not `int64_t`** — Kraken correlates replies by an integer `req_id`; Binance's WebSocket API correlates by an `id` that may be an integer *or* a string (their docs show UUID strings). Keying the internal `pending_` map on `std::string` covers both: the Kraken adapter stringifies its generated `int64_t` req_id before sending and the wire format still emits it as a JSON integer; the Binance adapter uses its `id` directly. The correlation key is internal-only — it never dictates the wire type.

**Why `route_key` instead of `channel`** — Kraken routes push frames by a `"channel"` field (`"ticker"`, `"book"`). Binance combined streams route by a `"stream"` field (`"btcusdt@aggTrade"`), and raw single-stream connections carry only an event-type `"e"` field. `route_key` is whatever string the subscribe request registered the callback under; each exchange's `MessageIdentifier`/frame-descriptor function extracts the matching key from an inbound frame. The `subscriptions_` map and `SubscriptionHandle` are otherwise unchanged.

The concrete `correlation_id` / `route_key` derivation for every Binance frame is tabulated in [001-appendix-binance-message-formats.md](001-appendix-binance-message-formats.md).

### A2. WebSocket request scaffold — `TypedWsRequest<R>` and `TypedSubscribeRequest`

The dispatch loop above is only half the WS story. The request side has its own typed scaffold — the WS counterpart of `TypedPublicRequest<R>` / `TypedPrivateRequest<R>` on the REST side — and it generalises along the same lines: the *binding of request → response/push types* is exchange-agnostic and moves to `exchange::ws::`; only the *wire rendering* (`to_json`) stays per-exchange.

#### Method-call requests — `TypedWsRequest<R>`

Today this is literally `template<typename R> struct TypedWsRequest { using response_type = R; };`, and each request struct carries its own `req_id` field that `ExchangeWsClient::execute_async` assigns (`req.req_id = id`). It moves to `exchange::ws::` verbatim, with the per-struct `req_id` slot hoisted into a shared base so the assignment is uniform:

```cpp
// exchange/common/ws.hpp
namespace exchange::ws {

// Client-assigned correlation-id slot. Each exchange's to_json() renders it
// in whatever wire location/format that exchange uses (Kraken: "req_id" inside
// the message; Binance: top-level "id").
struct WsRequestBase { int64_t req_id{0}; };

template<typename R>
struct TypedWsRequest : WsRequestBase {
    using response_type = R;
};

} // namespace exchange::ws
```

**Contract that `ExchangeWsClient::execute_async<Req>` requires** (unchanged in substance, now the only thing an adapter must satisfy):
1. `Req` derives `WsRequestBase` (so the client can assign `req.req_id`).
2. `Req::response_type` names the reply type.
3. `Req::to_json() const -> json` — must serialise `req_id` into the exchange's correlation field.
4. `Req::response_type::from_json(const json&) -> Resp`.
5. `detail::make_ws_response(Resp)` derives `ok` (already type-dispatched: `success` for `BaseResponse` subtypes, `status<400` for Binance replies, always-true for plain `PongMessage`).

So `KrakenAddOrderRequest`, `BinanceWsNewOrderRequest`, and `BinanceWsPingRequest` are all just `: TypedWsRequest<TheirResponse>` with their own `to_json()`. No client changes.

#### Subscription requests — generalising `TypedSubscribeRequest`

This is the one place the current scaffold is genuinely Kraken-shaped and needs widening. Today:

```cpp
template<typename PushMsg, SubscribeChannel Ch>   // Ch is a Kraken-only enum
struct TypedSubscribeRequest : SubscribeRequest {
    using push_type     = PushMsg;
    using response_type = SubscribeResponse;       // Kraken-only ack type
    static constexpr SubscribeChannel channel_value = Ch;
    TypedSubscribeRequest() { this->channel = Ch; }
};
```

…and `subscribe_async` reaches into Kraken specifics: it computes the routing key with `to_string(req.channel)` and hand-builds a Kraken `UnsubscribeRequest`. Binance streams have **no fixed channel enum** — a stream is an open-ended `"<symbol>@<stream>"` string (`"btcusdt@aggTrade"`) — and a different ack shape (`{"result":null,"id":N}`), so neither `SubscribeChannel` nor `SubscribeResponse` fits.

**Generalisation**: keep the compile-time `push_type` / `response_type` binding (that is the type-safety win and it is exchange-neutral), but replace the three Kraken-specific touch-points with small member functions every subscribe request provides. The informal concept becomes:

```cpp
// A WS subscribe request models:
//   using push_type      = <push message type>;   // e.g. BinanceAggTradeEvent
//   using response_type  = <ack type>;            // from_json + make_ws_response → ok
//   int64_t req_id;                                // inherited from WsRequestBase
//   std::string route_key() const;                 // key the push callback registers under
//   json to_json() const;                          // the SUBSCRIBE frame
//   json unsubscribe_json() const;                 // the matching UNSUBSCRIBE frame
```

`ExchangeWsClient::subscribe_async` then becomes fully exchange-agnostic — the two Kraken-specific lines change to:

```cpp
const std::string key      = req.route_key();          // was: to_string(req.channel)
std::string       unsub    = req.unsubscribe_json().dump();  // was: hand-built UnsubscribeRequest
// ack handling routes through detail::make_ws_response(Ack::from_json(j)),
// so ws.ok is derived uniformly instead of reading ack.success directly.
```

Each exchange keeps its own typed alias layer on top. **Kraken** retains its enum internally and implements the concept in two one-line members:

```cpp
// exchange/kraken/ws_api.hpp
template<typename PushMsg, SubscribeChannel Ch>
struct TypedSubscribeRequest : SubscribeRequest {        // SubscribeRequest : WsRequestBase
    using push_type     = PushMsg;
    using response_type = SubscribeResponse;
    static constexpr SubscribeChannel channel_value = Ch;
    TypedSubscribeRequest() { this->channel = Ch; }
    std::string route_key() const { return to_string(channel); }   // "ticker"
    json        unsubscribe_json() const;                          // builds UnsubscribeRequest
    // to_json() inherited from SubscribeRequest
};
using TickerSubscribeRequest = TypedSubscribeRequest<TickerMessage, SubscribeChannel::Ticker>;
```

**Binance** models the same concept with a stream string instead of an enum:

```cpp
// exchange/binance/ws_streams.hpp
template<typename PushMsg>
struct TypedStreamSubscribeRequest : exchange::ws::WsRequestBase {
    using push_type     = PushMsg;
    using response_type = BinanceStreamAck;            // {"result":null,"id":N}
    std::string stream;                                 // "btcusdt@aggTrade"
    std::string route_key() const { return stream; }
    json to_json() const {
        return {{"method","SUBSCRIBE"}, {"params", json::array({stream})}, {"id", req_id}};
    }
    json unsubscribe_json() const {
        return {{"method","UNSUBSCRIBE"}, {"params", json::array({stream})}, {"id", req_id}};
    }
};
using BinanceAggTradeSubscribe = TypedStreamSubscribeRequest<BinanceAggTradeEvent>;
```

#### Request-side concept across both exchanges

| Concept | Kraken | Binance streams | Binance WS API |
|---|---|---|---|
| Method-call base | `TypedWsRequest<R>` | — (streams have no method calls) | `TypedWsRequest<R>` |
| Correlation slot | `req_id` → `"req_id"` in msg | `req_id` → top-level `"id"` | `req_id` → top-level `"id"` |
| Method name | `"add_order"`, `"ping"` | `"SUBSCRIBE"` | `"order.place"`, `"ping"` |
| Subscribe base | `TypedSubscribeRequest<PushMsg, Ch>` | `TypedStreamSubscribeRequest<PushMsg>` | — (no push channel) |
| `route_key()` | `to_string(channel)` → `"ticker"` | `stream` → `"btcusdt@aggTrade"` | — |
| Ack type (`response_type`) | `SubscribeResponse` (`success` flag) | `BinanceStreamAck` (`result==null`) | — |
| `unsubscribe_json()` | `UnsubscribeRequest` frame | `UNSUBSCRIBE` frame | — |

Net effect: `TypedWsRequest<R>` moves to `exchange::ws::` unchanged; `TypedSubscribeRequest` loses its three Kraken-specific touch-points to `route_key()` / `unsubscribe_json()` / a `from_json`-able ack type, after which a single `ExchangeWsClient::subscribe_async` drives Kraken channels and Binance streams identically.

### B. `IRestAuth` — authentication strategy

```cpp
// exchange/common/rest.hpp
namespace exchange::rest {

struct HttpRequest {
    enum class Method { GET, POST, DELETE };   // as built — not a bare std::string
    Method      method{Method::GET};
    std::string path;
    std::string query;   // GET: URL-encoded query string
    std::string body;    // POST/DELETE: URL-encoded or JSON body
    std::map<std::string, std::string> headers;
};

struct IRestAuth {
    virtual ~IRestAuth() = default;
    // Called once per request; injects nonce, timestamp, signature into req.
    virtual void sign(HttpRequest& req) const = 0;
};

} // namespace exchange::rest
```

**Correction (discovered post-hoc — see Step 2's corrections below)**: at the time Step 2 shipped, no `KrakenAuth` type existed — `KrakenRestClient` signed directly via `Credentials::sign()`, never routing through `IRestAuth`, leaving that interface a *defined-but-unused* extension point in `exchange::rest::`. **Step 3** closes this gap by building `KrakenAuth : IRestAuth` and proving the interface against Kraken's real HMAC-SHA512+nonce scheme — making it `IRestAuth`'s **first** real implementor, ahead of (not behind) Binance.
`BinanceAuth` implements `IRestAuth` with HMAC-SHA256 (or RSA/Ed25519), injecting `X-MBX-APIKEY`, `timestamp`, `recvWindow`, and `signature` — and becomes `IRestAuth`'s **second** implementor in Step 4, inheriting whatever shape Step 3 settles on rather than defining it from scratch.

### C. `RestResponse<T>` — common error envelope

Both exchanges use different on-wire error formats but callers see the same type:

```cpp
// exchange/common/rest.hpp
template<typename T>
struct RestResponse {
    bool ok{false};
    std::optional<T> result;
    std::vector<std::string> errors;
};
```

Each exchange's client parses its own wire format and normalises into this envelope:

| | Kraken | Binance |
|---|---|---|
| Success shape | `{"error":[],"result":{…}}` | bare object/array (`{…}` or `[…]`) — no envelope |
| Error shape | `{"error":["EOrder:…"],"result":{}}` | `{"code":-1121,"msg":"Invalid symbol."}` |
| Success signal | `error` array empty | HTTP 2xx **and** no `code`/`msg` error object |
| `ok` derivation | `error.empty()` | `http_status < 400 && !has("code")` |

Two consequences for the Binance client:
1. **No wrapping envelope** — the result payload *is* the top-level JSON, so `parse_binance_response<T>()` passes the whole body to `T::from_json` on success and reads `code`/`msg` on failure. A success body can be a JSON array (e.g. `myTrades`, `openOrders`), which the helper handles.
2. **HTTP status matters** — unlike Kraken (always 200 + error array), Binance signals failure with non-2xx status codes. The HTTP performer must surface the status code to the parser; `HttpRequest`/the performer signature gains a status-code return path.

The Kraken parse helper `parse_rest_response<T>()` moves to `exchange::kraken::rest::`; the Binance helper `parse_binance_response<T>()` lives in `exchange::binance::rest::`. Both yield the same `exchange::rest::RestResponse<T>`.

### D. Binance WebSocket — two channels, one client

Binance has two distinct WebSocket protocols:

1. **WebSocket API** (`wss://ws-api.binance.com/ws-api/v3`) — bidirectional trading (same req/response pattern as Kraken WS). Uses `ExchangeWsClient`.
2. **WebSocket Streams** (`wss://stream.binance.com/stream`) — market data / user data push only. Subscribes by URL path or `SUBSCRIBE` method. Also uses `ExchangeWsClient` but with a Binance-stream-specific `MessageIdentifier` (`binance_stream_frame_descriptor` — see §A's naming note for why this is *not* called `identify_message`).

Both are instances of `ExchangeWsClient` with different identifiers and connection URLs.

### E. Transport and reconnect — generic core, narrow per-exchange seams

`IxWsConnection` (the ixwebsocket implementation of `IWsConnection`) and `WsReconnectSession` (the reconnect/backoff thread) look exchange-specific because they live in `kraken_*`/`kraken::ws::` today, but reading them shows **neither contains any Kraken protocol logic**:

- `IxWsConnection` is a pure transport adapter — it forwards ixwebsocket's open/close/message/error callbacks to the `IWsConnection` interface. Its only Kraken coupling is the namespace and a factory that returns `KrakenWsClient`.
- `WsReconnectSession` is pure machinery — a background thread, a mutex/cv, exponential backoff, and two caller-supplied callbacks (`ConnectFn`, `DisconnectFn`). Its own header already describes it as "transport-level." Zero protocol awareness.

Both therefore move to **`exchange/common/`** (namespace `exchange::ws::`), not into either exchange. The generic URL-string factory becomes:

```cpp
// exchange/common/ix_ws_connection.hpp
inline std::shared_ptr<ExchangeWsClient>
make_exchange_ws_client(const std::string&               url,
                       MessageIdentifier                identifier,
                       std::shared_ptr<IWsErrorHandler> error_handler = nullptr) {
    auto conn   = std::make_shared<IxWsConnection>(url);
    auto client = std::make_shared<ExchangeWsClient>(conn, std::move(identifier),
                                                    std::move(error_handler));
    client->init();
    conn->connect();
    return client;
}
```

Each exchange then supplies a one-line wrapper that binds its own `MessageIdentifier`/frame-descriptor function and URL constant (`make_kraken_ws_client`, `make_binance_stream_client`, `make_binance_ws_api_client`).

**The genuine per-exchange seams are narrow and data-shaped, not logic-shaped:**

| Seam | Kraken | Binance | Where it lives |
|---|---|---|---|
| Endpoint URLs | `PUBLIC_WS_URL`, `PRIVATE_WS_URL` | `STREAM_URL`, `WS_API_URL` | per-exchange `ws_client.hpp` consts |
| Client factory | `make_kraken_ws_client` | `make_binance_stream_client` / `…ws_api_client` | per-exchange `ws_client.hpp` |
| Frame routing | `kraken_frame_descriptor` (channel/req_id) | `binance_frame_descriptor` (stream/id) | per-exchange `ws_api.hpp` / `ws_streams.hpp` |
| Keepalive | app-level `{"channel":"heartbeat"}` frame (informational; already classified as `MessageKind::Heartbeat`) | RFC 6455 protocol ping → ixwebsocket auto-pongs within the 60 s window | transport (ixwebsocket) handles both for free |
| Reconnect trigger | on socket close | on socket close **and** Binance's mandatory ~24 h server-side disconnect | same `WsReconnectSession`; Binance just disconnects more often |
| Resubscribe-on-reconnect | re-send Kraken subscribe frames | re-send `SUBSCRIBE` for each stream | caller's `ConnectFn` (per-exchange, supplied at the call site) |

Two points worth calling out:
1. **Binance keepalive is free.** Binance requires the client to answer the server's 20 s ping with a pong inside 60 s; ixwebsocket auto-responds to protocol ping frames per RFC 6455, so no code is needed. `IWsConnection` deliberately has no ping method — keepalive stays a transport concern.
2. **Reconnect resubscription is the caller's job, by design.** `WsReconnectSession` stays generic because *what to re-subscribe* is supplied through `ConnectFn`. The Binance `binance_ws_client_example` and the Kraken examples each show their own resubscribe set; the session itself is identical. (An optional future convenience — a `ExchangeWsClient::resubscribe_all()` that replays its active `subscriptions_` — would live in common too, but is out of scope here.)

Optional, generic (not exchange-specific) extension: give `IxWsConnection`'s constructor optional ping-interval / extra-handshake-header parameters. Not required for Kraken or Binance, but it keeps future exchanges that need handshake auth headers from touching the interface.

### F. Per-exchange CMake build toggles

Four `option()` declarations in the top-level `CMakeLists.txt` control what is built, all defaulting to `ON`:

```cmake
option(CRYPTOCOGS_BUILD_KRAKEN       "Build the Kraken exchange adapter"         ON)
option(CRYPTOCOGS_BUILD_BINANCE      "Build the Binance exchange adapter"         ON)
option(CRYPTOCOGS_BUILD_TESTS        "Build tests and examples"                   ON)
option(CRYPTOCOGS_BUILD_COMPAT_SHIM  "Ship the deprecated kraken:: compat shim"  ON)
```

**Dependency rules** — checked at configure time with explicit `if`/`message(WARNING …)` guards placed immediately after the `option()` declarations:

```cmake
if(CRYPTOCOGS_BUILD_COMPAT_SHIM AND NOT CRYPTOCOGS_BUILD_KRAKEN)
    message(WARNING
        "CRYPTOCOGS_BUILD_COMPAT_SHIM requires CRYPTOCOGS_BUILD_KRAKEN; disabling shim.")
    set(CRYPTOCOGS_BUILD_COMPAT_SHIM OFF CACHE BOOL "" FORCE)
endif()

if(NOT CRYPTOCOGS_BUILD_KRAKEN AND NOT CRYPTOCOGS_BUILD_BINANCE)
    message(WARNING
        "Both CRYPTOCOGS_BUILD_KRAKEN and CRYPTOCOGS_BUILD_BINANCE are OFF — nothing will be built.")
endif()
```

**`src/CMakeLists.txt` structure** — ~~the common scaffold is a pure INTERFACE target~~

> **Correction (architectural — discovered post-Step-1)**: `exchange_common` **cannot** be header-only `INTERFACE`. `ExchangeWsClient`'s non-template methods are real compiled object code living in `src/exchange/common/ws_client.cpp` — already built today, folded directly into `cryptocogs`'s sources (see `src/CMakeLists.txt` as it stands now). It needs `nlohmann_json` transitively but, notably, *not* OpenSSL/libcurl. So `exchange_common` must be `STATIC` and compile that file; Step 9 is what extracts it from `cryptocogs` into its own target. Each exchange is still its own static library guarded by its flag, now linking `exchange_common PUBLIC`:

```cmake
# Always present — compiles ExchangeWsClient's exchange-agnostic non-template methods
add_library(exchange_common STATIC
    exchange/common/ws_client.cpp
)
target_include_directories(exchange_common PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(exchange_common PUBLIC nlohmann_json::nlohmann_json)
add_library(cryptocogs::common ALIAS exchange_common)

if(CRYPTOCOGS_BUILD_KRAKEN)
    add_library(cryptocogs STATIC
        kraken/rest_client.cpp
        kraken/types.cpp
    )
    target_link_libraries(cryptocogs
        PUBLIC  exchange_common
        PRIVATE OpenSSL::SSL OpenSSL::Crypto CURL::libcurl
    )
    add_library(cryptocogs::kraken ALIAS cryptocogs)
endif()

if(CRYPTOCOGS_BUILD_BINANCE)
    add_library(binance STATIC
        binance/rest_client.cpp
        # binance/ws_client.cpp only if Step 7/8 end up needing genuinely
        # non-template Binance WS code — Kraken needed none (KrakenWsClient
        # is a bare alias; make_kraken_ws_client is inline), so budget for none.
    )
    target_link_libraries(binance
        PUBLIC  exchange_common
        PRIVATE OpenSSL::SSL OpenSSL::Crypto CURL::libcurl
    )
    add_library(cryptocogs::binance ALIAS binance)
endif()
```

The two exchange libraries are peers — neither links against the other. Both depend on `exchange_common` and the same system libraries. OpenSSL and libcurl are `PRIVATE` so callers that link `cryptocogs` or `binance` do not need to repeat those dependencies in their own `target_link_libraries`.

**`tests/CMakeLists.txt` structure** — all test and example targets are nested inside the exchange guards, themselves inside the existing `CRYPTOCOGS_BUILD_TESTS` guard:

```cmake
if(CRYPTOCOGS_BUILD_TESTS)
    # FetchContent for spdlog + GTest — unconditional within this block.
    # Guard with NOT TARGET checks so re-configure is safe:
    #   if(NOT TARGET spdlog::spdlog) ... endif()
    #   if(NOT TARGET GTest::gtest_main) ... endif()

    if(CRYPTOCOGS_BUILD_KRAKEN)
        # Unit tests: kraken_unit_tests
        #   sources: test_signature.cpp  test_rest_requests.cpp
        #            test_rest_responses.cpp  test_client.cpp  test_ws_client.cpp
        #   links:   cryptocogs::kraken  GTest::gtest_main

        # Examples (tests/examples/kraken/):
        #   public_rest  private_rest  public_ws  private_ws
        #   ws_client_example  kraken_example
        #   each links: cryptocogs::kraken  spdlog::spdlog
        #   WS examples also link: ixwebsocket

        if(CRYPTOCOGS_BUILD_COMPAT_SHIM)
            # Unit tests: test_compat_shim
            # Compat compile-proof: tests/compat/ (pre-refactor sources, unmodified)
        endif()
    endif()

    if(CRYPTOCOGS_BUILD_BINANCE)
        # Unit tests: binance_unit_tests
        #   sources: test_binance_auth.cpp  test_binance_rest_requests.cpp
        #            test_binance_rest_responses.cpp  test_binance_ws_client.cpp
        #   links:   cryptocogs::binance  GTest::gtest_main

        # Examples (tests/examples/binance/):
        #   binance_rest_client_example
        #     links: cryptocogs::binance  spdlog::spdlog  CLI11::CLI11
        #   binance_ws_client_example
        #     links: cryptocogs::binance  ixwebsocket  spdlog::spdlog  CLI11::CLI11
    endif()
endif()
```

**Impact on each plan step:**

| Step | What the guard enables |
|---|---|
| 1 | No guard — `exchange_common` **STATIC** target (compiles `exchange/common/ws_client.cpp`) is always built |
| 2–2b, 3 | `CRYPTOCOGS_BUILD_KRAKEN` guards the Kraken library, all Kraken tests, Kraken examples, and the compat shim — Step 3's `IRestAuth` conformance work touches only Kraken sources, so it sits squarely inside this guard |
| 4–8 | `CRYPTOCOGS_BUILD_BINANCE` guards all Binance library work, tests, and examples |
| 9 | Wires all guards together; validates dependency rules at configure time |

Turning `CRYPTOCOGS_BUILD_TESTS=OFF` suppresses all tests and examples regardless of exchange flags. Exchange flags only gate library targets and their dependent tests/examples — they are orthogonal to `CRYPTOCOGS_BUILD_TESTS`.

---

## Steps

### Step 1 — Create common scaffold headers (no logic change) ✅ *done*

Created `include/exchange/common/{rest,ws,ws_client.hpp+.inl,ix_ws_connection,reconnect_session,types}.hpp` — the exchange-agnostic scaffold (`TypedPublicRequest<R>`/`TypedPrivateRequest<R>`/`RestResponse<T>`/`HttpRequest`/`IRestAuth`, `IWsConnection`/`IWsErrorHandler`/`WsResponse<T>`/`SubscriptionHandle`/`BaseWsResponse`/`WsRequestBase`/`TypedWsRequest<R>`/`FrameDescriptor`/`FrameKind`/`MessageIdentifier`, `ExchangeWsClient`, `IxWsConnection`, `WsReconnectSession`, and the canonical `Side`/`OrderType`/`TimeInForce`/`OrderStatus` enums) — moved into `exchange::`/`exchange::rest::`/`exchange::ws::`. `IWsErrorHandler`/`RateLimitedWsErrorHandler` were added beyond the original scope as an optional error-surfacing strategy (every `make_*_ws_client` factory now threads a defaulted `error_handler` parameter — see the corrected §A code sample). Old `kraken_*.hpp` paths became thin re-export shims so existing code and tests kept compiling unchanged.

**Done**: full build + all existing tests passed, no behaviour change. See [CLAUDE.md § Namespace layout](../../CLAUDE.md#namespace-layout) for the as-built result.

### Step 2 — Migrate Kraken to `exchange/kraken/` namespaces ✅ *done*

Created `include/exchange/kraken/{auth,types,rest_api,ws_api,rest_client,ws_client}.hpp` under `exchange::kraken::`/`::rest::`/`::ws::` and updated `src/kraken/*.cpp`. The WS request scaffold was adapted to the generalised §A2 contract — method-call requests now inherit `exchange::ws::TypedWsRequest<R>`, and `TypedSubscribeRequest<PushMsg, Ch>` gained `route_key()`/`unsubscribe_json()`. The old `include/kraken_*.hpp` paths were earmarked to become deprecated compatibility forwarders in Step 2b rather than being deleted.

**Two corrections to this step's original text, discovered post-hoc:**

1. **Test/example migration did *not* happen here**, despite the original "done when" calling for it. [Plan step-2b-compat-shim's audit](002-step-2b-compat-shim.md) found *zero* `exchange::kraken::` references anywhere in `tests/` immediately after this step landed — every unit test and example was still on `kraken::`. That migration was deferred to, and completed in, **Step 2b Phase 1** (commit `a6b82e3`).
2. **`KrakenRestClient` did not become a thin wrapper.** The plan called for it to "wrap `exchange::rest::GenericRestClient` with `KrakenAuth : IRestAuth`" — neither type exists anywhere in the tree. `KrakenRestClient` kept its pre-refactor shape: a standalone class with templated `execute()` that signs directly via `Credentials::sign()`, and every one of its ~28 private `build(const Credentials&)` overrides calls `creds.sign(...)` itself (26 through the shared `PrivateRequest::make_private_request()` chokepoint, 2 — `AddOrderBatchRequest`/`CancelOrderBatchRequest` — inline, because their bodies are JSON rather than form-encoded). `IRestAuth` remained a *defined-but-unused* extension point in `exchange::rest::`. **Closing this gap — and deciding whether `GenericRestClient` is even the right shape, given §C's finding that Kraken's and Binance's response envelopes differ structurally — is now Step 3**, scheduled deliberately *before* Binance work so `IRestAuth` is proven against a real, already-pinned-down signing scheme first. Kraken becomes `IRestAuth`'s first implementor; Binance (Step 4) is its second.

**Done**: full build + all unit tests passed; examples compiled against the new headers and namespaces (their *own* migration to those namespaces came later — see correction 1).

### Step 2b — Ship the Kraken backwards-compatibility shim

**Done when**: a verbatim pre-refactor translation unit compiles and runs unchanged with the shim on; `-DCRYPTOCOGS_BUILD_COMPAT_SHIM=OFF` makes the old paths disappear. Full design: [001-appendix-compat-shim.md](001-appendix-compat-shim.md).

- Add `option(CRYPTOCOGS_BUILD_COMPAT_SHIM … ON)`; when ON, the `cryptocogs::kraken` install rules also install the shim headers (no client CMake change needed).
- Create `include/kraken_compat.hpp` — the namespace shim: reopens `namespace kraken { … }` with `using`-directives for the bulk re-exports, targeted `using` for generic bases, and a `[[deprecated]]` `KrakenWsClient` alias. Real namespaces are required here so forwarder functions are legal — this is what lets the shipped shim cover factory renames that the guide's alias-only shim cannot.

  > **Correction — `make_ws_client` forwarders, as shipped**: this bullet originally claimed *both* `[[deprecated]] make_ws_client` overloads live in `kraken_compat.hpp`, with the connection-based one calling `make_exchange_ws_client(conn, identify_message)` directly. Neither holds. There are still two forwarders, but transport linkage forces them into **two different files**: the *connection-based* overload lives here in `kraken_compat.hpp` and **delegates** to `make_kraken_ws_client(conn, error_handler)`; the *URL-based* overload needs `IxWsConnection`/ixwebsocket (this header doesn't link it — it's reachable from pure-REST entry points) and instead lives in `kraken_ix_ws_connection.hpp`, delegating to `make_exchange_ws_client(url, kraken_frame_descriptor, error_handler)`. Both delegate to the real entry points rather than re-deriving them ("so the shim cannot drift from real behaviour" — a comment in the shipped code), and both correctly name the `MessageIdentifier` `kraken_frame_descriptor` — *not* `identify_message` (§A's naming note). Full rationale: [001-appendix-compat-shim.md](001-appendix-compat-shim.md).
- Create the forwarding headers at the original paths (`kraken_types.hpp`, `kraken_rest_api.hpp`, `kraken_rest_client.hpp`, `kraken_ws_api.hpp`, `kraken_ws_client.hpp`, `kraken_ix_ws_connection.hpp`, `ws_reconnect_session.hpp`) — each a `#pragma message` (guarded by `CRYPTOCOGS_SUPPRESS_DEPRECATION`) + include of the new header + include of `kraken_compat.hpp`.
- **Tests**:
  - `tests/compat/` — keep the **unmodified** pre-refactor `rest_client_example.cpp` and `ws_client_example.cpp` (old includes + `kraken::` names); compile them against the shim (compile-proof of an intact surface).
  - `tests/unit/test_compat_shim.cpp` — behavioural: a public REST round-trip via `make_test_client` through `kraken::rest::…`; a WS subscribe via `MockWsConnection` through `kraken::ws::make_ws_client(conn)` (exercises the forwarder), asserting the callback fires with a `kraken::ws::TickerMessage`; a `static_assert` that `make_ws_client(url)` resolves. Built with `-DCRYPTOCOGS_SUPPRESS_DEPRECATION`.
  - All gated on `CRYPTOCOGS_BUILD_COMPAT_SHIM=ON`. Full build + `ctest` green; then a clean configure with the option **OFF** builds the library + new-API tests with the old paths absent.

### Step 3 — Make `KrakenRestClient` conform to the `IRestAuth` architecture ✅ *done*

**Done when**: every Kraken private-request signing path routes through `KrakenAuth : exchange::rest::IRestAuth` — no `creds.sign(...)` call remains inline anywhere in `rest_api.hpp` — and `test_signature.cpp`, `test_client.cpp`, and `test_rest_requests.cpp` all pass **unchanged**, proving the refactor reproduces the pre-refactor wire format byte-for-byte. A new equivalence test pins `KrakenAuth::sign` against the legacy direct-`Credentials::sign()` path the same way `test_signature.cpp` already pins `Credentials::sign()` against the legacy `KAPI` reference.

**Why this comes before any Binance work**: Step 2's correction above is blunt about the gap — `IRestAuth` is a *defined-but-unused* extension point, designed to fit Binance's shape (HMAC-SHA256, timestamp injected alongside an already-built request) and never exercised against Kraken's structurally different one (HMAC-SHA512, where the nonce is simultaneously a standalone signing input *and* a field embedded in the signed body itself). If that single-mutation-point contract turns out not to fit Kraken, discovering it now — with `test_signature.cpp` already pinning the correct byte-for-byte output as a regression net, and only one well-understood adapter depending on the interface — is far cheaper than discovering it after Binance's entire REST layer has been built on top of an interface shaped by guesswork.

- Add `KrakenAuth : exchange::rest::IRestAuth` to `exchange/kraken/auth.hpp`, wrapping a `Credentials` and implementing `void sign(HttpRequest& req) const override`.
- **The crux, and the step's central open question** — resolve it explicitly and document the choice inline next to `KrakenAuth::sign`, because it will directly shape `BinanceAuth` in Step 4:
  Kraken's scheme requires the *raw nonce string* both as a standalone input to the digest (`HMAC(path + SHA256(nonce_str + body))`) **and** as a `nonce=<value>` field inside that same `body` — a tighter coupling between "anti-replay token" and "wire body" than `IRestAuth::sign`'s doc comment ("injects nonce, timestamp, signature, and auth headers" into an already-built request) anticipates. Worse, two of the ~28 private endpoints (`AddOrderBatchRequest`, `CancelOrderBatchRequest`) use **JSON** bodies rather than form-encoded ones, so "inject `nonce=` into the body" isn't even a single string operation. Pick one of:
  1. `KrakenAuth::sign` generates the nonce *and* injects it into `req.body` for both shapes (form-encoded prepend vs. JSON-key merge) — keeps `build()` uniform and nonce-free, but couples the auth strategy to body encoding, which smells like it's reaching past its stated job.
  2. `build()` keeps constructing the complete, nonce-embedded body exactly as `make_private_request` does today, and *only* the signature/header computation moves into `KrakenAuth::sign` — a narrower change, but means `IRestAuth::sign`'s "injects nonce" promise turns out to be Binance-shaped rather than universal; update the doc comment in `exchange/common/rest.hpp` to describe what the interface actually guarantees once two real adapters have answered that question, rather than what it was guessed to guarantee with zero.
- Refactor the signing chokepoint(s) so the actual `creds.sign(...)` call and `API-Key`/`API-Sign`/`Content-Type` header injection live in `KrakenAuth::sign` — called once by `KrakenRestClient::execute(req, creds)` — rather than duplicated across call sites as they are today: `PrivateRequest::make_private_request()` (the shared helper ~26 of the ~28 private `build(const Credentials&)` overrides already route through) plus the two JSON-bodied outliers that bypass it and sign inline. Whether `PrivateRequest::build` keeps taking `const Credentials&` or collapses to the bare `build() const` shape `PublicRequest` already has (matching `exchange::rest::TypedPublicRequest<R>`'s single-`build()` contract) falls out of which option above is chosen.
- Leave `RestResponse<T>`/`parse_rest_response`, the public-request path, and WebSocket auth (`WsCredentials` — a different mechanism entirely: a session token fetched once over REST and passed inside WS request `params`, not a per-request `IRestAuth` signer) untouched. This step is scoped to private REST signing only.
- **`GenericRestClient` extraction is deliberately out of scope.** The original Step 2 text proposed wrapping one, but no detailed design for it exists anywhere in this plan, and §C already documents that Kraken's and Binance's response envelopes are *shaped differently* — wrapping envelope vs. bare body, error-array vs. `code`/`msg`, always-200 vs. real HTTP status codes that the parser must see. Conjuring a shared transport-plus-parsing base into existence now, before Binance proves what (if anything) is genuinely common beyond `IRestAuth` itself, is exactly the kind of speculative abstraction the project avoids. If Step 5 (Binance REST) shows the transport loop really is identical modulo auth and envelope-parsing, that is the moment to extract one — generalising from two real implementations instead of one plus a guess.
- **Tests**:
  - `test_signature.cpp` and `test_client.cpp` must continue to pass **unchanged** — they already pin Kraken's HMAC-SHA512 output byte-for-byte against the legacy `KAPI` reference and exercise `execute()` end-to-end; any drift in wire output fails them immediately.
  - `test_rest_requests.cpp` must continue to pass unchanged — it asserts every private request's path/method/body/headers, so any change in how `KrakenAuth::sign` assembles the final `HttpRequest` surfaces here first.
  - Add a `KrakenAuth` equivalence test (extend `test_signature.cpp`, or add `test_kraken_auth.cpp`) asserting `KrakenAuth{creds}.sign(http)` produces an identical `API-Sign`/`API-Key`/body to the pre-refactor direct-`Credentials::sign()` path for the same input — the same "new path matches the trusted reference byte-for-byte" pattern `test_signature.cpp` already uses, just with the now-legacy direct-signing path as the new reference instead of `KAPI`.

**Open question resolved (Option 2)**: `build()` shapes are unchanged — each still constructs the complete nonce-embedded body. Only the three inline `creds.sign(...)` + header-injection sites (one in `make_private_request`, one each in `AddOrderBatchRequest::build` and `CancelOrderBatchRequest::build`) were replaced with `KrakenAuth{creds}.sign(req)`. `KrakenAuth::sign` reads the nonce back from the already-built body (checking `Content-Type` to select form-encoded vs. JSON extraction), delegates to `Credentials::sign`, and injects `API-Key`/`API-Sign`. `IRestAuth::sign`'s doc comment was updated to reflect the actual contract: nonce injection is exchange-specific — Kraken embeds it in `build()`, Binance (Step 4) will inject it in `sign()`. 8 new tests in `test_kraken_auth.cpp` assert byte-identical output to the direct `Credentials::sign()` path for both body formats.

**Done**: full build + all 169 tests passed (161 pre-existing, 8 new `KrakenAuth` tests); no changes to existing test or client public API.

### Step 4 — Add Binance authentication and REST client ✅ *done*

**Done when**: `BinanceRestClient` executes public requests; auth signs private requests correctly; unit tests verify signatures.

- Create `include/exchange/binance/auth.hpp`:
  - `enum class BinanceSignAlgorithm { HmacSha256, Rsa, Ed25519 }`.
  - `struct BinanceCredentials { api_key, secret_key, BinanceSignAlgorithm, recv_window_ms=5000 }`.
  - `BinanceAuth : IRestAuth` — injects `X-MBX-APIKEY` header, appends `timestamp` (+ `recvWindow`) to the query/body, computes the signature over the **entire** `query_string + body` concatenation, and appends `&signature=<sig>`.
  - HMAC-SHA256 signing in Step 4; RSA/Ed25519 deferred (see Deferred items).
- **Signing differences from Kraken** (these are the easy-to-get-wrong bits — call them out in the test):
  | | Kraken | Binance HMAC |
  |---|---|---|
  | Digest | HMAC-**SHA512** | HMAC-**SHA256** |
  | Signed payload | `path + SHA256(nonce + body)` | `query_string + body` (concatenated, already URL-encoded) |
  | Output encoding | **base64** | **lowercase hex** |
  | Anti-replay | `nonce` (µs counter) | `timestamp` (ms) + optional `recvWindow` |
  | Key material | base64-decoded secret | raw UTF-8 secret bytes |
  - Reuse the existing `hmac_*` OpenSSL helpers; add `hmac_sha256()` + a `to_hex()` encoder alongside the existing `base64_encode()`.
- Create `include/exchange/binance/rest_client.hpp` + `src/binance/rest_client.cpp` — `BinanceRestClient`, mirrors `KrakenRestClient` interface. Default base URL: `https://api.binance.com`. The HTTP performer must return the response **status code** as well as the body (see envelope section C).
- Create `tests/unit/test_binance_auth.cpp` — verify HMAC-SHA256 hex signature against Binance's published worked example (the SPOT `order` example in *REST API → SIGNED endpoint examples*, which gives a known key/secret/params → expected signature). This is the Binance analog of `test_signature.cpp`.
- **Tests**: New tests pass; all Kraken tests still pass.

**HMAC test vector verified**: `echo -n "<params>" | openssl dgst -sha256 -hmac "<secret>"` confirms that `detail::to_hex(detail::hmac_sha256(secret, total_params))` matches OpenSSL's output byte-for-byte. The expected value in `test_binance_auth.cpp` (`20a0e317...`) is independently confirmed; the value sometimes cited in older Binance docs (`c8db...`) is for a different parameter set and is not correct for these inputs.

**Signing differences proven by tests** (each covered by a dedicated assertion):
- Digest: HMAC-SHA256 (confirmed by `HmacSha256HexMatchesPublishedVector` against OpenSSL-verified vector)
- Output encoding: lowercase hex (asserted by `ToHex_IsLowercase`)
- Anti-replay: timestamp injected in `sign()`, not in `build()` (asserted by GET and POST injection tests)
- Key material: raw UTF-8 bytes — no base64 decoding (implicit in all crypto tests)

**Design note — `ClockFn` injectable clock**: `BinanceAuth` accepts a `std::function<int64_t()>` clock parameter so unit tests can inject a fixed timestamp. The constructor default is a real millisecond-resolution system clock. This is what makes the GET/POST injection tests deterministic without any mocking framework.

**`BinanceRestClient` performer signature** differs from `KrakenRestClient`: the performer returns `std::pair<int, std::string>` (HTTP status + body) rather than just the body, because Binance uses real HTTP status codes to signal errors (unlike Kraken, which always returns 200 and an error array). `parse_binance_response<T>()` reads both to derive `RestResponse::ok`.

**Done**: full build + all 179 tests passed (169 pre-existing, 10 new Binance auth tests); no changes to existing test or client public API. Files added: `include/exchange/binance/auth.hpp`, `include/exchange/binance/rest_api.hpp`, `include/exchange/binance/rest_client.hpp`, `src/binance/rest_client.cpp`, `tests/unit/test_binance_auth.cpp`; updated `src/CMakeLists.txt` and `tests/unit/CMakeLists.txt`.

### Step 5 — Binance REST public endpoints

**Done when**: Market data endpoints are implemented and unit-tested with mock HTTP performers.

Endpoints to implement (in `include/exchange/binance/rest_api.hpp`):

| Request type | Method + Path | Response type |
|---|---|---|
| `BinancePingRequest` | GET `/api/v3/ping` | `BinancePing` |
| `BinanceServerTimeRequest` | GET `/api/v3/time` | `BinanceServerTime` |
| `BinanceExchangeInfoRequest` | GET `/api/v3/exchangeInfo` | `BinanceExchangeInfo` |
| `BinanceTickerPriceRequest` | GET `/api/v3/ticker/price` | `BinanceTickerPrice` |
| `BinanceTicker24hrRequest` | GET `/api/v3/ticker/24hr` | `BinanceTicker24hr` |
| `BinanceKlinesRequest` | GET `/api/v3/klines` | `BinanceKlinesResult` |
| `BinanceOrderBookRequest` | GET `/api/v3/depth` | `BinanceOrderBook` |
| `BinanceRecentTradesRequest` | GET `/api/v3/trades` | `BinanceTradesResult` |

- Each inherits `exchange::rest::TypedPublicRequest<R>`.
- **Format specifics** (full JSON in the appendix; these drive the field types):
  - Numbers arrive as **JSON strings** (`"4.00000200"`), same as Kraken — parse with `std::stod(j.value("field","0"))`.
  - Timestamps are **integer milliseconds** (`1499865549590`), *not* ISO-8601 strings — store as `int64_t`. (Kraken WS uses ISO strings; do not copy that here.)
  - `klines` is an **array of 12-element mixed arrays** — parse positionally by index, not by key.
  - `depth` bids/asks are **2-element string arrays** `["price","qty"]` — parse positionally.
  - `ticker/price`, `ticker/24hr`, `bookTicker` accept a single `symbol` or a `symbols=[...]` list; with a list the response becomes a JSON **array** of the object. Support both (single object vs array) in `from_json`.
- Create `tests/unit/binance_rest_example_json.hpp` — captured response fixtures (the REST analog of `ws_client_example_json.hpp`; see testing-strategy doc). Populate from the appendix.
- Create `tests/unit/test_binance_rest_requests.cpp` (path/method/query) and `test_binance_rest_responses.cpp` (`from_json` field assertions against the fixtures).
- Add `tests/examples/binance/binance_rest_client_example.cpp` — the direct analog of `tests/examples/kraken/rest_client_example.cpp`: a CLI11 app with one subcommand per public endpoint (`ping`, `time`, `exchangeinfo`, `ticker [--symbols …]`, `book <symbol> [--limit N]`, `klines <symbol> --interval 1m`, `trades <symbol> [--limit N]`), each `run_*(BinanceRestClient&, args)` executing the typed request and logging the parsed fields via spdlog. `main()` mirrors the Kraken example: `curl_global_init` → construct `BinanceRestClient` → dispatch by subcommand → `curl_global_cleanup`. Public endpoints only — no credentials. Links `binance spdlog::spdlog CLI11::CLI11 example_backward`.
- **Tests**: All unit tests pass; example compiles and runs against live Binance (no credentials needed).

**Done**: implemented via [plan step-5-binance-rest-public](003-step-5-binance-rest-public.md) in five checkpoint commits (5.1–5.5). All 8 request/response types live in `include/exchange/binance/rest_api.hpp`, inheriting the **local** `exchange::binance::rest::TypedPublicRequest<R>` (plan step-5-binance-rest-public design decision 1 — the client's SFINAE gates on the local `PublicRequest`, mirroring Kraken). Fixtures in `tests/unit/binance_rest_example_json.hpp`; 32 new tests across `test_binance_rest_requests.cpp` + `test_binance_rest_responses.cpp` (full suite 211 green). CLI example verified live: all 7 subcommands returned 2xx against `api.binance.com`. Out of scope per plan step-5-binance-rest-public: `bookTicker`, `BinanceExchangeInfo` filter/permission fields.

### Step 6 — Binance REST private (account + trading) endpoints

**Done when**: Account and order endpoints implemented and unit-tested.

Endpoints to implement:

| Request type | Method + Path | Response type |
|---|---|---|
| `BinanceAccountRequest` | GET `/api/v3/account` | `BinanceAccount` |
| `BinanceOpenOrdersRequest` | GET `/api/v3/openOrders` | `BinanceOpenOrdersResult` |
| `BinanceAllOrdersRequest` | GET `/api/v3/allOrders` | `BinanceAllOrdersResult` |
| `BinanceNewOrderRequest` | POST `/api/v3/order` | `BinanceNewOrderResponse` |
| `BinanceCancelOrderRequest` | DELETE `/api/v3/order` | `BinanceCancelOrderResponse` |
| `BinanceCancelAllOpenOrdersRequest` | DELETE `/api/v3/openOrders` | `BinanceCancelAllResponse` |
| `BinanceMyTradesRequest` | GET `/api/v3/myTrades` | `BinanceMyTradesResult` |

- All inherit `exchange::rest::TypedPrivateRequest<R>`.
- `BinanceNewOrderRequest` maps `exchange::Side` → `"BUY"/"SELL"` and `exchange::OrderType` → `"LIMIT"/"MARKET"/…` (uppercase wire strings; Binance-only types like `STOP_LOSS_LIMIT` live in `exchange::binance::`).
- **Format specifics**:
  - `POST /api/v3/order` has three response shapes selected by the `newOrderRespType` param: **ACK** (ids only), **RESULT** (+ fill status), **FULL** (+ `fills[]` array). `BinanceNewOrderResponse` carries all fields as `std::optional` and a `fills` vector that is empty unless FULL. Default for LIMIT/MARKET is FULL.
  - `DELETE /api/v3/openOrders` returns a **JSON array** of cancelled-order objects; `myTrades`, `openOrders`, `allOrders` likewise return arrays.
  - Order fields reuse the string-number + int-ms-timestamp conventions from Step 5.
- Create `tests/unit/binance_account_example_json.hpp` fixtures (account, order ACK/RESULT/FULL, cancel, openOrders, myTrades) — captured from the appendix.
- Unit tests use `make_test_client()` (same injected-performer pattern as `test_client.cpp`): assert the signed request path/query and that `from_json` parses each fixture. Signing correctness is already covered by `test_binance_auth.cpp`.
- **Tests**: All unit tests pass.

**Done**: implemented via [plan step-6-binance-rest-private](004-step-6-binance-rest-private.md) in five checkpoint commits (6.1–6.5: `145bf70`, `c8e42c4`, `7c2db46`, `050b82d`, `8b63fa9`). All 7 request types + response/row structs live in `include/exchange/binance/rest_api.hpp`, inheriting the **local** `exchange::binance::rest::TypedPrivateRequest<R>` with credential-free `build()` — `execute(req, auth)` signs via `BinanceAuth` afterwards (plan step-6-binance-rest-private decision 1). POST params go body-side, GET/DELETE params query-side, matching `BinanceAuth::sign`'s signed-payload contract (decision 2). Enum mapping is canonical-where-total: `side`/`timeInForce`/`status` map to `exchange::` enums via new converters in `include/exchange/binance/types.hpp`; response `type` stays a raw wire string because `LIMIT_MAKER` has no canonical value (decision 3) — this resolves the line above about "Binance-only types like STOP_LOSS_LIMIT": `STOP_LOSS_LIMIT` *is* canonical (`StopLossLimit`); only `LIMIT_MAKER` is genuinely unmappable. Canonical `TimeInForce` gained `FOK`, with Kraken's converter throwing on it (decision 4). Order quantities/prices in requests are caller-formatted decimal strings (decision 6). Fixtures in `tests/unit/binance_account_example_json.hpp` (synthetic — credential-gated endpoints, shapes verbatim from the appendix); new binaries `test_binance_types` + `test_binance_client` (signed GET/POST/DELETE round-trips with recomputed signatures) alongside extended request/response tests — full suite 211 → 255 green. No example program (per the testing-strategy appendix, examples are public-only). Out of scope per plan step-6-binance-rest-private decision 9: `omitZeroBalances`, strategy/STP request params, `cancelRestrictions`, full OCO rows, orderList endpoints.

### Step 7 — Binance WebSocket market streams

**Done when**: `BinanceStreamClient` subscribes to ticker and trade streams; unit-tested with `MockWsConnection`.

- Create `include/exchange/binance/ws_streams.hpp`:
  - `binance_stream_frame_descriptor(const json&) -> exchange::ws::FrameDescriptor` — the streams' `MessageIdentifier`, named per the `kraken_frame_descriptor` pattern (§A's naming note — this is *not* `identify_message`, which is Kraken's separate, optional, richer caller-facing classifier; Binance may or may not also want one of those, but the frame descriptor is the non-negotiable piece). Use the **combined-stream** endpoint (`wss://stream.binance.com/stream`) so multiple streams share one connection; every push frame is then wrapped as `{"stream":"btcusdt@aggTrade","data":{…}}`. The `route_key` is the `"stream"` value; the subscribe-ack `{"result":null,"id":N}` is a `MethodResponse` with `correlation_id = id`.
  - Push message types: `BinanceAggTradeEvent`, `BinanceTradeEvent`, `BinanceTickerEvent` (`24hrTicker`), `BinanceMiniTickerEvent`, `BinanceKlineEvent`, `BinanceBookTickerEvent`, `BinanceDepthUpdateEvent` (diff) + `BinancePartialDepth` (snapshot).
  - **Format specifics**: push payloads use **terse single-letter keys** (`e`=event, `E`=event-time-ms, `s`=symbol, `p`=price, `q`=qty, `k`=kline-object, …) — `from_json` maps these explicitly. Numbers are strings; times are int-ms. Kline data is nested under `k`.
  - `TypedStreamSubscribeRequest<PushMsg>` (§A2) — stores the `"<symbol>@<stream>"` string; `route_key()` returns it; `to_json()`/`unsubscribe_json()` emit `{"method":"SUBSCRIBE|UNSUBSCRIBE","params":[stream],"id":req_id}`. `response_type = BinanceStreamAck` (parses `{"result":null,"id":N}`; `make_ws_response` derives `ok` from result-present / no-error). Per-stream aliases: `BinanceAggTradeSubscribe`, `BinanceTradeSubscribe`, `BinanceKlineSubscribe`, etc.
  - The subscribe ack carries **no stream echo**, so `SubscriptionHandle` remembers the `route_key` from the request — the generic client already supports this (the handle stores its own key), so no client change is needed.
- `make_binance_stream_client(url)` factory — one-liner over the common `make_exchange_ws_client(url, binance_stream_frame_descriptor)`, mirroring `make_kraken_ws_client`'s shape exactly (including the `error_handler` parameter — see §A's corrected code sample). No new transport or reconnect code: `IxWsConnection` and `WsReconnectSession` are reused unchanged from `exchange/common/` (see §E). ixwebsocket auto-pongs Binance's 20 s server pings; `WsReconnectSession` handles the mandatory ~24 h reconnect, with the resubscribe set supplied by the caller's `ConnectFn`.
- Create `tests/unit/binance_ws_stream_example_json.hpp` — captured push frames + subscribe ack (the direct analog of `ws_client_example_json.hpp`).
- Create `tests/unit/test_binance_ws_client.cpp` — `binance_stream_frame_descriptor` dispatch tests (`FrameDescriptor::kind`/`correlation_id`/`route_key` assertions, one per event type — the `kraken_frame_descriptor` test pattern), `from_json` field assertions, and `MockWsConnection` subscribe-lifecycle tests (fire_open → subscribe ack by id → inject push frame → callback fires → cancel).
- Add `tests/examples/binance/binance_ws_client_example.cpp` — the direct analog of `tests/examples/kraken/ws_client_example.cpp`: a CLI11 app with one subcommand per stream (`aggtrade <symbol>`, `trade <symbol>`, `kline <symbol> --interval 1m`, `ticker <symbol>`, `miniticker <symbol>`, `bookticker <symbol>`, `depth <symbol> [--levels N]`), each `run_*()` creating a client via `make_binance_stream_client(STREAM_URL)`, subscribing with the typed `TypedStreamSubscribeRequest` + a push callback that logs frames, then unsubscribing via the handle. Mirror the Kraken example's **connection-reuse demo** with the Binance-natural equivalent: subscribe to *several streams on one client* over the single combined-stream connection (e.g. `aggTrade` + `bookTicker` for the same symbol), showing multiple active `SubscriptionHandle`s sharing one socket. Public streams only — no credentials. Links `binance ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward`.
- **Tests**: All unit tests pass.

**Done**: implemented via [plan step-7-binance-ws-streams](005-step-7-binance-ws-streams.md) in five checkpoint commits (7.1–7.5: `7d461fc`, `5409a69`, `da8eda5`, `b833587`, `195d482`) with **zero changes to the generic `ExchangeWsClient`**. Everything lives in one header-only `include/exchange/binance/ws_streams.hpp`, namespace `exchange::binance::ws` (plan step-7-binance-ws-streams decision 1): `binance_stream_frame_descriptor` (`"stream"` key → `PushMessage` routed by stream name; `"id"`+`result`/`error` → `MethodResponse` with stringified `correlation_id`, matching the client's `std::to_string(req_id)` pending-key), `BinanceStreamAck : exchange::ws::BaseWsResponse` (decision 3), all 9 event types with explicit terse-key → named-member `from_json` (decision 9), 8 stream-name helpers owning the ASCII lowercasing (decision 7), `TypedStreamSubscribeRequest<PushMsg>` + 8 per-stream aliases, and the conn-based `make_binance_stream_client` factory mirroring Kraken's (decision 2). Every event `from_json` unwraps the combined-stream `{"stream","data"}` envelope via `detail::stream_payload`, accepting wrapped and bare frames alike (decision 4) — needed because the generic client hands the *whole* frame to push callbacks. Dispatch is descriptor-only — no Binance `identify_message`/`MessageKind` (decision 5); one stream per SUBSCRIBE so the ack's missing stream echo can't desynchronise routing (decision 6). The one non-additive touch: `BinanceBookLevel` hoisted from `rest_api.hpp` to `types.hpp` with a `using` re-export, REST depth tests as the regression net (decision 8). **Build note**: the generic `ExchangeWsClient` implementation (`src/exchange/common/ws_client.cpp`) is compiled into `libkraken.a`, not `libbinance.a`, so `test_binance_ws_client` and `binance_ws_client_example` link `binance cryptocogs` (commented at both sites — no Kraken-specific code is referenced); Step 9's `exchange_common` extraction is the proper fix. Fixtures in `tests/unit/binance_ws_stream_example_json.hpp`; `MockWsConnection` extracted to `tests/unit/mock_ws_connection.hpp` (shared with the Kraken suite, verbatim); 22 new tests in `test_binance_ws_client.cpp` (descriptor, ack, events, request frames, helpers, and 4 mock-connection lifecycle tests incl. error-ack-installs-no-callback, pre-open queuing, and two streams on one connection) — full suite 255 → 277 green. Example live-verified: all 8 subcommands exited 0 against `wss://stream.binance.com/stream`, and `multi` confirmed independent mid-run cancellation (bookTicker kept streaming after the aggTrade cancel) on a single socket. Out of scope per plan step-7-binance-ws-streams decision 10: user-data streams/listenKey, the WS API trading endpoint (Step 8), SUBSCRIBE batching, exotic-stream helpers (`!ticker@arr`, `@depth@100ms` — reachable via raw strings), `LIST_SUBSCRIPTIONS`/`SET_PROPERTY`, a wired reconnect/resubscribe demo, and ping/pong handling (ixwebsocket auto-pongs).

### Step 8 — Binance WebSocket API (bidirectional trading)

**Done when**: `BinanceWsClient` can place and cancel orders over WebSocket.

- Create `include/exchange/binance/ws_api.hpp`:
  - `BinanceWsCredentials` — per-request signing: `apiKey` + `signature` + `timestamp` inside `params` (the logon-session flow is deferred). Reuses `BinanceAuth`'s HMAC-SHA256 over the sorted `params`.
  - Request/response pairs, each inheriting `exchange::ws::TypedWsRequest<R>` (§A2) with its own `to_json()` rendering `req_id` as the top-level `"id"`: `BinanceWsNewOrderRequest → BinanceWsNewOrderResponse` (`method:"order.place"`), `BinanceWsCancelOrderRequest → BinanceWsCancelOrderResponse` (`method:"order.cancel"`), `BinanceWsPingRequest → BinanceWsPongMessage` (`method:"ping"`).
  - **Format specifics**: request is `{"id":"<uuid|int>","method":"…","params":{…}}`; success reply `{"id":…,"status":200,"result":{…},"rateLimits":[…]}`; error reply `{"id":…,"status":400,"error":{"code":-2010,"msg":"…"}}`. `binance_ws_api_frame_descriptor` (the WS-API `MessageIdentifier`, named per §A's `kraken_frame_descriptor` pattern) classifies every reply as a `MethodResponse` with `correlation_id = stringify(id)` — there is no `channel`/push concept on the WS API endpoint. `ok` is derived from `status < 400`; on error, populate `WsResponse::error` from `error.msg`.
  - The `id` is generated as an `int64_t` and stringified (the generic client's `correlation_id` is already a string), matching the Kraken adapter's approach.
- `make_binance_ws_api_client(url)` factory (endpoint `wss://ws-api.binance.com/ws-api/v3`) — again a one-liner over the common `make_exchange_ws_client`; transport/reconnect reused from `exchange/common/` (§E).
- Create `tests/unit/binance_ws_api_example_json.hpp` fixtures (success + error replies) and add `binance_ws_api_frame_descriptor` + `from_json` + `MockWsConnection` execute-lifecycle tests to `test_binance_ws_client.cpp`.
- **Tests**: All unit tests pass.

**Done**: implemented via [plan step-8-binance-ws-api](006-step-8-binance-ws-api.md) in five checkpoint commits (8.1–8.5: `398d2a8`, `0201b3f`, `d9592c7`, `b3504b0`, `480eba6`) with **zero changes to the generic `ExchangeWsClient`** — the entire WS API surface rides its existing `execute`/`execute_async` path. Everything lives in one header-only `include/exchange/binance/ws_api.hpp`, namespace `exchange::binance::ws` (plan step-8-binance-ws-api decision 1) — a sibling of `ws_streams.hpp`, co-includable since the shared re-export `using`-declarations are identical. `binance_ws_api_frame_descriptor` (the WS-API `MessageIdentifier`) classifies *every* reply as a `MethodResponse` — there is no `channel`/push concept on this endpoint — with `correlation_id = stringify(id)` (string ids pass through verbatim, int ids via `std::to_string`, matching the client's `std::to_string(req_id)` pending-key); a null/absent id is `Unknown` (decision 10). `BinanceWsApiResponse : exchange::ws::BaseWsResponse` is the shared envelope (`status`, optional `id`, optional `error_code`, parsed `rate_limits`); `detail::parse_ws_api_envelope` derives `success = !error && status < 400`, so the generic `make_ws_response` lights `WsResponse::ok` for free (decision 3). Per-request signing is `detail::ws_sign_params` (decision 7): it injects `apiKey`/`timestamp`/optional `recvWindow`, renders the payload from the *same* nlohmann object the wire frame is built from — alphabetical for free via nlohmann's `std::map` backing, pinned by a reverse-insertion test — with string values raw and non-strings via `dump()`, then HMAC-SHA256s it through the reused `rest::detail::hmac_sha256`/`to_hex` and writes back `signature` (excluded from its own payload). `BinanceWsCredentials` is a straight alias for `rest::BinanceCredentials` (decision 6); each request carries a `timestamp{0}` field (`0` → now at `to_json()` time) so tests assert exact signed frames without clock injection (decision 8), and the `id` stays *outside* `params` so the signature is independent of req_id assignment. The three method pairs — `BinanceWsPingRequest → BinanceWsPongMessage`, `BinanceWsNewOrderRequest → BinanceWsNewOrderResponse` (`order.place`), `BinanceWsCancelOrderRequest → BinanceWsCancelOrderResponse` (`order.cancel`) — reuse the Step 6 REST result structs (`rest::BinanceNewOrderResponse`/`BinanceCancelOrderResponse`) as their `result` payloads (decision 5), and the order requests mirror the REST field sets (caller-formatted decimal strings, same enum converters). `BinanceWsApiClient` is the `ExchangeWsClient` alias and `make_binance_ws_api_client(conn, eh)` the conn-based factory, mirroring `make_binance_stream_client` (decision 2). **Build note**: identical to Step 7 — the generic `ExchangeWsClient` impl lives in `libkraken.a`, so `test_binance_ws_client` and `binance_ws_api_example` link `binance cryptocogs` (commented at both sites); Step 9's `exchange_common` extraction is the proper fix. Fixtures in `tests/unit/binance_ws_api_example_json.hpp` (ping synthetic; `order.place` success/error verbatim from the appendix §4; `order.cancel` synthetic — the §2 DELETE shape inside the §4 envelope); 23 new tests in `test_binance_ws_client.cpp` (4 descriptor, 3 envelope, 1 ping frame, 4 signing, 3 `order.place`, 3 `order.cancel`, and 5 `MockWsConnection` execute-lifecycle: ping/order round-trips, error-resolves-no-hang, pre-open queuing, and the blocking-path timeout) — full suite 277 → 300 green. The ping example (`tests/examples/binance/binance_ws_api_example.cpp`, decision 11 — ping is public, no credentials) live-verified: exit 0 against `wss://ws-api.binance.com/ws-api/v3` with `status 200` and a parsed `REQUEST_WEIGHT` rate limit. **Known limitation** (flagged in plan step-8-binance-ws-api self-review): Binance publishes no official worked signature vector for the sorted-`params` scheme, so the signing tests recompute the expectation with the same primitives — they pin the *construction*, not the rule; the live `order.place`/`order.cancel` calls (outside this plan's scope) are the true validation, with a testnet smoke run the natural follow-up. Out of scope per plan step-8-binance-ws-api decision 12: the `session.logon`/Ed25519 session-auth flow, `order.test`, smart order routing (`sor.*`), OCO/`orderList`, `account.status`/`myTrades` over WS, user-data streams, `returnRateLimits` toggling, a testnet target, and a wired reconnect demo.

### Step 9 — CMake, build validation, final cleanup

**Done when**: Full build from clean with all tests passing; examples compile; CI-equivalent local check.

- **`top-level CMakeLists.txt`**:
  - Add the four `option()` declarations and dependency-rule guards from §F.
  - Ensure `add_subdirectory(src)` is present (the existing `add_subdirectory(tests)` is unchanged and picks up the exchange guards from there).
  - The `find_package(OpenSSL REQUIRED)` and `find_package(CURL REQUIRED)` calls stay unconditional — both exchange libs need them, and CMake's package cache makes duplicate `find_package` free.
- **`src/CMakeLists.txt`** — replace the existing single `cryptocogs` library definition with the three-target structure from §F:
  - `exchange_common` **STATIC** target (always present — see §F's correction: it compiles `exchange/common/ws_client.cpp`, the one piece of exchange-agnostic non-template `ExchangeWsClient` code, and provides the `include/exchange/common/` headers to all dependents via `$<BUILD_INTERFACE:…>`). This is a genuine *extraction*: that file is compiled directly into `cryptocogs` today (check `src/CMakeLists.txt` — its `add_library(cryptocogs STATIC …)` already lists `exchange/common/ws_client.cpp`); Step 9 is what moves it out.
  - `cryptocogs` STATIC target inside `if(CRYPTOCOGS_BUILD_KRAKEN)` — **sheds** `exchange/common/ws_client.cpp` (now supplied via the `exchange_common PUBLIC` link instead) and keeps just its own `kraken/rest_client.cpp` + `kraken/types.cpp`. There is no `kraken/ws_client.cpp` to rename — `KrakenWsClient` is a bare `using` alias for `ExchangeWsClient` with an `inline` factory, contributing zero non-template code (see the corrected proposed layout). OpenSSL and libcurl stay `PRIVATE`.
  - `binance` STATIC target inside `if(CRYPTOCOGS_BUILD_BINANCE)`, using `binance/rest_client.cpp` — and `binance/ws_client.cpp` *only if* Steps 6/7 turn out to need genuinely non-template Binance WS code. Default expectation, by analogy with Kraken, is that they won't (both factories should be `inline` one-liners over `make_exchange_ws_client`). Same `PUBLIC exchange_common` / `PRIVATE` OpenSSL+libcurl link pattern as `cryptocogs`.
  - Neither exchange library links the other — they are peers that both depend on `exchange_common`.
- **`tests/CMakeLists.txt`** — restructure test and example targets to use the nested guard layout from §F:
  - Move all existing Kraken test and example targets inside `if(CRYPTOCOGS_BUILD_KRAKEN)`. No logic changes — just wrap the existing definitions.
  - Move compat test targets (`test_compat_shim`, `tests/compat/` compile-proof) inside a further `if(CRYPTOCOGS_BUILD_COMPAT_SHIM)` within the Kraken block.
  - Add Binance test and example targets inside `if(CRYPTOCOGS_BUILD_BINANCE)`:
    - `binance_unit_tests` executable: `test_binance_auth.cpp`, `test_binance_rest_requests.cpp`, `test_binance_rest_responses.cpp`, `test_binance_ws_client.cpp` — links `cryptocogs::binance GTest::gtest_main`.
    - `binance_rest_client_example` (from `tests/examples/binance/`) — links `cryptocogs::binance spdlog::spdlog CLI11::CLI11`.
    - `binance_ws_client_example` (from `tests/examples/binance/`) — links `cryptocogs::binance ixwebsocket spdlog::spdlog CLI11::CLI11`.
    - Register both `binance_unit_tests` with `add_test()` so `ctest` picks them up.
  - FetchContent for spdlog and GTest stays at the top of the `if(CRYPTOCOGS_BUILD_TESTS)` block, unconditional but guarded with `if(NOT TARGET …)` checks to survive re-configure.
- Verify configure-time guard output: `cmake -B build -DCRYPTOCOGS_BUILD_BINANCE=OFF` should build only the Kraken library and tests, with no Binance targets present; `-DCRYPTOCOGS_BUILD_KRAKEN=OFF -DCRYPTOCOGS_BUILD_BINANCE=OFF` should emit the "nothing will be built" warning.
- Remove any dead code or stale comments from the refactor.
- Run `ctest --output-on-failure` — all tests pass.
- Verify **all** examples compile against the restructured headers (both Kraken and the two new Binance examples).
- Update `CLAUDE.md` to reflect new namespace layout, file structure, and patterns.
- Update `README.md` and link the migration guide ([001-appendix-migration-guide.md](001-appendix-migration-guide.md)) from the release notes so existing callers find it. Confirm the `exchange::kraken::*` re-exports from Step 2 are present so the guide's compatibility shim actually compiles.

**Done**: implemented via [plan step-9-cmake-build-validation](007-step-9-cmake-build-validation.md) in three checkpoint commits (9.1–9.3: `9e73dc1`, `28179fd`, `edf4081`). The generic `ExchangeWsClient` implementation (`src/exchange/common/ws_client.cpp`) is now a standalone `exchange_common` STATIC library (alias `cryptocogs::common`; PUBLIC `nlohmann_json` + the `include/` tree, and notably *not* OpenSSL/libcurl) — a genuine extraction, `nm`-verified: all 161 `ExchangeWsClient` symbols moved into `libexchange_common.a` and are now absent from `libkraken.a`. `cryptocogs` (`kraken/{rest_client,types}.cpp`) and `binance` (`binance/rest_client.cpp`) are **peers**, each linking `exchange_common PUBLIC`; neither links the other. The four `option()` toggles (`KRAKEN`/`BINANCE`/`TESTS`/`COMPAT_SHIM`, all default `ON`) plus the two configure-time dependency guards (compat-shim-requires-Kraken → warn + force OFF; both-exchanges-off → "nothing will be built") are wired per §F. The three Binance WS targets that previously borrowed `cryptocogs` for the generic impl — `test_binance_ws_client`, `binance_ws_client_example`, `binance_ws_api_example` — now link `binance` only, reaching the engine transitively through `exchange_common`; that false dependency is gone. **One §F correction (plan step-9-cmake-build-validation decision 4, made during 9.1)**: OpenSSL/libcurl stay **PUBLIC**, not PRIVATE — §F assumed the crypto lived in a `.cpp`, but `auth.hpp` defines the HMAC/SHA helpers inline in a public header against libcrypto and `rest_client.hpp` `#include`s `<curl/curl.h>`, so they are genuine public compile/link requirements (PRIVATE broke `private_rest.cpp`); `OpenSSL::Crypto` is now named explicitly. All test and example targets were wrapped in their `CRYPTOCOGS_BUILD_KRAKEN`/`_BINANCE` guards across `tests/CMakeLists.txt` + `tests/unit/CMakeLists.txt` (the legacy `kapi` reference lib moved inside the Kraken guard; `test_ws_reconnect_session` left untouched — it is header-only and links no exchange lib). Validation was a six-cell configure/build/`ctest` flag matrix run in throwaway build dirs: cell 3 (`-DCRYPTOCOGS_BUILD_KRAKEN=OFF`) is the headline proof — a Binance-only tree builds `libbinance.a` + `libexchange_common.a` with **no `libkraken.a`** present and all 129 Binance tests green; cell 2 (`BINANCE=OFF`) gives 129+171 = the full 300; cells 4–6 confirmed the two warnings and the `TESTS=OFF` library-only build. The full 300-test suite is unchanged and green at every checkpoint (no library source was modified — only archive membership and link edges). `CLAUDE.md` (build-options/outputs tables, the three-library conventions paragraph, the `src/` tree) and `README.md` (a build-targets/options section + a migration-guide link for pre-refactor callers) were updated; the `kraken_*.hpp` shim headers and `exchange::kraken::*` re-exports were confirmed present. Out of scope (tracked elsewhere): the compat-shim install/compile-proof wiring ([plan step-2b-compat-shim](002-step-2b-compat-shim.md), still Draft — Step 9 only makes the option's guard correct), `install()`/packaging, moving the inline crypto into a `.cpp` to make OpenSSL genuinely PRIVATE, and the Step 10 onboarding guide.

### Step 10 — Write the agent onboarding guide for new exchanges

**Prerequisite**: Step 9 complete — Binance is a working, tested reference implementation with a clean per-step commit history on the feature branch.

**Done when**: `docs/agent-add-exchange.md` exists and is verified against the Binance adapter: every checklist item has a concrete Binance counterpart that can be pointed to as a working example, and the reference diffs cited in the guide resolve to real commits on the feature branch.

The guide is a self-contained playbook handed to a Claude agent at the start of a new exchange integration. It must require no prior context beyond what the user supplies and the existing Kraken and Binance adapters in the repo. Where the guide says "follow this pattern", it cites a specific file path and, where the change is non-obvious, a commit hash from the feature branch that introduced it.

#### Structure of `docs/agent-add-exchange.md`

**1 — Inputs to collect from the user before starting**

The agent must ask for all of the following before writing any code:

| Input | What to ask for |
|---|---|
| Exchange name + namespace slug | e.g. `coinbase` → `exchange::coinbase::` and `src/coinbase/` |
| Auth documentation | Signing algorithm (HMAC / RSA / Ed25519), header names, nonce or timestamp scheme, exact signing payload format (what string is signed and how) |
| REST base URL | e.g. `https://api.exchange.com` |
| REST endpoint list | At least one public and one private endpoint, each with a sample request and the raw JSON response |
| WebSocket URL(s) | One or both of: market-data stream URL, trading API URL |
| WebSocket connection model | Single stream vs. combined streams; subscribe/unsubscribe wire format; correlation field name (`id`, `req_id`, etc.) |
| WebSocket channel list | Each channel with at least one captured push frame (the raw JSON as the server sends it) |
| Any exchange quirks | Non-standard error shapes, mandatory keepalives, reconnect requirements, per-request vs. session auth |

**2 — Implementation checklist**

Each item lists the files to create and the Binance file to use as the reference pattern.

| # | What to implement | Files | Binance reference |
|---|---|---|---|
| 1 | Auth | `include/exchange/<name>/auth.hpp`, `src/<name>/auth.cpp` (if non-trivial) | `exchange/binance/auth.hpp`, `src/binance/rest_client.cpp` |
| 2 | Exchange-specific types | `include/exchange/<name>/types.hpp` | `exchange/binance/types.hpp` |
| 3 | REST request/response structs | `include/exchange/<name>/rest_api.hpp` | `exchange/binance/rest_api.hpp` |
| 4 | REST client | `include/exchange/<name>/rest_client.hpp`, `src/<name>/rest_client.cpp` | `exchange/binance/rest_client.hpp`, `src/binance/rest_client.cpp` |
| 5 | REST fixture header + unit tests | `tests/unit/<name>_rest_example_json.hpp`, `test_<name>_auth.cpp`, `test_<name>_rest_requests.cpp`, `test_<name>_rest_responses.cpp` | `binance_rest_example_json.hpp`, `test_binance_auth.cpp`, `test_binance_rest_*.cpp` |
| 6 | WS request/response structs + `<name>_frame_descriptor` (the `MessageIdentifier` `ExchangeWsClient` requires; `identify_message`/`MessageKind` is an *optional* richer caller-facing classifier layered on top for code that bypasses the typed client — Kraken provides one, but it is not a hard requirement; see §A's naming note) | `include/exchange/<name>/ws_api.hpp` (and `ws_streams.hpp` if the exchange has separate stream and trading WS protocols) | `exchange/kraken/ws_api.hpp` (`kraken_frame_descriptor` + `identify_message` — the canonical naming reference), `exchange/binance/ws_api.hpp`, `exchange/binance/ws_streams.hpp` |
| 7 | WS client header + factory | `include/exchange/<name>/ws_client.hpp` | `exchange/binance/ws_client.hpp` |
| 8 | WS source | `src/<name>/ws_client.cpp` (non-template methods, if any) | `src/binance/ws_client.cpp` |
| 9 | WS fixture header + unit tests | `tests/unit/<name>_ws_example_json.hpp`, `test_<name>_ws_client.cpp` | `binance_ws_stream_example_json.hpp`, `test_binance_ws_client.cpp` |
| 10 | CMake wiring | `CRYPTOCOGS_BUILD_<NAME>` option in `CMakeLists.txt`; library target in `src/CMakeLists.txt`; test and example targets in `tests/CMakeLists.txt` — following the §F pattern | Binance blocks in each `CMakeLists.txt` |
| 11 | REST CLI example | `tests/examples/<name>/rest_client_example.cpp` | `tests/examples/binance/binance_rest_client_example.cpp` |
| 12 | WS CLI example | `tests/examples/<name>/ws_client_example.cpp` | `tests/examples/binance/binance_ws_client_example.cpp` |

**3 — Conventions to enforce**

The guide must remind the agent of the project-wide rules that apply to every new adapter (not obvious from context alone):

- File banner on every `.hpp`, `.inl`, `.cpp` (year, project name, MIT licence block — see `CLAUDE.md`).
- `CRYPTOCOGS_BUILD_<NAME>` option defaults to `ON`; add the compat-shim dependency rule if applicable.
- Numbers from REST responses arrive as JSON strings — use `std::stod(j.value("field", "0"))`, not `.get<double>()`.
- All optional fields are `std::optional<T>`; omitted fields must not be serialised in `to_json()`.
- Unit tests use `MockWsConnection` for all WS tests — no network access.
- Both CLI examples must compile when the exchange flag is `ON` and be absent from the build when it is `OFF`.
- `ctest --output-on-failure` must be green before declaring any checklist item done.

**4 — Done criteria**

The agent declares the integration complete only when:

1. `cmake -B build && cmake --build build` succeeds from a clean directory with all exchange flags `ON`.
2. `ctest --output-on-failure` passes every test, including the new exchange's auth, REST, and WS suites.
3. Both CLI examples (`rest_client_example`, `ws_client_example`) run against the live exchange and produce parsed output (no credentials needed for public endpoints).
4. `cmake -B build -DCRYPTOCOGS_BUILD_<NAME>=OFF && cmake --build build` succeeds with no trace of the new exchange's targets.

**Done**: implemented via [plan step-10-agent-onboarding-guide](008-step-10-agent-onboarding-guide.md) in two checkpoint commits (10.1 `3dcccee`, 10.2 — this). The guide is [`docs/agent-add-exchange.md`](../agent-add-exchange.md): a self-contained, commit-pinned playbook with the four §10 sections (inputs · implementation checklist · conventions · done-criteria) plus a §0 build/test loop and a §2 architecture primer covering the load-bearing contracts an agent must satisfy — the `Typed{Public,Private}Request<R>` response binding, the `RestResponse<T>`/`parse_*_response` envelope, the canonical-enum re-export, and above all the `MessageIdentifier` (`<name>_frame_descriptor → FrameDescriptor`) the generic `ExchangeWsClient` needs versus the *optional* `identify_message`/`MessageKind`. Three corrections to §10's pre-Binance sketch were folded in and verified against the tree: **auth is header-only** (no `src/binance/auth.cpp`), **the WS client is header-only** (no `src/binance/ws_client.cpp` — bare `using` alias + inline factory), and the real fixture/test filenames (`binance_{rest,account,ws_stream,ws_api}_example_json.hpp`, `test_binance_{auth,types,rest_requests,rest_responses,client,ws_client}.cpp`). Every cited file path resolves and every cited commit hash (`b20dc35`, `4350ce6`, `145bf70`, `352efd0`, `c8e42c4`, `8b63fa9`, `7d461fc`, `b833587`, `398d2a8`, `d9592c7`, `9e73dc1`, `28179fd`, `13671f1`) `git cat-file`-resolves — mechanically checked in 10.2, including a spot-check that the load-bearing line numbers (`rest.hpp:77/87`, `rest_api.hpp:71`, `auth.hpp:91`, `types.hpp:47`) still point at the right declarations. The two paths that resolve as *absent* (`src/binance/{auth,ws_client}.cpp`) are cited precisely because the guide tells the agent they should not exist. Linked from `CLAUDE.md` (new "Adding a whole new exchange" pointer). The full 300-test suite is unchanged — this step added only documentation. **This completes plan multi-exchange-abstraction**: the common scaffold, the Kraken migration + compat shim, the full Binance adapter (REST public/private, WS streams, WS trading API), the three-library CMake split, and now the onboarding guide are all done.

---

## Self-Review — Risks, Assumptions, and Open Questions

### Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Breaking existing consumers of `kraken_*.hpp` headers | High (intentional) | This is a deliberate breaking change; callers update once. No legacy forwarding headers. |
| `ExchangeWsClient` template complexity makes errors harder to read | Medium | Keep `MessageIdentifier` as a runtime `std::function`, not a template param — avoids template error cascades. |
| `IRestAuth::sign(HttpRequest&)`'s single-mutation contract was designed to Binance's shape and never validated against Kraken's structurally different one (nonce embedded in *both* the signed digest and the wire body, across two different body encodings) | Medium | Step 3 retrofits Kraken onto `IRestAuth` *first*, with `test_signature.cpp`/`test_client.cpp` already pinning correct byte-for-byte output as a regression net — so a bad-fit interface is caught and reshaped while only one (well-understood) adapter depends on it, not after Binance's REST layer is built on top. |
| Binance HMAC-SHA256 signing: query + body concatenation differs subtly from Kraken | Medium | Step 4 includes a known-good test vector from Binance docs. |
| Binance RSA/Ed25519 signing omitted from plan | Low (intentional) | HMAC-SHA256 covers the common case; RSA/Ed25519 can be added later with the same `IRestAuth` extension point. |
| Binance WebSocket authentication: logon-flow vs. per-request signing | Medium | Step 8 implements per-request API-key approach first; logon session deferred. |
| `ExchangeWsClient` shares dispatch for both Kraken and Binance — bugs are shared | Low | The dispatch logic is the same for both; exchange-specific bugs are isolated to each adapter's `MessageIdentifier` function (`kraken_frame_descriptor` / `binance_frame_descriptor` — *not* `identify_message`, a separate optional caller-facing classifier; see §A's naming note). |
| Binance timestamp (`ms` vs `μs`) requires careful serialisation | Low | Wrap in `BinanceAuth::sign()` using milliseconds by default; add `microseconds` flag later. |

### Assumptions

1. The canonical `exchange::OrderType` enum covers all values both exchanges share (Limit, Market). Kraken-only types (Iceberg, TrailingStopLimit, SettlePosition) stay in `exchange::kraken::`. Binance-only types (e.g. STOP\_LOSS\_LIMIT, TAKE\_PROFIT\_LIMIT, TRAILING\_STOP\_MARKET) stay in `exchange::binance::`.
2. Binance `recvWindow` defaults to 5000 ms and is not exposed on every request struct — callers can set it on `BinanceCredentials`.
3. Binance user data streams (listen key mechanism) are out of scope for this plan; only named market streams and the WS API are covered.
4. Example programs link against ixwebsocket for real connections; unit tests remain fully mock-based.
5. Three CMake targets are produced: `cryptocogs::common` (**STATIC**, not header-only `INTERFACE` — see §F's correction; it compiles `exchange/common/ws_client.cpp`, the one piece of exchange-agnostic non-template `ExchangeWsClient` code — always present), `cryptocogs::kraken` (`libkraken.a`, gated on `CRYPTOCOGS_BUILD_KRAKEN`), and `cryptocogs::binance` (`libbinance.a`, gated on `CRYPTOCOGS_BUILD_BINANCE`). All flags default to `ON`. The repo name stays `cryptocogs` for now.

### Deferred items

**RSA and Ed25519 signing for Binance** — Binance supports three signature algorithms. HMAC-SHA256 is the most common and is implemented in Step 4. RSA (PKCS#8 private key, SHA-256 digest) and Ed25519 are alternatives that Binance recommends for higher-security or high-throughput use cases; Ed25519 is the fastest. Both are supported by the `IRestAuth` extension point already in the design — adding them later means implementing a new `BinanceAuth` subclass and wiring `BinanceSignAlgorithm::Rsa` / `::Ed25519` in `BinanceCredentials`. No structural changes required.

**Binance user data streams (listen key mechanism)** — Binance supports a real-time feed of account events (order fills, balance changes, position updates) delivered over WebSocket. Unlike market streams, access requires first calling a REST endpoint (`POST /api/v3/userDataStream`) to obtain a short-lived *listen key*, then connecting to a stream URL of the form `wss://stream.binance.com/ws/<listenKey>`. The key must be kept alive via periodic `PUT` pings (every 30 minutes) and can be closed with `DELETE`. This is structurally distinct from the WS API (bidirectional trading) and named market streams, and requires a small session-management wrapper around the listen key lifecycle. Deferred to a follow-on plan; the `IWsConnection` and `ExchangeWsClient` plumbing already supports it.
