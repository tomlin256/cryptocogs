# Crypto.com `user.trade` WS channel (deferred)

**Status:** Stub — not yet scoped
**Depends on:** [plan cryptocom-exchange-adapter — Crypto.com Exchange adapter](020-cryptocom-exchange-adapter.md) (Done)

This is a **gap record**, not an actionable step list. Do not implement from this
doc — expand it into a real design + step list first, per the repo's planning
convention, if and when this is scheduled.

## Gap

`exchange::cryptocom::ws` models the authenticated user channel's `user.order`
(order lifecycle/snapshots) and `user.balance` push events, but not
`user.trade` — the per-fill push channel. Recorded in-code at
`include/exchange/cryptocom/ws.hpp`:

> `(user.trade is not modelled — private fills are available via REST
> get-trades and via user.order fill updates; its WS row schema is left for a
> later plan.)`

Consumers currently get fills two ways instead: polling REST `get-trades`
(`CryptoComGetTradesRequest` → `CryptoComUserTrade` — exact price, real fee,
`liquidity_indicator`, already shipped in v0.5.2) triggered by a
`user.order` cumulative-quantity change, or reading `user.order`'s own
`cumulative_quantity`/`avg_price` fields directly.

## Why deferred, not blocking

A caller can design around the gap rather than wait on it: fills + fees are
sourced from REST `get-trades` (exact price, real fee, maker/taker — no
inference), with WS `user.order` used only as the low-latency trigger to fetch.
The unmodelled WS `user.trade` channel would only be a future *push-latency*
optimization — never a correctness prerequisite.

So `user.trade` buys **lower-latency fill delivery** (a push row the instant a
trade executes, vs. a REST round-trip after the `user.order` trigger) — not
correctness, not a missing capability. Same shape as plan coinbase-fix-order-entry (Coinbase FIX):
a real capability gap, worth a design doc, not worth blocking a shipping
consumer on.

## Likely shape (orientation only — not designed)

- A new push event struct, field-for-field close to the existing REST
  `CryptoComUserTrade` (`trade_id`, `order_id`, `client_oid`,
  `instrument_name`, side, `traded_quantity`, `traded_price`, `fees`,
  `fee_instrument_name`, `liquidity_indicator`, timestamps) — presumably the
  same underlying data pushed instead of polled, but **unverified**: no public
  docs vector or live capture pinned yet (same R2/R3 risk class as plan cryptocom-exchange-adapter —
  terse keys, string-vs-number ambiguity by field).
- A channel-name helper `user_trade_channel(instrument)` → `"user.trade.<i>"`
  (mirrors `user_order_channel`), a `CryptoComUserTradeSubscribe` alias, and a
  frame-descriptor/route-key extension — the same `CryptoComSubscribeRequest<T>`
  scaffold `user.order`/`user.balance` already use, so this is additive, not a
  redesign of `ws.hpp`.

## Next step

Pin the wire shape (official docs or a live capture under a funded sandbox/prod
account) before writing any code, then expand this into a real plan with a
step list and test plan, per the repo's planning convention (see plan cryptocom-exchange-adapter or
coinbase-fix-order-entry for the expected shape). Not scheduled; no consumer is blocked on it.
