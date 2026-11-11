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
                    return Outcome{.outcomeType = OutcomeType::Reject, .rejectReason = ctx.validationResult};
                }
                break;
        }
    }

    return Outcome{.outcomeType = OutcomeType::Bug};
}
