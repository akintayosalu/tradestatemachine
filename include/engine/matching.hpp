// Matching: an incoming order trades against the book by price-time priority.
// design.md section 8 (decisions), todo/order_book.txt section 2 (the loop).
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "engine/book.hpp"
#include "engine/objects.hpp"
#include "engine/store.hpp"
#include "engine/types.hpp"

// One fill between the incoming (aggressor) order and ONE resting order.
// An incoming order that crosses several resting orders produces several trades.
struct Trade {
    uint64_t tradeId;      // from the caller's counter, consecutive
    uint64_t aggressorId;  // internal ID of the incoming order
    uint64_t restingId;    // internal ID of the resting order it traded with
    int64_t price;         // always the RESTING order's price
    uint64_t qty;
};

// What went wrong, when match hits a Bug (design.md sections 6 and 8).
struct MatchBug {
    uint64_t orderId;            // the order the failing step was about
    std::optional<Event> event;  // the event apply refused, or empty if a
                                 // precondition failed (unknown / not live / already in book)
};

struct MatchResult {
    std::vector<Trade> trades;    // in the order they happened (on a Bug: those made so far)
    std::optional<MatchBug> bug;  // set = stop; the caller logs and aborts
};

// Can `incoming` trade with the best level on the other side right now?
//   buy limit:  opposite side not empty and price >= best ask
//   sell limit: opposite side not empty and price <= best bid
//   market:     opposite side not empty
bool crosses(const Order& incoming, const OrderBook& book);

// Match the stored order `incomingId` against `book`, then finish it:
//   - while it has remaining and crosses: trade with the FRONT order of the best
//     opposite level at that order's price; apply a fill to both orders (FullFill
//     when the trade qty equals that order's remaining, else PartialFill); remove
//     resting orders that are now Filled from the book.
//   - then a limit order with remaining rests at the back of its level; a market
//     order with remaining gets MarketNoLiquidity (-> Cancelled).
//
// Preconditions (a broken one is a Bug, nothing changes): `incomingId` is in
// `store`, is live (New or PartiallyFilled: a replacement may already be partly
// filled), and is not in `book`.
//
// Each trade takes `nextTradeId` and increments it, so trade IDs continue across
// calls. Any non-Accept outcome from apply, or a book entry the store doesn't
// have as a live order, stops matching and is returned as a Bug.
MatchResult match(uint64_t incomingId, OrderBook& book, OrderStore& store, uint64_t& nextTradeId);
