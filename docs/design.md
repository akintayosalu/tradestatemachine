# Design: Matching Engine (Project 1)

This file records design decisions and the reasons behind them.
The full order lifecycle (states, events, transition table) lives in
[`order_state_machine.md`](order_state_machine.md), with a per-cell explanation in
[`order_state_machine_explained.md`](order_state_machine_explained.md).

---

## 1. Rejected request vs. bug

Every (state, event) pair in the transition table has one of three outcomes:
an accepted transition, a REJECT, or a BUG. The line between the last two is
the most important rule in the state machine.

### The rule

- **A client request that can't be done is a REJECT.**
  The engine sends a reject report with a reason, and **the order doesn't change**.
  This is normal and expected. Examples:
  - cancelling an order that filled a moment earlier ("too late, already filled")
  - cancelling or replacing an ID the engine has never seen ("unknown order")
  - sending a new order with an ID that's already used ("duplicate order ID")
  - a new order or replace that fails validation

- **An engine event that should be impossible is a BUG.**
  The engine fails loudly and stops. It never carries on. Examples:
  - a fill on an order that doesn't exist, or that's already final
  - MarketNoLiquidity on a limit order (a limit order should rest in the book instead)

### Why

- **Clients are outside our control.** They send stale IDs, retries and requests
  that cross with fills. Treating those as errors would make the engine fragile, and
  silently ignoring them would leave clients unsure of their position. A clear
  reject with a reason is the right answer.
- **The engine's own events come from our matching code.** If matching produces
  an impossible event, the book is already wrong. Continuing would spread the
  damage into fills, reports and, later, replicas. Stopping immediately makes the
  bug visible at the point it happens.
- **A shortcut for checking the table:** a client event (NewOrder, Cancel, Replace)
  is never BUG. Only engine events (PartialFill, FullFill, MarketNoLiquidity) can be.

---

## 2. Invariants

These must hold after every event. Each one becomes a test, and they're checked
after every event during replay and benchmark runs too.

1. **Quantities add up for live orders.**
   For an order in New or PartiallyFilled: filled + remaining = original quantity.
2. **Final orders have nothing remaining.**
   For an order in Filled, Cancelled, Rejected or Replaced: remaining = 0.
   A Filled order has filled = original. A Cancelled order keeps whatever it had filled.
3. **The book holds exactly the live orders.**
   An order is in the book if and only if it's in New or PartiallyFilled.
   (A market order never rests, so for market orders this is only true while the engine processes it.)
4. **Filled quantity never goes down.**
   A trade can't be undone. Cancel and replace only affect the part that hasn't traded.

Also checked, though not part of the core four:
- every order ends in exactly one final state
- every accepted transition sends exactly one report
- after matching, the book is never crossed (best bid < best ask)

---

## 3. Replace: FIX style (option B)

### Decision

A replace **ends the old order** in `Replaced` (a final state) and **creates a new
replacement order** that takes its place.

- The replacement gets a new client ID. The replace request carries both the
  original client ID (to find the order) and the new one.
- The old client ID stops pointing to a live order. A later Cancel or Replace using it
  is rejected: "too late, already replaced".
- The replacement records which order it replaced, so a chain of replaces can be traced.
- Each order also has an internal ID created by the engine and used for lookups. Client IDs map to it.

### Alternatives considered

- **Option A, amend in place:** the order keeps its identity and state, and only its
  price or quantity changes. Simpler, since there's no Replaced state and no second order.
- **Why B instead:** it matches how real exchanges and FIX clients work. Each version
  of an order has its own client ID and its own reports, so the history of changes is
  explicit in the output log. That history is useful for the determinism test and for
  replaying an order's life. The cost is one more state and some ID bookkeeping.

### The replacement order

- **Starting state:** the same as the old order's. PartiallyFilled if the old order had
  fills, otherwise New.
- **Quantities:** the filled quantity carries over. The new original quantity is what the
  client asked for. Remaining = new quantity − filled.
- **Matching:** if the new price crosses the book, the replacement matches immediately,
  like a freshly arrived order.
- **Validation:** the new price and quantity go through the same checks as a new order.
- **Replace to a quantity at or below what's already filled:** REJECT, and the old order is
  unchanged. A client who wants out sends a Cancel.
  *Why:* it keeps replace meaning "change my order" and cancel meaning "stop my order",
  with no special case where a replace quietly acts as a cancel.

### Priority rules

Time priority at a price level is ordered by a **sequence number** assigned by the engine.

| Change | Keeps its place in the queue? |
|---|---|
| Quantity down, price unchanged | **Yes**, keeps the old sequence number |
| Quantity up (price unchanged) | No, goes to the back |
| Price changed (any direction) | No, goes to the back of the new price level |
| Price and quantity both changed | No, goes to the back |

**Why:**
- **Reducing size can only help everyone else in the queue,** since less of the
  available liquidity is taken by orders ahead of them. So there's no reason to penalize it.
- **Increasing size would let an order jump ahead of others** with shares they didn't
  have when they joined the queue. That's unfair to the orders that were already waiting,
  so the larger order goes to the back.
- **A price change is effectively a new decision** at a different level. The order
  wasn't waiting at the new price, so it joins the back of that level.
- **Sequence numbers, not the wall clock:** time priority comes from an
  engine-assigned sequence number, never from reading the clock. Reading the clock
  inside the engine would break the byte-identical replay test.

## 4. Order fields

- **Price is an integer number of ticks** (`int64_t`), never floating point. It is
  signed so price differences can be negative and a bad negative input stays visible
  to validation instead of wrapping into a huge positive value.
- **Market orders ignore price.** The price field is not used for a market order:
  matching never reads it, and it plays no part in the replace priority rule.
