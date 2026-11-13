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

// A fresh market buy for `qty` (price is ignored for market orders), seq 1.
static Order makeMarketBuy(uint64_t qty) {
    return Order{OrderIds{.orderId = 2, .clOrdId = 200, .origClOrdId = 0, .chainId = 2},
                 /*seq=*/1, OrderType::Market, OrderSide::Buy, /*price=*/0, qty};
}

// Invariant across steps: filled never goes down. Pass a copy taken before the step.
static void checkFilledNeverDecreases(const Order& before, const Order& after) {
    CHECK(after.getFilled() >= before.getFilled());
}

// For Reject and Bug outcomes: apply must leave every field exactly as it was.
static void checkUnchanged(const Order& before, const Order& after) {
    CHECK_EQ(after.getState(), before.getState());
    CHECK_EQ(after.getFilled(), before.getFilled());
    CHECK_EQ(after.getRemaining(), before.getRemaining());
    CHECK_EQ(after.getOriginal(), before.getOriginal());
    CHECK_EQ(after.getPrice(), before.getPrice());
    CHECK_EQ(after.getSeq(), before.getSeq());
    CHECK_EQ(after.getOrderId(), before.getOrderId());
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

TEST_CASE("order: partial 30 then full fill 70 -> Filled") {
    Order o = makeLimitBuy(100);

    Order before = o;
    REQUIRE_EQ(o.apply(Event::PARTIAL_FILL, 30, true).outcomeType, OutcomeType::Accept);
    checkInvariants(o);
    checkFilledNeverDecreases(before, o);

    before = o;
    Outcome out = o.apply(Event::FULL_FILL, /*fillQuantity=*/70, true);
    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::FILLED);

    CHECK_EQ(o.getState(), State::FILLED);
    CHECK_EQ(o.getFilled(), 100u);
    CHECK_EQ(o.getRemaining(), 0u);
    checkInvariants(o);
    checkFilledNeverDecreases(before, o);
}

TEST_CASE("order: market buy partial 30 then no liquidity -> Cancelled") {
    Order o = makeMarketBuy(100);

    Order before = o;
    REQUIRE_EQ(o.apply(Event::PARTIAL_FILL, 30, true).outcomeType, OutcomeType::Accept);
    checkInvariants(o);
    checkFilledNeverDecreases(before, o);

    // The book ran out: the unfilled 70 is cancelled; the 30 that traded stays traded.
    before = o;
    Outcome out = o.apply(Event::MARKET_NO_LIQUIDITY, /*fillQuantity=*/0, true);
    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::CANCELLED);

    CHECK_EQ(o.getState(), State::CANCELLED);
    CHECK_EQ(o.getFilled(), 30u);
    CHECK_EQ(o.getRemaining(), 0u);
    checkInvariants(o);
    checkFilledNeverDecreases(before, o);
}

TEST_CASE("order: cancel a Filled order -> Reject(AlreadyFilled), order unchanged") {
    Order o = makeLimitBuy(100);
    REQUIRE_EQ(o.apply(Event::FULL_FILL, 100, true).outcomeType, OutcomeType::Accept);
    REQUIRE_EQ(o.getState(), State::FILLED);

    Order before = o;
    Outcome out = o.apply(Event::CANCEL, 0, true);

    REQUIRE_EQ(out.outcomeType, OutcomeType::Reject);
    CHECK_EQ(out.rejectReason, RejectReason::ALREADY_FILLED);
    checkUnchanged(before, o);
    checkInvariants(o);
}

TEST_CASE("order: valid replace after partial 30 -> old version Replaced") {
    Order o = makeLimitBuy(100);
    REQUIRE_EQ(o.apply(Event::PARTIAL_FILL, 30, true).outcomeType, OutcomeType::Accept);

    // apply only handles the OLD version; creating the new version is the caller's job.
    Order before = o;
    Outcome out = o.apply(Event::REPLACE, 0, /*validReplace=*/true);
    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::REPLACED);

    CHECK_EQ(o.getState(), State::REPLACED);
    CHECK_EQ(o.getFilled(), 30u);     // what traded stays traded (it carries to the new version)
    CHECK_EQ(o.getRemaining(), 0u);   // the old version has nothing left
    checkInvariants(o);
    checkFilledNeverDecreases(before, o);
}

// ---------------------------------------------------------------------------
// YOUR TURN: two scenarios to write yourself, same pattern as above.
//
// 1. "order: partial 30 then cancel -> Cancelled, filled stays 30"
//    Partial fill 30 of 100, then Cancel. Expect Accept -> Cancelled,
//    filled still 30, remaining 0. Check invariants + filled never decreases.
//
// 2. "order: fill of 150 on 100 -> Bug, order unchanged"
//    Fresh order of 100, apply a PartialFill (or FullFill) of 150.
//    Expect a Bug outcome and checkUnchanged(before, o): no state change and,
//    importantly, remaining did NOT wrap around to a huge number.
// ---------------------------------------------------------------------------
