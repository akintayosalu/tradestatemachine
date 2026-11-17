// The order book: an INDEX of resting live limit orders by side, price and time.
// It does not own orders (the order store does, design.md section 7); it only
// holds internal order IDs, so it reads each order's side/price/state from the
// Order passed in.
//
// No matching happens here: add() never trades. The matcher (next step) is
// responsible for only adding an order's non-crossing remainder.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "engine/objects.hpp"
#include "engine/types.hpp"

class OrderBook {
public:
    // Rest `order` at the BACK of its price level's queue (creating the level
    // if needed). Only live limit orders may rest. Returns false, and changes
    // nothing, if the order is a market order, not live (state other than New
    // or PartiallyFilled), or already in the book.
    bool add(const Order& order);

    // Take `order` out of the book (cancel, old version on replace, filled
    // during matching). Deletes its level if that level is now empty.
    // Returns false, and changes nothing, if the order isn't in the book.
    bool remove(const Order& order);

    // Replace that KEEPS priority: `newVersion` takes `oldVersion`'s exact
    // position in the same queue, and `oldVersion` leaves the book.
    // Both must be on the same side at the same price. Returns false, and
    // changes nothing, if oldVersion isn't in the book, newVersion is already
    // in it, or newVersion isn't a live limit order at the same side/price.
    // (A replace that LOSES priority is just remove(old) + add(new).)
    bool replaceInPlace(const Order& oldVersion, const Order& newVersion);

    // Best prices: highest bid, lowest ask. Empty if that side has no orders.
    std::optional<int64_t> bestBid() const;
    std::optional<int64_t> bestAsk() const;

    // --- Queries (used by tests, the matcher and later market data) ---

    // Is this internal order ID resting in the book?
    bool contains(uint64_t orderId) const;

    // Total number of resting orders, both sides.
    std::size_t size() const;

    // Number of non-empty price levels on one side.
    std::size_t levelCount(OrderSide side) const;

    // Prices with resting orders on one side, BEST FIRST
    // (bids: highest -> lowest, asks: lowest -> highest).
    std::vector<int64_t> prices(OrderSide side) const;

    // Internal IDs resting at one price, FRONT of the queue first.
    // Empty if there is no such level.
    std::vector<uint64_t> idsAt(OrderSide side, int64_t price) const;

private:
    // Your data members go here (design.md section 7):
    //   - per side: ordered map price -> price level (std::list of internal IDs)
    //     bids best = highest, asks best = lowest
    //   - internal ID -> handle (side, price, list iterator) for O(1) remove
};
