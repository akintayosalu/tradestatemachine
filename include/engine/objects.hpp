#pragma once

#include <iostream>
#include <cstdint>

#include <engine/types.hpp>

// Who an order is (docs/design.md section 5). Grouped in a struct so the four
// IDs can't be passed in the wrong order; build it with designated initializers:
//   OrderIds{.orderId = 7, .clOrdId = 100, .origClOrdId = 0, .chainId = 7}
struct OrderIds {
    uint64_t orderId;      // internal, engine-assigned, unique per version
    uint64_t clOrdId;      // client's ID for this version
    uint64_t origClOrdId;  // client ID of the version this one replaced; 0 = not a replacement
    uint64_t chainId;      // orderId of the first version in the replace chain (== orderId if first)
};

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
        void apply(const Event& event, uint64_t fillQuantity, bool validReplace){}
};
