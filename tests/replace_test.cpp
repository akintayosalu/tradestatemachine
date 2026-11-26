// FIX-style replace: isValidReplace and Order::replace.
// Naming: "replace: ..." so `make test FILTER=replace:` runs just these.
//
// Base order (makeLimitBuy): orderId 1, clOrdId 100, chainId 1, seq 1, price 10000.
// Every replace here gives the new version orderId 3 / clOrdId 300 and offers
// freshSeq 9, which must be used only when priority is lost.
#include <order_helpers.hpp>

// ---------------------------------------------------------------------------
// isValidReplace: new total quantity must exceed what's already filled,
// and a limit order's new price must be > 0 (same price rule as a new order)
// ---------------------------------------------------------------------------

TEST_CASE("replace: valid? fresh order, new qty 50 -> yes") {
    Order o = makeLimitBuy(100);
    CHECK(isValidReplace(o, 10000, 50));
}

TEST_CASE("replace: valid? fresh order, new qty 0 -> no") {
    Order o = makeLimitBuy(100);
    CHECK(!isValidReplace(o, 10000, 0));
}

TEST_CASE("replace: valid? filled 30, new qty 31 -> yes") {
    Order o = makeLimitBuy(100);
    REQUIRE_EQ(o.apply(Event::PARTIAL_FILL, 30, true).outcomeType, OutcomeType::Accept);
    CHECK(isValidReplace(o, 10000, 31));
}

TEST_CASE("replace: valid? filled 30, new qty 30 -> no (nothing left to work)") {
    Order o = makeLimitBuy(100);
    REQUIRE_EQ(o.apply(Event::PARTIAL_FILL, 30, true).outcomeType, OutcomeType::Accept);
    CHECK(!isValidReplace(o, 10000, 30));
}

TEST_CASE("replace: valid? filled 30, new qty 20 -> no") {
    Order o = makeLimitBuy(100);
    REQUIRE_EQ(o.apply(Event::PARTIAL_FILL, 30, true).outcomeType, OutcomeType::Accept);
    CHECK(!isValidReplace(o, 10000, 20));
}

TEST_CASE("replace: valid? limit order, new price 0 -> no") {
    // A replace must not create a version that Order::create would have rejected.
    Order o = makeLimitBuy(100);
    CHECK(!isValidReplace(o, /*newPrice=*/0, 50));
}

TEST_CASE("replace: valid? limit order, negative new price -> no") {
    Order o = makeLimitBuy(100);
    CHECK(!isValidReplace(o, /*newPrice=*/-5, 50));
}

TEST_CASE("replace: valid? market order, new price 0 -> yes (price ignored)") {
    Order o = makeMarketBuy(100);
    CHECK(isValidReplace(o, /*newPrice=*/0, 50));
}

// ---------------------------------------------------------------------------
// Order::replace: accepted
// ---------------------------------------------------------------------------

TEST_CASE("replace: after partial 30, qty 100->80 same price -> keeps priority") {
    Order o = makeLimitBuy(100);
    REQUIRE_EQ(o.apply(Event::PARTIAL_FILL, 30, true).outcomeType, OutcomeType::Accept);

    ReplaceResult r = o.replace(/*newOrderId=*/3, /*newClOrdId=*/300, /*newPrice=*/10000,
                                /*newQuantity=*/80, /*freshSeq=*/9);

    // old version
    REQUIRE_EQ(r.outcome.outcomeType, OutcomeType::Accept);
    CHECK_EQ(r.outcome.newState, State::REPLACED);
    CHECK_EQ(o.getState(), State::REPLACED);
    CHECK_EQ(o.getRemaining(), 0u);
    CHECK_EQ(o.getFilled(), 30u);
    checkInvariants(o);

    // new version
    REQUIRE(r.replacement.has_value());
    const Order& n = *r.replacement;
    CHECK_EQ(n.getState(), State::PARTIALLY_FILLED);  // starts in the old order's state
    CHECK_EQ(n.getOriginal(), 80u);                    // new total quantity
    CHECK_EQ(n.getFilled(), 30u);                      // carried over
    CHECK_EQ(n.getRemaining(), 50u);                   // 80 - 30
    CHECK_EQ(n.getPrice(), 10000);
    CHECK_EQ(n.getSeq(), 1u);                          // same price + qty down: keeps old seq
    checkInvariants(n);
}

TEST_CASE("replace: new version links back to the old one") {
    Order o = makeLimitBuy(100);
    ReplaceResult r = o.replace(3, 300, 10000, 80, 9);
    REQUIRE(r.replacement.has_value());
    const Order& n = *r.replacement;

    CHECK_EQ(n.getOrderId(), 3u);       // new internal ID (supplied by caller)
    CHECK_EQ(n.getClOrdId(), 300u);     // new client ID (from the request)
    CHECK_EQ(n.getOrigClOrdId(), 100u); // = old version's client ID
    CHECK_EQ(n.getChainId(), 1u);       // same chain as the old version
    CHECK_EQ(n.getSide(), OrderSide::Buy);     // side and type carry over
    CHECK_EQ(n.getType(), OrderType::Limit);
}

TEST_CASE("replace: New order, price change -> new version New, loses priority") {
    Order o = makeLimitBuy(100);
    ReplaceResult r = o.replace(3, 300, /*newPrice=*/10100, /*newQuantity=*/100, /*freshSeq=*/9);

    REQUIRE_EQ(r.outcome.outcomeType, OutcomeType::Accept);
    REQUIRE(r.replacement.has_value());
    CHECK_EQ(r.replacement->getState(), State::NEW);
    CHECK_EQ(r.replacement->getPrice(), 10100);
    CHECK_EQ(r.replacement->getFilled(), 0u);
    CHECK_EQ(r.replacement->getRemaining(), 100u);
    CHECK_EQ(r.replacement->getSeq(), 9u);  // price changed: back of the queue
    checkInvariants(*r.replacement);
}

TEST_CASE("replace: same price, qty up 100->150 -> loses priority") {
    Order o = makeLimitBuy(100);
    ReplaceResult r = o.replace(3, 300, 10000, /*newQuantity=*/150, /*freshSeq=*/9);

    REQUIRE(r.replacement.has_value());
    CHECK_EQ(r.replacement->getSeq(), 9u);
    CHECK_EQ(r.replacement->getRemaining(), 150u);
}

TEST_CASE("replace: same price, same qty -> loses priority (qty did not go down)") {
    // Rule (design.md s.3): keep priority only if price unchanged AND qty decreased.
    Order o = makeLimitBuy(100);
    ReplaceResult r = o.replace(3, 300, 10000, 100, 9);

    REQUIRE(r.replacement.has_value());
    CHECK_EQ(r.replacement->getSeq(), 9u);
}

TEST_CASE("replace: twice A->B->C -> C links to B, same chain") {
    Order a = makeLimitBuy(100);
    ReplaceResult rb = a.replace(3, 300, 10000, 90, 9);
    REQUIRE(rb.replacement.has_value());
    Order b = *rb.replacement;

    ReplaceResult rc = b.replace(4, 400, 10000, 80, 10);
    REQUIRE_EQ(rc.outcome.outcomeType, OutcomeType::Accept);
    REQUIRE(rc.replacement.has_value());

    CHECK_EQ(b.getState(), State::REPLACED);
    CHECK_EQ(rc.replacement->getOrigClOrdId(), 300u);  // the CURRENT version it replaced, not A
    CHECK_EQ(rc.replacement->getChainId(), 1u);        // still A's chain
    CHECK_EQ(rc.replacement->getSeq(), 1u);            // qty down twice, price same: still A's seq
}

// ---------------------------------------------------------------------------
// Order::replace: rejected (no replacement, old version untouched)
// ---------------------------------------------------------------------------

TEST_CASE("replace: new qty <= filled -> Reject(InvalidReplace), unchanged") {
    Order o = makeLimitBuy(100);
    REQUIRE_EQ(o.apply(Event::PARTIAL_FILL, 30, true).outcomeType, OutcomeType::Accept);

    Order before = o;
    ReplaceResult r = o.replace(3, 300, 10000, /*newQuantity=*/30, 9);

    REQUIRE_EQ(r.outcome.outcomeType, OutcomeType::Reject);
    CHECK_EQ(r.outcome.rejectReason, RejectReason::INVALID_REPLACE);
    CHECK(!r.replacement.has_value());
    checkUnchanged(before, o);
}

TEST_CASE("replace: limit order to price 0 -> Reject(InvalidReplace), unchanged") {
    Order o = makeLimitBuy(100);

    Order before = o;
    ReplaceResult r = o.replace(3, 300, /*newPrice=*/0, 80, 9);

    REQUIRE_EQ(r.outcome.outcomeType, OutcomeType::Reject);
    CHECK_EQ(r.outcome.rejectReason, RejectReason::INVALID_REPLACE);
    CHECK(!r.replacement.has_value());
    checkUnchanged(before, o);
}

TEST_CASE("replace: on a Filled order -> Reject(AlreadyFilled), unchanged") {
    Order o = makeLimitBuy(100);
    REQUIRE_EQ(o.apply(Event::FULL_FILL, 100, true).outcomeType, OutcomeType::Accept);

    Order before = o;
    ReplaceResult r = o.replace(3, 300, 10000, 150, 9);

    REQUIRE_EQ(r.outcome.outcomeType, OutcomeType::Reject);
    CHECK_EQ(r.outcome.rejectReason, RejectReason::ALREADY_FILLED);
    CHECK(!r.replacement.has_value());
    checkUnchanged(before, o);
}

TEST_CASE("replace: on an already-Replaced version -> Reject(AlreadyReplaced), unchanged") {
    Order a = makeLimitBuy(100);
    REQUIRE(a.replace(3, 300, 10000, 90, 9).replacement.has_value());
    REQUIRE_EQ(a.getState(), State::REPLACED);

    Order before = a;
    ReplaceResult r = a.replace(4, 400, 10000, 80, 10);  // client used a stale ID

    REQUIRE_EQ(r.outcome.outcomeType, OutcomeType::Reject);
    CHECK_EQ(r.outcome.rejectReason, RejectReason::ALREADY_REPLACED);
    CHECK(!r.replacement.has_value());
    checkUnchanged(before, a);
}
