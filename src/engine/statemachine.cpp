#include "engine/statemachine.hpp"

// PLACEHOLDER so tests can link. It answers Bug for everything, so every test
// fails until you replace this body with the real matrix logic.
Outcome decide(std::optional<State> /*current*/, Event /*event*/, const Context& /*ctx*/) {
    return Outcome{.outcomeType = OutcomeType::Bug};
}
