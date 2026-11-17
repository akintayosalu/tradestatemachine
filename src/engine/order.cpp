#include "engine/objects.hpp"

// PLACEHOLDERS so the create/replace tests link. Each returns a deliberately
// wrong answer, so those tests fail until you replace the bodies.
// (Signatures are a proposal; change them if you prefer, and update the tests.)

std::optional<RejectReason> validateNewOrder(OrderType /*type*/, int64_t /*price*/, uint64_t /*quantity*/) {
    return std::nullopt;  // TODO: quantity check, then price check for limit orders
}

bool isValidReplace(const Order& /*current*/, uint64_t /*newQuantity*/) {
    return false;  // TODO: new quantity must exceed what's already filled
}

NewOrderResult Order::create(const OrderIds& ids, uint64_t seq, OrderType type,
                             OrderSide side, int64_t price, uint64_t quantity) {
    // TODO: validate, decide(std::nullopt, NEW_ORDER, ...), build New or Rejected
    return NewOrderResult{Outcome{.outcomeType = OutcomeType::Bug},
                          Order{ids, seq, type, side, price, quantity}};
}

ReplaceResult Order::replace(uint64_t /*newOrderId*/, uint64_t /*newClOrdId*/, int64_t /*newPrice*/,
                             uint64_t /*newQuantity*/, uint64_t /*freshSeq*/) {
    // TODO: validity check, apply Replace to this order, build the new version
    return ReplaceResult{Outcome{.outcomeType = OutcomeType::Bug}, std::nullopt};
}
