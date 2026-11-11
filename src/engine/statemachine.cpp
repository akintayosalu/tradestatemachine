#include "engine/statemachine.hpp"

// PLACEHOLDER so tests can link. It answers Bug for everything, so every test
// fails until you replace this body with the real matrix logic.
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
                break;
        }
    }

    if (current.value() == State::NEW) {
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

    if (current.value() == State::FILLED) {
        switch (event) {
            case Event::NEW_ORDER:
                return Outcome{.outcomeType = OutcomeType::Reject, .rejectReason = RejectReason::DUPLICATE_ORDER_ID};
            case Event::CANCEL:
                return Outcome{.outcomeType = OutcomeType::Reject, .rejectReason = RejectReason::ALREADY_FILLED};
            case Event::REPLACE:
                return Outcome{.outcomeType = OutcomeType::Reject, .rejectReason = RejectReason::ALREADY_FILLED};
            case Event::PARTIAL_FILL:
            case Event::FULL_FILL:
            case Event::MARKET_NO_LIQUIDITY:
                return Outcome{.outcomeType = OutcomeType::Bug};
        }
    }

    return Outcome{.outcomeType = OutcomeType::Bug};
}
