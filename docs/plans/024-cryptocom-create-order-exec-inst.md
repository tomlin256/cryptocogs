# `exec_inst` (post-only) support on `CryptoComCreateOrderRequest`

**Status:** Done
**Depends on:** [plan cryptocom-exchange-adapter — Crypto.com Exchange adapter](020-cryptocom-exchange-adapter.md) (Done)

## Motivation

`CryptoComCreateOrderRequest` has no way to express a post-only (maker-only)
order. Confirmed against the official Crypto.com Exchange v1 API docs
(`exchange-docs.crypto.com/exchange/v1/rest-ws/index.html`, `private/create-order`),
the venue accepts an `exec_inst` array parameter:

```json
{
  "method": "private/create-order",
  "params": {
    "instrument_name": "BTCUSD-PERP",
    "side": "SELL",
    "type": "LIMIT",
    "price": "50000.5",
    "quantity": "1",
    "exec_inst": ["POST_ONLY"],
    "time_in_force": "FILL_OR_KILL"
  }
}
```

`include/exchange/cryptocom/rest_api.hpp`'s file-header comment records this as a
deliberate v1 omission: *"exec_inst (an array) is deliberately omitted from v1 to
avoid the list-serialisation ambiguity in params_to_str."* It is a real gap: a
caller that wants a post-only order today can only send a plain GTC limit order,
which **can cross and pay taker fees** despite the intent.

## Is the "list-serialisation ambiguity" real?

Traced mechanically against the current `params_to_str` (`src/cryptocom/auth.cpp`):
for `{"exec_inst": ["POST_ONLY"]}`, the array branch iterates the one element and
recurses (`level=1`); the recursive call sees a JSON **string**, not an object, so
`!params.is_object()` is true and it falls straight to `scalar_to_str("POST_ONLY")`
→ `"POST_ONLY"` verbatim. Result: `params_to_str` emits `"exec_instPOST_ONLY"` —
deterministic, no special-casing needed, and consistent with the documented rule
("an array value iterates its elements, recursing at level + 1"). The existing
`ArrayOfObjects_IteratesAndRecurses` test already exercises the array-iterate path,
just not for a bare string element — Step 1 adds that missing case explicitly
rather than assuming it.

So there is no actual serialisation ambiguity for an array-of-strings value. The
real, **unchanged** residual risk is the one every Crypto.com private request
already carries (per `test_cryptocom_auth.cpp`'s own header comment): *"No official
Crypto.com signature vector is published, so ... sign() is pinned by recomputing
its documented digest assembly independently."* Adding `exec_inst` doesn't make
signing any less verified than `side`/`price`/`quantity`/`time_in_force` are today
— it's the same accepted risk class as the rest of plan cryptocom-exchange-adapter, not a new one.

## Scope

- **In scope:** an `exec_inst` field on `CryptoComCreateOrderRequest`, wired into
  `build()`; a `params_to_str` unit test for a bare array-of-strings value; a
  request-building test for `create-order` asserting the JSON array shape in the
  body; a signing-digest test asserting the exact `params_to_str` output used in
  the signature for a request carrying `exec_inst`; docs (this repo's `CLAUDE.md`
  + the stale `rest_api.hpp` header comment); a non-breaking version bump.
- **Out of scope:** any exec_inst value beyond `POST_ONLY` — Crypto.com's docs
  reference others (e.g. for margin/liquidation paths) but this plan only pins
  what's needed and verified for spot order entry, modelled as a raw array of
  strings so future values are additive, not a closed enum.

## Design decisions (confirm at approval)

- **Raw `std::optional<std::vector<std::string>> exec_inst`, not a narrow `bool
  post_only`.** Mirrors the venue's own wire shape 1:1 (consistent with how
  `time_in_force` exposes the enum rather than a narrower `bool ioc`) and needs no
  redesign if a future caller needs another instruction alongside `POST_ONLY`.
  `build()` omits the key entirely when unset or empty, exactly like `price`/
  `quantity`/`notional`/`client_oid`/`time_in_force` today.
- **A convenience constant, not a convenience method.**
  `inline constexpr const char* EXEC_INST_POST_ONLY = "POST_ONLY";` next to the
  request struct, so callers write `req.exec_inst = {EXEC_INST_POST_ONLY}` instead
  of a raw string literal — cheap typo-safety without inventing a builder API this
  plan can't fully scope (only one value is verified).
- **nlohmann::json serialises `std::vector<std::string>` as a native JSON array**
  — `params["exec_inst"] = *req.exec_inst` needs no manual array construction.
- **Version bump: `0.5.2` → `0.5.3`** (purely additive — an unset `exec_inst`
  produces a byte-identical body to today, so this isn't the breaking-change class
  plan credentials-from-file bumped a minor version for).

## Steps

Each step ends with a checkpoint: full `cmake --build build` **and**
`ctest --output-on-failure` from `build/`, both green, then a commit.

### Step 1 — Field, wire format, and the two risk-bearing tests
- `include/exchange/cryptocom/rest_api.hpp`: add
  `std::optional<std::vector<std::string>> exec_inst;` to
  `CryptoComCreateOrderRequest` (doc comment: "e.g. {\"POST_ONLY\"}; omitted when
  unset"); add the `EXEC_INST_POST_ONLY` constant. Remove the now-stale "exec_inst
  ... deliberately omitted" sentence from the file's header comment (replace with
  a pointer to this plan).
- `src/cryptocom/rest_api.cpp`'s `CryptoComCreateOrderRequest::build()`: add
  `if (exec_inst && !exec_inst->empty()) params["exec_inst"] = *exec_inst;`
  alongside the existing optional-field guards.
- `tests/unit/test_cryptocom_auth.cpp`: new
  `ArrayOfStrings_IteratesAndRecursesElementwise` case for
  `params_to_str({"exec_inst": ["POST_ONLY"]})` == `"exec_instPOST_ONLY"`
  (the missing case this plan's "is the ambiguity real?" section reasons about —
  don't just assume the existing array-of-objects test covers it). Extend
  `CryptoComSign::ComposesHexHmacOfDocumentedDigest`-style coverage with a second
  case whose `params` includes `exec_inst`, asserting the recomputed digest still
  matches independently (same pinning approach, not a new mechanism).
- `tests/unit/test_cryptocom_rest_requests.cpp`: extend the existing
  `CreateLimitBuy...` case (or add a sibling) asserting
  `p.at("exec_inst") == json::array({"POST_ONLY"})` when set, and
  `EXPECT_FALSE(p.contains("exec_inst"))` when left unset (mirrors the existing
  `notional`/`price` absence assertions).
- **Done:** build + all tests pass.

### Step 2 — Docs + version bump
- `CLAUDE.md`: add `exec_inst` to the `CryptoComCreateOrderRequest` field
  description under "Crypto.com adapter reference" → REST table/prose; note the
  `EXEC_INST_POST_ONLY` constant.
- `CMakeLists.txt`: bump `project(CRYPTOCOGS VERSION 0.5.2 ...)` → `0.5.3`.
- Mark plan cryptocom-create-order-exec-inst **Done** in `docs/plans.md`; update
  [plan cryptocom-exchange-adapter](020-cryptocom-exchange-adapter.md)'s own text if it still says
  exec_inst is omitted.
- **Done:** full build + test green.

## Self-review — risks and assumptions

- **No official Crypto.com signature vector exists for any private request**,
  `exec_inst` included — this plan doesn't change that risk class, it just adds
  one more field to the same self-consistent (not server-verified) pinning
  approach every other `create-order` param already uses. If Rob later gets live
  credentials, a real signed `create-order` call with `exec_inst` set is the
  actual verification this plan can't do from here.
- **Only `POST_ONLY` is modelled**, deliberately — see Scope. A generic
  `vector<string>` means adding a second verified value later is additive (no
  struct redesign), but this plan doesn't claim to know or test the full set.
- **Alternative considered and rejected:** a `bool post_only` convenience field
  instead of the raw array. Rejected because it hides the wire shape behind a
  narrower API for a single-value win, inconsistent with how `time_in_force`
  (an enum, not `bool ioc`) is already exposed on the same struct.
