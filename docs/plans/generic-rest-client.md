# Fully generic `RestClient` (unify the typed execute path)

**Status**: Proposed — **deferred**; do [plan shared-curl-http-transport](shared-curl-http-transport.md) first
**Branch**: `feature/multi-exchange-abstraction`

---

## Goal

After [plan shared-curl-http-transport](shared-curl-http-transport.md) shares the cURL *transport*,
the only remaining per-adapter REST code is each client's typed `execute()`
methods. This plan (approach **B**) collapses those too, into a **single generic
`RestClient`** in the common layer, parameterised by a small per-exchange policy
that supplies the envelope/parse and the auth model. Adapters would then ship
*no* `rest_client.{hpp,cpp}` of their own — only a policy struct + their request/
response types.

This is the "almost all of it can be shared generically" end state. It is
**deferred** because the payoff over plan shared-curl-http-transport is modest and the prerequisite
touches security-critical signing.

## Why it isn't free — the auth-model divergence

The two adapters apply auth differently (verified in the headers):

- **Binance**: `req.build()` produces an unsigned `HttpRequest`; the client calls
  `auth.sign(http)` — the `IRestAuth` interface. Clean seam.
- **Kraken**: `req.build(const Credentials&)` builds **and self-signs** in one
  step (the nonce is embedded in the body and the signature is computed over
  `path + SHA256(nonce + body)` inside `build`). The client never calls
  `IRestAuth::sign`.

`KrakenAuth : exchange::rest::IRestAuth` **does exist and is unit-tested**
(`include/exchange/kraken/auth.hpp:195`, `tests/unit/test_kraken_auth.cpp`) — it
was built in Step 3 to prove the interface — but `KrakenRestClient::execute`
does **not** use it. So Kraken has a working `IRestAuth` signer that is currently
dead in the client path.

A single generic `execute()` needs *one* auth seam. The natural choice is the
Binance/`IRestAuth` model: `execute()` always does `req.build()` then
`auth.sign(req)`. That requires **moving Kraken's private requests off
`build(creds)` self-signing** onto `build()` (unsigned, nonce embedded) +
`KrakenAuth::sign(http)` — and proving the relocated signature is byte-identical
(the `test_signature.cpp` reference-equality test is the guard). This is the
risky, security-touching part, and the whole reason B is its own plan.

## Sketch of the generic design

```cpp
// exchange/common/rest_client.hpp  (illustrative)
template <typename Policy>          // Policy = BinancePolicy | KrakenPolicy
class RestClient {
    CurlHttpClient http_;           // from plan shared-curl-http-transport
public:
    template <typename Req>
    auto execute(const Req& req, const IRestAuth* auth = nullptr) {
        HttpRequest h = req.build();
        if (auth) auth->sign(h);
        HttpResponse r = http_.perform(h);
        return Policy::template parse<typename Req::response_type>(r);  // status+body
    }
};
```

- `Policy::parse<T>(HttpResponse)` hides the envelope difference
  (`parse_binance_response<T>(r.status, json)` vs
  `parse_rest_response<T>(json(r.body))`, returning each exchange's
  `RestResponse<T>`).
- Public vs. private is the `auth == nullptr` distinction (or two overloads, as
  today), enforced with the existing `PublicRequest`/`PrivateRequest` SFINAE.
- `KrakenRestClient` / `BinanceRestClient` become `using` aliases for
  `RestClient<KrakenPolicy>` / `RestClient<BinancePolicy>` — source-compatible.

## Prerequisites

1. **Plan shared-curl-http-transport done** (shared `CurlHttpClient` + `HttpResponse`).
2. **Kraken auth unified onto `IRestAuth`**: private requests `build()` unsigned
   with the nonce embedded; `KrakenRestClient` routes through `KrakenAuth::sign`;
   `test_signature.cpp` still proves byte-identical signatures. This is a
   self-contained sub-step and the gating risk.

## Why deferred (cost/benefit)

- The remaining duplication after plan shared-curl-http-transport is **~4 lines of `execute()` per
  adapter** — small. The generic client trades that for a `Policy` indirection
  and a template, which is not obviously a net readability win at **two**
  exchanges.
- The prerequisite rewires Kraken's **signing** — the one area where a subtle
  bug is most costly. Not worth it without a forcing function.
- **Revisit when**: a third exchange lands (the per-adapter `execute()`
  boilerplate then multiplies and the policy abstraction starts paying for
  itself), or if the auth-model split causes friction elsewhere.

## Risks (when it is eventually done)

| Risk | Likelihood | Mitigation |
|---|---|---|
| Relocating Kraken signing breaks byte-identical output | Med | `test_signature.cpp` reference-equality test is the hard gate; do the auth move as its own checkpoint before touching the client |
| `Policy` template indirection hurts readability more than the duplication it removes | Med | This is the core judgement call — only proceed if a 3rd exchange makes the boilerplate genuinely hurt |
| Source compatibility for existing `KrakenRestClient`/`BinanceRestClient` callers | Low | Keep them as `using` aliases over `RestClient<Policy>` |
| Public/private SFINAE interacts badly with a single `execute()` | Low | Keep the two-overload shape; only the body is shared |

**Bottom line**: plan shared-curl-http-transport captures ~95% of the duplication at low risk. Plan generic-rest-client
captures the last sliver at real risk to the signing path — worth it only with a
third adapter or a concrete pain point, not speculatively.
