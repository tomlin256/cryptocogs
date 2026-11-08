# Extract a shared cURL HTTP transport (de-duplicate the REST clients)

**Status**: Done — implemented in commits `f17f66c`, `2153208`, `e2d79fc` + docs wrap-up
**Branch**: `feature/multi-exchange-abstraction`

> **Done**: the duplicated libcurl transport is now a single
> `exchange::rest::CurlHttpClient` (`include/exchange/common/http_client.hpp` +
> `src/exchange/common/http_client.cpp`) in a new always-built `exchange_http`
> static library (Decision 1, recommended option — `exchange_common` stays
> curl-free). `HttpResponse{status, body}` is the unified return; the handle is a
> `unique_ptr<CURL,deleter>` (folds in review nit **N1**). Both adapters' clients
> now hold a `shared_ptr<CurlHttpClient>` + an `HttpResponse` performer and keep
> only their constructors out-of-line — **both `rest_client.cpp` files dropped
> from ~100 lines to ~30, zero `curl_*` calls in either** (`nm`-verified: the
> `curl_easy_perform` symbol lives only in `libexchange_http.a`, not in
> `libkraken.a`/`libbinance.a`). Kraken's `make_test_client` stayed
> body-only (the client adapts it to `{200, body}` internally), so **every
> existing Kraken test compiled unchanged**; only Binance's four test lambdas
> changed `-> std::pair<int,std::string>` to `-> HttpResponse`. **One correctness
> fold-in**: `CurlHttpClient::perform` resets `CURLOPT_CUSTOMREQUEST` before
> choosing a method, fixing a latent sticky-method bug (a DELETE-then-GET on the
> same reused handle would otherwise still send DELETE). Suite stayed 300 green at
> every checkpoint; both live REST examples (Binance `time`, Kraken `time`) and a
> `KRAKEN=OFF` build were re-verified end-to-end through the new transport.
> Out of scope as planned: the auth-model unification ([plan generic-rest-client](011-generic-rest-client.md))
> and the REST error-model hardening (review M2).

---

## Goal

`src/kraken/rest_client.cpp` and `src/binance/rest_client.cpp` duplicate the
entire libcurl transport — `write_cb`, the production constructor
(`curl_easy_init` + throw), the destructor (`curl_easy_cleanup`), and ~90% of
`curl_perform` (URL build, header slist, method options, write buffer, perform +
error throw). That is ~60 near-identical lines per adapter plus parallel header
boilerplate (the raw `CURL* curl_`, the test constructor, `make_test_client`).

Extract the transport into a single generic component, `CurlHttpClient`, that
both adapters use. Each adapter keeps only its short, genuinely
exchange-specific typed `execute()` methods (envelope/parse + auth model).

**Scope**: transport only (approach **A**). Making `execute()` itself fully
generic requires unifying the auth-application model and is tracked separately in
[plan generic-rest-client](011-generic-rest-client.md) (approach **B**).

**Done when**: neither `rest_client.cpp` contains any `curl_*` call; both go
through `CurlHttpClient`; the full 300-test suite is green; and the live REST
examples still run.

## What's shared vs. exchange-specific (verified)

| | Kraken | Binance | Shareable? |
|---|---|---|---|
| `write_cb`, prod ctor, dtor | identical | identical | **yes** |
| `curl_perform` body | GET/POST | GET/POST/**DELETE** | **yes** (superset) |
| Performer return | `std::string` (body; **status discarded**) | `std::pair<int,std::string>` (status+body) | unify on `{status, body}` |
| `execute()` parse | `parse_rest_response<T>(json)` | `parse_binance_response<T>(status, json)` | no — stays per-adapter |
| Auth application | `req.build(creds)` (self-signs) | `req.build()` + `auth.sign(http)` (`IRestAuth`) | no — see plan generic-rest-client |

The transport is trivially shareable; the `execute()` bodies are 4 lines each and
legitimately differ.

## Design decisions

1. **Where it lives — new `exchange_http` static library** *(recommended)*.
   `CurlHttpClient` goes in `include/exchange/common/http_client.hpp` +
   `src/exchange/common/http_client.cpp`, built into a **new** `exchange_http`
   target that links `CURL::libcurl` PUBLIC. Both adapter libs link
   `exchange_http` PUBLIC. This **preserves the Step 9 property that
   `exchange_common` is curl/ssl-free** (the WS engine needs no HTTP), and keeps
   the honest split: `exchange_common` = protocol-agnostic dispatch,
   `exchange_http` = HTTP transport.
   *Alternative (simpler, one fewer target):* fold `http_client.cpp` into
   `exchange_common` and have it link `CURL::libcurl` PUBLIC — at the cost of
   making curl-free consumers (e.g. `test_tick_price`) transitively pull libcurl.
   **Pick one at approval.**

2. **`HttpResponse { int status; std::string body; }`** — a small struct in
   `exchange::rest::` (`http_client.hpp`). `CurlHttpClient::perform(const
   HttpRequest&) -> HttpResponse` always captures the status; Kraken simply
   ignores `.status`.

3. **Unify the performer on `std::function<HttpResponse(const HttpRequest&)>`.**
   Binance already returns `{status, body}` → trivial. Kraken's body-only test
   performer is preserved by a **back-compat overload**: `make_test_client(
   std::function<std::string(const HttpRequest&)>)` wraps the body as
   `{200, body}`, so **every existing Kraken client test keeps compiling
   unchanged**. Binance's `make_binance_test_client` already uses the pair form
   (rename its element type to `HttpResponse`).

4. **`CurlHttpClient` supports GET / POST / DELETE** (the superset). Kraken never
   emits DELETE; harmless.

5. **Ownership**: `CurlHttpClient` holds the handle as
   `std::unique_ptr<CURL, deleter>` (folds in review nit **N1**); non-copyable,
   move-only. Each adapter client holds a `std::function` performer plus a
   `std::shared_ptr<CurlHttpClient>` keep-alive set in its production ctor; the
   test ctor leaves the transport null and installs the injected mock.

6. **`execute()` stays per-adapter** — it binds the exchange's parse function,
   envelope type, and auth model. `json::parse(resp.body)` stays in `execute()`.

7. **Out of scope** (explicitly): unifying the auth model / a single generic
   `execute()` ([plan generic-rest-client](011-generic-rest-client.md)); the REST error-model
   hardening from the code review (M2 — folding curl/`json::parse` throws into
   `RestResponse{ok:false}`); curl thread-safety/timeouts. Those become easy,
   *centralised* follow-ups once the transport is one place — noted, not done
   here.

## Step 1 — Add `CurlHttpClient` + wire the build

- `include/exchange/common/http_client.hpp`: banner, `HttpResponse`,
  `class CurlHttpClient { perform(const HttpRequest&) -> HttpResponse; }`
  (move-only; `unique_ptr<CURL,deleter>`). Declaration + the `write_cb`/`perform`
  in `src/exchange/common/http_client.cpp` (the consolidated curl body, GET/POST/
  DELETE).
- CMake (decision 1): add the `exchange_http` STATIC target (or fold into
  `exchange_common`).
- **Done**: library builds; `ctest` still 300 green (no adapter wired yet).
- **Checkpoint**: `feat: add CurlHttpClient shared HTTP transport`.

## Step 2 — Migrate `BinanceRestClient`

- Drop `curl_perform`/`write_cb`/`curl_` from `binance/rest_client.{hpp,cpp}`;
  hold a `CurlHttpClient` (prod) and the `HttpResponse` performer. `execute()`
  unchanged except `perform_` now returns `HttpResponse` (use `.status`/`.body`).
- `make_binance_test_client` performer element type → `HttpResponse`; update its
  few test call sites if the type name is referenced.
- **Done**: full build + `ctest` green (300); `binance` no longer compiles any
  `curl_*` in `rest_client.cpp`.
- **Checkpoint**: `refactor: BinanceRestClient uses shared CurlHttpClient`.

## Step 3 — Migrate `KrakenRestClient`

- Same shed; `execute()` calls `perform_(http)` and reads `.body` (ignores
  `.status`). Add the body-only `make_test_client` back-compat overload
  (decision 3) so existing Kraken tests are untouched.
- **Done**: full build + `ctest` green (300); `src/kraken/rest_client.cpp` no
  longer references curl. Confirm `nm` shows the curl symbols only in the
  transport lib.
- **Checkpoint**: `refactor: KrakenRestClient uses shared CurlHttpClient`.

## Step 4 — Docs + wrap-up

- CLAUDE.md: project tree (`src/exchange/common/http_client.cpp`), build-outputs
  / library list, and the conventions paragraph (adjust the "exchange_common
  links only nlohmann" note per decision 1). Cross-reference plan generic-rest-client.
- Flip plan shared-curl-http-transport → Done (`docs/plans.md` + header). Final build + `ctest`.
- **Checkpoint**: `docs: mark plan 010 done`.

## Testing strategy

No new behaviour — pure extraction. The adapters' `execute()`/parse paths stay
covered by the existing mock-performer unit tests (`test_client.cpp`,
`test_binance_client.cpp`), which are the regression net and must stay green at
every step. The curl wiring itself was never unit-tested (it needs a network) and
remains validated by the live REST examples — extraction does not reduce that
coverage, it *centralises* it. An optional small `test_http_client.cpp` (asserting
`HttpRequest`→curlopt mapping via an injectable setopt seam) is a possible add but
is **not** required for this plan; flagged for discussion.

## Self-review — risks & assumptions

| Risk / assumption | Likelihood | Mitigation |
|---|---|---|
| Unifying the performer to `HttpResponse` churns Kraken tests | Med | Decision 3's body-only `make_test_client` overload keeps every existing call site compiling |
| `exchange_http` adds a 4th library for ~60 lines | Low (judgment) | Surfaced as decision 1 with a simpler fold-into-`exchange_common` alternative — Rob picks at approval |
| ODR if the transport were compiled into both adapter libs | Would break | Avoided by design — it lives in exactly one shared lib both depend on |
| Behaviour drift in the consolidated `curl_perform` (e.g. DELETE body handling) | Low | Superset of the two existing bodies; the live examples + the green suite catch regressions |
| Hidden coupling: Kraken relied on status being discarded | None | Kraken `execute()` keeps ignoring `.status`; behaviour identical |

**Assumption**: no external consumer constructs the adapter clients' private
test constructor directly (they go through the `make_*_test_client` factories),
so changing the performer signature is contained to the factories + their tests.
