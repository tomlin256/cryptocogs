# Appendix to multi-exchange-abstraction — Shipped Backwards-Compatibility Shim

Companion to [001-multi-exchange-abstraction.md](001-multi-exchange-abstraction.md) and [001-appendix-migration-guide.md](001-appendix-migration-guide.md).

This supersedes the earlier "no backwards-compatibility aliases" decision. The library **ships** a deprecated compatibility shim so existing `cryptocogs` consumers can adopt the refactored library with **zero source changes**, confirm everything builds and behaves, and only then migrate to the `exchange::…` API at their own pace using the migration guide.

The key difference from the hand-rolled shim sketched in the migration guide §7: because the shipped shim uses **real namespaces with forwarder functions** (not just namespace aliases), it covers the two things the alias-only version could not — `make_ws_client(url)` and the mock `make_ws_client(conn)` overload. The shipped shim is therefore **fully transparent**: a pre-refactor translation unit compiles and runs unchanged.

---

## 1. Goals and governance

- **Transparency**: any code that compiled against the pre-refactor `kraken::` / `kraken_*.hpp` surface compiles and behaves identically with the shim, with no edits.
- **Visibility**: every use of the old surface emits a deprecation diagnostic pointing at the migration guide, suppressible with one macro.
- **Opt-out**: a CMake option builds/installs the shim; default **ON** for the deprecation window. Clients flip it **OFF** to prove their tree is clean before the shim is removed.
- **Removal milestone**: the shim ships in the version that introduces the refactor and is removed **no earlier than the next major version**. State the removal version in the shim headers and `README`.

This is **source compatibility**, not ABI compatibility — the shim is header-only forwarding; callers recompile against the new library.

---

## 2. Design — two layers

### Layer 1 — forwarding headers at the original include paths

Existing callers do `#include "kraken_rest_client.hpp"`. The shim keeps headers at those exact paths so include lines resolve unchanged. Each is a thin forwarder: a deprecation pragma, an include of the new header, and an include of the namespace shim.

```cpp
// include/kraken_rest_client.hpp  (shim; only when CRYPTOCOGS_BUILD_COMPAT_SHIM=ON)
#pragma once
#ifndef CRYPTOCOGS_SUPPRESS_DEPRECATION
#  pragma message("kraken_rest_client.hpp is deprecated; include exchange/kraken/rest_client.hpp. See docs/plans/001-appendix-migration-guide.md (removed in vNEXT_MAJOR).")
#endif
#include "exchange/kraken/rest_client.hpp"
#include "kraken_compat.hpp"   // the namespace shim (Layer 2)
```

One forwarder per old header: `kraken_types.hpp`, `kraken_rest_api.hpp`, `kraken_rest_client.hpp`, `kraken_ws_api.hpp`, `kraken_ws_client.hpp`, `kraken_ix_ws_connection.hpp`, `ws_reconnect_session.hpp`.

### Layer 2 — the namespace shim (`kraken_compat.hpp`)

Reconstructs the old `kraken::` namespace tree on top of the new layout. It uses **real namespaces** (so it can also declare forwarder functions), `using namespace` directives to pull in the bulk of the re-prefixed names, targeted `using` declarations for the generic bases, a deprecated type alias for the renamed client class, and inline forwarders for the two factory functions.

```cpp
// include/kraken_compat.hpp  (shim)
#pragma once
#include "exchange/kraken/rest_api.hpp"
#include "exchange/kraken/rest_client.hpp"
#include "exchange/kraken/ws_api.hpp"
#include "exchange/kraken/ws_client.hpp"          // make_kraken_ws_client + URL consts
#include "exchange/common/ix_ws_connection.hpp"   // make_generic_ws_client
#include "exchange/common/reconnect_session.hpp"

namespace kraken {

    // ── shared enums lifted to exchange:: ───────────────────────────────────
    using exchange::Side;
    using exchange::OrderType;
    using exchange::TimeInForce;
    using exchange::OrderStatus;

    // ── Kraken-specific structs that used to live in kraken:: ───────────────
    using exchange::kraken::TickPrice;
    using exchange::kraken::OrderParams;
    using exchange::kraken::Triggers;
    using exchange::kraken::Conditional;
    using exchange::kraken::OrderInfo;
    using exchange::kraken::TradeInfo;
    using exchange::kraken::LedgerEntry;
    using exchange::kraken::OrderDescription;
    using exchange::kraken::PriceType;
    using exchange::kraken::TriggerReference;
    using exchange::kraken::StpType;
    using exchange::kraken::FeePreference;

    // parse_rest_response was reachable unqualified via `using namespace kraken`
    using exchange::kraken::rest::parse_rest_response;

    namespace rest {
        using namespace exchange::kraken::rest;   // all *Request / *Result / Credentials
        using exchange::rest::RestResponse;       // generic bases
        using exchange::rest::HttpRequest;
        using exchange::rest::TypedPublicRequest;
        using exchange::rest::TypedPrivateRequest;
    }

    namespace ws {
        using namespace exchange::kraken::ws;     // *SubscribeRequest, *Message,
                                                  // SubscribeChannel, MessageKind,
                                                  // identify_message, WsCredentials,
                                                  // PUBLIC_WS_URL, PRIVATE_WS_URL
        using exchange::ws::IWsConnection;        // generic / transport
        using exchange::ws::IWsErrorHandler;
        using exchange::ws::IxWsConnection;
        using exchange::ws::WsResponse;
        using exchange::ws::SubscriptionHandle;
        using exchange::ws::GenericWsClient;

        // old class name
        using KrakenWsClient [[deprecated("use exchange::ws::GenericWsClient")]]
            = exchange::ws::GenericWsClient;

        // old factory: URL form → renamed make_kraken_ws_client
        [[deprecated("use exchange::kraken::ws::make_kraken_ws_client; see migration guide")]]
        inline std::shared_ptr<exchange::ws::GenericWsClient>
        make_ws_client(const std::string& url,
                       std::shared_ptr<exchange::ws::IWsErrorHandler> eh = nullptr) {
            return exchange::kraken::ws::make_kraken_ws_client(url, std::move(eh));
        }

        // old factory: managed/mock connection form → make_generic_ws_client + identifier
        [[deprecated("use exchange::ws::make_generic_ws_client(conn, identify_message)")]]
        inline std::shared_ptr<exchange::ws::GenericWsClient>
        make_ws_client(std::shared_ptr<exchange::ws::IWsConnection> conn,
                       std::shared_ptr<exchange::ws::IWsErrorHandler> eh = nullptr) {
            return exchange::ws::make_generic_ws_client(
                       std::move(conn), exchange::kraken::ws::identify_message,
                       std::move(eh));
        }
    }
}
```

Why this works where the alias-only version did not: `namespace ws` here is a *real* namespace, so the two `make_ws_client` forwarders are legal. A namespace **alias** (`namespace ws = exchange::kraken::ws;`) cannot have new members added, which is why the migration-guide §7 shim left those two as residual manual edits. The shipped shim removes that gap.

This relies on the Step 2 re-exports (the `exchange::kraken::rest`/`ws` namespaces re-export the common bases). Where a generic base is referenced explicitly above (`exchange::rest::RestResponse`, …) that is belt-and-suspenders and harmless if also re-exported.

---

## 3. Deprecation signalling

| Surface | Mechanism |
|---|---|
| Old header included | `#pragma message(...)` at top of each forwarder, guarded by `CRYPTOCOGS_SUPPRESS_DEPRECATION` |
| Old factory called | `[[deprecated("…")]]` on the `make_ws_client` forwarders → compiler warning at the call site |
| Old class name used | `[[deprecated]]` on the `KrakenWsClient` alias |
| Bulk re-exported types | no per-name attribute possible (brought in via `using namespace`); the header-level pragma covers these |

`-DCRYPTOCOGS_SUPPRESS_DEPRECATION` (or `#define` before the include) silences the pragmas for clients who have acknowledged the deprecation but are not ready to migrate. `[[deprecated]]` warnings remain unless they also suppress those via their own warning flags — intentional, so the migration is visible but not blocking.

---

## 4. Build and install

```cmake
# top-level CMakeLists.txt
option(CRYPTOCOGS_BUILD_COMPAT_SHIM
       "Install deprecated kraken_*.hpp compatibility headers (source-compat for pre-refactor callers)"
       ON)
```

- The shim is header-only. When the option is ON, the `cryptocogs::kraken` target's install rules also install the shim headers (`kraken_*.hpp`, `kraken_compat.hpp`) under the same include prefix, so an existing `find_package(cryptocogs)` + `target_link_libraries(app cryptocogs::kraken)` keeps resolving the old include paths with **no client CMake change**.
- Optionally expose a distinct `cryptocogs::compat` INTERFACE target (links `cryptocogs::kraken`, adds the shim include dir) for clients who prefer to opt in explicitly rather than get the shim transparently. Recommend the transparent install as the default; document the interface target as the explicit alternative.
- When the option is OFF, none of the `kraken_*.hpp` paths exist → a pre-refactor TU fails to find the header, which is exactly the signal a client wants when verifying they have fully migrated.

---

## 5. Tests — proving the shim is transparent

Two layers of proof, both gated on `CRYPTOCOGS_BUILD_COMPAT_SHIM=ON`:

1. **Compile-proof from preserved originals** — keep the **unmodified** pre-refactor `rest_client_example.cpp` and `ws_client_example.cpp` under `tests/compat/` (verbatim, still using `kraken::` names and old includes). They are compiled (not necessarily run) against the shim. If they build, the public surface is intact. This is the highest-signal, lowest-maintenance check: the originals are exactly what real clients wrote.
2. **Behavioural `tests/unit/test_compat_shim.cpp`** — drives the shim through old names with the existing mock infrastructure, asserting outcomes (not just compilation):
   - Public REST round-trip via `make_test_client` using `kraken::rest::GetServerTimeRequest` → assert parsed fields.
   - WS subscribe via `MockWsConnection` through **`kraken::ws::make_ws_client(conn)`** (exercises the mock-connection forwarder) → fire_open, inject a ticker frame, assert the callback fires with a `kraken::ws::TickerMessage`.
   - `kraken::ws::make_ws_client(url)` overload resolves to the URL forwarder (compile-time check via `static_assert` on the return type, no network).
   - Build the test TU with `-DCRYPTOCOGS_SUPPRESS_DEPRECATION` so the suite is warning-clean; a separate one-line target without the suppress confirms the pragma fires (optional).

Determinism is preserved — all WS testing is `MockWsConnection`-driven, no sleeps (project guideline).

---

## 6. Client adoption workflow

The whole point — a client can sequence their migration with confidence:

1. **Drop in** the new library version. Shim is ON by default; existing `#include "kraken_*.hpp"` + `kraken::` code compiles untouched.
2. **Build and run** their own test suite. Deprecation pragmas list what they use; behaviour is identical.
3. **Gain confidence** that the refactored library works in their environment.
4. **Migrate incrementally** using [001-appendix-migration-guide.md](001-appendix-migration-guide.md) — file by file, at their pace, deprecation warnings shrinking as they go.
5. **Verify clean** by configuring with `-DCRYPTOCOGS_BUILD_COMPAT_SHIM=OFF`; a green build proves no remaining dependency on the old surface.
6. **Done** before the shim's removal version; no fire-drill at the major bump.

---

## 7. Caveats

- The shim restores **source** compatibility only; it does not freeze ABI (header-only, recompile required).
- Names brought in via `using namespace` cannot carry per-symbol `[[deprecated]]`; the header pragma is the deprecation signal for those. The two factory functions and the `KrakenWsClient` alias do carry `[[deprecated]]`.
- The shim is Kraken-only by definition; it has no bearing on the Binance API, which is new and has no legacy surface.
- If a client defined their own `namespace kraken { … }` extensions, reopening the namespace in the shim coexists fine, but a name they added that now collides with a re-exported name would be ambiguous — vanishingly unlikely, and surfaced at compile time.
