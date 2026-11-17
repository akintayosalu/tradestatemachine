#include "engine/objects.hpp"

std::optional<RejectReason> validateNewOrder(OrderType type, int64_t Price, uint64_t quantity) {
    if (quantity == 0) {
        return RejectReason::INVALID_QUANTITY;
    } else if (type == OrderType::Limit && Price <= 0) {
        return RejectReason::INVALID_PRICE;
    }
    return std::nullopt;  
}

bool isValidReplace(const Order& current, uint64_t newQuantity) {
    return newQuantity > current.getFilled();
}

NewOrderResult Order::create(const OrderIds& ids, uint64_t seq, OrderType type,
                             OrderSide side, int64_t price, uint64_t quantity) {
    std::optional<RejectReason> reason = validateNewOrder(type, price, quantity);
    Context ct = {.orderType = type, .validationResult = reason}; 
    Outcome o = decide(std::nullopt, Event::NEW_ORDER, ct);
    if (o.outcomeType == OutcomeType::Reject) {
        Order order = Order{ids, seq, type, side, price, 0u}; // Create a rejected order with remaining 0
        order.state = o.newState;
        return NewOrderResult{.outcome = o, .order = Order{ids, seq, type, side, price, 0u}};
    }
    else{
        //new order is valid
        Order order = Order{ids, seq, type, side, price, quantity};
        order.state = o.newState;
        return NewOrderResult{.outcome = o, .order = Order{ids, seq, type, side, price, quantity}};
    }
}

ReplaceResult Order::replace(uint64_t newOrderId, uint64_t newClOrdId, int64_t newPrice,
                             uint64_t newQuantity, uint64_t freshSeq) {
    // TODO: validity check, apply Replace to this order, build the new version
    bool validReplace = isValidReplace(*this, newQuantity);
    Outcome o = decide(state, Event::REPLACE, Context{.orderType = type, .replaceValid = validReplace});
    if (o.outcomeType == OutcomeType::Accept) {
        // Update the current order's state to Replaced
        state = o.newState;
        // Create the new order version
        uint64_t updateChainId = (ids.chainId == 0) ? ids.orderId : ids.chainId;
        OrderIds newIds{.orderId = newOrderId, .clOrdId = newClOrdId, .origClOrdId = ids.clOrdId, .chainId = updateChainId};
        Order newOrder(newIds, freshSeq, type, side, newPrice, newQuantity);
        return ReplaceResult{.outcome = o, .replacement = newOrder};
    } 
    else {
        return ReplaceResult{.outcome = o, .replacement = std::nullopt};  
    }
}
