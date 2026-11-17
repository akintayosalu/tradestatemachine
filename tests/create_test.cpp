// Creating orders: validateNewOrder and Order::create (the (none) x NewOrder path).
// Naming: "create: ..." so `make test FILTER=create:` runs just these.
#include "order_helpers.hpp"

static const OrderIds kIds{.orderId = 10, .clOrdId = 1000, .origClOrdId = 0, .chainId = 10};

// ---------------------------------------------------------------------------
// validateNewOrder
// ---------------------------------------------------------------------------

TEST_CASE("create: validate limit qty 100 px 10000 -> valid") {
    CHECK_EQ(validateNewOrder(OrderType::Limit, 10000, 100), std::optional<RejectReason>{});
}

TEST_CASE("create: validate qty 0 -> InvalidQuantity") {
    CHECK_EQ(validateNewOrder(OrderType::Limit, 10000, 0),
             std::optional<RejectReason>(RejectReason::INVALID_QUANTITY));
}

TEST_CASE("create: validate limit price 0 -> InvalidPrice") {
    CHECK_EQ(validateNewOrder(OrderType::Limit, 0, 100),
             std::optional<RejectReason>(RejectReason::INVALID_PRICE));
}

TEST_CASE("create: validate limit negative price -> InvalidPrice") {
    CHECK_EQ(validateNewOrder(OrderType::Limit, -5, 100),
             std::optional<RejectReason>(RejectReason::INVALID_PRICE));
}

TEST_CASE("create: validate market price 0 -> valid (price ignored)") {
    CHECK_EQ(validateNewOrder(OrderType::Market, 0, 100), std::optional<RejectReason>{});
}

TEST_CASE("create: validate qty 0 and price 0 -> InvalidQuantity (quantity checked first)") {
    // Fixed check order keeps the reported reason deterministic.
    CHECK_EQ(validateNewOrder(OrderType::Limit, 0, 0),
             std::optional<RejectReason>(RejectReason::INVALID_QUANTITY));
}

// ---------------------------------------------------------------------------
// Order::create
// ---------------------------------------------------------------------------

TEST_CASE("create: valid limit order -> New, nothing filled") {
    NewOrderResult r = Order::create(kIds, /*seq=*/5, OrderType::Limit, OrderSide::Buy, 10000, 100);

    REQUIRE_EQ(r.outcome.outcomeType, OutcomeType::Accept);
    CHECK_EQ(r.outcome.newState, State::NEW);
    CHECK_EQ(r.outcome.rejectReason, std::optional<RejectReason>{});

    CHECK_EQ(r.order.getState(), State::NEW);
    CHECK_EQ(r.order.getOriginal(), 100u);
    CHECK_EQ(r.order.getFilled(), 0u);
    CHECK_EQ(r.order.getRemaining(), 100u);
    CHECK_EQ(r.order.getSeq(), 5u);
    CHECK_EQ(r.order.getClOrdId(), 1000u);
    checkInvariants(r.order);
}

TEST_CASE("create: valid market order -> New") {
    NewOrderResult r = Order::create(kIds, 5, OrderType::Market, OrderSide::Sell, /*price=*/0, 50);

    REQUIRE_EQ(r.outcome.outcomeType, OutcomeType::Accept);
    CHECK_EQ(r.order.getState(), State::NEW);
    CHECK_EQ(r.order.getType(), OrderType::Market);
    CHECK_EQ(r.order.getSide(), OrderSide::Sell);
    checkInvariants(r.order);
}

TEST_CASE("create: qty 0 -> recorded as Rejected(InvalidQuantity)") {
    NewOrderResult r = Order::create(kIds, 5, OrderType::Limit, OrderSide::Buy, 10000, /*quantity=*/0);

    // A transition into Rejected, not "nothing happened": the order is recorded.
    REQUIRE_EQ(r.outcome.outcomeType, OutcomeType::Accept);
    CHECK_EQ(r.outcome.newState, State::REJECTED);
    CHECK_EQ(r.outcome.rejectReason, RejectReason::INVALID_QUANTITY);

    CHECK_EQ(r.order.getState(), State::REJECTED);
    CHECK_EQ(r.order.getRemaining(), 0u);
    CHECK_EQ(r.order.getFilled(), 0u);
    checkInvariants(r.order);
}

TEST_CASE("create: limit price 0 -> recorded as Rejected(InvalidPrice), remaining 0") {
    NewOrderResult r = Order::create(kIds, 5, OrderType::Limit, OrderSide::Buy, /*price=*/0, 100);

    REQUIRE_EQ(r.outcome.outcomeType, OutcomeType::Accept);
    CHECK_EQ(r.outcome.newState, State::REJECTED);
    CHECK_EQ(r.outcome.rejectReason, RejectReason::INVALID_PRICE);

    CHECK_EQ(r.order.getState(), State::REJECTED);
    CHECK_EQ(r.order.getRemaining(), 0u);  // final state: nothing left, even though qty was 100
    checkInvariants(r.order);
}

TEST_CASE("create: rejected order then Cancel -> Reject(AlreadyRejected), unchanged") {
    // Ties creation to the matrix: a recorded Rejected order answers "already rejected".
    NewOrderResult r = Order::create(kIds, 5, OrderType::Limit, OrderSide::Buy, 10000, 0);
    REQUIRE_EQ(r.order.getState(), State::REJECTED);

    Order before = r.order;
    Outcome out = r.order.apply(Event::CANCEL, 0, true);

    REQUIRE_EQ(out.outcomeType, OutcomeType::Reject);
    CHECK_EQ(out.rejectReason, RejectReason::ALREADY_REJECTED);
    checkUnchanged(before, r.order);
}
