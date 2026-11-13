// Order::apply tests: multi-step scenarios on a real Order, with the invariants
// from docs/design.md section 2 checked after every step.
// Naming: "order: <scenario>" so `make test FILTER=order:` runs just these.
#include "test_framework.hpp"

#include "engine/objects.hpp"
#include "engine/print.hpp"

// A fresh limit buy for `qty` at 100.00 (10000 ticks), seq 1.
static Order makeLimitBuy(uint64_t qty) {
    return Order{OrderIds{.orderId = 1, .clOrdId = 100, .origClOrdId = 0, .chainId = 1},
                 /*seq=*/1, OrderType::Limit, OrderSide::Buy, /*price=*/10000, qty};
}

// The quantity invariants. Call after every step of a scenario.
static void checkInvariants(const Order& o) {
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

TEST_CASE("order: New -> partial fill 30 of 100 -> PartiallyFilled") {
    Order o = makeLimitBuy(100);
    checkInvariants(o);

    Outcome out = o.apply(Event::PARTIAL_FILL, /*fillQuantity=*/30, /*validReplace=*/true);

    // what decide said
    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::PARTIALLY_FILLED);

    // what apply did to the order
    CHECK_EQ(o.getState(), State::PARTIALLY_FILLED);
    CHECK_EQ(o.getFilled(), 30u);
    CHECK_EQ(o.getRemaining(), 70u);
    checkInvariants(o);
}
