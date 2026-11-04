# Order State Machine: Every Cell Explained

This is the plain-English companion to [`order_state_machine.md`](order_state_machine.md).
That file holds the transition table and the design decisions. This one explains what
each cell means, what changes when it happens, and why.

---

## How to read the table

Each cell answers one question: **"An order is in state X. Event Y arrives. What happens?"**

A cell has one of three kinds of answer:

| Answer | Meaning | What the client sees |
|---|---|---|
| `→ NextState` | The event is accepted and the order moves to a new state | A report confirming the change |
| `REJECT` | A client request is refused. **The order doesn't change at all.** | A reject report giving the reason |
| `BUG` | The engine produced an event that should be impossible. Fail loudly and stop | Nothing. This is your code being wrong |

### Where events come from

| Event | Source | Plain meaning |
|---|---|---|
| NewOrder | Client | "Here's a new order" |
| Cancel | Client | "Cancel my order" |
| Replace | Client | "Change my order's price and/or quantity" |
| PartialFill | Engine | "Part of this order traded, some is still open" |
| FullFill | Engine | "The rest of this order traded" |
| MarketNoLiquidity | Engine | "This market order has open quantity but nothing left to trade against" |

### Quantities each order tracks

- **Original quantity:** how much the client asked for.
- **Filled quantity:** how much has traded so far. It never goes down.
- **Remaining quantity:** how much is still waiting to trade.

For a **live** order (New or PartiallyFilled): filled + remaining = original.
Once an order is **final**, remaining is 0. Whatever hadn't traded is gone (cancelled or replaced).

---

## The three rules behind the whole table

1. **Client events never cause a BUG.** Clients can send anything: mistakes, stale IDs,
   requests that cross with a fill. The engine always answers with a clear REJECT and a reason.
2. **Engine events only make sense on live orders.** Fills and MarketNoLiquidity can only
   happen to New or PartiallyFilled orders. Anywhere else, the matching code has a bug.
3. **Only New and PartiallyFilled can change.** Filled, Cancelled, Rejected and Replaced are
   final. Nothing moves an order out of a final state.

---

## Row 1: *(none)*, no order with this ID exists yet

This row is about IDs the engine has never seen.

### NewOrder → New or → Rejected
- **What happens:** the engine runs the validation checks (decision 2):
  quantity, price, tick size, side, instrument, order type.
- **If they pass:** the order is created in **New**. A limit order is placed in the book
  (unless it can match right away, which comes through as fill events next). The client gets an **accepted** report.
- **If any check fails:** the order is recorded as **Rejected** and never enters the book.
  The client gets a **reject** report naming the failed check.
- **Why record a rejected order instead of ignoring it?** So the ID is known. If the client
  sends the same ID again, it's caught as a duplicate (decision 1).
- **Example:** buy 0 shares → Rejected, "quantity must be greater than 0".

### Cancel → REJECT, "unknown order"
- **What happens:** the client asked to cancel something that doesn't exist. Usually a
  mistyped ID or an ID from another session.
- **Why not BUG:** a client sending a bad ID is normal. It's a bad request, not an engine error.

### Replace → REJECT, "unknown order"
- Same as Cancel: there's nothing to replace.

### PartialFill / FullFill / MarketNoLiquidity → BUG
- **What happens:** the engine is trying to trade an order that doesn't exist.
- **Why BUG:** the matching code is only supposed to act on orders in the book. If this
  fires, something is badly wrong, for example a stale reference to a removed order.

---

## Row 2: New, accepted, nothing traded yet

The order is live. For a limit order, it's sitting in the book waiting.

### NewOrder → REJECT, "duplicate order ID"
- **What happens:** the client sent a new order using an ID that's already taken.
- **Why it happens:** usually a retry after the client timed out waiting for a reply.
  In Project 2 this changes to "send back the original reply" (idempotency).
- **The existing order is not affected.**

### Cancel → Cancelled
- **What happens:** the order is removed from the book. Nothing had traded, so the whole
  quantity is cancelled.
- **Quantities:** filled stays 0, remaining becomes 0.
- **Report:** **cancelled**.
- **Example:** buy 100 at 50, then cancel → Cancelled, 0 filled, 100 cancelled.

### Replace → Replaced (+ replacement in New), or REJECT
- **If the new price and quantity are valid:**
  - The **old order** becomes **Replaced** (final) and leaves the book. Its client ID no longer points to a live order.
  - A **replacement order** is created in **New**, with the new price and quantity and a new client ID. It records which order it replaced.
  - **Time priority** (decision 4): if the price is unchanged and the quantity went down, the replacement keeps the old place in the queue. Otherwise it goes to the back.
  - If the new price crosses the book, the replacement can match immediately, just like a freshly arrived order.
  - **Report:** **replaced**, with both the old and new client IDs.
- **If invalid** (fails validation): REJECT, and the old order is untouched. It stays New, in the same place in the book.
- **Example:** buy 100 at 50 → replace to buy 80 at 50 → old order Replaced; replacement New, 80 at 50, keeps its place (only the quantity went down).

### PartialFill → PartiallyFilled
- **What happens:** part of the order traded against an incoming order.
- **Quantities:** filled goes up, remaining goes down, and both are above 0.
- **Book:** the order stays in the book, in the same place.
- **Report:** **partially filled**, with the fill quantity and price.
- **Example:** buy 100, 30 traded → PartiallyFilled, 30 filled, 70 remaining.

### FullFill → Filled
- **What happens:** the whole order traded.
- **Quantities:** filled = original, remaining = 0.
- **Book:** the order is removed.
- **Report:** **filled**.

### MarketNoLiquidity → Cancelled (market order), BUG (limit order)
- **Market order:** it arrived, found nothing to trade with, and can't wait (it has no price to rest at).
  It becomes **Cancelled** with the reason "no liquidity" (decision 3). Not Rejected,
  because the order itself was valid.
- **Limit order:** a limit order that can't match just rests in the book, so this event
  should never fire for one. If it does, the matching code is treating a limit order like
  a market order: **BUG**.

---

## Row 3: PartiallyFilled, some traded, the rest still waiting

The order is live and still in the book with its remaining quantity.

### NewOrder → REJECT, "duplicate order ID"
- Same as the New row. The existing order is not affected.

### Cancel → Cancelled
- **What happens:** the remaining quantity is removed from the book.
- **The filled part stays filled.** Those trades already happened and can't be undone.
- **Quantities:** filled unchanged, remaining becomes 0.
- **Report:** **cancelled**, showing both how much filled and how much was cancelled.
- **Example:** buy 100, 30 filled, then cancel → Cancelled, 30 filled, 70 cancelled.
  The client bought 30, and the other 70 never trades.

### Replace → Replaced (+ replacement in PartiallyFilled), or REJECT
- **If valid:**
  - The **old order** becomes **Replaced** and leaves the book.
  - The **replacement** starts in **PartiallyFilled**, because it carries over the filled quantity (decision 4).
  - Its remaining quantity = new quantity − already filled.
  - Same time-priority and matching rules as in the New row.
- **If invalid:** REJECT, and the old order is untouched. This includes asking for a quantity at or below
  what's already filled (decision 5). To get out, the client should send a Cancel.
- **Example:** buy 100, 30 filled → replace to 60 → replacement PartiallyFilled, 30 filled, 30 remaining.
  Replace to 20 → REJECT, since 20 is below the 30 already filled.

### PartialFill → PartiallyFilled (stays)
- **What happens:** more traded, but some is still open.
- **Quantities:** filled goes up, remaining goes down, and remaining is still above 0.
- **Report:** **partially filled**.

### FullFill → Filled
- **What happens:** the rest traded.
- **Quantities:** filled = original, remaining = 0. The order leaves the book.
- **Report:** **filled**.

### MarketNoLiquidity → Cancelled (market order), BUG (limit order)
- **Market order:** it traded some, then ran out of liquidity. The rest is **Cancelled**
  and the filled part is kept.
- **Limit order:** BUG, for the same reason as in the New row.
- **Example:** market buy 100, only 40 available → 40 filled, 60 cancelled, "no liquidity".

---

## Rows 4–7: final states (Filled, Cancelled, Rejected, Replaced)

These four rows follow one pattern. The order is finished, so nothing can change it.

### NewOrder → REJECT, "duplicate order ID"
- The ID is used, even though the order is finished. IDs are never reused.

### Cancel / Replace → REJECT, with the reason why

| Row | Reason | Typical cause |
|---|---|---|
| Filled | "too late, already filled" | The client's cancel crossed with the final fill |
| Cancelled | "already cancelled" | A duplicate cancel, or a retry |
| Rejected | "already rejected" | The client didn't notice the original reject |
| Replaced | "too late, already replaced" | The client used the **old** client ID instead of the replacement's |

- **The state never changes.**
- **Why give a reason:** a client whose cancel lost a race needs to know their real
  position. "Too late, already filled" tells them they own the shares.

### PartialFill / FullFill / MarketNoLiquidity → BUG
- The engine tried to trade a finished order. Finished orders aren't in the book, so
  the matching code should never reach them.

---

## Worked scenarios

Each scenario follows one order through the table.

**1. Simple fill**
NewOrder (buy 100 @ 50, valid) → **New** → PartialFill 40 → **PartiallyFilled** (40/100)
→ FullFill 60 → **Filled** (100/100). Out of the book.

**2. Cancel race**
NewOrder → **New** → PartialFill 100 → **Filled**. The client's Cancel arrives a moment later
→ **REJECT, "too late, already filled"**. The order stays Filled.

**3. Replace after a partial fill**
NewOrder (buy 100 @ 50) → **New** → PartialFill 30 → **PartiallyFilled**
→ Replace to buy 60 @ 51 → old order **Replaced**; replacement **PartiallyFilled**
(30 filled, 30 remaining @ 51). The price changed, so it goes to the back of the queue at 51.

**4. Market order with thin liquidity**
NewOrder (market buy 100) → **New** → PartialFill 40 → **PartiallyFilled**
→ MarketNoLiquidity → **Cancelled** (40 filled, 60 cancelled).

**5. Bad input**
NewOrder (buy −5) → **Rejected**, "quantity must be greater than 0".
The client retries with the same ID → **REJECT, "duplicate order ID"**.

---

## Turning this into tests

- **One test per cell:** 42 cells, plus the extra outcomes in the split cells (valid vs. invalid
  replace, market vs. limit order on MarketNoLiquidity).
- **REJECT tests** check two things: the reject report is sent with the right reason, *and*
  the order's state and quantities are unchanged.
- **BUG tests** check that the engine refuses loudly rather than carrying on.
- **Invariant checks** after every event:
  - filled never goes down
  - live orders: filled + remaining = original
  - final orders: remaining = 0
  - in the book ⇔ New or PartiallyFilled
  - exactly one report per accepted transition
