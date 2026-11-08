# Rename Kraken `Credentials` → `KrakenCredentials`

## Motivation

Every other adapter names its credential struct after the exchange
(`BinanceCredentials`, `CoinbaseCredentials`, `CryptoComCredentials`) and pairs it
with a `*Auth` signer. Kraken alone calls its struct the bare
`exchange::kraken::rest::Credentials`, which is ambiguous at call sites that touch
more than one exchange and inconsistent with the rest of the codebase (it already
has a `KrakenAuth`). Rename it to **`KrakenCredentials`**.

This is a **breaking change** (accepted — consistent with the project's no-compat
-shim philosophy from plan remove-compat-shim, so **no deprecated alias** is kept).

## Decision

- New name: **`KrakenCredentials`** (in the same `exchange::kraken::rest`
  namespace, mirroring `exchange::binance::rest::BinanceCredentials`).
- **Also in scope (per approval):** rename `exchange::kraken::ws::WsCredentials`
  → **`KrakenWsCredentials`** for full symmetry with `BinanceWsCredentials` /
  `CoinbaseWsCredentials`. Kraken's bare `WsCredentials` lives only in
  `kraken/ws_api.hpp` (def), `src/kraken/ws_api.cpp` (`to_json`), and
  `kraken_example.cpp` (3×); `\bWsCredentials\b` does not match the
  already-prefixed `BinanceWsCredentials`/`CoinbaseWsCredentials`.

## Scope (sized by grep — ~109 bare `Credentials` references)

`\bCredentials\b` cleanly excludes `WsCredentials`, the `*Credentials` compounds,
and `read_toml_credentials`, so the rename is a safe whole-word replace.

- **Library:** `include/exchange/kraken/auth.hpp` (struct + `KrakenAuth` ctor),
  `rest_api.hpp` (`build(const Credentials&)`), `rest_client.hpp` +
  `rest_client.inl` (`execute(req, const Credentials&)`), `src/kraken/auth.cpp`,
  `src/kraken/rest_api.cpp`.
- **Tests:** `test_client.cpp`, `test_kraken_auth.cpp`, `test_signature.cpp`,
  `test_rest_requests.cpp`.
- **Examples:** `private_rest.cpp`, `private_ws.cpp`, `kraken_example.cpp`.
- **Living docs:** `CLAUDE.md` (authoritative reference — fully updated).
  On inspection, `docs/agent-add-exchange.md`'s `Credentials` mentions are
  **generic/conceptual** (the pattern for any new exchange, e.g.
  `build(const <Exchange>Credentials&)`), not the Kraken type — left as-is.
  `appendix-migration-guide.md` and the other **historical plan docs**
  (multi-exchange-abstraction, step-6-binance-rest-private, step-8-binance-ws-api, generic-rest-client,
  declarative-headers, coinbase-exchange-adapter, cryptocom-exchange-adapter…) record past state and are **not** retro-edited.
- One **comment** mention in `include/exchange/common/credentials_file.hpp`
  ("every adapter's Credentials::from_file") — genericise the wording.

## Steps

### Step 1 — Library + tests + examples
- Whole-word rename `Credentials` → `KrakenCredentials` across the library, test,
  and example files above (verify each diff hunk — no `WsCredentials` /
  `read_toml_credentials` / compound caught).
- Full `cmake --build build` + `ctest` green.
- **Done:** builds and all tests pass; `grep -rn "\bCredentials\b" include src tests/unit`
  returns only `WsCredentials`/compounds/`read_toml_credentials` (zero bare hits).
- Commit.

### Step 2 — Docs
- Update `CLAUDE.md`, `agent-add-exchange.md`, the migration guide, and the
  `credentials_file.hpp` comment.
- Mark plan rename-kraken-credentials Done in `docs/plans.md`.
- Commit.

## Sequencing (relative to plan credentials-from-file, in flight)

Recommended: **finish plan credentials-from-file first** (Steps 4 Coinbase, 5 Crypto.com, 6 docs),
then execute this plan. The remaining code steps of credentials-from-file don't touch Kraken's `Credentials`,
so they're unaffected; this plan will re-touch the few Kraken credential lines that the doc
step of credentials-from-file writes (small, mechanical rework) in exchange for keeping each plan a clean,
independently-committed unit. Alternative on request: do this plan now (before the doc
step of credentials-from-file) so the docs are written once with the final name.

## Self-review — risks

- **Whole-word safety:** the one risk is over-matching; mitigated by `\bCredentials\b`
  and a per-hunk review, plus the post-rename grep assertion in Step 1's Done.
- **Breaking change:** intentional, no alias; folds into the same 0.5.0 breaking
  bump already planned in credentials-from-file (no separate bump needed).
- **No behavioural change:** pure rename — existing tests passing unchanged is the
  proof.
