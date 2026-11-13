#include "engine/statemachine.hpp"
#include <optional>

// The reject reason a final state gives to a client Cancel/Replace.
static RejectReason tooLateReason(State finalState) {
    switch (finalState) {
        case State::FILLED:    return RejectReason::ALREADY_FILLED;
        case State::CANCELLED: return RejectReason::ALREADY_CANCELLED;
        case State::REJECTED:  return RejectReason::ALREADY_REJECTED;
        case State::REPLACED:  return RejectReason::ALREADY_REPLACED;
        case State::NEW:
        case State::PARTIALLY_FILLED:
            break;  // not final; never called with these
    }
    return RejectReason::UNKNOWN_ORDER;
}

// One call = one cell of docs/order_state_machine.md. Rows are grouped:
//   (none)                      no order with this ID yet
//   New, PartiallyFilled        live: identical outcomes in every cell
//   Filled, Cancelled,
//   Rejected, Replaced          final: client events rejected, engine events Bug
Outcome decide(std::optional<State> current, Event event, const Context& ctx) {
    if (!current.has_value()){
        switch (event){
            case Event::PARTIAL_FILL:
            case Event::FULL_FILL:
            case Event::MARKET_NO_LIQUIDITY:
                return Outcome{.outcomeType = OutcomeType::Bug};    
            case Event::CANCEL:
            case Event::REPLACE:
                return Outcome{.outcomeType = OutcomeType::Reject, .rejectReason = RejectReason::UNKNOWN_ORDER};
            case Event::NEW_ORDER:
                //need to validate new order first
                if (!ctx.validationResult.has_value()){
                    return Outcome{.outcomeType = OutcomeType::Accept, .newState = State::NEW};
                }
                else {
                    return Outcome{.outcomeType = OutcomeType::Accept, .rejectReason = ctx.validationResult, .newState = State::REJECTED};
                }
        }
    }

    // Live rows. PartiallyFilled behaves exactly like New here: a PartialFill
    // keeps it PartiallyFilled, and Replace moves the OLD order to Replaced.
    // (The replacement's starting state, New vs PartiallyFilled, is set by the
    // caller that creates it, not by decide.)
    if (current.value() == State::NEW || current.value() == State::PARTIALLY_FILLED) {
        switch (event) {
            case Event::NEW_ORDER:
                return Outcome{.outcomeType = OutcomeType::Reject, .rejectReason = RejectReason::DUPLICATE_ORDER_ID};
            case Event::CANCEL:
                return Outcome{.outcomeType = OutcomeType::Accept, .newState = State::CANCELLED};
            case Event::REPLACE:
                if (!ctx.replaceValid){
                    return Outcome{.outcomeType = OutcomeType::Reject, .rejectReason = RejectReason::INVALID_REPLACE};
                } else {
                    return Outcome{.outcomeType = OutcomeType::Accept, .newState = State::REPLACED};
                }
            case Event::PARTIAL_FILL:
                if (ctx.fillLevel == FillLevel::Partial){
                    return Outcome{.outcomeType = OutcomeType::Accept, .newState = State::PARTIALLY_FILLED};
                } else {
                    return Outcome{.outcomeType = OutcomeType::Bug};
                }
            case Event::FULL_FILL:
                if (ctx.fillLevel == FillLevel::Exact){
                    return Outcome{.outcomeType = OutcomeType::Accept, .newState = State::FILLED};
                } else {
                    return Outcome{.outcomeType = OutcomeType::Bug};
                }
            case Event::MARKET_NO_LIQUIDITY:
                if (ctx.orderType == OrderType::Market) {
                    return Outcome{.outcomeType = OutcomeType::Accept, .newState = State::CANCELLED};
                } else {
                    return Outcome{.outcomeType = OutcomeType::Bug};
                }
        }
    }

    // Final rows: your Filled branch, generalized. Only the "too late" reason
    // differs between the four states, so it's looked up from the state.
    if (current.value() == State::FILLED || current.value() == State::CANCELLED ||
        current.value() == State::REJECTED || current.value() == State::REPLACED) {
        switch (event) {
            case Event::NEW_ORDER:
                return Outcome{.outcomeType = OutcomeType::Reject, .rejectReason = RejectReason::DUPLICATE_ORDER_ID};
            case Event::CANCEL:
            case Event::REPLACE:
                return Outcome{.outcomeType = OutcomeType::Reject, .rejectReason = tooLateReason(current.value())};
            case Event::PARTIAL_FILL:
            case Event::FULL_FILL:
            case Event::MARKET_NO_LIQUIDITY:
                return Outcome{.outcomeType = OutcomeType::Bug};
        }
    }

    return Outcome{.outcomeType = OutcomeType::Bug};
}
