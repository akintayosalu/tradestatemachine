#include "engine/print.hpp"

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

std::string_view to_string(RejectReason r) {
    switch (r) {
        case RejectReason::UNKNOWN_ORDER:      return "UnknownOrder";
        case RejectReason::DUPLICATE_ORDER_ID: return "DuplicateOrderId";
        case RejectReason::INVALID_REPLACE:    return "InvalidReplace";
        case RejectReason::ALREADY_FILLED:     return "AlreadyFilled";
        case RejectReason::ALREADY_CANCELLED:  return "AlreadyCancelled";
        case RejectReason::ALREADY_REJECTED:   return "AlreadyRejected";
        case RejectReason::ALREADY_REPLACED:   return "AlreadyReplaced";
        case RejectReason::INVALID_QUANTITY:   return "InvalidQuantity";
        case RejectReason::INVALID_PRICE:      return "InvalidPrice";
    }
    return "<invalid RejectReason>";
}

std::string_view to_string(OutcomeType t) {
    switch (t) {
        case OutcomeType::Accept: return "Accept";
        case OutcomeType::Reject: return "Reject";
        case OutcomeType::Bug:    return "Bug";
    }
    return "<invalid OutcomeType>";
}

std::string_view to_string(FillLevel f) {
    switch (f) {
        case FillLevel::Partial: return "Partial";
        case FillLevel::Exact:   return "Exact";
        case FillLevel::Over:    return "Over";
    }
    return "<invalid FillLevel>";
}

std::ostream& operator<<(std::ostream& os, State s)        { return os << to_string(s); }
std::ostream& operator<<(std::ostream& os, Event e)        { return os << to_string(e); }
std::ostream& operator<<(std::ostream& os, OrderType t)    { return os << to_string(t); }
std::ostream& operator<<(std::ostream& os, OrderSide s)    { return os << to_string(s); }
std::ostream& operator<<(std::ostream& os, RejectReason r) { return os << to_string(r); }
std::ostream& operator<<(std::ostream& os, OutcomeType t)  { return os << to_string(t); }
std::ostream& operator<<(std::ostream& os, FillLevel f)    { return os << to_string(f); }

std::ostream& operator<<(std::ostream& os, const std::optional<RejectReason>& r) {
    return r ? os << *r : os << "none";
}

std::ostream& operator<<(std::ostream& os, const std::optional<State>& s) {
    return s ? os << *s : os << "none";
}

// Shows only the fields that mean something for the outcome's kind.
std::ostream& operator<<(std::ostream& os, const Outcome& o) {
    switch (o.outcomeType) {
        case OutcomeType::Accept: return os << "Accept->" << o.newState;
        case OutcomeType::Reject: return os << "Reject(" << o.rejectReason << ")";
        case OutcomeType::Bug:    return os << "Bug";
    }
    return os << "<invalid Outcome>";
}

// One line, fixed field order, no floating point: identical orders always print
// identically, which keeps logs diffable and the determinism test honest.
std::ostream& operator<<(std::ostream& os, const Order& o) {
    return os << "Order{id=" << o.ids.orderId
              << " cl=" << o.ids.clOrdId
              << " orig=" << o.ids.origClOrdId
              << " chain=" << o.ids.chainId
              << " seq=" << o.seq
              << " " << o.side
              << " " << o.type
              << " px=" << o.price
              << " qty=" << o.original
              << " filled=" << o.filled
              << " rem=" << o.remaining
              << " " << o.state
              << "}";
}
