# Order Lifecycle State Machine

Replace semantics: **FIX style**. A replace ends the old order in `Replaced`
(final) and creates a new order that takes its place.

## States

| State | Meaning | Final? | In the book? |
|---|---|---|---|
| *(none)* | No order with this ID exists yet | — | no |
| New | Accepted, nothing filled yet | | |
| PartiallyFilled | Some filled, some still open | | |
| Filled | Fully filled | | |
| Cancelled | Cancelled by the client, or the leftover part of a market order | | |
| Rejected | Failed validation, so it never existed in the book | | |
| Replaced | Taken over by a replacement order | | |

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
| *(none)* | | | | | | |
| New | | | | | | |
| PartiallyFilled | | | | | | |
| Filled | | | | | | |
| Cancelled | | | | | | |
| Rejected | | | | | | |
| Replaced | | | | | | |

## Questions to answer before filling the table

1. **NewOrder on an existing ID:** is a duplicate client order ID a REJECT, or is it impossible at this layer?
2. **Validation:** list what makes a NewOrder REJECT (zero or negative quantity, bad price, unknown instrument, ...).
3. **Market orders:** if nothing fills at all, is the result Cancelled or Rejected?
4. **The replacement order:**
   - What state does it start in? (Think about an order that was PartiallyFilled before the replace.)
   - Which quantities carry over from the old order (filled, remaining)?
   - Does it keep the old order's time priority, or get a new sequence number? Under what conditions?
   - Can it match immediately if its new price crosses the book?
5. **Replace to a quantity at or below what's already filled:** REJECT, or treat it as a cancel?
6. **Replace or Cancel on a final state:** what reject type does the client get, and does the reply say *why* (e.g. "already filled")?
7. **IDs:** in FIX the replace request carries a new client order ID plus the original one. What does your engine use to find the order, and what goes on the reports?

## Invariants (to turn into tests)

- filled + remaining = original quantity
- remaining is 0 exactly when the order is Filled, and is set to 0 on any final state
- an order is in the book if and only if it's New or PartiallyFilled
- filled quantity never goes down
- every order ends in exactly one final state
- every accepted transition sends exactly one report
