# Engine input / output format

The exact text the engine reads and writes. The determinism test compares output
byte for byte, so every detail here matters: field order, spelling, spacing.
Decisions behind it: `docs/design.md` section 9.

## Input: one request per line

```
NEW     clOrdId=100 side=Buy type=Limit px=10100 qty=50
NEW     clOrdId=101 side=Sell type=Market qty=20
CANCEL  clOrdId=100
REPLACE origClOrdId=100 clOrdId=102 px=10200 qty=80
```

- The first token is the request type: `NEW`, `CANCEL` or `REPLACE` (exact case).
- Then `key=value` fields, separated by one or more spaces, **in any order**.
- Required fields:

  | Request | Fields |
  |---|---|
  | `NEW` | `clOrdId`, `side` (`Buy`/`Sell`), `type` (`Limit`/`Market`), `qty`, and `px` for Limit (optional and ignored for Market) |
  | `CANCEL` | `clOrdId`: the order to cancel |
  | `REPLACE` | `origClOrdId`: the version being replaced; `clOrdId`: the new version's ID; `px`; `qty` (new TOTAL quantity) |

- `clOrdId`, `origClOrdId`, `qty`: unsigned integers. `px`: signed integer (ticks).
  A value of 0 or a negative price parses fine: the order is then *rejected* by
  validation, which is different from being malformed.
- **Malformed** = unknown request type, missing required field, unknown key,
  the same key twice, a value that isn't a valid number / side / type, or a
  token without `=`. A malformed line produces `MALFORMED` and the engine
  carries on with the next line. Bad input is not an engine Bug.
- **Blank lines and lines starting with `#`** are skipped (no output), but still
  counted for line numbers. Line numbers start at 1.

## Output: one event per line

Every line is `KIND key=value key=value ...` with single spaces, fields in
exactly the order shown, and `\n` at the end. IDs in the output are **client
IDs** (ClOrdID), except `orderId` (the engine's internal ID) and `BUG`.
States, reasons and events are spelled as `print.hpp` prints them.

```
ACCEPT request=New clOrdId=100 orderId=1
ACCEPT request=Replace clOrdId=102 origClOrdId=100 orderId=3
REJECT request=New clOrdId=100 reason=InvalidPrice
REJECT request=Cancel clOrdId=100 reason=UnknownOrder
REJECT request=Replace clOrdId=102 origClOrdId=100 reason=InvalidReplace
TRADE tradeId=1 aggressor=101 resting=100 px=10100 qty=30
STATE clOrdId=100 state=Filled filled=30 remaining=0
MALFORMED line=7
BUG orderId=5 event=FullFill
BUG orderId=5
```

## What each request writes

**NEW**
1. Client ID already used (by any order ever stored) -> `REJECT request=New ... reason=DuplicateOrderId`. Nothing stored.
2. Otherwise the order gets the next internal ID and is created and stored.
   - Rejected by validation -> `REJECT request=New ... reason=<InvalidQuantity|InvalidPrice>`.
     It stays in the store (its client ID now counts as used).
   - Accepted -> `ACCEPT request=New ...`, then matching:
     for each trade, in order: `TRADE ...` then `STATE` of that resting order;
     finally `STATE` of the incoming order (always, even with no trades: it shows
     New/PartiallyFilled resting, Filled, or Cancelled for a market order).

**CANCEL** (names the order by its client ID)
1. Unknown client ID -> `REJECT request=Cancel ... reason=UnknownOrder`.
2. The state machine rejects it (too late) -> `REJECT request=Cancel ... reason=<AlreadyFilled|AlreadyCancelled|AlreadyRejected|AlreadyReplaced>`.
3. Accepted -> taken out of the book, then `STATE clOrdId=... state=Cancelled ...`.

**REPLACE**, checks in this order:
1. New `clOrdId` already used -> `REJECT request=Replace ... reason=DuplicateOrderId`.
2. Unknown `origClOrdId` -> `REJECT request=Replace ... reason=UnknownOrder`.
3. `Order::replace` rejects it -> `REJECT request=Replace ... reason=<its reason>`
   (InvalidReplace, or AlreadyFilled / AlreadyCancelled / ... when too late).
4. Accepted -> the new version gets the next internal ID.
   `ACCEPT request=Replace ...`, then `STATE` of the old version (Replaced), then:
   - priority kept (same seq): it takes the old version's place in the queue
     (`replaceInPlace`), then `STATE` of the new version.
   - priority lost: the old version leaves the book and the new version goes
     through matching exactly like a NEW (`TRADE`/`STATE` pairs, then `STATE` of
     the new version).

**Malformed line** -> `MALFORMED line=<n>`.

**Bug** (from `match`, `apply` or a book call that can't fail) -> one
`BUG orderId=<internal ID>[ event=<Event>]` line, and the engine stops: no more
lines are processed. The program writes everything out, then calls `std::abort()`.

## Counters

- **Internal order IDs** count stored orders, from 1: every NEW that isn't a
  duplicate (accepted or rejected) and every accepted REPLACE. A rejected
  REPLACE or a duplicate NEW does not use one.
- **Trade IDs**: from 1, one per trade (owned by the engine, passed to `match`).
- **Seq**: any strictly increasing counter (one per NEW, one fresh per REPLACE).
  Not printed, so only its order matters.
