// Order::apply tests: multi-step scenarios on a real Order, with the invariants
// from docs/design.md section 2 checked after every step.
// Naming: "order: <scenario>" so `make test FILTER=order:` runs just these.
#include <order_helpers.hpp>  // makeLimitBuy, makeMarketBuy, checkInvariants, ...

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

TEST_CASE("order: partial 30 then cancel -> Cancelled, filled stays 30"){
    Order o = makeLimitBuy(100);
    REQUIRE_EQ(o.apply(Event::PARTIAL_FILL, 30, true).outcomeType, OutcomeType::Accept);

    Order before = o;
    Outcome out = o.apply(Event::CANCEL, 0, /*validReplace=*/true);

    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::CANCELLED);

    CHECK_EQ(o.getState(), State::CANCELLED);
    CHECK_EQ(o.getFilled(), 30u);     // what traded stays traded (it carries to the new version)
    CHECK_EQ(o.getRemaining(), 0u);   // the old version has nothing left
    checkInvariants(o);
    checkFilledNeverDecreases(before, o);
} 

TEST_CASE("order: fill of 150 on 100 -> Bug, order unchanged"){
    Order o = makeLimitBuy(100);

    Order before = o;
    Outcome out = o.apply(Event::FULL_FILL, 150, /*validReplace=*/true);

    REQUIRE_EQ(out.outcomeType, OutcomeType::Bug);
    checkUnchanged(before, o);
    checkInvariants(o);
}
