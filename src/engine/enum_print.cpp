#include "engine/enum_print.hpp"

// Each switch deliberately has no `default:`. If you add an enumerator and
// forget to name it here, -Wall warns (-Wswitch) at compile time.

std::string_view to_string(State s) {
    switch (s) {
        case State::NEW:              return "New";
        case State::PARTIALLY_FILLED: return "PartiallyFilled";
        case State::FILLED:           return "Filled";
        case State::CANCELLED:        return "Cancelled";
        case State::REJECTED:         return "Rejected";
        case State::REPLACED:         return "Replaced";
    }
    return "<invalid State>";
}

std::string_view to_string(Event e) {
    switch (e) {
        case Event::NEW_ORDER:           return "NewOrder";
        case Event::CANCEL:              return "Cancel";
        case Event::REPLACE:             return "Replace";
        case Event::PARTIAL_FILL:        return "PartialFill";
        case Event::FULL_FILL:           return "FullFill";
        case Event::MARKET_NO_LIQUIDITY: return "MarketNoLiquidity";
    }
    return "<invalid Event>";
}

std::string_view to_string(OrderType t) {
    switch (t) {
        case OrderType::Limit:  return "Limit";
        case OrderType::Market: return "Market";
    }
    return "<invalid OrderType>";
}

std::string_view to_string(OrderSide s) {
    switch (s) {
        case OrderSide::Buy:  return "Buy";
        case OrderSide::Sell: return "Sell";
    }
    return "<invalid OrderSide>";
}

std::ostream& operator<<(std::ostream& os, State s)     { return os << to_string(s); }
std::ostream& operator<<(std::ostream& os, Event e)     { return os << to_string(e); }
std::ostream& operator<<(std::ostream& os, OrderType t) { return os << to_string(t); }
std::ostream& operator<<(std::ostream& os, OrderSide s) { return os << to_string(s); }
