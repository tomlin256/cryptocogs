# Step 10: Agent onboarding guide for new exchanges

**Status**: Done — implemented in commits `3dcccee` (10.1) + the 10.2 verify/link/wrap-up
**Parent**: [Plan multi-exchange-abstraction, Step 10](multi-exchange-abstraction.md#step-10--write-the-agent-onboarding-guide-for-new-exchanges)
**Branch**: `feature/multi-exchange-abstraction`

---

## Goal

Write `docs/agent-add-exchange.md` — a self-contained playbook handed to a Claude
agent at the start of a *new* exchange integration. It must need no context
beyond what the user supplies and the existing Kraken + Binance adapters. Every
"follow this pattern" cites a real file path and, where the change is
non-obvious, a real commit hash on this branch.

**Done when**: the guide exists with §10's four sections (inputs, implementation
checklist, conventions, done-criteria) plus the architectural primer an agent
needs; **and** every file path it cites resolves in the tree and every commit
hash resolves via `git cat-file -e` (mechanically checked in 10.2).

This is a documentation deliverable — no library source changes. The validation
is reference-resolution (paths + commits) plus a final unchanged-300 `ctest`
sanity run.

## Reconciliations with §10 (verified against the tree)

§10's file sketch was written before the Binance adapter existed; the guide must
correct three things against reality, so an agent isn't sent chasing files that
do not exist:

1. **Auth is header-only.** There is no `src/binance/auth.cpp` — `auth.hpp`
   defines the HMAC/SHA helpers inline (`detail::`). The guide presents
   `src/<name>/auth.cpp` as *only-if-non-trivial*, with the Binance header-only
   form as the default.
2. **The WS client is header-only.** There is no `src/binance/ws_client.cpp`:
   `BinanceStreamClient` / `BinanceWsApiClient` are bare `using` aliases for
   `ExchangeWsClient`, and the factories are `inline` (Step 9 confirmed this —
   `binance` is single-source `rest_client.cpp`). The guide drops §10's
   checklist item 8 to an "almost never needed" note.
3. **Real fixture/test names**: `binance_rest_example_json.hpp`,
   `binance_account_example_json.hpp`, `binance_ws_stream_example_json.hpp`,
   `binance_ws_api_example_json.hpp`; tests `test_binance_{auth,types,rest_requests,rest_responses,client,ws_client}.cpp`.

## Reference commit map (to cite in the guide; all verified present)

| Concern | Reference file(s) | Commit(s) |
|---|---|---|
| Common scaffold (the contracts an adapter targets) | `include/exchange/common/{rest,ws,ws_client}.hpp` | `b20dc35` (step 1) |
| `IRestAuth` conformance pattern | `include/exchange/common/rest.hpp` | `b57bf6b` (step 3) |
| Auth (header-only HMAC) + REST client infra | `exchange/binance/auth.hpp`, `src/binance/rest_client.cpp` | `4350ce6` (step 4) |
| Enum converters + canonical re-exports | `exchange/binance/types.hpp` | `145bf70` (step 6.1) |
| REST public req/resp + `parse_binance_response` + fixtures | `exchange/binance/rest_api.hpp`, `tests/unit/binance_rest_example_json.hpp` | `352efd0` (step 5.1) |
| REST private (signed) + account fixtures | `exchange/binance/rest_api.hpp`, `tests/unit/binance_account_example_json.hpp` | `c8e42c4` (6.2), `050b82d` (6.4) |
| Signed client round-trip test (mock performer) | `tests/unit/test_binance_client.cpp` | `8b63fa9` (step 6.5) |
| WS market streams: descriptor + ack + events + subscribe | `exchange/binance/ws_streams.hpp`, `tests/unit/test_binance_ws_client.cpp` | `7d461fc` (7.1), `b833587` (7.4) |
| WS trading API: envelope + descriptor + signed requests | `exchange/binance/ws_api.hpp` | `398d2a8` (8.1), `0201b3f` (8.2), `d9592c7` (8.3) |
| Per-exchange CMake (3-lib split, guards, PUBLIC ssl/curl) | `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/unit/CMakeLists.txt` | `9e73dc1` (9.1), `28179fd` (9.2) |
| Kraken `identify_message`/`MessageKind` (optional richer classifier) | `exchange/kraken/ws_api.hpp` | `13671f1` (step 2) |

## Sub-step 10.1 — Write `docs/agent-add-exchange.md`

**Done when**: the file exists with the sections below; reads as a standalone
playbook; reflects the three reconciliations.

Sections:
- **0 — How to use this guide & the build/test loop**: the per-item rhythm
  (write → `cmake --build build` → `ctest --output-on-failure` → checkpoint
  commit), and the three-library/flag model from Step 9.
- **1 — Inputs to collect first** (§10 table, verbatim intent).
- **2 — Architecture primer** (the load-bearing contracts, *new* vs §10): the
  `TypedPublicRequest`/`TypedPrivateRequest` + `response_type` binding; the
  Kraken-style REST envelope vs. the `RestResponse<T>`/`parse_*_response`
  pattern; the canonical enum re-export trick; the `MessageIdentifier`
  (`<name>_frame_descriptor` → `FrameDescriptor{kind, correlation_id,
  route_key}`) that `ExchangeWsClient` needs vs. the *optional*
  `identify_message`/`MessageKind`; `TypedWsRequest<R>` (method calls) and
  `TypedStreamSubscribeRequest`/`route_key()`/`unsubscribe_json()`
  (subscriptions); why the WS client is a bare alias + inline factory.
- **3 — Implementation checklist** (§10 table, corrected per reconciliations,
  each row → real Binance file + commit).
- **4 — Conventions to enforce** (§10 list: banners, optionals, `std::stod`,
  `MockWsConnection`, flag on/off, green ctest).
- **5 — Done criteria** (§10's four-point bar).

### Checkpoint commit
`docs: step 10.1 — write agent onboarding guide (docs/agent-add-exchange.md)`

## Sub-step 10.2 — Verify references, wire links, wrap up

**Done when**: every cited path + commit resolves; the guide is discoverable;
plan marked done; full `ctest` green (unchanged 300).

- **Mechanical check**: extract every `include/…`, `src/…`, `tests/…` path and
  every 7-hex commit token from the guide; assert each path exists and each
  `git cat-file -e <hash>`. Fix any miss.
- **Wire links**: add a pointer to the guide from `CLAUDE.md` and from
  `docs/plans.md` context (plan step-10-agent-onboarding-guide row already lands it in the index).
- **Wrap-up**: append the Step 10 "**Done**:" paragraph to plan multi-exchange-abstraction; flip
  plan step-10-agent-onboarding-guide → Done in `docs/plans.md` + this header. Final `cmake --build build`
  + `ctest` sanity (300, unchanged).

### Checkpoint commit(s)
`docs: step 10.2 — verify references + link onboarding guide`, then
`docs: mark plan 008 done`.

## Self-review — risks & assumptions

| Risk / assumption | Likelihood | Mitigation |
|---|---|---|
| A cited path/commit is wrong → agent chases a ghost | Med | 10.2's mechanical resolver checks every one before "done" |
| Guide drifts from §10's intent | Low | Sections map 1:1 to §10's four parts; reconciliations are additive corrections, documented above |
| Agent over-trusts §10's `auth.cpp`/`ws_client.cpp` rows | Med | Reconciliations 1–2 explicitly recast both as header-only-by-default |
| Guide goes stale as the codebase moves | Inherent | Citations are commit-pinned (immutable) + path-based (checked); staleness is visible, not silent |
| Docs-only change yet "tests must pass" rule | n/a | No source changes; final `ctest` confirms the 300 suite is untouched |

**Assumptions**: Binance is the canonical reference (richest adapter: REST
public+private, WS streams + trading API); Kraken is the secondary reference for
the optional `identify_message` classifier; no new exchange is actually
implemented here — the guide is the deliverable.
