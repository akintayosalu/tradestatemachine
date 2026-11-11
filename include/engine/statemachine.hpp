#pragma once

#include <engine/types.hpp>
#include <optional>


enum class RejectReason {
    UNKNOWN_ORDER,
    DUPLICATE_ORDER_ID,
    INVALID_REPLACE,
    ALREADY_FILLED,
    ALREADY_CANCELLED,
    ALREADY_REJECTED,
    ALREADY_REPLACED,
    INVALID_QUANTITY,   // NewOrder with quantity 0
    INVALID_PRICE,      // NewOrder: limit order with price <= 0 (market orders skip this)
};

enum class OutcomeType {
    Accept,
    Reject,
    Bug,
};

struct Outcome {
    OutcomeType outcomeType;
    std::optional<RejectReason> rejectReason = std::nullopt;  // set only when outcomeType == Reject
    State newState = State::NEW;                              // meaningful only when outcomeType == Accept
};

enum class FillLevel {
    Partial,
    Exact, 
    Over,
};

// Extra facts a few matrix cells depend on. Defaults describe the ordinary case,
// so a test sets only the field its cell reads, e.g. Context{.orderType = OrderType::Market}.
struct Context {
    OrderType orderType = OrderType::Limit;                       // MarketNoLiquidity: Market -> Cancelled, Limit -> Bug
    bool replaceValid = true;                                     // Replace: false -> Reject(INVALID_REPLACE)
    FillLevel fillLevel = FillLevel::Partial;                     // fill quantity vs remaining; Over is always a Bug
    std::optional<RejectReason> validationResult = std::nullopt;  // NewOrder on (none): empty = valid
};

// The state machine: one call = one cell of docs/order_state_machine.md.
//   current: the order's state, or std::nullopt if no such order exists (the "(none)" row)
//   event:   what happened
//   ctx:     extra facts for the conditional cells
// Pure: no side effects, same inputs always give the same Outcome.
Outcome decide(std::optional<State> current, Event event, const Context& ctx);
