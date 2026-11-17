#pragma once

#include <iostream>
#include <cstdint>

#include <engine/types.hpp>
#include <engine/statemachine.hpp>

// Who an order is (docs/design.md section 5). Grouped in a struct so the four
// IDs can't be passed in the wrong order; build it with designated initializers:
//   OrderIds{.orderId = 7, .clOrdId = 100, .origClOrdId = 0, .chainId = 7}
struct OrderIds {
    uint64_t orderId;      // internal, engine-assigned, unique per version
    uint64_t clOrdId;      // client's ID for this version
    uint64_t origClOrdId;  // client ID of the version this one replaced; 0 = not a replacement
    uint64_t chainId;      // orderId of the first version in the replace chain (== orderId if first)
};

struct NewOrderResult;  // defined after Order
struct ReplaceResult;   // defined after Order

class Order {
    OrderIds ids;
    uint64_t seq;          // time priority; assigned by the engine instance, never a static counter
    OrderType type;
    OrderSide side;
    int64_t price;         // ticks; ignored for market orders
    uint64_t original;
    uint64_t filled;
    uint64_t remaining;    // stored: final orders have 0, not original - filled
    State state;

    public:
        friend std::ostream& operator<<(std::ostream& os, const Order& o);

        // `seq` is passed in, not generated here: the engine that accepts the order owns the counter.
        Order(const OrderIds& orderIds, uint64_t seq, OrderType orderType, OrderSide orderSide, int64_t price, uint64_t size) :
            ids{orderIds}, seq{seq}, type{orderType}, side{orderSide}, price{price}, original{size}, filled{0}, remaining{size}, state{State::NEW}
        {
        }

        // Read-only access. There are deliberately no setters: state, filled and
        // remaining change only by applying an event (design.md section 5).
        const OrderIds& getIds() const { return ids; }
        uint64_t getOrderId() const { return ids.orderId; }
        uint64_t getClOrdId() const { return ids.clOrdId; }
        uint64_t getOrigClOrdId() const { return ids.origClOrdId; }
        uint64_t getChainId() const { return ids.chainId; }
        uint64_t getSeq() const { return seq; }
        OrderType getType() const { return type; }
        OrderSide getSide() const { return side; }
        int64_t getPrice() const { return price; }
        uint64_t getOriginal() const { return original; }
        uint64_t getFilled() const { return filled; }
        uint64_t getRemaining() const { return remaining; }
        State getState() const { return state; }

        // Update the order's state, filled, and remaining fields based on an event.
        Outcome apply(const Event& event, uint64_t fillQuantity, bool validReplace);

        // --- Creation: the (none) x NewOrder path (tests/create_test.cpp) ---
        // Validates the request, asks decide, and builds the order in New, or in
        // Rejected (remaining 0) with the reason in the outcome. A static member so
        // it can set private state; the only intended way to create a real order.
        static NewOrderResult create(const OrderIds& ids, uint64_t seq, OrderType type,
                                     OrderSide side, int64_t price, uint64_t quantity);

        // --- Replace: FIX-style cancel/replace (tests/replace_test.cpp) ---
        // Called on the CURRENT version. On success this order becomes Replaced and
        // the new version is returned. The caller supplies the new version's
        // internal ID and client ID; origClOrdId and chainId are linked from this
        // order. `freshSeq` is used only if the replacement loses time priority.
        // newQuantity is the new TOTAL quantity (including what's already filled).
        ReplaceResult replace(uint64_t newOrderId, uint64_t newClOrdId, int64_t newPrice,
                              uint64_t newQuantity, uint64_t freshSeq);
};

// Result of Order::create: what decide said, plus the order it produced.
struct NewOrderResult {
    Outcome outcome;   // Accept -> New, or Accept -> Rejected + reason
    Order order;
};

// Result of Order::replace.
struct ReplaceResult {
    Outcome outcome;                   // for the OLD version
    std::optional<Order> replacement;  // set only when the replace was accepted
};

// First failing check for a new order, or empty if valid. Checks run in a fixed
// order so an order with several problems always reports the same reason:
//   1. quantity > 0                     else INVALID_QUANTITY
//   2. limit orders only: price > 0     else INVALID_PRICE   (market price is ignored)
std::optional<RejectReason> validateNewOrder(OrderType type, int64_t price, uint64_t quantity);

// Is replacing `current` with a new total quantity allowed?
// The new quantity must be greater than what has already been filled.
bool isValidReplace(const Order& current, uint64_t newQuantity);
