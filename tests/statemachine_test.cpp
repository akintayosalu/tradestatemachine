// One TEST_CASE per cell of docs/order_state_machine.md (conditional cells get
// one test per case). Naming: "state: <Row> x <Event> -> <expected>" so
// `make test FILTER=New` etc. work.
//
// Pattern: call decide, REQUIRE the outcome kind first (if it's wrong, the other
// fields are meaningless), then CHECK the field that matters for that kind:
//   Accept -> out.newState     Reject -> out.rejectReason     Bug -> nothing else
#include "test_framework.hpp"

#include "engine/print.hpp"
#include "engine/statemachine.hpp"

// ---------------------------------------------------------------------------
// (none) row: no order with this ID exists -> pass std::nullopt as the state
// ---------------------------------------------------------------------------

// Client events

TEST_CASE("state: (none) x NewOrder (valid) -> New") {
    Outcome out = decide(std::nullopt, Event::NEW_ORDER, Context{});  // default: validation passed

    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::NEW);
}

// Invalid new orders are RECORDED in state Rejected (the matrix: (none) x NewOrder
// -> Rejected + reason). That's a transition, not "state unchanged", so these
// expect Accept -> Rejected carrying the validation reason.
TEST_CASE("state: (none) x NewOrder (invalid quantity) -> Rejected(InvalidQuantity)") {
    Outcome out = decide(std::nullopt, Event::NEW_ORDER,
                         Context{.validationResult = RejectReason::INVALID_QUANTITY});

    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::REJECTED);
    CHECK_EQ(out.rejectReason, RejectReason::INVALID_QUANTITY);
}

TEST_CASE("state: (none) x NewOrder (invalid price) -> Rejected(InvalidPrice)") {
    Outcome out = decide(std::nullopt, Event::NEW_ORDER,
                         Context{.validationResult = RejectReason::INVALID_PRICE});

    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::REJECTED);
    CHECK_EQ(out.rejectReason, RejectReason::INVALID_PRICE);
}

TEST_CASE("state: (none) x Cancel -> Reject") {
    Outcome out = decide(std::nullopt, Event::CANCEL, Context{});

    REQUIRE_EQ(out.outcomeType, OutcomeType::Reject);
    CHECK_EQ(out.rejectReason, RejectReason::UNKNOWN_ORDER);
}

TEST_CASE("state: (none) x Replace -> Reject(UnknownOrder)") {
    Outcome out = decide(std::nullopt, Event::REPLACE, Context{});

    REQUIRE_EQ(out.outcomeType, OutcomeType::Reject);
    CHECK_EQ(out.rejectReason, RejectReason::UNKNOWN_ORDER);
}

// Engine events: the engine can't fill or cancel an order that doesn't exist.

TEST_CASE("state: (none) x PartialFill -> Bug") {
    Outcome out = decide(std::nullopt, Event::PARTIAL_FILL, Context{});

    CHECK_EQ(out.outcomeType, OutcomeType::Bug);
}

TEST_CASE("state: (none) x FullFill -> Bug") {
    Outcome out = decide(std::nullopt, Event::FULL_FILL, Context{.fillLevel = FillLevel::Exact});

    CHECK_EQ(out.outcomeType, OutcomeType::Bug);
}

TEST_CASE("state: (none) x MarketNoLiquidity -> Bug") {
    Outcome out = decide(std::nullopt, Event::MARKET_NO_LIQUIDITY, Context{.orderType = OrderType::Market});

    CHECK_EQ(out.outcomeType, OutcomeType::Bug);
}

// ---------------------------------------------------------------------------
// New row
// ---------------------------------------------------------------------------

// Client events

TEST_CASE("state: New x NewOrder -> Reject(DuplicateOrderId)") {
    Outcome out = decide(State::NEW, Event::NEW_ORDER, Context{});

    REQUIRE_EQ(out.outcomeType, OutcomeType::Reject);
    CHECK_EQ(out.rejectReason, RejectReason::DUPLICATE_ORDER_ID);
}

TEST_CASE("state: New x Cancel -> Cancelled") {
    Outcome out = decide(State::NEW, Event::CANCEL, Context{});

    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::CANCELLED);
}

// decide describes the OLD order: it becomes Replaced. Creating the new
// version (which starts in New) is the caller's job, not decide's.
TEST_CASE("state: New x Replace (valid) -> Replaced") {
    Outcome out = decide(State::NEW, Event::REPLACE, Context{.replaceValid = true});

    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::REPLACED);
}

TEST_CASE("state: New x Replace (invalid) -> Reject(InvalidReplace)") {
    Outcome out = decide(State::NEW, Event::REPLACE, Context{.replaceValid = false});

    REQUIRE_EQ(out.outcomeType, OutcomeType::Reject);
    CHECK_EQ(out.rejectReason, RejectReason::INVALID_REPLACE);
}

// Engine events: fills. The event must agree with how the fill fits `remaining`.

TEST_CASE("state: New x PartialFill (partial) -> PartiallyFilled") {
    Outcome out = decide(State::NEW, Event::PARTIAL_FILL, Context{.fillLevel = FillLevel::Partial});

    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::PARTIALLY_FILLED);
}

TEST_CASE("state: New x PartialFill (exact) -> Bug") {
    // A "partial" fill that uses up everything should have been a FullFill.
    Outcome out = decide(State::NEW, Event::PARTIAL_FILL, Context{.fillLevel = FillLevel::Exact});

    CHECK_EQ(out.outcomeType, OutcomeType::Bug);
}

TEST_CASE("state: New x PartialFill (over) -> Bug") {
    Outcome out = decide(State::NEW, Event::PARTIAL_FILL, Context{.fillLevel = FillLevel::Over});

    CHECK_EQ(out.outcomeType, OutcomeType::Bug);
}

TEST_CASE("state: New x FullFill (exact) -> Filled") {
    Outcome out = decide(State::NEW, Event::FULL_FILL, Context{.fillLevel = FillLevel::Exact});

    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::FILLED);
}

TEST_CASE("state: New x FullFill (partial) -> Bug") {
    // A "full" fill that leaves quantity behind should have been a PartialFill.
    Outcome out = decide(State::NEW, Event::FULL_FILL, Context{.fillLevel = FillLevel::Partial});

    CHECK_EQ(out.outcomeType, OutcomeType::Bug);
}

TEST_CASE("state: New x FullFill (over) -> Bug") {
    Outcome out = decide(State::NEW, Event::FULL_FILL, Context{.fillLevel = FillLevel::Over});

    CHECK_EQ(out.outcomeType, OutcomeType::Bug);
}

// Engine events: no liquidity. Only meaningful for market orders.

TEST_CASE("state: New x MarketNoLiquidity (market) -> Cancelled") {
    Outcome out = decide(State::NEW, Event::MARKET_NO_LIQUIDITY, Context{.orderType = OrderType::Market});

    REQUIRE_EQ(out.outcomeType, OutcomeType::Accept);
    CHECK_EQ(out.newState, State::CANCELLED);
}

TEST_CASE("state: New x MarketNoLiquidity (limit) -> Bug") {
    // A limit order with nothing to match just rests in the book; the matcher
    // should never send this event for it.
    Outcome out = decide(State::NEW, Event::MARKET_NO_LIQUIDITY, Context{.orderType = OrderType::Limit});

    CHECK_EQ(out.outcomeType, OutcomeType::Bug);
}
