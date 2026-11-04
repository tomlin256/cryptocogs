# CLAUDE.md — cryptocogs

A type-safe C++ library for the [Kraken](https://kraken.com) and
[Binance](https://binance.com) Spot REST and WebSocket APIs. The compiler links
each request type to its exact response type — no casts, no stringly-typed keys.

> **Multi-exchange layout** — the [001 migration plan](docs/plans/001-multi-exchange-abstraction.md)
> is **complete** (Steps 1–10). Code is organised in three tiers:
> - a generic, exchange-agnostic **scaffold** (`exchange::` / `exchange::rest::` /
>   `exchange::ws::`), compiled as `libexchange_common.a` and never edited by an
>   adapter;
> - four **adapters** built on it — **Kraken** (`exchange::kraken::*`,
>   `libkraken.a`), **Binance** (`exchange::binance::*`, `libbinance.a`),
>   **Coinbase** (`exchange::coinbase::*`, `libcoinbase.a`), and **Crypto.com**
>   (`exchange::cryptocom::*`, `libcryptocom.a`) — peers that each link the
>   scaffold but never each other, behind independent
>   `CRYPTOCOGS_BUILD_KRAKEN` / `CRYPTOCOGS_BUILD_BINANCE` /
>   `CRYPTOCOGS_BUILD_COINBASE` / `CRYPTOCOGS_BUILD_CRYPTOCOM` flags.
>
> The pre-refactor `kraken_*.hpp` / `kraken::` compatibility shim was **removed in
> v0.1.1** ([plan 014](docs/plans/014-remove-compat-shim.md)) — `exchange::kraken::*`
> is the only Kraken surface. To add a
> fifth exchange, follow [docs/agent-add-exchange.md](docs/agent-add-exchange.md).
> This file documents Kraken in depth, Binance in the
> [Binance adapter reference](#binance-adapter-reference), Coinbase in the
> [Coinbase adapter reference](#coinbase-adapter-reference), and Crypto.com in the
> [Crypto.com adapter reference](#cryptocom-adapter-reference); all follow the same
> three-tier shape. **Coinbase ([plan 018](docs/plans/018-coinbase-exchange-adapter.md))
> places orders over REST — it has no WebSocket order entry, and its FIX
> order-entry path is recorded as deferred
> ([plan 019](docs/plans/019-coinbase-fix-order-entry.md)). Crypto.com
> ([plan 020](docs/plans/020-cryptocom-exchange-adapter.md)) also places orders
> over REST; its WebSocket reuses the generic `ExchangeWsClient` behind a
> `HeartbeatResponder` connection decorator (§2 Option A).**

---

## Project structure

```
cryptocogs/
├── CMakeLists.txt                       # Top-level build; fetches deps; CRYPTOCOGS_BUILD_* options
├── include/
│   ├── exchange/
│   │   ├── common/                      # Exchange-agnostic scaffold — exchange::, ::rest::, ::ws::
│   │   │   ├── cxx_standard.hpp         #   The C++23 guard: #error below C++23 (plan 029); included first by the 7 headers below that include no other exchange/ header
│   │   │   ├── types.hpp                #   Canonical enums: Side, OrderType, TimeInForce, OrderStatus
│   │   │   ├── tick_price.hpp           #   TickPrice — exact-decimal price representation (namespace exchange::)
│   │   │   ├── rest.hpp                 #   TypedPublicRequest<R>, TypedPrivateRequest<R>, RestResponse<T>,
│   │   │   │                            #     HttpRequest, IRestAuth
│   │   │   ├── http_client.hpp          #   CurlHttpClient + HttpResponse — shared libcurl transport (→ libexchange_http.a)
│   │   │   ├── credentials_file.hpp     #   read_toml_credentials — shared TOML from_file loader (→ libexchange_common.a)
│   │   │   ├── ws.hpp                   #   IWsConnection, IWsErrorHandler, RateLimitedWsErrorHandler,
│   │   │   │                            #     WsResponse<T>, SubscriptionHandle, WsRequestBase,
│   │   │   │                            #     TypedWsRequest<R>, FrameDescriptor, MessageIdentifier
│   │   │   ├── ws_client.hpp / .inl     #   ExchangeWsClient — exchange-agnostic dispatch
│   │   │   ├── ix_ws_connection.hpp     #   IxWsConnection + make_exchange_ws_client(url, identifier)
│   │   │   └── reconnect_session.hpp/.inl # WsReconnectSession — generic reconnect/backoff machinery
│   │   ├── kraken/                      # Kraken adapter — exchange::kraken::, ::rest::, ::ws::
│   │   │   ├── auth.hpp                 #   KrakenCredentials, sign(), make_nonce() (+ private crypto helpers)
│   │   │   ├── types.hpp                #   PriceType, TriggerReference, StpType, FeePreference,
│   │   │   │                            #     OrderParams, OrderInfo, TradeInfo, LedgerEntry,
│   │   │   │                            #     RestResponse<T> / parse_rest_response<T>
│   │   │   ├── rest_api.hpp             #   All Kraken REST request/response types
│   │   │   ├── rest_client.hpp          #   KrakenRestClient — typed libcurl executor
│   │   │   ├── ws_api.hpp               #   All Kraken WS request/response types + identify_message /
│   │   │   │                            #     kraken_frame_descriptor
│   │   │   └── ws_client.hpp            #   KrakenWsClient alias, PUBLIC/PRIVATE_WS_URL, make_kraken_ws_client()
│   │   ├── binance/                     # Binance adapter — exchange::binance::, ::rest::, ::ws::
│   │   │   ├── auth.hpp                 #   BinanceCredentials, BinanceAuth (IRestAuth; HMAC-SHA256)
│   │   │   ├── types.hpp                #   Binance enums + converters; re-exports canonical enums
│   │   │   ├── rest_api.hpp             #   All Binance REST req/resp types + parse_binance_response<T>
│   │   │   ├── rest_client.hpp          #   BinanceRestClient — typed libcurl executor
│   │   │   ├── ws_streams.hpp           #   Market-data streams: events, binance_stream_frame_descriptor,
│   │   │   │                            #     BinanceStreamClient alias, make_binance_stream_client(), STREAM_URL
│   │   │   └── ws_api.hpp               #   Trading WS API: order.place/cancel/ping, signed params,
│   │   │                                #     binance_ws_api_frame_descriptor, make_binance_ws_api_client(), WS_API_URL
│   │   ├── coinbase/                    # Coinbase adapter — exchange::coinbase::, ::rest::, ::ws::
│   │   │   ├── auth.hpp                 #   CoinbaseCredentials{key,secret,passphrase}, CoinbaseAuth (CB-ACCESS-* signing)
│   │   │   ├── types.hpp                #   Coinbase enums + converters; parse_coinbase_response<T>
│   │   │   ├── rest_api.hpp             #   All Coinbase REST req/resp types (public + private)
│   │   │   ├── rest_client.hpp          #   CoinbaseRestClient — typed libcurl executor (sets User-Agent)
│   │   │   └── ws_streams.hpp           #   Bespoke CoinbaseStreamClient (id-less pub/sub) + events + STREAM_URL
│   │   └── cryptocom/                   # Crypto.com adapter — exchange::cryptocom::, ::rest::, ::ws::
│   │       ├── auth.hpp                 #   CryptoComCredentials{key,secret}, sign() (in-body sig), params_to_str, make_nonce
│   │       ├── types.hpp                #   Crypto.com enums (UPPERCASE) + converters; parse_cryptocom_response<T>
│   │       ├── rest_api.hpp             #   All Crypto.com REST req/resp types (public + private; signed envelope)
│   │       ├── rest_client.hpp / .inl   #   CryptoComRestClient — typed libcurl executor
│   │       ├── heartbeat_connection.hpp #   HeartbeatResponder — IWsConnection decorator answering public/heartbeat
│   │       └── ws.hpp                   #   Market+user events, subscribe scaffold, public/auth, cryptocom_frame_descriptor,
│   │                                    #     CryptoComMarketClient/UserClient + factories, MARKET/USER_WS_URL
├── src/                                 # Headers are purely declarative (plan 016): every
│   │                                    #   non-template body lives in a .cpp here, every
│   │                                    #   template body in a sibling .inl under include/.
│   │                                    #   Sole exception: ix_ws_connection.hpp stays
│   │                                    #   header-only (D1) so the libs never link ixwebsocket.
│   ├── CMakeLists.txt                   # Peer libs: exchange_common/_http (always),
│   │                                    #   kraken, binance, coinbase, cryptocom (per-flag)
│   ├── exchange/common/
│   │   ├── ws_client.cpp                # ExchangeWsClient non-template impl → libexchange_common.a
│   │   ├── ws.cpp                       # SubscriptionHandle + RateLimitedWsErrorHandler → libexchange_common.a
│   │   ├── types.cpp                    # Canonical enum converters → libexchange_common.a
│   │   ├── tick_price.cpp               # TickPrice from()/str()/to_json()/from_json → libexchange_common.a
│   │   ├── credentials_file.cpp        # read_toml_credentials — shared TOML from_file loader → libexchange_common.a
│   │   └── http_client.cpp             # CurlHttpClient — shared libcurl transport → libexchange_http.a
│   ├── kraken/                          # → libkraken.a
│   │   ├── types.cpp  auth.cpp          #   enum converters, crypto/sign/nonce
│   │   ├── rest_api.cpp  rest_client.cpp #   request build() + response from_json; client ctors + factory
│   │   └── ws_api.cpp  ws_client.cpp    #   WS to_json/from_json, identify_message, descriptor; make_kraken_ws_client
│   ├── binance/                         # → libbinance.a
│   │   ├── types.cpp  auth.cpp          #   enum converters, HMAC-SHA256 helpers
│   │   ├── rest_api.cpp  rest_client.cpp #   request build() + response from_json; client ctors + factory
│   │   └── ws_streams.cpp  ws_api.cpp   #   stream events + descriptor + factory; trading API + signing
│   ├── coinbase/                        # → libcoinbase.a
│   │   ├── types.cpp  auth.cpp          #   enum converters, base64/HMAC-SHA256 + CB-ACCESS-* signing
│   │   ├── rest_api.cpp  rest_client.cpp #   request build() + response from_json; client ctor + factory
│   │   └── ws_streams.cpp               #   feed events + bespoke CoinbaseStreamClient (plan 018 §2)
│   └── cryptocom/                       # → libcryptocom.a
│       ├── types.cpp  auth.cpp          #   UPPERCASE enum converters; HMAC-SHA256/hex + params_to_str signing
│       ├── rest_api.cpp  rest_client.cpp #   request build()+sign envelope + response from_json; client ctor + factory
│       └── heartbeat_connection.cpp  ws.cpp #   heartbeat decorator; feed events + descriptor + factories (plan 020 §2)
└── tests/
    ├── CMakeLists.txt                   # Fetches spdlog/CLI11/backward-cpp; wires examples + unit tests
    ├── examples/
    │   ├── public_rest.cpp              # Fetch recent trades (no credentials)
    │   ├── private_rest.cpp             # Get WS token from ~/.kraken/default
    │   ├── public_ws.cpp                # Subscribe to ticker over WS (low-level)
    │   ├── private_ws.cpp               # Subscribe to balances over authenticated WS
    │   ├── ws_client_example.cpp        # KrakenWsClient all public channels + connection reuse demo
    │   ├── rest_client_example.cpp      # CLI11 demo of every public REST endpoint via KrakenRestClient
    │   ├── kraken_example.cpp           # REST + WebSocket combined demo
    │   ├── binance/                     # Binance demos (each behind CRYPTOCOGS_BUILD_BINANCE)
    │   │   ├── binance_rest_client_example.cpp   # CLI11 demo of every public Binance REST endpoint
    │   │   ├── binance_ws_client_example.cpp     # All 8 market-data streams + connection-reuse demo
    │   │   └── binance_ws_api_example.cpp        # Trading WS API ping (live-verified)
    │   ├── coinbase/                    # Coinbase demos (each behind CRYPTOCOGS_BUILD_COINBASE)
    │   │   ├── coinbase_rest_client_example.cpp  # CLI11 demo of every public Coinbase REST endpoint
    │   │   └── coinbase_ws_client_example.cpp    # ticker/level2/matches/heartbeat + connection-reuse demo
    │   ├── cryptocom/                   # Crypto.com demos (each behind CRYPTOCOGS_BUILD_CRYPTOCOM)
    │   │   ├── cryptocom_rest_client_example.cpp # CLI11 demo of every public Crypto.com REST endpoint
    │   │   └── cryptocom_ws_client_example.cpp   # ticker/trade/book/candlestick + connection-reuse demo
    │   ├── backward_init.cpp            # Shared crash-backtrace init, linked into examples via `example_backward`
    │   └── kapi.hpp / kapi.cpp          # Legacy KAPI reference wrapper (not installed)
    └── unit/
        ├── CMakeLists.txt
        ├── test_signature.cpp           # HMAC-SHA512 output vs. reference KAPI impl
        ├── test_rest_requests.cpp
        ├── test_rest_responses.cpp
        ├── test_client.cpp              # Full execute() cycle with mock HTTP performer
        ├── test_ws_client.cpp           # ExchangeWsClient (KrakenWsClient) lifecycle with MockWsConnection
        ├── test_ws_responses.cpp        # from_json field assertions against captured fixtures
        ├── test_order_type.cpp          # Generic vs. Kraken OrderType wire format (underscore vs. hyphen)
        ├── test_tick_price.cpp          # TickPrice exact-decimal serialisation (FP-noise-free)
        ├── test_credentials_file.cpp    # read_toml_credentials — TOML from_file loader (exchange-agnostic)
        ├── test_ws_reconnect_session.cpp # WsReconnectSession lifecycle — deterministic, no sleeps
        ├── ws_client_example_json.hpp
        ├── mock_ws_connection.hpp        # Shared MockWsConnection (Kraken + Binance + Coinbase + Crypto.com WS tests)
        ├── test_binance_auth.cpp         # BinanceAuth HMAC-SHA256 signing
        ├── test_binance_types.cpp        # Binance enum converters
        ├── test_binance_rest_requests.cpp / test_binance_rest_responses.cpp
        ├── test_binance_client.cpp       # Signed REST round-trip via mock performer
        ├── test_binance_ws_client.cpp    # Stream + WS-API lifecycle with MockWsConnection
        ├── binance_{rest,account,ws_stream,ws_api}_example_json.hpp  # Captured Binance fixtures
        ├── test_coinbase_auth.cpp        # base64/HMAC-SHA256 (RFC 4231) + CB-ACCESS-* injection
        ├── test_coinbase_types.cpp       # Coinbase enum converters + parse_coinbase_response
        ├── test_coinbase_rest_requests.cpp / test_coinbase_rest_responses.cpp
        ├── test_coinbase_client.cpp      # Signed REST round-trip + User-Agent via mock performer
        ├── test_coinbase_ws_client.cpp   # CoinbaseStreamClient pub/sub lifecycle with MockWsConnection
        ├── coinbase_{rest,account,ws}_example_json.hpp  # Coinbase fixtures (public live, private synthetic)
        ├── test_cryptocom_auth.cpp       # params_to_str + HMAC-SHA256/hex + sign() digest composition
        ├── test_cryptocom_types.cpp      # Crypto.com enum converters + parse_cryptocom_response
        ├── test_cryptocom_rest_requests.cpp / test_cryptocom_rest_responses.cpp
        ├── test_cryptocom_client.cpp     # Signed-envelope REST round-trip via mock performer
        ├── test_cryptocom_ws_client.cpp  # HeartbeatResponder + subscribe/ack/push lifecycle with MockWsConnection
        └── cryptocom_{rest,account,ws}_example_json.hpp  # Crypto.com fixtures (market live, private/user synthetic)
```

---

## Build system

- **CMake 4.0+**, **C++23** required.
- **System dependencies** (must be installed before configuring):
  - OpenSSL (`libssl-dev`)
  - libcurl (`libcurl4-openssl-dev`)
  - zlib (`zlib1g-dev`): the fetched IXWebSocket needs it for the examples and tests, and on Ubuntu
    `libcurl4-openssl-dev` only suggests it
- **Fetched automatically** by `FetchContent` at configure time:
  | Library | Version | Used by |
  |---|---|---|
  | IXWebSocket | v12.1.0 | WebSocket examples + `IxWsConnection` |
  | nlohmann/json | v3.12.0 | All JSON parsing |
  | toml++ | v3.4.0 | `read_toml_credentials` (`from_file` loaders) — build-only, in `exchange_common` |
  | spdlog | v1.17.0 | Examples and tests |
  | Google Test | v1.18.0 | Unit tests |

### Build options

| Option | Default | Effect |
|---|---|---|
| `CRYPTOCOGS_BUILD_KRAKEN` | `ON` | Build the Kraken adapter (`libkraken.a`) and its tests/examples |
| `CRYPTOCOGS_BUILD_BINANCE` | `ON` | Build the Binance adapter (`libbinance.a`) and its tests/examples |
| `CRYPTOCOGS_BUILD_COINBASE` | `ON` | Build the Coinbase adapter (`libcoinbase.a`) and its tests/examples |
| `CRYPTOCOGS_BUILD_CRYPTOCOM` | `ON` | Build the Crypto.com adapter (`libcryptocom.a`) and its tests/examples |
| `CRYPTOCOGS_BUILD_TESTS` | `ON` | Build unit tests and example programs |
| `CRYPTOCOGS_INSTALL` | top-level: `ON` | Generate `install()` rules + the `cryptocogs` CMake package config ([plan 012](docs/plans/012-install-and-package-config.md)). Defaults off when cryptocogs is a `FetchContent` subproject |

The four exchange flags are independent: e.g. `-DCRYPTOCOGS_BUILD_KRAKEN=OFF
-DCRYPTOCOGS_BUILD_BINANCE=OFF -DCRYPTOCOGS_BUILD_COINBASE=OFF` builds a
Crypto.com-only tree. All default `ON`. `exchange_common` (the generic
`ExchangeWsClient` implementation) is always built — every adapter links it.
Turning all four exchanges off emits a "nothing will be built" warning.

**Installing**: `cmake --install <tree> --prefix <p>` lays down the six static
libs, the public headers (component-gated by the build flags), and a package
config so a downstream project can `find_package(cryptocogs)` +
`target_link_libraries(app cryptocogs::kraken)` — and inherit the C++23
requirement automatically (the targets carry `cxx_std_23`, so a consumer need
not set `CMAKE_CXX_STANDARD` itself; plan 017). The header-only `nlohmann_json`
is **vendored** into the prefix (it is FetchContent'd and so can't be an export
dependency); OpenSSL/libcurl resolve via `find_dependency`. ixwebsocket, GTest,
and backward-cpp are **not** installed — each fetched dep's own install is
suppressed (`IXWEBSOCKET_INSTALL` / `INSTALL_GTEST` / `JSON_Install` OFF), so the
prefix is cryptocogs-only in any config (no need for `-DCRYPTOCOGS_BUILD_TESTS=OFF`).
See [plan 012](docs/plans/012-install-and-package-config.md) and
[plan 017](docs/plans/017-cmake-install-nits.md).

### Build trees

A CMake build tree belongs to one OS and one architecture: it records the absolute path of its source, the
compiler, the target and the flags. One checkout is built on the Mac and in a Linux container that mounts it, so each
platform and each kind of build gets a tree of its own, named `<kind>-<os>-<arch>` and kept under `build/`
([plan 028](docs/plans/028-platform-build-dirs.md)). Below, `<tree>` stands for one of them:
`build/release-macos-arm64`, `build/debug-linux-arm64`, `build/release-linux-x86_64`, …

| Part | Values |
|---|---|
| `<kind>` | one lower-case word. `release` and `debug` have presets; `bench`, `ubsan`, `asan`, `tsan` and the like are made by hand, and any word is allowed |
| `<os>` | `macos` or `linux` |
| `<arch>` | `arm64` or `x86_64` (`aarch64` is spelled `arm64`, `amd64` is spelled `x86_64`) |

- **Presets.** `CMakePresets.json` spells out the six trees that have presets (`release-` and `debug-` for
  `macos-arm64`, `linux-arm64` and `linux-x86_64`). `cmake --list-presets` shows the ones for this host; each has a
  build preset and a test preset of the same name. A kind preset sets `CMAKE_BUILD_TYPE` (`Release`, `Debug`) and
  nothing else: CI adds its own flags on its command line. `test_presets` pins the file.
- **The guard.** The top of `CMakeLists.txt` refuses a configure that would make a tree lie: a directory named for a
  platform that is not this host's (`cmake -B build/release-linux-x86_64` on an arm64 machine), and any tree that was
  first configured on another platform, whatever it is called. The error names the tree to use. It examines the OS and
  the architecture in the name, not the kind, and not other names (`build`, `scratch`). Only the top-level project is
  examined, so a consumer's tree never is. A refused configure into an existing tree leaves its settings alone, but
  CMake saves its cache as it stood at the failure; the next valid configure restores it and rebuilds nothing.
- **Hand-made trees.** `cmake -S . -B build/bench-macos-arm64 -DCMAKE_BUILD_TYPE=RelWithDebInfo
  -DCRYPTOCOGS_BUILD_BENCHMARKS=ON` is a tree like any other; the guard checks only its platform.
- **Floor.** The project's floor is CMake 4.0 ([plan 029](docs/plans/029-cmake4-cxx23.md)): a plain
  `cmake_minimum_required`, not a range, and `test_cmake_floor` holds every other minimum in the checkout, and the
  README, to it. `CMAKE_POLICY_VERSION_MINIMUM 3.5` stays for two fetched dependencies that ask for less (backward-cpp
  v1.6, ixwebsocket v12.1.0). `CMakePresets.json` stays at `cmakeMinimumRequired` 3.21 on purpose: it says which CMake
  can read the file, not what the project needs, and a higher number turns CMake 3's clear `CMake 4.0 or higher is
  required` into "major version 4 must be less than 3". `README.md`'s quick start is for a user with one platform: a
  free-form name such as `build` is allowed.
- **Names** are compared in lower case, since a bind-mounted checkout is case-insensitive. `.gitignore` already
  ignores `build*`, which is why the checks live in `tests/tree_checks/` and not `tests/build_checks/`.

### Common build commands

```bash
# Install system deps (Debian/Ubuntu)
sudo apt install libssl-dev libcurl4-openssl-dev zlib1g-dev

# Configure, build and test a tree through its preset (cmake --list-presets lists this host's)
cmake --preset release-macos-arm64
cmake --build --preset release-macos-arm64
ctest --preset release-macos-arm64

# A debug tree; the same on Linux
cmake --preset debug-macos-arm64 && cmake --build --preset debug-macos-arm64
cmake --preset release-linux-arm64          # release-linux-x86_64 on an x86_64 host

# Skip tests and examples (a hand-made tree: any kind word)
cmake -S . -B build/release-macos-arm64 -DCMAKE_BUILD_TYPE=Release -DCRYPTOCOGS_BUILD_TESTS=OFF
cmake --build build/release-macos-arm64
```

### Build outputs

| Path | Description |
|---|---|
| `<tree>/src/libexchange_common.a` | Generic `ExchangeWsClient` impl — always built; every adapter links it (`PUBLIC`) |
| `<tree>/src/libexchange_http.a` | Shared `CurlHttpClient` libcurl transport — always built; every REST adapter links it (`PUBLIC`) |
| `<tree>/src/libkraken.a` | Kraken adapter static library — link this for Kraken (transitively pulls `exchange_common`) |
| `<tree>/src/libbinance.a` | Binance adapter static library — link this for Binance (transitively pulls `exchange_common`) |
| `<tree>/src/libcoinbase.a` | Coinbase adapter static library — link this for Coinbase (transitively pulls `exchange_common`) |
| `<tree>/src/libcryptocom.a` | Crypto.com adapter static library — link this for Crypto.com (transitively pulls `exchange_common`) |
| `<tree>/bin/public_rest` | Public REST demo |
| `<tree>/bin/private_rest` | Private REST demo |
| `<tree>/bin/public_ws` | Public WebSocket demo (low-level) |
| `<tree>/bin/private_ws` | Private WebSocket demo |
| `<tree>/bin/ws_client_example` | `KrakenWsClient` — all 5 public channels + connection reuse demo |
| `<tree>/bin/rest_client_example` | CLI11 demo of every public REST endpoint via `KrakenRestClient` |
| `<tree>/bin/kraken_example` | Combined REST + WebSocket demo |
| `<tree>/bin/binance_rest_client_example` | CLI11 demo of every public Binance REST endpoint via `BinanceRestClient` |
| `<tree>/bin/binance_ws_client_example` | `BinanceStreamClient` — all 8 market-data streams + connection reuse |
| `<tree>/bin/binance_ws_api_example` | `BinanceWsApiClient` — trading WS API `ping` (live-verified) |
| `<tree>/bin/coinbase_rest_client_example` | CLI11 demo of every public Coinbase REST endpoint via `CoinbaseRestClient` |
| `<tree>/bin/coinbase_ws_client_example` | `CoinbaseStreamClient` — ticker/level2/matches/heartbeat + connection-reuse demo |
| `<tree>/bin/cryptocom_rest_client_example` | CLI11 demo of every public Crypto.com REST endpoint via `CryptoComRestClient` |
| `<tree>/bin/cryptocom_ws_client_example` | `CryptoComMarketClient` — ticker/trade/book/candlestick + connection-reuse demo (heartbeat answered automatically) |

---

## Running tests

```bash
ctest --preset release-macos-arm64        # the test preset of the tree you built; adds --output-on-failure
```

There are twenty-five test executables (496 tests, plus nine `cmake -P` checks that are listed after the table — 505 in
ctest) — seven Kraken/common, six Binance, six Coinbase, six Crypto.com:

| Binary | Source | What it tests |
|---|---|---|
| `<tree>/bin/kraken_unit_tests` | `test_signature/rest_requests/rest_responses/client/kraken_auth.cpp` | REST request building, response parsing, signature, HTTP mock |
| `<tree>/bin/test_ws_client` | `test_ws_client.cpp` | `ExchangeWsClient` (as `KrakenWsClient`) lifecycle with `MockWsConnection` |
| `<tree>/bin/test_ws_responses` | `test_ws_responses.cpp` | `identify_message` + `from_json` against captured WS fixtures |
| `<tree>/bin/test_tick_price` | `test_tick_price.cpp` | `TickPrice::from`/`str` exact-decimal round-tripping |
| `<tree>/bin/test_credentials_file` | `test_credentials_file.cpp` | `read_toml_credentials` — TOML parse, `$HOME`/explicit path resolution, and every error branch |
| `<tree>/bin/test_order_type` | `test_order_type.cpp` | Generic (`exchange::to_string`) vs. Kraken (`kraken_order_type_to_string`) wire formats |
| `<tree>/bin/test_ws_reconnect_session` | `test_ws_reconnect_session.cpp` | `WsReconnectSession` start/stop/backoff/reconnect — deterministic, no real sleeps |
| `<tree>/bin/test_binance_auth` | `test_binance_auth.cpp` | `BinanceAuth` HMAC-SHA256 signing + header injection |
| `<tree>/bin/test_binance_types` | `test_binance_types.cpp` | Binance enum converters (incl. canonical FOK time-in-force) |
| `<tree>/bin/test_binance_rest_requests` | `test_binance_rest_requests.cpp` | Each Binance REST request builds the correct path/query/body |
| `<tree>/bin/test_binance_rest_responses` | `test_binance_rest_responses.cpp` | `from_json` + `parse_binance_response` against captured fixtures |
| `<tree>/bin/test_binance_client` | `test_binance_client.cpp` | `BinanceRestClient::execute()` signed round-trip via mock performer |
| `<tree>/bin/test_binance_ws_client` | `test_binance_ws_client.cpp` | Binance stream + trading-WS-API lifecycle with `MockWsConnection` |
| `<tree>/bin/test_coinbase_auth` | `test_coinbase_auth.cpp` | `CoinbaseAuth` base64/HMAC-SHA256 (RFC 4231 vector) + CB-ACCESS-* header injection |
| `<tree>/bin/test_coinbase_types` | `test_coinbase_types.cpp` | Coinbase enum converters + `parse_coinbase_response` status mapping |
| `<tree>/bin/test_coinbase_rest_requests` | `test_coinbase_rest_requests.cpp` | Each Coinbase REST request builds the correct path/query/JSON body |
| `<tree>/bin/test_coinbase_rest_responses` | `test_coinbase_rest_responses.cpp` | `from_json` field assertions (public live + private synthetic fixtures) |
| `<tree>/bin/test_coinbase_client` | `test_coinbase_client.cpp` | `CoinbaseRestClient::execute()` signed round-trip + User-Agent via mock performer |
| `<tree>/bin/test_coinbase_ws_client` | `test_coinbase_ws_client.cpp` | `CoinbaseStreamClient` optimistic subscribe / type-keyed dispatch with `MockWsConnection` |
| `<tree>/bin/test_cryptocom_auth` | `test_cryptocom_auth.cpp` | `params_to_str` matrix + HMAC-SHA256/hex + `CryptoComCredentials::sign()` digest |
| `<tree>/bin/test_cryptocom_types` | `test_cryptocom_types.cpp` | Crypto.com UPPERCASE enum converters + `parse_cryptocom_response` `code` mapping |
| `<tree>/bin/test_cryptocom_rest_requests` | `test_cryptocom_rest_requests.cpp` | Each Crypto.com REST request builds the right path/query (public) or signed envelope (private) |
| `<tree>/bin/test_cryptocom_rest_responses` | `test_cryptocom_rest_responses.cpp` | `from_json` field assertions (market live + private synthetic fixtures) |
| `<tree>/bin/test_cryptocom_client` | `test_cryptocom_client.cpp` | `CryptoComRestClient::execute()` signed-envelope round-trip via mock performer |
| `<tree>/bin/test_cryptocom_ws_client` | `test_cryptocom_ws_client.cpp` | `HeartbeatResponder` auto-reply + id-correlated subscribe/ack/push lifecycle with `MockWsConnection` |

The nine `cmake -P` checks need no build and finish in seconds; they are registered at the top level, in every tree:

| Test | Script | What it holds |
|---|---|---|
| `test_platform_name` | `tests/tree_checks/check_platform.cmake` | the platform tag and the tree-name check in `cmake/cryptocogs_platform.cmake` (plan 028) |
| `test_tree_name` | `tests/tree_checks/check_tree_name.cmake` | the build-tree guard in the root `CMakeLists.txt`, by configuring the real project offline (plan 028) |
| `test_presets` | `tests/tree_checks/check_presets.cmake` | `CMakePresets.json` names one tree per kind and platform (plan 028) |
| `test_cmake_floor` | `tests/cmake_checks/check_cmake_floor.cmake` | one CMake floor: the root's plain minimum, every other `cmake_minimum_required` outside `build*/`, and the README's "CMake 4.0 or later" (plan 029) |
| `test_standard_requirement` | `tests/cmake_checks/check_standard_requirement.cmake` | every library created in `src/` compiles its consumers as C++23, read from `INTERFACE_COMPILE_FEATURES` (plan 029) |
| `test_standard_guard` | `tests/cmake_checks/check_standard_guard.cmake` | every `include/exchange/*/*.hpp` refuses to preprocess at `-std=c++17` and `-std=c++20` with "requires C++23"; GCC and Clang only (plan 029) |
| `test_package_version` | `tests/cmake_checks/check_package_version.cmake` | which `find_package(cryptocogs <version>)` requests `cryptocogsConfigVersion.cmake` accepts — `SameMajorVersion` — derived from the project's own version (plan 029) |
| `test_ci_cache_key` | `tests/ci_checks/check_cache_key.cmake` | the CI cache of the fetched dependencies is keyed over every file that declares one: each `CMakeLists.txt`, `*.cmake` and `*.cmake.in` with a `FetchContent_Declare` call is in the `hashFiles(...)` list of the `deps-` key in `.github/workflows/cmake-single-platform.yml`, and each listed file exists (plan 030) |
| `test_ci_paths` | `tests/ci_checks/check_paths.cmake` | the `paths-ignore` lists under the workflow's `push` and `pull_request` triggers are equal, each entry names an existing file or `<dir>/**`, and no entry covers a file that a check reads or a CMake or C++ source (plan 031) |

Tests do **not** require network access or credentials — all I/O is mocked.
The flag matrix splits cleanly: each exchange flag drops its own suite. A
Crypto.com-only tree (`-DCRYPTOCOGS_BUILD_KRAKEN=OFF -DCRYPTOCOGS_BUILD_BINANCE=OFF
-DCRYPTOCOGS_BUILD_COINBASE=OFF`) runs 104 gtests — the 85-test Crypto.com suite plus the
exchange-agnostic 11 `TickPrice` and 8 `read_toml_credentials` tests, which build
in any tree — and the nine `cmake -P` checks.

### Test suite breakdown

| File | What it verifies |
|---|---|
| `test_signature.cpp` | `KrakenCredentials::sign()` produces byte-identical output to the legacy `KAPI::signature()` for the same inputs |
| `test_rest_requests.cpp` | Each request type builds the correct HTTP path, method, query string, and body |
| `test_rest_responses.cpp` | JSON deserialization is correct for every response type |
| `test_client.cpp` | `KrakenRestClient::execute()` round-trips public and private requests end-to-end using an injected mock performer |
| `test_ws_client.cpp` | `ExchangeWsClient` execute/subscribe lifecycle, timeout, pre-connection queuing, `SubscriptionHandle::cancel()` idempotency |
| `test_ws_responses.cpp` | `identify_message` classification and `from_json` field assertions against `ws_client_example_json.hpp` fixtures |
| `test_order_type.cpp` | Generic vs. Kraken `OrderType` converters round-trip and produce the *correct, distinct* wire strings (underscore vs. hyphen) |
| `test_tick_price.cpp` | `TickPrice::str()` produces exact decimals — including the floating-point-noise repro case |
| `test_ws_reconnect_session.cpp` | `WsReconnectSession` start/stop/backoff/reconnect callbacks, driven by synchronous step injection (no `sleep_for`) |

---

## Namespace layout

| Namespace | Location | Contains |
|---|---|---|
| `exchange::` | `exchange/common/{types,tick_price}.hpp` | Canonical enums shared by every adapter: `Side`, `OrderType`, `TimeInForce`, `OrderStatus` (+ `to_string`/`from_string`); and `TickPrice` (exact-decimal price representation) |
| `exchange::rest::` | `exchange/common/{rest,http_client,credentials_file}.hpp` | `TypedPublicRequest<R>`, `TypedPrivateRequest<R>`, `RestResponse<T>`, `HttpRequest`, `IRestAuth`; `CurlHttpClient` + `HttpResponse` (shared libcurl transport, `libexchange_http.a`); and `read_toml_credentials` (shared TOML `from_file` loader, `libexchange_common.a`) |
| `exchange::ws::` | `exchange/common/{ws,ws_client,ix_ws_connection,reconnect_session}.hpp` | `IWsConnection`, `IWsErrorHandler`, `RateLimitedWsErrorHandler`, `IxWsConnection`, `WsReconnectSession`, `ExchangeWsClient`, `SubscriptionHandle`, `WsResponse<T>`, `WsRequestBase`, `TypedWsRequest<R>`, `FrameDescriptor`, `FrameKind`, `MessageIdentifier`, `make_exchange_ws_client()` |
| `exchange::kraken::` | `exchange/kraken/types.hpp` | Kraken-only types — `PriceType`, `TriggerReference`, `StpType`, `FeePreference`, `OrderParams`, `OrderInfo`, `TradeInfo`, `LedgerEntry` — plus `RestResponse<T>` / `parse_rest_response<T>()` (Kraken's REST envelope; re-exports the canonical enums **and** `exchange::TickPrice`) |
| `exchange::kraken::rest::` | `exchange/kraken/{auth,rest_api,rest_client}.hpp` | `KrakenCredentials`, all REST request/response types, `KrakenRestClient` |
| `exchange::kraken::ws::` | `exchange/kraken/{ws_api,ws_client}.hpp` | All WS request/response types, `SubscribeChannel`, `KrakenWsCredentials`, `identify_message`/`kraken_frame_descriptor`, `KrakenWsClient` (alias for `ExchangeWsClient`), `make_kraken_ws_client()`, URL constants |
| `exchange::binance::` | `exchange/binance/types.hpp` | Binance enums + per-exchange string converters (`binance_order_type_to_string`, …); re-exports the canonical four |
| `exchange::binance::rest::` | `exchange/binance/{auth,rest_api,rest_client}.hpp` | `BinanceCredentials`, `BinanceAuth` (`IRestAuth`), all REST request/response types, `parse_binance_response<T>`, `BinanceRestClient` |
| `exchange::binance::ws::` | `exchange/binance/{ws_streams,ws_api}.hpp` | Stream events + `binance_stream_frame_descriptor` + `BinanceStreamClient`/`make_binance_stream_client()` (`STREAM_URL`); trading API req/resp + `binance_ws_api_frame_descriptor` + `BinanceWsApiClient`/`make_binance_ws_api_client()` (`WS_API_URL`). Both clients are `ExchangeWsClient` aliases |
| `exchange::coinbase::` | `exchange/coinbase/types.hpp` | Coinbase enums + lowercase-wire converters (`coinbase_order_type_to_string`, …); `parse_coinbase_response<T>`; re-exports the canonical four |
| `exchange::coinbase::rest::` | `exchange/coinbase/{auth,rest_api,rest_client}.hpp` | `CoinbaseCredentials` (key + secret + **passphrase**), `CoinbaseAuth` (`IRestAuth`; CB-ACCESS-* signing), all REST req/resp types, `CoinbaseRestClient` |
| `exchange::coinbase::ws::` | `exchange/coinbase/ws_streams.hpp` | Feed events + the **bespoke** `CoinbaseStreamClient` / `make_coinbase_stream_client()` (`STREAM_URL`) — id-less type-keyed pub/sub over `IWsConnection`, **not** an `ExchangeWsClient` alias (plan 018 §2) |
| `exchange::cryptocom::` | `exchange/cryptocom/types.hpp` | Crypto.com enums + UPPERCASE-wire converters (`cryptocom_order_type_to_string`, …); `parse_cryptocom_response<T>` (the `{code,result}` envelope, `code == 0` = success); re-exports the canonical four |
| `exchange::cryptocom::rest::` | `exchange/cryptocom/{auth,rest_api,rest_client}.hpp` | `CryptoComCredentials` (key + secret; **plaintext** HMAC key) + in-body `sign()` / `params_to_str` / `make_nonce`, all REST req/resp types (signed envelope built in `build(creds)`), `CryptoComRestClient` |
| `exchange::cryptocom::ws::` | `exchange/cryptocom/{ws,heartbeat_connection}.hpp` | Market + user feed: events, `CryptoComSubscribeRequest<PushMsg>`, `CryptoComAuthRequest` (`public/auth`), `cryptocom_frame_descriptor`, `CryptoComMarketClient`/`CryptoComUserClient` (`ExchangeWsClient` aliases) + factories (`MARKET_WS_URL`/`USER_WS_URL`), and the `HeartbeatResponder` `IWsConnection` decorator (plan 020 §2) |

**Why the split**: `exchange::common::*` holds the scaffold that is genuinely exchange-agnostic — request/response binding templates, the WS dispatch loop, the connection interface, reconnect machinery. Each adapter supplies only what's exchange-specific: wire formats, auth, endpoint URLs, and its `*_frame_descriptor`. `exchange::binance::*` reuses the entire common layer and follows the same three-tier shape as `exchange::kraken::*` — it is the worked reference for [adding a new exchange](#adding-a-whole-new-exchange). `exchange::cryptocom::*` likewise reuses the generic `ExchangeWsClient` (its WS is id-correlated like Binance/Kraken), adding only a `HeartbeatResponder` connection decorator for Crypto.com's mandatory keepalive — the common layer stays untouched except for an additive `IxWsConnection` handshake-header override (plan 020 §2).

---

## Shared types reference

### Canonical enumerations (`exchange/common/types.hpp`, namespace `exchange::`)

All enums have free-function converters: `to_string(Enum)` and `foo_from_string(const std::string&)`. The `*_from_string` functions throw `std::invalid_argument` on unknown input. `exchange::kraken::` re-exports all four via `using` declarations, so Kraken code refers to them unprefixed.

| Enum | Values |
|---|---|
| `Side` | `Buy`, `Sell` |
| `OrderType` | `Limit`, `Market`, `Iceberg`, `StopLoss`, `StopLossLimit`, `TakeProfit`, `TakeProfitLimit`, `TrailingStop`, `TrailingStopLimit`, `SettlePosition` |
| `TimeInForce` | `GTC`, `GTD`, `IOC` |
| `OrderStatus` | `PendingNew`, `New`, `PartiallyFilled`, `Filled`, `Canceled`, `Expired`, `Unknown` |

**`OrderType` wire format diverges per exchange — this is the one enum every adapter must convert itself.** The canonical `to_string`/`from_string` produce underscore-separated strings (`"stop_loss"`); Kraken's wire format uses hyphens (`"stop-loss"`). Kraken therefore keeps its own pair — `kraken_order_type_to_string` / `kraken_order_type_from_string` in `exchange::kraken::` — alongside (not instead of) the canonical converters. `test_order_type.cpp` asserts both produce the *correct, genuinely different* strings for every value. `Side`, `TimeInForce`, and `OrderStatus` have identical wire formats across both layers, so Kraken simply re-exports the canonical converters.

### Kraken-specific enumerations (`exchange/kraken/types.hpp`, namespace `exchange::kraken::`)

| Enum | Values |
|---|---|
| `PriceType` | `Static`, `Pct`, `Quote` |
| `TriggerReference` | `Index`, `Last` |
| `StpType` | `CancelNewest`, `CancelOldest`, `CancelBoth` |
| `FeePreference` | `Base`, `Quote` |

### `TickPrice` — exact-decimal price representation (`exchange/common/tick_price.hpp`, namespace `exchange::`)

```cpp
struct TickPrice {
    int64_t ticks{0};
    int     decimals{0};

    static TickPrice from(double price, int decimals);  // snap to the tick grid
    std::string      str() const;                       // exact decimal string
    static TickPrice from_json(const json&);
};
```

`TickPrice` is **exchange-agnostic** — it lives in `exchange::` (compiled into `libexchange_common.a`), not in any adapter. Kraken re-exports it (`using exchange::TickPrice;` in `exchange/kraken/types.hpp`), so `exchange::kraken::TickPrice` and the `kraken::TickPrice` compat alias keep resolving; an adapter that needs exact-decimal prices uses it directly. Stores a price as an integer tick count plus a decimal-point position. `str()` builds the decimal string by integer point-insertion — no floating-point formatting — so `TickPrice::from(3096217 * 0.0001, 4).str()` yields the exact `"309.6217"` rather than `"309.62169999..."`. `test_tick_price.cpp::FpNoiseBugRepro` is a dedicated regression test for exactly this class of bug (it links `exchange_common` and runs in any build).

### Core structs (namespace `exchange::kraken::`)

| Struct | Key fields | Used for |
|---|---|---|
| `Triggers` | `price`, `reference` (`TriggerReference`), `price_type` (`PriceType`) | Stop/trailing order trigger configuration |
| `Conditional` | `order_type` (`OrderType`), `limit_price`, `limit_price_type`, `trigger_price`, `trigger_price_type` | OTO (One Triggers Other) close orders |
| `OrderParams` | `order_type`, `side`, `symbol`, `limit_price`, `qty`, `display_qty`, `time_in_force`, `triggers`, `conditional`, `stp_type`, `fee_preference`, `reduce_only`, `sender_sub_id`, `client_order_id`, `order_userref`, `validate`, `token` | Universal order parameter block (26+ fields) |
| `OrderDescription` | `pair`, `type` (`Side`), `order_type` (`OrderType`), `price`, `price2`, `leverage`, `order`, `close` | Order metadata returned by REST API |
| `OrderInfo` | `status` (`OrderStatus`), `descr` (`OrderDescription`), `open_tm`, `close_tm`, `vol`, `vol_exec`, `cost`, `fee`, `avg_price`, `stop_price`, `limit_price`, `misc`, `oflags`, `trades` | Full order record |
| `TradeInfo` | `pair`, `price`, `vol`, `cost`, `fee`, `margin`, `time`, `type` (`Side`), `order_type` (`OrderType`), `pos_status`, `closing` | Trade execution record |
| `LedgerEntry` | `refid`, `time`, `type`, `subtype`, `aclass`, `asset`, `amount`, `fee`, `balance` | Ledger transaction record |

### `RestResponse<T>` and `parse_rest_response<T>` — Kraken's REST envelope

```cpp
template<typename T>
struct RestResponse {
    std::vector<std::string> errors;
    bool                     ok{false};
    std::optional<T>         result;
};

template<typename T>
RestResponse<T> parse_rest_response(const json& j);
```

This is the envelope type `KrakenRestClient::execute()` actually returns, and it lives in **`exchange::kraken::`** — not `exchange::rest::`. (The common scaffold separately defines a same-shaped `exchange::rest::RestResponse<T>` in `exchange/common/rest.hpp` for adapters that choose to build on it directly; Kraken kept its pre-refactor envelope type rather than switching, so every existing call site naming `exchange::kraken::RestResponse<T>` continues to resolve to the same type unchanged.)

Always check `resp.ok` before accessing `resp.result`.

---

## REST API reference (`exchange/kraken/rest_api.hpp`, namespace `exchange::kraken::rest::`)

### Authentication (`exchange/kraken/auth.hpp`)

```cpp
struct KrakenCredentials {
    std::string api_key;
    std::string api_secret;  // base64-encoded

    // sign(uri_path, nonce_str, post_body) → API-Sign header value
    // Algorithm: base64_encode(HMAC-SHA512(base64_decode(secret),
    //                          uri_path + SHA256(nonce + post_body)))
    std::string sign(const std::string& path,
                     const std::string& nonce_str,
                     const std::string& postdata) const;

    // Load from a TOML file (location empty ⇒ $HOME/.kraken/<name>).
    static KrakenCredentials from_file(const std::string& name,
                                 const std::string& location = "");
};

// Monotonic µs-based nonce generator — the only other public symbol in auth.hpp.
uint64_t make_nonce();
```

The base64/SHA-256/HMAC-SHA-512/URL-encoding helpers that back `sign()` are private implementation details in `exchange::kraken::rest::detail::` — they are not part of the public surface (encapsulated per the project's interface-design convention; only `KrakenCredentials` and `make_nonce()` are exposed). `test_signature.cpp` exercises `sign()` end-to-end against the legacy reference implementation rather than testing the helpers individually.

### Public REST endpoints

| Request type | HTTP | Path | Response type |
|---|---|---|---|
| `GetServerTimeRequest` | GET | `/0/public/Time` | `ServerTime` |
| `GetSystemStatusRequest` | GET | `/0/public/SystemStatus` | `SystemStatus` |
| `GetAssetInfoRequest` | GET | `/0/public/Assets` | `AssetInfoResult` |
| `GetAssetPairsRequest` | GET | `/0/public/AssetPairs` | `AssetPairsResult` |
| `GetTickerRequest` | GET | `/0/public/Ticker` | `TickerResult` |
| `GetOHLCRequest` | GET | `/0/public/OHLC` | `OHLCResult` |
| `GetOrderBookRequest` | GET | `/0/public/Depth` | `OrderBookResult` |
| `GetRecentTradesRequest` | GET | `/0/public/Trades` | `RecentTradesResult` |

### Private REST endpoints

| Request type | HTTP | Path | Response type |
|---|---|---|---|
| `GetAccountBalanceRequest` | POST | `/0/private/Balance` | `AccountBalanceResult` |
| `GetExtendedBalanceRequest` | POST | `/0/private/BalanceEx` | `ExtendedBalanceResult` |
| `GetTradeBalanceRequest` | POST | `/0/private/TradeBalance` | `TradeBalance` |
| `GetOpenOrdersRequest` | POST | `/0/private/OpenOrders` | `OpenOrdersResult` |
| `GetClosedOrdersRequest` | POST | `/0/private/ClosedOrders` | `ClosedOrdersResult` |
| `QueryOrdersRequest` | POST | `/0/private/QueryOrders` | `QueryOrdersResultWrapper` |
| `GetTradesHistoryRequest` | POST | `/0/private/TradesHistory` | `TradesHistoryResult` |
| `QueryTradesRequest` | POST | `/0/private/QueryTrades` | `QueryTradesResultWrapper` |
| `GetOpenPositionsRequest` | POST | `/0/private/OpenPositions` | `OpenPositionsResult` |
| `GetLedgersRequest` | POST | `/0/private/Ledgers` | `LedgersResult` |
| `QueryLedgersRequest` | POST | `/0/private/QueryLedgers` | `QueryLedgersResultWrapper` |
| `AddOrderRequest` | POST | `/0/private/AddOrder` | `AddOrderResult` |
| `AddOrderBatchRequest` | POST | `/0/private/AddOrderBatch` | `AddOrderBatchResult` |
| `EditOrderRequest` | POST | `/0/private/EditOrder` | `EditOrderResult` |
| `AmendOrderRequest` | POST | `/0/private/AmendOrder` | `AmendOrderResult` |
| `CancelOrderRequest` | POST | `/0/private/CancelOrder` | `CancelOrderResult` |
| `CancelAllOrdersRequest` | POST | `/0/private/CancelAllOrders` | `CancelAllResult` |
| `CancelAllOrdersAfterRequest` | POST | `/0/private/CancelAllOrdersAfter` | `CancelAllAfterResult` |
| `CancelOrderBatchRequest` | POST | `/0/private/CancelOrderBatch` | `CancelOrderBatchResult` |
| `GetWebSocketsTokenRequest` | POST | `/0/private/GetWebSocketsToken` | `WebSocketsTokenResult` |
| `GetDepositMethodsRequest` | POST | `/0/private/GetDepositMethods` | `DepositMethodsResult` |
| `GetDepositAddressesRequest` | POST | `/0/private/GetDepositAddresses` | `DepositAddressesResult` |
| `WithdrawRequest` | POST | `/0/private/Withdraw` | `WithdrawResult` |
| `CancelWithdrawalRequest` | POST | `/0/private/CancelWithdrawal` | `CancelWithdrawalResult` |
| `CreateSubaccountRequest` | POST | `/0/private/CreateSubaccount` | `CreateSubaccountResult` |
| `AllocateEarnRequest` | POST | `/0/private/AllocateEarn` | `EarnBoolResult` |
| `DeallocateEarnRequest` | POST | `/0/private/DeallocateEarn` | `EarnBoolResult` |

### REST client API (`exchange/kraken/rest_client.hpp`)

```cpp
class KrakenRestClient {
public:
    explicit KrakenRestClient(std::string base_url = "https://api.kraken.com");

    // Public endpoint — no credentials needed
    template<typename Req>
    RestResponse<typename Req::response_type> execute(const Req& req);

    // Private endpoint — credentials required
    template<typename Req>
    RestResponse<typename Req::response_type> execute(const Req& req,
                                                       const KrakenCredentials& creds);
};

// Test factory — injects a custom HTTP performer (no libcurl)
inline KrakenRestClient make_test_client(
    std::function<std::string(const HttpRequest&)> fn);
```

`HttpRequest` is re-exported from `exchange::rest::` (`using exchange::rest::HttpRequest;`) — it is one of the genuinely exchange-agnostic scaffold types.

---

## WebSocket API reference (`exchange/kraken/ws_api.hpp`, namespace `exchange::kraken::ws::`)

### WebSocket endpoints

| Endpoint | URL |
|---|---|
| Public | `wss://ws.kraken.com/v2` (`exchange::kraken::ws::PUBLIC_WS_URL`) |
| Private (authenticated) | `wss://ws-auth.kraken.com/v2` (`exchange::kraken::ws::PRIVATE_WS_URL`) |

### Authentication

```cpp
struct KrakenWsCredentials {
    std::string token;  // obtained via GetWebSocketsTokenRequest over REST
};
```

Private WebSocket channels require a session token, not the API key/secret directly. Obtain it once via REST, then pass it in WebSocket requests.

### Method-call request/response pairs

These use `execute()` / `execute_async()` (single request → single response):

| Request type | Response type | Purpose |
|---|---|---|
| `PingRequest` | `PongMessage` | Heartbeat / latency check |
| `AddOrderRequest` | `AddOrderResponse` | Place a new order |
| `AmendOrderRequest` | `AmendOrderResponse` | Amend an existing order's price/qty |
| `EditOrderRequest` | `EditOrderResponse` | Edit an existing order (replace price/qty) |
| `CancelOrderRequest` | `CancelOrderResponse` | Cancel one or more orders |
| `CancelAllRequest` | `CancelAllResponse` | Cancel all open orders |
| `CancelOnDisconnectRequest` | `CancelOnDisconnectResponse` | Set cancel-on-disconnect mode |
| `BatchAddRequest` | `BatchAddResponse` | Place multiple orders atomically |
| `BatchCancelRequest` | `BatchCancelResponse` | Cancel multiple orders atomically |

`AddOrderRequest`, `BatchAddRequest`, `EditOrderRequest`, and `AmendOrderRequest` accept a `KrakenWsCredentials` token for private channels. Every method-call request derives `exchange::ws::TypedWsRequest<R>` (re-exported in this namespace), which in turn derives `WsRequestBase` — the common scaffold's `int64_t req_id{0}` slot that `ExchangeWsClient` assigns automatically.

**`PingRequest`** structure — note that `req_id` is optional (unlike other requests where it is auto-assigned):

```cpp
struct PingRequest {
    std::optional<int64_t> req_id;
    // to_json() emits {"method":"ping"} + optional req_id
};
```

### Subscription channels

These use `subscribe()` / `subscribe_async()` (request → ack + continuous push callbacks):

| Channel enum | Type alias | Push message | Token required |
|---|---|---|---|
| `SubscribeChannel::Ticker` | `TickerSubscribeRequest` | `TickerMessage` | No |
| `SubscribeChannel::Book` | `BookSubscribeRequest` | `BookMessage` | No |
| `SubscribeChannel::Level3` | `Level3SubscribeRequest` | `Level3Message` | No |
| `SubscribeChannel::Trade` | `TradeSubscribeRequest` | `TradeMessage` | No |
| `SubscribeChannel::OHLC` | `OHLCSubscribeRequest` | `OHLCMessage` | No |
| `SubscribeChannel::Instrument` | `InstrumentSubscribeRequest` | `InstrumentMessage` | No |
| `SubscribeChannel::Executions` | `ExecutionsSubscribeRequest` | `ExecutionsMessage` | Yes |
| `SubscribeChannel::Balances` | `BalancesSubscribeRequest` | `BalancesMessage` | Yes |

`UnsubscribeRequest` mirrors `SubscribeRequest` for the same channels. Each `TypedSubscribeRequest<PushMsg, Ch>` additionally provides `route_key()` (→ `to_string(channel)`) and `unsubscribe_json()` (→ builds the matching `UnsubscribeRequest` frame) — the two members the generic `ExchangeWsClient::subscribe_async` needs and that make the dispatch loop exchange-agnostic (see [Architecture](#architecture-and-key-patterns) below).

### `BaseResponse` (inherited by all method-call responses)

```cpp
struct BaseResponse : exchange::ws::BaseWsResponse {  // success, error
    std::string                method;
    std::optional<int64_t>     req_id;
    std::optional<std::string> time_in;
    std::optional<std::string> time_out;
};
```

`exchange::ws::BaseWsResponse` is the common scaffold's minimal `{success, error}` base — `ExchangeWsClient`'s response-construction helper detects it via `std::is_base_of_v` to decide how to derive `WsResponse::ok`. Kraken's `BaseResponse` adds the Kraken-specific reply fields on top; the shape callers see is unchanged from before the refactor.

### `MessageKind` and dispatch

`identify_message(const json&)` classifies inbound frames by inspecting `"method"` (for replies) or `"channel"` (for push messages) and returns a Kraken-specific `MessageKind` — this is the **caller-facing** classifier for code that bypasses `KrakenWsClient` and handles raw frames itself (see [low-level dispatch](#websocket-message-dispatch-low-level)).

| MessageKind value | Trigger |
|---|---|
| `AddOrderResponse` | `"method": "add_order"` |
| `AmendOrderResponse` | `"method": "amend_order"` |
| `EditOrderResponse` | `"method": "edit_order"` |
| `CancelOrderResponse` | `"method": "cancel_order"` |
| `CancelAllResponse` | `"method": "cancel_all"` |
| `CancelOnDisconnectResponse` | `"method": "cancel_on_disconnect"` |
| `BatchAddResponse` | `"method": "batch_add"` |
| `BatchCancelResponse` | `"method": "batch_cancel"` |
| `Pong` | `"method": "pong"` |
| `SubscribeResponse` | `"method": "subscribe"` |
| `UnsubscribeResponse` | `"method": "unsubscribe"` |
| `Ticker` | `"channel": "ticker"` |
| `Book` | `"channel": "book"` |
| `Level3` | `"channel": "level3"` |
| `Trade` | `"channel": "trade"` |
| `OHLC` | `"channel": "ohlc"` |
| `Instrument` | `"channel": "instrument"` |
| `Executions` | `"channel": "executions"` |
| `Balances` | `"channel": "balances"` |
| `Status` | `"channel": "status"` |
| `Heartbeat` | `"channel": "heartbeat"` |
| `Unknown` | No match |

A second, **internal** classifier — `kraken_frame_descriptor(const json&) -> exchange::ws::FrameDescriptor` — drives `ExchangeWsClient`'s generic dispatch loop (it is Kraken's `MessageIdentifier`; see Architecture). It answers a coarser question (`MethodResponse` vs. `PushMessage`, plus a `correlation_id`/`route_key` string) than `MessageKind` does, and the two are independent: `identify_message` exists for callers who want fine-grained frame classification, `kraken_frame_descriptor` exists so `ExchangeWsClient` can route frames without knowing any Kraken-specific type names.

### `WsResponse<T>`

```cpp
template<typename T>
struct WsResponse {
    bool ok{false};
    std::optional<std::string> error;
    std::optional<T>           result;
};
```

Defined once in `exchange::ws::` and re-exported here. `ok` is derived from `success`/`error` for response types that derive `BaseWsResponse` (which includes Kraken's `BaseResponse`); for plain types like `PongMessage` it is always `true`.

---

## Binance adapter reference

The Binance adapter (`include/exchange/binance/`, `src/binance/rest_client.cpp`)
mirrors Kraken's three-tier shape on the same `exchange_common` scaffold. It is
the **worked reference** for [adding a new exchange](#adding-a-whole-new-exchange).
Captured wire formats live in
[docs/plans/001-appendix-binance-message-formats.md](docs/plans/001-appendix-binance-message-formats.md).

**How Binance differs from Kraken** (the per-adapter specifics):

| Aspect | Kraken | Binance |
|---|---|---|
| REST signing | HMAC-**SHA512**, base64, nonce | HMAC-**SHA256**, lowercase hex, `timestamp`(+`recvWindow`) |
| REST envelope | `{error[], result}` → `kraken::RestResponse<T>` | HTTP status + body → `exchange::rest::RestResponse<T>` via `parse_binance_response<T>(status, j)` |
| Credentials | `KrakenCredentials{api_key, api_secret}` or `::from_file()` | `BinanceCredentials{api_key, secret_key, recv_window_ms=5000}` — a plain struct; `from_file()` loads `api_key`/`api_secret` and keeps `recv_window_ms` default |
| WS auth | session token (`KrakenWsCredentials`) | per-request HMAC over sorted `params` (`BinanceWsCredentials` = alias for `BinanceCredentials`) |
| WS protocols | one endpoint (`ws_api.hpp`) | **two** — market streams (`ws_streams.hpp`) and a trading API (`ws_api.hpp`) |
| WS frame classifier | `identify_message` + `kraken_frame_descriptor` | `binance_{stream,ws_api}_frame_descriptor` only (no `identify_message` — not required) |

### Authentication (`exchange/binance/auth.hpp`, `exchange::binance::rest::`)

`BinanceAuth : exchange::rest::IRestAuth` signs private REST requests; the crypto
helpers (`detail::hmac_sha256`, `detail::to_hex`) are declared in the header and
defined in `src/binance/auth.cpp` (plan 016 — they are also used by the WS-API
signer). The signed payload is `query + body`, appended as a
`signature=<hex>` parameter, with `X-MBX-APIKEY` carrying the key.

### REST (`exchange/binance/{rest_api,rest_client}.hpp`, `exchange::binance::rest::`)

`BinanceRestClient` (default base `https://api.binance.com`) is templated exactly
like `KrakenRestClient`: `execute(req)` for public, `execute(req, creds)` for
private; both return `exchange::rest::RestResponse<Req::response_type>`. Public
requests derive `TypedPublicRequest<R>`, private derive `TypedPrivateRequest<R>`.
`make_test_client(fn)` injects a mock performer for tests.

| Public request | Path | Private request | Path |
|---|---|---|---|
| `BinancePingRequest` | `/api/v3/ping` | `BinanceAccountRequest` | `/api/v3/account` |
| `BinanceServerTimeRequest` | `/api/v3/time` | `BinanceOpenOrdersRequest` | `/api/v3/openOrders` (GET) |
| `BinanceTickerPriceRequest` | `/api/v3/ticker/price` | `BinanceAllOrdersRequest` | `/api/v3/allOrders` |
| `BinanceOrderBookRequest` | `/api/v3/depth` | `BinanceMyTradesRequest` | `/api/v3/myTrades` |
| `BinanceRecentTradesRequest` | `/api/v3/trades` | `BinanceNewOrderRequest` | `/api/v3/order` (POST) |
| `BinanceKlinesRequest` | `/api/v3/klines` | `BinanceCancelOrderRequest` | `/api/v3/order` (DELETE) |
| `BinanceExchangeInfoRequest` | `/api/v3/exchangeInfo` | `BinanceCancelAllOpenOrdersRequest` | `/api/v3/openOrders` (DELETE) |
| `BinanceTicker24hrRequest` | `/api/v3/ticker/24hr` | | |

### WebSocket market streams (`exchange/binance/ws_streams.hpp`, `exchange::binance::ws::`)

`BinanceStreamClient` (alias for `ExchangeWsClient`), built via
`make_binance_stream_client(conn)` or the URL factory over `STREAM_URL`
(`wss://stream.binance.com/stream`). `binance_stream_frame_descriptor` routes
`"stream"`-keyed pushes by stream name and `"id"`-keyed acks by `correlation_id`.
One stream per `SUBSCRIBE`; every event `from_json` unwraps the combined-stream
`{"stream","data"}` envelope.

| Subscribe alias | Push event | Stream-name helper |
|---|---|---|
| `BinanceAggTradeSubscribe` | `BinanceAggTradeEvent` | `agg_trade_stream` |
| `BinanceTradeSubscribe` | `BinanceTradeEvent` | `trade_stream` |
| `BinanceKlineSubscribe` | `BinanceKlineEvent` | `kline_stream` |
| `BinanceTickerSubscribe` | `BinanceTickerEvent` | `ticker_stream` |
| `BinanceMiniTickerSubscribe` | `BinanceMiniTickerEvent` | `mini_ticker_stream` |
| `BinanceBookTickerSubscribe` | `BinanceBookTickerEvent` | `book_ticker_stream` |
| `BinanceDepthSubscribe` | `BinanceDepthUpdateEvent` | `depth_stream` |
| `BinancePartialDepthSubscribe` | `BinancePartialDepth` | `partial_depth_stream` |

### WebSocket trading API (`exchange/binance/ws_api.hpp`, `exchange::binance::ws::`)

`BinanceWsApiClient` (alias for `ExchangeWsClient`), built via
`make_binance_ws_api_client(conn)` or the URL factory over `WS_API_URL`
(`wss://ws-api.binance.com/ws-api/v3`). Every reply is a `MethodResponse`
(no push concept); `binance_ws_api_frame_descriptor` correlates by `id`.
Responses derive `BinanceWsApiResponse : BaseWsResponse` (`ok = status < 400`).
Order requests sign their `params` per-request via `detail::ws_sign_params`
(alphabetically-sorted HMAC-SHA256) and **reuse the REST result structs** as
their payloads.

| Request | Method | Response |
|---|---|---|
| `BinanceWsPingRequest` | `ping` | `BinanceWsPongMessage` |
| `BinanceWsNewOrderRequest` | `order.place` | `BinanceWsNewOrderResponse` (wraps `rest::BinanceNewOrderResponse`) |
| `BinanceWsCancelOrderRequest` | `order.cancel` | `BinanceWsCancelOrderResponse` (wraps `rest::BinanceCancelOrderResponse`) |

---

## Coinbase adapter reference

The Coinbase adapter (`include/exchange/coinbase/`, `src/coinbase/`) targets the
**Coinbase Exchange** API (`api.exchange.coinbase.com`, ex–Coinbase Pro) and
mirrors the three-tier shape on the same `exchange_common` scaffold
([plan 018](docs/plans/018-coinbase-exchange-adapter.md)). **Orders are placed
over REST** (`POST`/`DELETE /orders`) — Coinbase has no WebSocket order entry;
the FIX order-entry path is recorded as deferred
([plan 019](docs/plans/019-coinbase-fix-order-entry.md)).

**How Coinbase differs** from the other adapters:

| Aspect | Coinbase |
|---|---|
| REST auth | HMAC-**SHA256**, base64 output; **three** creds (key + secret + **passphrase**); headers `CB-ACCESS-KEY/SIGN/TIMESTAMP/PASSPHRASE`; prehash = `timestamp + method + requestPath + body` (requestPath includes the query string) |
| REST envelope | HTTP status + body → `exchange::rest::RestResponse<T>` via `parse_coinbase_response<T>(status, j)` (a 2xx body *is* the result; an error body is `{"message":…}`) |
| Required header | every REST request needs a `User-Agent` — `CoinbaseRestClient` sets `cryptocogs` by default |
| WS model | **id-less pub/sub** — a **bespoke** `CoinbaseStreamClient` (not an `ExchangeWsClient` alias); optimistic subscribe, dispatch by message `type` (plan 018 §2, Option A) |
| Numbers | REST + WS monetary/size fields are JSON **strings** (`std::stod`); candle rows are positional JSON-number arrays |

### Authentication (`exchange/coinbase/auth.hpp`, `exchange::coinbase::rest`)

`CoinbaseCredentials{api_key, api_secret, passphrase}` — a plain struct (or
`CoinbaseCredentials::from_file(name, location)`, which reads all three keys from
a TOML file; see [Credentials file format](#credentials-file-format)).
`CoinbaseAuth : exchange::rest::IRestAuth` signs every request as
`base64(HMAC-SHA256(base64_decode(secret), timestamp + method + requestPath +
body))` and injects the four `CB-ACCESS-*` headers; the base64/HMAC-SHA256
primitives in `detail::` are reused by the WS user-channel signer. An injectable
`ClockFn` makes the timestamp deterministic in tests.

### REST (`exchange/coinbase/{rest_api,rest_client}.hpp`, `exchange::coinbase::rest`)

`CoinbaseRestClient` (default base `https://api.exchange.coinbase.com`):
`execute(req)` for public, `execute(req, const CoinbaseCredentials&)` for private
(constructs `CoinbaseAuth` and signs); both return
`exchange::rest::RestResponse<Req::response_type>`. `make_coinbase_test_client(fn)`
injects a mock performer. Order `price`/`size`/`funds` are caller-formatted exact
decimal strings — produce them with `TickPrice::str()`.

| Public request | Path | Private request | Path |
|---|---|---|---|
| `CoinbaseServerTimeRequest` | `/time` | `CoinbaseAccountsRequest` | `/accounts` |
| `CoinbaseProductsRequest` | `/products` | `CoinbaseAccountRequest` | `/accounts/{id}` |
| `CoinbaseProductRequest` | `/products/{id}` | `CoinbasePlaceOrderRequest` | `/orders` (POST) |
| `CoinbaseOrderBookRequest` | `/products/{id}/book` | `CoinbaseGetOrderRequest` | `/orders/{id}` |
| `CoinbaseTickerRequest` | `/products/{id}/ticker` | `CoinbaseListOrdersRequest` | `/orders` |
| `CoinbaseTradesRequest` | `/products/{id}/trades` | `CoinbaseCancelOrderRequest` | `/orders/{id}` (DELETE) |
| `CoinbaseCandlesRequest` | `/products/{id}/candles` | `CoinbaseCancelAllOrdersRequest` | `/orders` (DELETE) |
| `CoinbaseStatsRequest` | `/products/{id}/stats` | `CoinbaseFillsRequest` | `/fills` |

### WebSocket feed (`exchange/coinbase/ws_streams.hpp`, `exchange::coinbase::ws`)

`CoinbaseStreamClient`, built via `make_coinbase_stream_client(conn, eh=nullptr)`
over `STREAM_URL` (`wss://ws-feed.exchange.coinbase.com`). **Coinbase's feed has
no per-request correlation id** — its `subscriptions` ack is a full-state
broadcast — so it does *not* fit the generic id-correlated
`ExchangeWsClient::subscribe_async`. Per plan 018 §2 (Option A), this small
client reuses the same `IWsConnection` transport (and `IxWsConnection` /
`MockWsConnection` / `IWsErrorHandler` / `WsReconnectSession`) with **optimistic
subscribe** and dispatch keyed by the inbound message `type`. `exchange_common`
is untouched.

Typed channels: `subscribe_ticker` / `_matches` / `_level2` (snapshot + update) /
`_heartbeat` / `_full`, plus the authenticated `subscribe_user` (signs
`timestamp + "GET" + "/users/self/verify"`). `subscriptions` acks and `error`
frames route to optional callbacks (and the `IWsErrorHandler`);
`CoinbaseSubscriptionHandle::cancel()` removes the channel's callbacks, sends the
`unsubscribe` frame, and is idempotent.

---

## Crypto.com adapter reference

The Crypto.com adapter (`include/exchange/cryptocom/`, `src/cryptocom/`) targets
the **Crypto.com Exchange v1** API (`api.crypto.com/exchange/v1`,
`stream.crypto.com`) and mirrors the three-tier shape on the same
`exchange_common` scaffold ([plan 020](docs/plans/020-cryptocom-exchange-adapter.md)).
**Orders are placed over REST** (`private/create-order` / `private/cancel-order`);
advanced orders, bots, staking, fiat, and margin are out of scope.

Crypto.com's defining trait is a **single method-based envelope shared by REST and
WebSocket**: requests are `{id, method, api_key, params, nonce, sig}` and replies
`{id, method, code, result}` with **`code == 0` = success**.

**How Crypto.com differs** from the other adapters:

| Aspect | Crypto.com |
|---|---|
| REST auth | HMAC-**SHA256** → lowercase **hex**; secret is the HMAC key **verbatim** (not base64-decoded, unlike Kraken/Coinbase); **two** creds (key + secret, no passphrase) |
| Signed string | `sig = hex(HMAC-SHA256(secret, method + id + api_key + params_string + nonce))`; the `sig` lives **inside the JSON body**, so private requests sign themselves in `build(creds)` (Kraken-style), not via `IRestAuth` |
| `params_string` | recursive, **keys sorted ascending**, concatenated `key+value`; arrays iterate + recurse; `null`→`"null"`; `MAX_LEVEL = 3` (`detail::params_to_str`). Every param value is serialised as a **string** so the cross-language `str()` is deterministic (the API also requires numbers-as-strings) |
| REST verbs | public = GET (query params); private = POST (the signed envelope as the JSON body), all under `/exchange/v1/<method>` |
| REST envelope | HTTP status + body → `exchange::rest::RestResponse<T>` via `parse_cryptocom_response<T>(status, j)` (`ok = status < 400 && code == 0`; the inner `result` is `T::from_json`'d) |
| Numbers / keys | monetary/size fields are JSON **strings** (`std::stod`); ms timestamps are **numbers**; ticker/trade rows use **terse single-letter keys** (`i/h/l/a/b/k/v/…`) |
| WS model | **id-correlated** like Binance/Kraken → reuses the generic `ExchangeWsClient`. The misfit — a **mandatory heartbeat** — is handled by a `HeartbeatResponder` `IWsConnection` decorator (plan 020 §2 Option A); `exchange_common` is reused, with one additive `IxWsConnection` header override (see WS note) |

### Authentication (`exchange/cryptocom/auth.hpp`, `exchange::cryptocom::rest`)

`CryptoComCredentials{api_key, api_secret}` — a plain struct (or
`CryptoComCredentials::from_file(name, location)`, which reads both keys from a
TOML file; see [Credentials file format](#credentials-file-format)).
`creds.sign(method, id, params, nonce)` returns the hex `sig`; `make_nonce()` is a
millisecond epoch. The base64-free primitives (`detail::hmac_sha256`,
`detail::to_hex`, `detail::params_to_str`) are reused by the WS user-channel
`public/auth` signer. There is no `IRestAuth` implementor — signing is part of
envelope construction.

### REST (`exchange/cryptocom/{rest_api,rest_client}.hpp`, `exchange::cryptocom::rest`)

`CryptoComRestClient` (default base `https://api.crypto.com`): `execute(req)` for
public, `execute(req, const CryptoComCredentials&)` for private (assigns a unique
correlation `id`, then `req.build(creds)` signs the envelope); both return
`exchange::rest::RestResponse<Req::response_type>`. `make_cryptocom_test_client(fn)`
injects a mock performer. Order `price`/`quantity`/`notional` are caller-formatted
exact decimal strings — produce them with `TickPrice::str()`.
`CryptoComCreateOrderRequest::exec_inst` is an optional array of order-instruction
strings (e.g. `req.exec_inst = {EXEC_INST_POST_ONLY}` for a maker-only order),
serialised as a native JSON array and omitted from the body when unset or empty
([plan 024](docs/plans/024-cryptocom-create-order-exec-inst.md)).

| Public request | Path | Private request | Path |
|---|---|---|---|
| `CryptoComInstrumentsRequest` | `public/get-instruments` | `CryptoComUserBalanceRequest` | `private/user-balance` |
| `CryptoComTickersRequest` | `public/get-tickers` | `CryptoComCreateOrderRequest` | `private/create-order` |
| `CryptoComOrderBookRequest` | `public/get-book` | `CryptoComCancelOrderRequest` | `private/cancel-order` |
| `CryptoComCandlesRequest` | `public/get-candlestick` | `CryptoComCancelAllOrdersRequest` | `private/cancel-all-orders` |
| `CryptoComTradesRequest` | `public/get-trades` | `CryptoComGetOrderDetailRequest` | `private/get-order-detail` |
| | | `CryptoComGetOpenOrdersRequest` | `private/get-open-orders` |
| | | `CryptoComGetOrderHistoryRequest` | `private/get-order-history` |
| | | `CryptoComGetTradesRequest` | `private/get-trades` |

### WebSocket (`exchange/cryptocom/{ws,heartbeat_connection}.hpp`, `exchange::cryptocom::ws`)

`CryptoComMarketClient` / `CryptoComUserClient` are `ExchangeWsClient` aliases,
built via `make_cryptocom_market_client(conn, eh)` / `make_cryptocom_user_client(...)`
over `MARKET_WS_URL` / `USER_WS_URL`. Crypto.com's subscribe carries a per-request
`id` the ack echoes, so it fits the generic id-correlated client; the factories
wrap `conn` in a **`HeartbeatResponder`** that answers `public/heartbeat` with
`public/respond-heartbeat` and swallows it. `cryptocom_frame_descriptor` routes a
frame with `id == -1` as a **push** by `result.subscription`, and any other
id-bearing frame (subscribe ack / `public/auth`) as a **method response** by id.

Subscribe one channel per request (`CryptoComTickerSubscribe` / `CryptoComTradeSubscribe` /
`CryptoComBookSubscribe` / `CryptoComCandlestickSubscribe`, plus `CryptoComUserOrderSubscribe` /
`CryptoComUserBalanceSubscribe`); the channel string must be the **exact form the
server echoes** (e.g. candlestick period `1m`, not `M1`). The initial frame is the
ack (it may carry a snapshot); ongoing updates arrive via the callback. The user
feed calls `execute(CryptoComAuthRequest{creds})` (`public/auth`) before any
`user.*` subscribe.

> **Transport note:** ixwebsocket emits `Host: stream.crypto.com:443`, which
> Crypto.com's gateway rejects (HTTP 400). Construct the `IxWsConnection` with
> `{{"Host", std::string(WS_HOST)}}` to drop the port — `IxWsConnection` gained an
> optional handshake-header override for this (additive; other adapters
> unaffected).

> **Verification:** the market channels (ticker/trade/book/candlestick) are
> live-verified against prod; private REST and the user feed are tested against
> **synthetic** fixtures (no live credentials), and signing pins the *construction*
> rather than an official vector.

---

## Architecture and key patterns

### REST layer

Every public REST endpoint follows the **`TypedPublicRequest<R>`** pattern (the base classes `PublicRequest`/`PrivateRequest` are Kraken's; the `response_type` binding idiom mirrors `exchange::rest::TypedPublicRequest<R>` in the common scaffold):

```cpp
struct GetServerTimeRequest : PublicRequest {
    using response_type = ServerTime;           // links request → result type
    HttpRequest build() const;                  // produces path + query string
};
```

Every private REST endpoint follows **`TypedPrivateRequest<R>`**:

```cpp
struct GetAccountBalanceRequest : PrivateRequest {
    using response_type = AccountBalanceResult;
    HttpRequest build(const KrakenCredentials&) const; // adds nonce + HMAC signature
};
```

`KrakenRestClient::execute()` is templated on the request type. The compiler resolves `Req::response_type` at call-site, giving end-to-end type safety without any casts:

```cpp
auto resp = client.execute(GetServerTimeRequest{});
// resp is exchange::kraken::RestResponse<ServerTime>
```

### REST authentication (private endpoints)

1. Generate a nonce — a monotonically increasing `uint64` (microsecond timestamp via `make_nonce()`).
2. Build the POST body as `nonce=<value>[&extra_params]`.
3. Compute signature:
   ```
   msg    = URI_path + SHA256(nonce_string + POST_body)
   sign   = HMAC-SHA512(base64_decode(api_secret), msg)
   header = base64_encode(sign)
   ```
4. Send headers `API-Key` and `API-Sign` with the POST request.

`KrakenCredentials::sign(path, nonce_str, postdata)` in `exchange/kraken/auth.hpp` implements this (the supporting crypto primitives are private `detail::` helpers — see the [REST API reference](#rest-api-reference-exchangekrakenrest_apihpp-namespace-exchangekrakenrest)). Unit tests in `test_signature.cpp` verify it matches the legacy reference implementation byte-for-byte.

### WebSocket layer — `ExchangeWsClient` (generic) and `KrakenWsClient` (Kraken alias)

The entire request/subscription dispatch loop — pending-handler map, push-subscription map, pre-connection queue, `SubscriptionHandle`, thread safety — lives in **`exchange::ws::ExchangeWsClient`**, fully exchange-agnostic. It is parameterised at *construction time* (not via templates) by a `MessageIdentifier`:

```cpp
// exchange/common/ws.hpp
using MessageIdentifier = std::function<FrameDescriptor(const json&)>;

struct FrameDescriptor {
    FrameKind                  kind;            // MethodResponse | PushMessage | Unknown
    std::optional<std::string> correlation_id;  // MethodResponse: matches a pending request by req_id/id
    std::string                route_key;       // PushMessage: matches an active subscription callback
};
```

**`KrakenWsClient` is a type alias for `ExchangeWsClient`** — not a subclass, not a wrapper:

```cpp
// exchange/kraken/ws_client.hpp
using KrakenWsClient = exchange::ws::ExchangeWsClient;
```

Kraken supplies its `MessageIdentifier` as a free function, `kraken_frame_descriptor(const json&) -> FrameDescriptor` (in `exchange/kraken/ws_api.hpp`), and binds it once in the factory:

```cpp
// exchange/kraken/ws_client.hpp
inline std::shared_ptr<KrakenWsClient>
make_kraken_ws_client(std::shared_ptr<IWsConnection> conn,
                      std::shared_ptr<IWsErrorHandler> error_handler = nullptr) {
    return exchange::ws::make_exchange_ws_client(std::move(conn), kraken_frame_descriptor,
                                                  std::move(error_handler));
}
```

Existing code holding `shared_ptr<KrakenWsClient>` keeps compiling and behaving identically — it is the same runtime type as `shared_ptr<ExchangeWsClient>`. All the request/response/subscription types, method names, and call patterns described below are unchanged from before the refactor; only their *namespace and underlying client type* moved.

#### Method calls (single request → single response)

```cpp
// Blocking – waits up to timeout (default 5 s)
auto resp = client->execute(PingRequest{});              // WsResponse<PongMessage>

// Non-blocking – returns std::future
auto fut  = client->execute_async(AddOrderRequest{…});  // future<WsResponse<AddOrderResponse>>
```

#### Subscriptions (three-phase lifecycle)

```cpp
// Blocking – waits for the server ack (Phase 3)
auto [ack, handle] = client->subscribe(
    sub_req,
    [](TickerMessage msg) { /* push callback */ },
    std::chrono::milliseconds{10000}
);
if (!ack.ok) { /* handle error */ }

handle.cancel();  // unsubscribes; idempotent
```

**Three phases:**
1. **Phase 1** — WebSocket connection opens (`on_open` fires). Requests made before `on_open` are queued internally and flushed atomically when the socket opens.
2. **Phase 2** — `SubscribeRequest` is sent (with an auto-assigned unique `req_id`).
3. **Phase 3** — `SubscribeResponse` ack received and matched by `req_id`/`correlation_id`.
   - Success: push callback installed in the dispatch table; `SubscriptionHandle` is active.
   - Failure: push callback never installed; `SubscriptionHandle` is inactive.

Incoming server frames are dispatched to either a pending handler (matched by `correlation_id`, derived from `req_id`) or an active push subscription callback (matched by `route_key`, which Kraken derives from the `"channel"` field). `subscribe_async` itself contains **no Kraken-specific code** — it calls `req.route_key()` / `req.unsubscribe_json()` on the request and routes the ack through the generic `make_ws_response(Ack::from_json(j))`, so the same client drives any exchange whose subscribe requests satisfy that structural contract.

#### Internal state and thread safety

All mutable state is protected by `std::mutex` with RAII lock guards:

| Field | Type | Purpose |
|---|---|---|
| `next_req_id_` | `std::atomic<int64_t>` | Auto-incrementing request ID (1-based) |
| `connected_` | `std::atomic<bool>` | Connection state flag |
| `send_queue_` | `std::vector<std::string>` | Outbound messages queued before `on_open` |
| `pending_` | `std::map<std::string, handler>` | One-shot handlers keyed by `correlation_id` |
| `subscriptions_` | `std::map<std::string, callback>` | Active push callbacks keyed by `route_key` |

`SubscriptionHandle` holds a `std::weak_ptr<ExchangeWsClient>` and an atomic active flag, making `cancel()` safe to call from any thread and after the client is destroyed.

#### Connection abstraction and error handling

`IWsConnection` is a pure abstract interface with no ixwebsocket symbols visible to callers (now in `exchange::ws::`, `exchange/common/ws.hpp`):

```cpp
class IWsConnection {
public:
    virtual void connect()                    = 0;
    virtual void disconnect()                 = 0;
    virtual bool is_connected() const         = 0;
    virtual void send(const std::string& msg) = 0;
    virtual void set_on_message(MessageCb cb) = 0;
    virtual void set_on_open(OpenCb cb)       = 0;
    virtual void set_on_close(CloseCb cb)     = 0;
    virtual void set_on_error(ErrorCb cb)     = 0;
};
```

`IxWsConnection` (in `exchange/common/ix_ws_connection.hpp` — it has no Kraken-specific logic and moved to the common layer wholesale) implements this using ixwebsocket. Unit tests inject `MockWsConnection` instead — no network required.

`IWsErrorHandler` / `RateLimitedWsErrorHandler` (also in `exchange::ws::`) are an optional strategy for surfacing malformed-frame and connection-error events without flooding logs — `RateLimitedWsErrorHandler` logs at most once per configurable interval (default 60 s), tracking a dropped-event count in between. Pass a `shared_ptr<IWsErrorHandler>` to `make_kraken_ws_client` / `make_exchange_ws_client` to opt in; it defaults to `nullptr` (silent).

#### `WsReconnectSession` — generic reconnect/backoff machinery

Also moved to `exchange::ws::` (`exchange/common/reconnect_session.hpp` + `.inl`) — it contains zero protocol awareness (a background thread, mutex/cv, exponential backoff capped at 60 s, and two caller-supplied callbacks `ConnectFn`/`DisconnectFn`). `test_ws_reconnect_session.cpp` drives it deterministically via synchronous step injection — no `sleep_for` or wall-clock polling, per the project's testing conventions.

#### File split: `.hpp` / `.inl` / `.cpp`

| File | Contents |
|---|---|
| `exchange/common/ws_client.hpp` | `ExchangeWsClient` declaration, re-exports of `IWsConnection`/`SubscriptionHandle`/`WsResponse<T>` from `ws.hpp` |
| `exchange/common/ws_client.inl` | Template method bodies (`execute`, `execute_async`, `subscribe`, `subscribe_async`) — `#include`d at the bottom of the `.hpp` |
| `src/exchange/common/ws_client.cpp` | Non-template method bodies (`init`, `on_open_handler`, `on_raw_message`, `cancel_subscription`, `enqueue_or_send`) — exchange-agnostic |
| `exchange/common/ix_ws_connection.hpp` | `IxWsConnection` + the URL-string `make_exchange_ws_client(url, identifier, …)` factory |
| `exchange/common/reconnect_session.hpp` / `.inl` | `WsReconnectSession` |
| `exchange/kraken/ws_client.hpp` | `KrakenWsClient` alias, URL constants, `make_kraken_ws_client()` — a thin one-page wrapper over the common factory |

Include only `exchange/kraken/ws_client.hpp` (which pulls in `exchange/common/ws_client.hpp`) for test/mock usage. Include `exchange/common/ix_ws_connection.hpp` when you need the real ixwebsocket transport.

#### Factory functions

```cpp
// Generic — wraps an already-managed connection with any exchange's identifier.
// Defined in exchange/common/ws_client.inl via make_exchange_ws_client(conn, identifier, …).
std::shared_ptr<exchange::ws::ExchangeWsClient>
exchange::ws::make_exchange_ws_client(std::shared_ptr<IWsConnection> conn,
                                      MessageIdentifier identifier,
                                      std::shared_ptr<IWsErrorHandler> error_handler = nullptr);

// Generic — creates a fresh IxWsConnection, calls init() + connect().
// Defined in exchange/common/ix_ws_connection.hpp.
std::shared_ptr<exchange::ws::ExchangeWsClient>
exchange::ws::make_exchange_ws_client(const std::string& url,
                                      MessageIdentifier identifier,
                                      std::shared_ptr<IWsErrorHandler> error_handler = nullptr);

// Kraken — one-line wrappers binding kraken_frame_descriptor.
// Defined in exchange/kraken/ws_client.hpp.
std::shared_ptr<KrakenWsClient>
make_kraken_ws_client(std::shared_ptr<IWsConnection> conn, …);
```

All factories call `client->init()` — never call `init()` yourself.

### WebSocket authentication

Private WebSocket channels use a **session token** (not the API key/secret directly):

1. Call `GetWebSocketsTokenRequest` via the REST client to obtain a token.
2. Pass the token as the `"token"` field inside each WebSocket request's `params`.

`KrakenWsCredentials` wraps the token; `AddOrderRequest`, `SubscribeRequest`, etc. accept it directly.

### WebSocket message dispatch (low-level)

For callers that bypass `KrakenWsClient` and handle raw frames themselves — using `MessageKind`, **not** the internal `FrameDescriptor`/`kraken_frame_descriptor` pair that drives `ExchangeWsClient`:

```cpp
auto kind = exchange::kraken::ws::identify_message(json);
switch (kind) {
    case exchange::kraken::ws::MessageKind::Ticker:
        auto m = exchange::kraken::ws::TickerMessage::from_json(json);
        break;
    // ...
}
```

`identify_message()` inspects the `"method"` key for replies and the `"channel"` key for push messages.

---

## Adding a whole new exchange

To integrate a **new exchange** (not just an endpoint), follow the self-contained
playbook in [docs/agent-add-exchange.md](agent-add-exchange.md) — a commit-pinned
checklist that uses the Binance adapter as the worked reference. The sections
below cover adding endpoints/channels to an *existing* adapter.

## Adding a new REST endpoint

1. **Declare request and response types** in `exchange/kraken/rest_api.hpp`:
   - Public: inherit `PublicRequest`, define `using response_type = YourResult`, implement `build()`.
   - Private: inherit `PrivateRequest`, define `using response_type = YourResult`, implement `build(const KrakenCredentials&)`.
   - Add `YourResult::from_json(const json&)` as a static method.

2. **Add a unit test** in `tests/unit/test_rest_requests.cpp` verifying the path, method, and body fields, and in `test_rest_responses.cpp` verifying JSON deserialization.

3. No changes needed to `KrakenRestClient` — it is fully templated.

---

## Adding a new WebSocket method call

1. Add a request struct in `exchange/kraken/ws_api.hpp`:
   - Inherit `exchange::ws::TypedWsRequest<YourResponse>` (brings `response_type` and the `req_id` slot from `WsRequestBase`).
   - `json to_json() const` — must serialise `req_id` into Kraken's correlation field (`"req_id"`).
2. Add `YourResponse` with `static YourResponse from_json(const json&)`.
   - If the server returns `success`/`error` fields, inherit `BaseResponse` (which itself derives the common `BaseWsResponse`).
3. Add the new `MessageKind` enum value and handle it in **both** `identify_message()` (caller-facing classifier) and `kraken_frame_descriptor()` (drives `ExchangeWsClient` — needs a `correlation_id`).
4. Add unit tests in `test_ws_client.cpp` using `MockWsConnection`.

---

## Adding a new WebSocket subscription

`TypedSubscribeRequest` binds the push message type and the channel **at compile time** via two template parameters, and supplies the two members (`route_key()`, `unsubscribe_json()`) the generic `ExchangeWsClient::subscribe_async` needs:

```cpp
template<typename PushMsg, SubscribeChannel Ch>
struct TypedSubscribeRequest : SubscribeRequest {        // SubscribeRequest : WsRequestBase
    using push_type     = PushMsg;
    using response_type = SubscribeResponse;
    static constexpr SubscribeChannel channel_value = Ch;
    TypedSubscribeRequest() { this->channel = Ch; }      // channel set automatically
    std::string route_key() const { return to_string(channel); }
    json        unsubscribe_json() const;                // builds the matching UnsubscribeRequest frame
};
```

To add a new subscription channel:

1. Add the `SubscribeChannel` enum value in `exchange/kraken/ws_api.hpp`.
2. Add the channel → string mapping in `to_string(SubscribeChannel)`.
3. Add `YourPushMessage` with `static YourPushMessage from_json(const json&)`.
4. Add the `MessageKind` enum value and handle the `"channel"` string in **both** `identify_message()` and `kraken_frame_descriptor()` (the latter sets `route_key` from `"channel"`).
5. Add a convenience type alias:
   ```cpp
   using YourSubscribeRequest = TypedSubscribeRequest<YourPushMessage, SubscribeChannel::YourChannel>;
   ```
6. Add unit tests in `test_ws_client.cpp`.

---

## Adding a new low-level WebSocket message type

1. Add request struct with `json to_json() const` and response struct with `static T from_json(const json&)` in `exchange/kraken/ws_api.hpp`.
2. Add the new `MessageKind` enum value, and handle the new method/channel string in `identify_message()`.
3. If the message participates in `ExchangeWsClient` dispatch (method calls and subscriptions do; one-way pushes you only inspect manually do not), also handle it in `kraken_frame_descriptor()`.

---

## Testing without network

### REST (mock HTTP performer)

Unit tests inject a custom HTTP performer via `make_test_client`:

```cpp
auto client = make_test_client([](const exchange::kraken::rest::HttpRequest& http) -> std::string {
    // inspect http.path, http.method, http.body, http.headers
    return R"({"error":[],"result":{...}})";
});
auto resp = client.execute(SomeRequest{});
```

This factory is declared `inline` in `exchange/kraken/rest_client.hpp` and friends `KrakenRestClient`'s private constructor, so no changes to the library source are needed.

### WebSocket (MockWsConnection)

Unit tests create a `MockWsConnection` (defined in `test_ws_client.cpp`) and inject it via `make_kraken_ws_client(conn)`:

```cpp
auto conn   = std::make_shared<MockWsConnection>();
auto client = exchange::kraken::ws::make_kraken_ws_client(
                  std::static_pointer_cast<exchange::ws::IWsConnection>(conn));

conn->fire_open();            // simulate connection open
conn->sent_messages;          // inspect outbound messages (std::vector<std::string>)
conn->inject_message(raw);    // inject inbound server frame
conn->fire_close();           // simulate disconnect
```

`MockWsConnection::connect()` does **not** auto-fire `on_open` — tests call `fire_open()` explicitly, enabling precise control over the pre-connection outbound queue.

---

## Credentials file format

Every adapter's credential struct has a `from_file(name, location = "")` loader
([plan 021](docs/plans/021-credentials-from-file.md)). All four parse the **same
TOML format** — top-level string keys — via the shared
`exchange::rest::read_toml_credentials` helper (in `exchange_common`, backed by
toml++):

```toml
api_key    = "PUBLICKEY..."
api_secret = "secret..."
# passphrase = "..."   # Coinbase only
```

| Adapter | Default dir | Required keys |
|---|---|---|
| Kraken | `~/.kraken` | `api_key`, `api_secret` |
| Binance | `~/.binance` | `api_key`, `api_secret` (→ `secret_key`; `algorithm`/`recv_window_ms` keep defaults) |
| Coinbase | `~/.coinbase` | `api_key`, `api_secret`, `passphrase` |
| Crypto.com | `~/.cryptocom` | `api_key`, `api_secret` |

`location` empty ⇒ `$HOME/<dir>/<name>`; otherwise `<location>/<name>` (no `~`
expansion, no forced `.toml` extension). Throws `std::runtime_error` if the file
is missing/malformed or a key is absent, non-string, or empty. Load in code:

```cpp
auto creds = exchange::kraken::rest::KrakenCredentials::from_file("default");
// exchange::binance::rest::BinanceCredentials::from_file(...)  — ~/.binance/<name>
// exchange::coinbase::rest::CoinbaseCredentials::from_file(...) — ~/.coinbase/<name>
// exchange::cryptocom::rest::CryptoComCredentials::from_file(...) — ~/.cryptocom/<name>
```

> **Breaking change (v0.5.0):** Kraken's loader previously parsed a bespoke
> two-line text file. Existing `~/.kraken/<name>` files must be rewritten as TOML.

---

## File header

Every `.hpp`, `.inl`, and `.cpp` file — including everything under `include/exchange/` and `src/exchange/` — must begin with the following banner:

```cpp
// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
```

Place the banner before `#pragma once` (for headers) or before the first `#include` (for source files).

---

## Coding conventions

- **C++23** throughout (plan 029; no C++23 language or library feature is adopted yet); use structured bindings, `if constexpr`, `std::optional`, `std::string_view` where appropriate.
- **Every public header refuses to compile below C++23.** `exchange/common/cxx_standard.hpp` holds the `#error` (it tests for newer than C++20, because GCC 13 reports 202100L for `-std=c++23`), and the seven headers that include no other `exchange/` header include it first: `common/{credentials_file,tick_price,types,ws,rest,reconnect_session}.hpp` and `cryptocom/auth.hpp`. A new public header either includes one of them or includes the guard itself; `test_standard_guard` preprocesses every `include/exchange/*/*.hpp` at `-std=c++17` and `-std=c++20` and fails, naming it, for one that does not stop at the guard. A library added to `src/` carries `target_compile_features(<name> PUBLIC cxx_std_23)`, and `test_standard_requirement` fails for one whose consumers would be compiled at another standard.
- All optional fields on request and response structs use `std::optional<T>`. Only set them when needed; omitted fields are not serialised.
- JSON serialisation uses `to_json()` / `from_json()` static methods on each struct. Prefer `j.value("key", default)` over `j.at("key")` for fields that may be absent in responses.
- Enum conversions are done by free functions `to_string(Enum)` and `foo_from_string(const std::string&)`. The canonical four (`Side`, `OrderType`, `TimeInForce`, `OrderStatus`) live in `exchange/common/types.hpp`; Kraken-only enums (`PriceType`, `TriggerReference`, `StpType`, `FeePreference`) and Kraken's hyphenated `OrderType` overrides (`kraken_order_type_to_string`/`from_string`) live in `exchange/kraken/types.hpp`. All throw `std::invalid_argument` on unknown values.
- Monetary / volume fields returned by the REST API arrive as JSON **strings** (e.g., `"1.5"`) — deserialise with `std::stod(j.value("field", "0"))` rather than `.get<double>()`. For prices that must round-trip to an *exact* decimal string (e.g. for order placement), use `TickPrice` instead of raw `double` formatting — see [Shared types reference](#tickprice--exact-decimal-price-representation).
- The build produces **six** static libraries — two always-built common libs plus the four adapters. `exchange_common` compiles the exchange-agnostic non-template code — `src/exchange/common/{ws_client,ws,types,tick_price,credentials_file}.cpp` (generic WS dispatch, `SubscriptionHandle`/`RateLimitedWsErrorHandler`, canonical enum converters, `TickPrice`, and the shared `read_toml_credentials` TOML loader) — and links `nlohmann_json` (`PUBLIC`) plus toml++ (`PRIVATE`, build-only — header-only, used only by `credentials_file.cpp`), **not** OpenSSL/libcurl (the WS engine needs no HTTP/crypto). `exchange_http` compiles `src/exchange/common/http_client.cpp` (the shared `CurlHttpClient` REST transport) and links `CURL::libcurl` + `nlohmann_json` (`PUBLIC`) — kept separate so `exchange_common` stays curl-free (plan 010). `kraken` (six `.cpp` under `src/kraken/`: types, auth, rest_api, rest_client, ws_api, ws_client), `binance` (six under `src/binance/`: types, auth, rest_api, rest_client, ws_streams, ws_api), `coinbase` (five under `src/coinbase/`: types, auth, rest_api, rest_client, ws_streams), and `cryptocom` (six under `src/cryptocom/`: types, auth, rest_api, rest_client, heartbeat_connection, ws) are **peers**: each links `exchange_common`, `exchange_http`, and OpenSSL (`PUBLIC`), and **none links another**. OpenSSL is `PUBLIC` because `auth.hpp` declares HMAC/SHA helpers in a public header (defined in `auth.cpp`); libcurl reaches consumers transitively through `exchange_http`. After plan 010 the curl handling lives once in `CurlHttpClient`; after plan 016 every adapter header is purely declarative — request/response bodies live in these `.cpp` files and template bodies in sibling `.inl` files. None of the libraries link ixwebsocket; callers that use `IxWsConnection` must link `ixwebsocket` separately.
- IXWebSocket and spdlog are **not** linked into any of the three libraries; they are used only by examples and tests.
- Template methods for `ExchangeWsClient` live in `exchange/common/ws_client.inl` (included at the bottom of the `.hpp`). Non-template methods live in `src/exchange/common/ws_client.cpp`. The generic client itself is exchange-agnostic; each adapter supplies its `*_frame_descriptor()` plus its WS request/response types — whose bodies, post-plan-016, live in the adapter's `ws_api.cpp` (Kraken also has `ws_client.cpp` for `make_kraken_ws_client`; its `TypedSubscribeRequest` template bodies are in `ws_api.inl`). Keep the hpp/inl/cpp split consistent when adding new methods.
- Push callbacks stored in `subscriptions_` are type-erased to `std::function<void(const json&)>` internally; the typed lambda wrapper is created once in the template method and stored at subscription time.
- **The deprecated `kraken::` / `kraken_*.hpp` compatibility shim was removed in v0.1.1** ([plan 014](docs/plans/014-remove-compat-shim.md)). `exchange::kraken::*` is the only Kraken surface; the [migration guide](docs/plans/001-appendix-migration-guide.md) maps every old name to its replacement.

---

## Running examples

```bash
# Public (no credentials needed)
./<tree>/bin/public_rest
./<tree>/bin/public_ws BTC/EUR
./<tree>/bin/ws_client_example BTC/USD     # KrakenWsClient subscription + connection reuse
./<tree>/bin/rest_client_example time      # CLI11 demo — try `--help` for every public-endpoint subcommand

# Private (requires ~/.kraken/default)
./<tree>/bin/private_rest
./<tree>/bin/private_ws

# Combined REST + WS demo
./<tree>/bin/kraken_example

# Binance (all public — no credentials)
./<tree>/bin/binance_rest_client_example ticker --symbol BTCUSDT   # --help lists every endpoint
./<tree>/bin/binance_ws_client_example aggtrade BTCUSDT            # one of 8 stream subcommands
./<tree>/bin/binance_ws_api_example ping                          # trading WS API heartbeat

# Coinbase (all public — no credentials)
./<tree>/bin/coinbase_rest_client_example ticker BTC-USD          # --help lists every endpoint
./<tree>/bin/coinbase_ws_client_example ticker BTC-USD            # one of 5 channel subcommands

# Crypto.com (all public — no credentials)
./<tree>/bin/cryptocom_rest_client_example tickers BTC_USD        # --help lists every endpoint
./<tree>/bin/cryptocom_ws_client_example ticker BTC_USD --seconds 35   # one of 5 channel subcommands
```

Callers that embed a REST client must call `curl_global_init(CURL_GLOBAL_ALL)` before constructing `KrakenRestClient` / `BinanceRestClient` / `CryptoComRestClient` and `curl_global_cleanup()` on teardown.
