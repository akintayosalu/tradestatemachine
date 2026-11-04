# Order Lifecycle State Machine

Replace semantics: **FIX style**. A replace ends the old order in `Replaced`
(final) and creates a new order that takes its place.

## States

| State | Meaning | Final? | In the book? |
|---|---|---|---|
| *(none)* | No order with this ID exists yet | — | no |
| New | Accepted, nothing filled yet |no |yes |
| PartiallyFilled | Some filled, some still open |no | yes|
| Filled | Fully filled |yes |no |
| Cancelled | Cancelled by the client, or the leftover part of a market order |yes |no |
| Rejected | Failed validation, so it never existed in the book |yes |no|
| Replaced | Taken over by a replacement order |yes |no |

## Events

| Event | Source | Description |
|---|---|---|
| NewOrder | client | Submit a new limit or market order |
| Cancel | client | Cancel an existing order by ID |
| Replace | client | Change the price and/or quantity of an existing order |
| PartialFill | engine | Matched, but some quantity is still open |
| FullFill | engine | Matched, and no quantity is left open |
| MarketNoLiquidity | engine | A market order has open quantity but nothing left to match against |

## Transition table

How to write each cell:
- `→ NextState / report sent` for an accepted transition
- `REJECT / reject type` for a client request that's refused (state stays the same)
- `BUG` if the engine itself should never produce this pair (fail loudly)

| State \ Event | NewOrder | Cancel | Replace | PartialFill | FullFill | MarketNoLiquidity |
|---|---|---|---|---|---|---|
| *(none)* |→ New / accepted report, or → Rejected / reject report (validation reason) |REJECT / unknown order |REJECT / unknown order|BUG |BUG |BUG |
| New | REJECT / duplicate order ID | → Cancelled / order cancelled | → Replaced / order replaced and → New / order replaced, or REJECT / invalid replace (order unchanged) | → PartiallyFilled / order partially filled | → Filled / order filled | → Cancelled / no liquidity |
| PartiallyFilled | REJECT / duplicate order ID | → Cancelled / order cancelled | → Replaced / order replaced and → PartiallyFilled / order replaced, or REJECT / invalid replace (order unchanged) | → PartiallyFilled / order partially filled | → Filled / order filled | → Cancelled / no liquidity |
| Filled | REJECT / duplicate order ID | REJECT / too late, already filled | REJECT / too late, already filled | BUG | BUG | BUG |
| Cancelled | REJECT / duplicate order ID | REJECT / already cancelled | REJECT / already cancelled | BUG | BUG | BUG |
| Rejected | REJECT / duplicate order ID | REJECT / already rejected | REJECT / already rejected | BUG | BUG | BUG |
| Replaced | REJECT / duplicate order ID | REJECT / too late, already replaced | REJECT / too late, already replaced | BUG | BUG | BUG |

## Decisions

1. **NewOrder with an ID that already exists:** REJECT, "duplicate order ID".
   Clients pick IDs (FIX style), so duplicates really happen, often as retries.
   In Project 2 this changes to "return the original reply" (idempotency).

2. **Validation.** A NewOrder is REJECTED if any of these are true:
   - quantity is 0 or less, or above the maximum order size
   - limit order has no price, or a price of 0 or less
   - price isn't a multiple of the tick size
   - market order includes a price
   - side isn't buy or sell
   - unknown instrument
   - unsupported order type

   Each check is its own reject reason. A Replace's new price and quantity go through the same checks.

3. **Market order where nothing fills:** Cancelled, with the reason "no liquidity".
   The order was valid, so it was accepted. Rejected is only for orders that fail validation.

4. **The replacement order:**
   - **Starting state:** the same as the old order's: PartiallyFilled if the old order had fills, otherwise New.
   - **Quantities:** the filled quantity carries over. The new original quantity is what the client asked for, and remaining = new quantity − filled.
   - **Time priority:** it keeps the old sequence number only if the price is unchanged and the quantity went down. Otherwise it gets a new sequence number (back of the queue).
   - **Matching right away:** if the new price crosses the book, it's treated like a freshly arrived order and matched.

5. **Replace to a quantity at or below what's already filled:** REJECT. A client who wants out sends a Cancel.

6. **Cancel or Replace on a final state:** REJECT, and the reject report says why:
   "too late, already filled", "already cancelled", "already replaced", or "unknown order".

7. **IDs:**
   - Each order gets an internal ID created by the engine, used for lookups and in the book.
   - The engine keeps a lookup from client ID → internal ID.
   - A Replace request carries the original client ID (used to find the order) and a new client ID (for the replacement).
   - On replace, the old client ID stops pointing to a live order, and the replacement records which order it replaced.
   - Reports include both client IDs.
   - A Cancel or Replace that uses an already-replaced client ID → REJECT, "too late, already replaced".

## Invariants (to turn into tests)

- filled + remaining = original quantity
- remaining is 0 exactly when the order is Filled, and is set to 0 on any final state
- an order is in the book if and only if it's New or PartiallyFilled
- filled quantity never goes down
- every order ends in exactly one final state
- every accepted transition sends exactly one report
