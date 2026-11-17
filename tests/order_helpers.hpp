// Shared helpers for tests that work with real Order objects
// (order_test.cpp, create_test.cpp, replace_test.cpp).
#pragma once

#include "test_framework.hpp"

#include "engine/objects.hpp"
#include "engine/print.hpp"

// The constructor is private, so helpers create orders the real way: Order::create.
// REQUIRE the order came out New, so a scenario never starts from a broken order.

// A fresh limit buy for `qty` at 100.00 (10000 ticks), seq 1.
inline Order makeLimitBuy(uint64_t qty) {
    NewOrderResult r = Order::create(OrderIds{.orderId = 1, .clOrdId = 100, .origClOrdId = 0, .chainId = 1},
                                     /*seq=*/1, OrderType::Limit, OrderSide::Buy, /*price=*/10000, qty);
    REQUIRE_EQ(r.order.getState(), State::NEW);
    return r.order;
}

// A fresh market buy for `qty` (price is ignored for market orders), seq 1.
inline Order makeMarketBuy(uint64_t qty) {
    NewOrderResult r = Order::create(OrderIds{.orderId = 2, .clOrdId = 200, .origClOrdId = 0, .chainId = 2},
                                     /*seq=*/1, OrderType::Market, OrderSide::Buy, /*price=*/0, qty);
    REQUIRE_EQ(r.order.getState(), State::NEW);
    return r.order;
}

// A fresh limit order with chosen id (also used as clOrdId/chainId), side,
// price, quantity and seq. For book tests that need many distinct orders.
inline Order makeLimit(uint64_t id, OrderSide side, int64_t price, uint64_t qty, uint64_t seq) {
    NewOrderResult r = Order::create(OrderIds{.orderId = id, .clOrdId = id, .origClOrdId = 0, .chainId = id},
                                     seq, OrderType::Limit, side, price, qty);
    REQUIRE_EQ(r.order.getState(), State::NEW);
    return r.order;
}

// Invariant across steps: filled never goes down. Pass a copy taken before the step.
inline void checkFilledNeverDecreases(const Order& before, const Order& after) {
    CHECK(after.getFilled() >= before.getFilled());
}

// For Reject and Bug outcomes: the order must be left exactly as it was.
inline void checkUnchanged(const Order& before, const Order& after) {
    CHECK_EQ(after.getState(), before.getState());
    CHECK_EQ(after.getFilled(), before.getFilled());
    CHECK_EQ(after.getRemaining(), before.getRemaining());
    CHECK_EQ(after.getOriginal(), before.getOriginal());
    CHECK_EQ(after.getPrice(), before.getPrice());
    CHECK_EQ(after.getSeq(), before.getSeq());
    CHECK_EQ(after.getOrderId(), before.getOrderId());
}

// The quantity invariants. Call after every step of a scenario.
inline void checkInvariants(const Order& o) {
    switch (o.getState()) {
        case State::NEW:
        case State::PARTIALLY_FILLED:
            // live: filled + remaining == original
            CHECK_EQ(o.getFilled() + o.getRemaining(), o.getOriginal());
            break;
        case State::FILLED:
            // Filled: nothing left, and everything was filled
            CHECK_EQ(o.getRemaining(), 0u);
            CHECK_EQ(o.getFilled(), o.getOriginal());
            break;
        case State::CANCELLED:
        case State::REJECTED:
        case State::REPLACED:
            // other final states: nothing left
            CHECK_EQ(o.getRemaining(), 0u);
            break;
    }
}
