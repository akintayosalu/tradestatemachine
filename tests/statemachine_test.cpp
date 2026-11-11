// One TEST_CASE per cell of docs/order_state_machine.md.
// Naming: "state: <Row> x <Event> -> <expected>" so `make test FILTER=New` etc. work.
#include "test_framework.hpp"

#include "engine/print.hpp"
#include "engine/statemachine.hpp"

// Example: an Accept cell.
// Pattern: call decide, REQUIRE the outcome kind first (if it's wrong, the other
// fields are meaningless), then CHECK the field that matters for that kind.
TEST_CASE("state: (none) x Cancel -> Reject") {
    Outcome out = decide(std::nullopt, Event::CANCEL, Context{});

    REQUIRE_EQ(out.outcomeType, OutcomeType::Reject);
    CHECK_EQ(out.rejectReason, RejectReason::UNKNOWN_ORDER);
}

// Other shapes you'll need, same pattern:
//   Reject cell:      REQUIRE_EQ kind == Reject, then CHECK_EQ on out.rejectReason
//                     (compare with std::optional<RejectReason>(RejectReason::...)).
//   Bug cell:         CHECK_EQ kind == Bug.
//   (none) row:       pass std::nullopt as the state.
//   Conditional cell: set only the field it reads, e.g.
//                     Context{.orderType = OrderType::Market}
//                     Context{.fillLevel = FillLevel::Exact}
