# cryptocogs

A type-safe C++ library for the [Kraken](https://kraken.com) **and**
[Binance](https://binance.com) Spot REST and WebSocket APIs.

The compiler links each request type to its exact response type — no casts, no
stringly-typed keys. Both exchanges are built on one shared, exchange-agnostic
engine; each is an independent static library you can enable or disable at
configure time (`CRYPTOCOGS_BUILD_KRAKEN` / `CRYPTOCOGS_BUILD_BINANCE`, both `ON`
by default). Adding a third exchange follows a documented
[playbook](docs/agent-add-exchange.md).

---

## Quick start

```bash
# 1. Install system dependencies (Debian/Ubuntu)
sudo apt install libssl-dev libcurl4-openssl-dev

# 2. Clone and build
git clone https://github.com/tomlin256/cryptocogs.git
cd cryptocogs
cmake -B build && cmake --build build

# 3. Run an example — Kraken
./build/bin/rest_client_example time
./build/bin/rest_client_example ticker --pairs XXBTZUSD
./build/bin/ws_client_example ticker BTC/USD

# ...or Binance
./build/bin/binance_rest_client_example ticker --symbol BTCUSDT
./build/bin/binance_ws_client_example aggtrade BTCUSDT
```

All other dependencies (IXWebSocket, nlohmann/json, spdlog, Google Test) are fetched automatically by CMake.
Building needs a C++23 compiler and CMake 4.0 or later.

`build/` above is the tree this snippet makes, and the examples below run from it. A checkout that is built on more
than one platform, as the maintainers' is, keeps one tree per kind of build and platform under `build/` instead
(`build/release-macos-arm64`, `build/debug-linux-arm64`), named by `CMakePresets.json`: `cmake --list-presets` lists
them for your host, and `CLAUDE.md`'s "Build trees" says what the names mean.

---

## Examples

The fastest way to understand the library is to read and run the examples in [`tests/examples/`](tests/examples/). Each one is a complete, buildable program.

### `rest_client_example` — all public REST endpoints

```bash
./build/bin/rest_client_example time
./build/bin/rest_client_example status
./build/bin/rest_client_example assets   --assets XBT,ETH
./build/bin/rest_client_example pairs    --pairs XBTUSD,ETHUSD
./build/bin/rest_client_example ticker   --pairs XXBTZUSD,XETHZUSD
./build/bin/rest_client_example ohlc     XXBTZUSD --interval 60
./build/bin/rest_client_example depth    XXBTZUSD --count 10
./build/bin/rest_client_example trades   XXBTZUSD --count 5
```

Covers every public REST endpoint in a single binary. Each subcommand maps to one typed request/response pair — no raw JSON, no casts.
Source: [tests/examples/rest_client_example.cpp](tests/examples/rest_client_example.cpp)

```cpp
#include "exchange/kraken/rest_client.hpp"
#include "exchange/kraken/rest_api.hpp"

curl_global_init(CURL_GLOBAL_ALL);
exchange::kraken::rest::KrakenRestClient client;

// Server time
auto time_resp = client.execute(exchange::kraken::rest::GetServerTimeRequest{});
// time_resp is RestResponse<ServerTime>
if (time_resp.ok && time_resp.result)
    spdlog::info("unixtime={} rfc1123={}", time_resp.result->unixtime,
                 time_resp.result->rfc1123);

// OHLC candles
exchange::kraken::rest::GetOHLCRequest ohlc_req;
ohlc_req.pair     = "XXBTZUSD";
ohlc_req.interval = 60;  // 1-hour candles

auto ohlc_resp = client.execute(ohlc_req);  // RestResponse<OHLCResult>
if (ohlc_resp.ok && ohlc_resp.result)
    for (const auto& c : ohlc_resp.result->candles)
        spdlog::info("ts={} O={:.4f} H={:.4f} L={:.4f} C={:.4f}",
                     c.time, c.open, c.high, c.low, c.close);

// Order book
exchange::kraken::rest::GetOrderBookRequest depth_req;
depth_req.pair  = "XXBTZUSD";
depth_req.count = 10;

auto depth_resp = client.execute(depth_req);  // RestResponse<OrderBookResult>
if (depth_resp.ok && depth_resp.result) {
    for (const auto& ask : depth_resp.result->asks)
        spdlog::info("ask price={:.4f} vol={:.6f}", ask.price, ask.volume);
}

curl_global_cleanup();
```

---

### `public_rest` — fetch recent trades (no credentials)

```bash
./build/bin/public_rest
```

Calls `GET /0/public/Trades` and prints each trade's price, volume, and side.
Source: [tests/examples/public_rest.cpp](tests/examples/public_rest.cpp)

```cpp
#include "exchange/kraken/rest_client.hpp"
#include "exchange/kraken/rest_api.hpp"

curl_global_init(CURL_GLOBAL_ALL);
exchange::kraken::rest::KrakenRestClient client;

exchange::kraken::rest::GetRecentTradesRequest req;
req.pair = "XXBTZEUR";

auto resp = client.execute(req);  // RestResponse<RecentTradesResult>
if (resp.ok && resp.result) {
    for (const auto& t : resp.result->trades)
        spdlog::info("price={} volume={} side={}", t.price, t.volume,
                     t.side == exchange::kraken::Side::Buy ? "buy" : "sell");
}
curl_global_cleanup();
```

---

### `private_rest` — authenticated REST call

```bash
./build/bin/private_rest   # requires ~/.kraken/default
```

Loads credentials from `~/.kraken/default` and calls `POST /0/private/GetWebSocketsToken`.
Source: [tests/examples/private_rest.cpp](tests/examples/private_rest.cpp)

```cpp
#include "exchange/kraken/rest_client.hpp"
#include "exchange/kraken/rest_api.hpp"

exchange::kraken::rest::KrakenRestClient client;
auto creds = exchange::kraken::rest::Credentials::from_file("default");

auto resp = client.execute(exchange::kraken::rest::GetWebSocketsTokenRequest{}, creds);
if (resp.ok && resp.result)
    spdlog::info("token={} expires={}", resp.result->token, resp.result->expires);
```

---

### `ws_client_example` — typed WebSocket subscriptions via `KrakenWsClient`

```bash
./build/bin/ws_client_example ticker     BTC/USD
./build/bin/ws_client_example book       BTC/USD --depth 10
./build/bin/ws_client_example trade      ETH/USD
./build/bin/ws_client_example ohlc       BTC/USD --interval 5
./build/bin/ws_client_example instrument
```

Uses `KrakenWsClient` — the high-level WebSocket wrapper — to subscribe to any public channel. Demonstrates typed subscribe/unsubscribe and connection reuse.
Source: [tests/examples/ws_client_example.cpp](tests/examples/ws_client_example.cpp)

```cpp
#include "exchange/kraken/ws_client.hpp"
#include "exchange/common/ix_ws_connection.hpp"  // real ixwebsocket transport

auto client = exchange::ws::make_exchange_ws_client(std::string(exchange::kraken::ws::PUBLIC_WS_URL), exchange::kraken::ws::kraken_frame_descriptor);

exchange::kraken::ws::TickerSubscribeRequest req;
req.symbols = {"BTC/USD"};

auto [ack, handle] = client->subscribe(
    req,
    [](exchange::kraken::ws::TickerMessage msg) {
        spdlog::info("{} last={}", msg.data[0].symbol, msg.data[0].last);
    }
);

if (!ack.ok)
    spdlog::error("subscribe failed: {}", ack.error.value_or("unknown"));

std::this_thread::sleep_for(std::chrono::seconds(10));
handle.cancel();  // unsubscribe; safe to call from any thread
```

---

### `public_ws` — low-level WebSocket (raw ixwebsocket)

```bash
./build/bin/public_ws BTC/EUR
```

Builds and sends a `SubscribeRequest` manually using raw ixwebsocket callbacks, then dispatches inbound frames with `identify_message()`. Useful if you already have a WebSocket transport layer.
Source: [tests/examples/public_ws.cpp](tests/examples/public_ws.cpp)

```cpp
#include "exchange/kraken/ws_api.hpp"

// On WebSocket open:
exchange::kraken::ws::SubscribeRequest req;
req.channel = exchange::kraken::ws::SubscribeChannel::Ticker;
req.symbols = {"BTC/USD"};
webSocket.send(req.to_json().dump());

// On each inbound message:
auto j = nlohmann::json::parse(msg->str);
switch (exchange::kraken::ws::identify_message(j)) {
    case exchange::kraken::ws::MessageKind::Ticker: {
        auto m = exchange::kraken::ws::TickerMessage::from_json(j);
        spdlog::info("{} last={}", m.data[0].symbol, m.data[0].last);
        break;
    }
    default: break;
}
```

---

### `private_ws` — authenticated WebSocket (balances stream)

```bash
./build/bin/private_ws   # requires ~/.kraken/default
```

Fetches a WebSocket token via REST, connects to the private endpoint, and subscribes to the `Balances` channel.
Source: [tests/examples/private_ws.cpp](tests/examples/private_ws.cpp)

```cpp
// 1. Get token via REST
auto resp = client.execute(exchange::kraken::rest::GetWebSocketsTokenRequest{}, creds);
std::string token = resp.result->token;

// 2. Subscribe to private channel
exchange::kraken::ws::SubscribeRequest req;
req.channel = exchange::kraken::ws::SubscribeChannel::Balances;
req.token   = token;
webSocket.send(req.to_json().dump());

// 3. Dispatch inbound messages
case exchange::kraken::ws::MessageKind::Balances: {
    auto m = exchange::kraken::ws::BalancesMessage::from_json(j);
    // m.data contains balance updates per asset
    break;
}
```

---

### `kraken_example` — REST + WebSocket combined demo

Source: [tests/examples/kraken_example.cpp](tests/examples/kraken_example.cpp)

Shows how the REST and WebSocket layers share types from `exchange/kraken/types.hpp` — the same `OrderParams`, `Side`, and `OrderType` enums are used for placing orders on both transports.

---

## Binance

The Binance adapter (`exchange::binance::*`, library `cryptocogs::binance`)
covers Spot **REST** (public market data + private account/trading), **WebSocket
market streams**, and a bidirectional **WebSocket trading API** — all on the same
typed engine as Kraken. Three example programs (all public, no credentials):

```bash
# Every public REST endpoint (try --help)
./build/bin/binance_rest_client_example ticker --symbol BTCUSDT
./build/bin/binance_rest_client_example book BTCUSDT --limit 10

# Market-data streams — one of 8 subcommands (aggtrade/trade/kline/ticker/…)
./build/bin/binance_ws_client_example aggtrade BTCUSDT
./build/bin/binance_ws_client_example multi BTCUSDT     # aggTrade + bookTicker, one socket

# Trading WebSocket API heartbeat
./build/bin/binance_ws_api_example ping
```

**REST** — same `execute(req)` / `execute(req, creds)` pattern as Kraken:

```cpp
#include "exchange/binance/rest_client.hpp"
#include "exchange/binance/rest_api.hpp"
using namespace exchange::binance::rest;

curl_global_init(CURL_GLOBAL_ALL);
BinanceRestClient client;                          // https://api.binance.com
auto resp = client.execute(BinanceServerTimeRequest{});
if (resp.ok) { /* resp.result->server_time */ }
// Private: client.execute(BinanceAccountRequest{}, BinanceCredentials{key, secret});
```

**WebSocket streams** — subscribe via `BinanceStreamClient`:

```cpp
#include "exchange/binance/ws_streams.hpp"
#include "exchange/common/ix_ws_connection.hpp"
using namespace exchange::binance::ws;

auto client = exchange::ws::make_exchange_ws_client(
    std::string(STREAM_URL), binance_stream_frame_descriptor);

BinanceAggTradeSubscribe req;
req.stream = agg_trade_stream("BTCUSDT");
auto [ack, handle] = client->subscribe(req, [](const BinanceAggTradeEvent& e) {
    /* e.symbol, e.price, e.qty, … */
});
```

The full Binance type/endpoint/channel reference — including the trading WS API
(`order.place` / `order.cancel`) and how its per-request signing works — is in the
comment block at the top of each header in [`include/exchange/binance/`](include/exchange/binance/)
(`auth.hpp`, `rest_api.hpp`, `ws_streams.hpp`, `ws_api.hpp`); the captured wire formats are in
[docs/plans/appendix-binance-message-formats.md](docs/plans/appendix-binance-message-formats.md).

---

## Building your own project

The build produces three peer static libraries: `exchange_common` (the generic
WebSocket-client engine, always built) and two adapters that each link it,
`cryptocogs::kraken` (Kraken) and `cryptocogs::binance` (Binance). Link
whichever adapter(s) you use — each one transitively pulls in `exchange_common`,
OpenSSL, and libcurl, so you do not name those yourself.

| CMake option | Default | Effect |
|---|---|---|
| `CRYPTOCOGS_BUILD_KRAKEN` | `ON` | Build the Kraken adapter + its tests/examples |
| `CRYPTOCOGS_BUILD_BINANCE` | `ON` | Build the Binance adapter + its tests/examples |
| `CRYPTOCOGS_BUILD_TESTS` | `ON` | Build unit tests and example programs |
| `CRYPTOCOGS_WARNINGS_AS_ERRORS` | `ON` (top level only) | `-Werror` on cryptocogs's own targets, on top of `-Wall -Wextra -Wpedantic`; a project that fetches cryptocogs keeps its own flags |

The two exchange flags are independent — `-DCRYPTOCOGS_BUILD_KRAKEN=OFF` builds a
Binance-only tree, and vice versa.

A build of cryptocogs itself is strict: its own targets compile with `-Wall -Wextra -Wpedantic` and `-Werror` on GCC and Clang, and
none of it reaches a dependency or a project that fetches it. `-DCRYPTOCOGS_WARNINGS_AS_ERRORS=OFF` takes `-Werror` off, for a build
where a newer compiler's new warning would otherwise stop you before it is fixed. A build tree keeps the value it was configured
with; `cmake --preset <tree> -UCRYPTOCOGS_WARNINGS_AS_ERRORS` sets it back to the default.

### CMake (FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(cryptocogs
    GIT_REPOSITORY https://github.com/tomlin256/cryptocogs.git
    GIT_TAG        v2.1.0
)
FetchContent_MakeAvailable(cryptocogs)

target_link_libraries(my_app PRIVATE cryptocogs::kraken)   # and/or cryptocogs::binance
```

v1.0.0 is the first release that is C++23 and needs CMake 4.0; v0.6.0 and the tags before it are C++17 and CMake 3.15.
v2.0.0 gave the 14 public enums an underlying type of `std::uint8_t`, which changes the layout of the structs that hold
them: moving from 1.x, rebuild everything that includes the headers.

The library is C++23. Linking a `cryptocogs::` target asks CMake for `cxx_std_23`, so your target is compiled as C++23
(a higher standard of your own is left alone), and a header included below C++23 stops with `cryptocogs requires C++23`;
a build outside CMake must pass `-std=c++23` itself. Adding the library with `FetchContent` needs CMake 4.0 or later in
your project too, because the library's own `CMakeLists.txt` asks for it.

### Installing & `find_package`

```bash
cmake -B build -DCRYPTOCOGS_BUILD_TESTS=OFF
cmake --build build
cmake --install build --prefix /your/prefix
```

This installs the four static libraries, the public headers, and a CMake package
config. A downstream project then just:

```cmake
find_package(cryptocogs 2 REQUIRED)      # add -DCMAKE_PREFIX_PATH=/your/prefix
target_link_libraries(my_app PRIVATE cryptocogs::kraken)   # and/or cryptocogs::binance
```

`find_package(cryptocogs 2)` accepts any 2.y.z release and nothing older or newer (`SameMajorVersion`), so a project
that still asks for 1 is refused rather than handed a library whose public enums, and the structs that hold them, are
laid out differently.

The install is **self-contained**: OpenSSL and libcurl are resolved via
`find_dependency`, and the header-only `nlohmann_json` is vendored into the
prefix — a consumer needs no separate `nlohmann_json` (if you already use your
own, note that the bundled copy sits on cryptocogs's include path). `IxWsConnection`
(the real WebSocket transport) still needs `ixwebsocket` linked separately, as it
is not part of the installed package. The install rules are gated by
`CRYPTOCOGS_INSTALL` (on for a top-level build, off when cryptocogs is pulled in via
`FetchContent`).

An installed package does not need CMake 4: it asks for `cxx_std_23` and nothing newer, and CMake 3.31.12 configured,
built and ran a consumer against one (compiling it as `-std=gnu++2b`, CMake 3's spelling of C++23). `cxx_std_23` needs
CMake 3.20 or later, and a release that knows your compiler's C++23 flag: CMake 3.20.6 does not know Apple Clang 21's.

### Linker flags (manual)

```
-L/path/to/cryptocogs/build/src -lkraken -lexchange_common -lexchange_http -lcurl -lssl -lcrypto
```

For WebSocket support, also link against ixwebsocket:

```
-lixwebsocket -lz
```

> **Upgrading from a pre-`exchange::`-refactor checkout?** The deprecated
> `kraken_*.hpp` headers and the `kraken::` namespace shim were **removed in
> v0.1.1**. Use the `exchange::kraken::*` surface shown throughout this README;
> the [migration guide](docs/plans/appendix-migration-guide.md) maps every
> old name to its replacement.

---

## Usage patterns

### Credentials

```cpp
// From ~/.kraken/default (line 1: api key, line 2: base64 secret)
auto creds = exchange::kraken::rest::Credentials::from_file("default");

// Inline
exchange::kraken::rest::Credentials creds{ .api_key = "...", .api_secret = "..." };
```

### REST — public endpoint

```cpp
curl_global_init(CURL_GLOBAL_ALL);
exchange::kraken::rest::KrakenRestClient client;

exchange::kraken::rest::GetTickerRequest req;
req.pair = "XXBTZUSD";

auto resp = client.execute(req);   // RestResponse<TickerResult>
if (resp.ok && resp.result) {
    // use resp.result->...
}
curl_global_cleanup();
```

### REST — private endpoint

```cpp
auto resp = client.execute(exchange::kraken::rest::GetAccountBalanceRequest{}, creds);
// resp is RestResponse<AccountBalanceResult>
```

Always check `resp.ok` before dereferencing `resp.result`:

```cpp
if (resp.ok && resp.result) {
    // safe to use resp.result->...
} else {
    for (const auto& e : resp.errors)
        spdlog::error("{}", e);
}
```

### REST — placing an order

```cpp
exchange::kraken::rest::AddOrderRequest req;
req.params.order_type  = exchange::kraken::OrderType::Limit;
req.params.side        = exchange::kraken::Side::Buy;
req.params.symbol      = "XBTUSD";
req.params.limit_price = 26500.0;
req.params.qty         = 0.001;

auto resp = client.execute(req, creds);  // RestResponse<AddOrderResult>
if (resp.ok && resp.result)
    spdlog::info("txid: {}", resp.result->txids[0]);
```

### REST — timeouts

Every REST client bounds each call. By default it gives up on a connection that is not made within 10 s, and on a call that has not
finished within 30 s. To change either limit, pass `exchange::rest::HttpTimeouts` as the second constructor argument. Both limits must
be positive (`std::invalid_argument` otherwise):

```cpp
using namespace std::chrono_literals;

exchange::kraken::rest::KrakenRestClient client(
    "https://api.kraken.com", exchange::rest::HttpTimeouts{.connect = 5s, .total = 15s});
```

`BinanceRestClient`, `CoinbaseRestClient` and `CryptoComRestClient` take the same argument.

A call that runs out of time comes back with `resp.ok == false` and one line in `resp.errors` that gives both limits and says whether
the request was sent:

```
request failed: curl_easy_perform failed: Timeout was reached (connect limit 5000 ms, total limit 15000 ms). The request was not sent.
request failed: curl_easy_perform failed: Timeout was reached (connect limit 5000 ms, total limit 15000 ms). The request may have been received and acted on: its outcome is unknown, so check the exchange before sending it again.
```

After the first, nothing reached the server and the call can be repeated. After the second, an order may be live at the exchange: check
your open orders before you send it again, or you may place it twice. The second text is also the one used when libcurl cannot say how
far the call got. The shared transport, `exchange::rest::CurlHttpClient`, throws a `std::runtime_error` with the same text; the four
clients catch it and put it in `resp.errors`.

### REST — connections

A GET reuses an open connection when there is one. A POST or a DELETE never does: `exchange::rest::CurlHttpClient` gives each its own
connection. When the server closes a connection that was idle, libcurl cannot tell a request that the server never read from one that
it read and acted on, so it writes the request again on a new connection, and for an order that would be an order placed twice. With
a connection of its own there is none to retry on, so the transport sends a POST or a DELETE at most once
([#58](https://github.com/tomlin256/cryptocogs/issues/58)).

The cost is a TCP connect, and a TLS handshake for `https`, for every POST and DELETE, the calls that only read included: every Kraken
and Crypto.com private endpoint is a POST. A loop that polls one pays it on each poll
([#59](https://github.com/tomlin256/cryptocogs/issues/59) is a way to avoid it for the calls that only read).

If the connection of a POST or a DELETE closes without a reply, the call fails and the request may have been received and acted on:
its outcome is unknown. Check your open orders before you send an order again, or you may place it twice. The four clients put the
failure in `resp.errors`; its text does not say this yet ([#57](https://github.com/tomlin256/cryptocogs/issues/57)).

### WebSocket — `KrakenWsClient` (recommended)

`KrakenWsClient` is the high-level wrapper. It handles connection lifecycle, auto-assigns request IDs, matches responses to pending handlers, and exposes typed callbacks.

```cpp
#include "exchange/kraken/ws_client.hpp"
#include "exchange/common/ix_ws_connection.hpp"

// Connect
auto client = exchange::ws::make_exchange_ws_client(std::string(exchange::kraken::ws::PUBLIC_WS_URL), exchange::kraken::ws::kraken_frame_descriptor);

// Ping (single request → single response)
auto pong = client->execute(exchange::kraken::ws::PingRequest{});  // WsResponse<PongMessage>

// Subscribe (request → ack + continuous push)
exchange::kraken::ws::BookSubscribeRequest sub;
sub.symbols = {"BTC/USD"};
sub.depth   = 10;

auto [ack, handle] = client->subscribe(
    sub,
    [](exchange::kraken::ws::BookMessage msg) {
        // called for every book update
    }
);

handle.cancel();  // stop receiving updates
```

### WebSocket — async variants

```cpp
// Fire and forget; returns std::future
auto fut = client->execute_async(exchange::kraken::ws::PingRequest{});

auto [ack_fut, handle] = client->subscribe_async(sub, callback);
auto ack = ack_fut.get();
```

### WebSocket — subscription channels

| Channel | Request type | Push message | Auth required |
|---|---|---|---|
| `Ticker` | `TickerSubscribeRequest` | `TickerMessage` | No |
| `Book` | `BookSubscribeRequest` | `BookMessage` | No |
| `Trade` | `TradeSubscribeRequest` | `TradeMessage` | No |
| `OHLC` | `OHLCSubscribeRequest` | `OHLCMessage` | No |
| `Instrument` | `InstrumentSubscribeRequest` | `InstrumentMessage` | No |
| `Level3` | `Level3SubscribeRequest` | `Level3Message` | No |
| `Executions` | `ExecutionsSubscribeRequest` | `ExecutionsMessage` | Yes |
| `Balances` | `BalancesSubscribeRequest` | `BalancesMessage` | Yes |

Private channels need a token obtained via `GetWebSocketsTokenRequest` over REST. Pass it in the subscribe request's `token` field.

---

## REST endpoint reference

### Public

| Request | Path | Response |
|---|---|---|
| `GetServerTimeRequest` | `/0/public/Time` | `ServerTime` |
| `GetSystemStatusRequest` | `/0/public/SystemStatus` | `SystemStatus` |
| `GetAssetInfoRequest` | `/0/public/Assets` | `AssetInfoResult` |
| `GetAssetPairsRequest` | `/0/public/AssetPairs` | `AssetPairsResult` |
| `GetTickerRequest` | `/0/public/Ticker` | `TickerResult` |
| `GetOHLCRequest` | `/0/public/OHLC` | `OHLCResult` |
| `GetOrderBookRequest` | `/0/public/Depth` | `OrderBookResult` |
| `GetRecentTradesRequest` | `/0/public/Trades` | `RecentTradesResult` |

### Private — account

| Request | Response |
|---|---|
| `GetAccountBalanceRequest` | `AccountBalanceResult` |
| `GetExtendedBalanceRequest` | `ExtendedBalanceResult` |
| `GetTradeBalanceRequest` | `TradeBalance` |
| `GetOpenOrdersRequest` | `OpenOrdersResult` |
| `GetClosedOrdersRequest` | `ClosedOrdersResult` |
| `GetTradesHistoryRequest` | `TradesHistoryResult` |
| `GetOpenPositionsRequest` | `OpenPositionsResult` |
| `GetLedgersRequest` | `LedgersResult` |

### Private — trading

| Request | Response |
|---|---|
| `AddOrderRequest` | `AddOrderResult` |
| `AddOrderBatchRequest` | `AddOrderBatchResult` |
| `EditOrderRequest` | `EditOrderResult` |
| `AmendOrderRequest` | `AmendOrderResult` |
| `CancelOrderRequest` | `CancelOrderResult` |
| `CancelAllOrdersRequest` | `CancelAllResult` |
| `CancelAllOrdersAfterRequest` | `CancelAllAfterResult` |
| `CancelOrderBatchRequest` | `CancelOrderBatchResult` |

### Private — other

| Request | Response |
|---|---|
| `GetWebSocketsTokenRequest` | `WebSocketsTokenResult` |
| `GetDepositMethodsRequest` | `DepositMethodsResult` |
| `GetDepositAddressesRequest` | `DepositAddressesResult` |
| `WithdrawRequest` | `WithdrawResult` |
| `CancelWithdrawalRequest` | `CancelWithdrawalResult` |
| `AllocateEarnRequest` | `EarnBoolResult` |
| `DeallocateEarnRequest` | `EarnBoolResult` |

---

## Running the tests

```bash
ctest --preset release-macos-arm64                     # a tree made with the presets; `cmake --list-presets` lists yours
ctest --test-dir build --output-on-failure                # the tree the quick start made
```

clang-tidy runs as tests too, off until a configure names the tool. It is LLVM 23: `brew install llvm` on a Mac (keg-only, so link
its `clang-tidy` and `clangd` into a directory on your `PATH`), `clang-tidy-23` from [apt.llvm.org](https://apt.llvm.org) on Ubuntu.
The tests are green ([plan clear-clang-tidy-findings](docs/plans/clear-clang-tidy-findings.md), [#46](https://github.com/tomlin256/cryptocogs/issues/46)), so a new finding turns its unit red; [`cmake/cryptocogs_clang_tidy.cmake`](cmake/cryptocogs_clang_tidy.cmake) says what each one proves. CI runs them on every push and pull request that changes more than documentation ([`clang-tidy.yml`](.github/workflows/clang-tidy.yml), [plan clang-tidy-ci-job](docs/plans/clang-tidy-ci-job.md)).

```bash
cmake --preset debug-macos-arm64 -DCRYPTOCOGS_CLANG_TIDY=clang-tidy
ctest --preset debug-macos-arm64 -R '^clang_tidy_' -j8
```

Tests require no network access or credentials — all I/O is mocked. **312 tests**
across twelve executables (six Kraken/common, six Binance):

| Binary | What it covers |
|---|---|
| `kraken_unit_tests` | HMAC signing, REST request building, JSON deserialization, HTTP mock round-trip |
| `test_ws_client` / `test_ws_responses` | `KrakenWsClient` lifecycle, subscription handle, pre-connection queue; WS `from_json` |
| `test_tick_price` / `test_order_type` / `test_ws_reconnect_session` | exact-decimal prices; wire formats; reconnect/backoff |
| `test_binance_auth` / `test_binance_types` | HMAC-SHA256 signing; Binance enum converters |
| `test_binance_rest_requests` / `test_binance_rest_responses` / `test_binance_client` | REST request building, JSON parsing, signed round-trip |
| `test_binance_ws_client` | Binance market-stream + trading-WS-API lifecycle with `MockWsConnection` |

With `-DCRYPTOCOGS_BUILD_KRAKEN=OFF`, 147 tests build and run (the Binance suite
plus the exchange-agnostic `TickPrice` tests); with `-DCRYPTOCOGS_BUILD_BINANCE=OFF`,
the 176 Kraken/common tests.

---

## Dependencies

| Library | Version | Fetched by CMake |
|---|---|---|
| [IXWebSocket](https://github.com/machinezone/IXWebSocket) | v11.4.6 | Yes |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.12.0 | Yes |
| [spdlog](https://github.com/gabime/spdlog) | v1.17.0 | Yes |
| [Google Test](https://github.com/google/googletest) | v1.18.0 | Yes |
| OpenSSL | system | No — `sudo apt install libssl-dev` |
| libcurl | system | No — `sudo apt install libcurl4-openssl-dev` |

---

## License

MIT
