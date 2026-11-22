#include "engine/matching.hpp"
#include <algorithm>

// PLACEHOLDERS so tests/matching_test.cpp links. Each returns a deliberately
// wrong answer, so the matching tests fail until you implement them.

bool crosses(const Order& incoming, const OrderBook& book) {
    // if (incoming.getState() == State::CANCELLED || incoming.getState() == State::REJECTED 
    //     || incoming.getState() == State::REPLACED || incoming.getState() == State::FILLED) {
    //     return false;  // not live
    // }
    if (incoming.getSide() == OrderSide::Buy){
        //check top of ask side
        if (book.bestAsk().has_value() && 
            ((incoming.getPrice() >= book.bestAsk().value()) ||
            incoming.getType() == OrderType::Market)){
                return true;
        }
    } 
    else if (incoming.getSide() == OrderSide::Sell){
        if (book.bestBid().has_value() && 
            ((incoming.getPrice() <= book.bestBid().value())
            || incoming.getType() == OrderType::Market)){
                return true;
        } 
    }
    return false;
}

MatchResult match(uint64_t incomingId, OrderBook& book, OrderStore& store, uint64_t& nextTradeId) {
    //Preconditions
    Order* o = store.find(incomingId);
    if (o == nullptr){
        return {.bug = MatchBug{.orderId = incomingId, .event = std::nullopt}};
    }
    Order order = *o;

    if (order.getState() != State::NEW || order.getState() != State::PARTIALLY_FILLED){
        return {.bug = MatchBug{.orderId = incomingId, .event = std::nullopt}};
    }

    if (book.contains(incomingId)){
        return {.bug = MatchBug{.orderId = incomingId, .event = std::nullopt}};
    }
    OrderSide side = order.getSide();

    std::vector<Trade> trades {};

    while (order.getRemaining() > 0 && crosses(order, book)){
        switch (side) {
            case OrderSide::Buy: {
                //get the best ask posiiton
                auto askPrice = book.bestAsk().value();
                auto priceLevel = book.idsAt(OrderSide::Sell, askPrice);
                auto matchingOrderId = priceLevel.front();
                Order* matchingOrder = store.find(matchingOrderId);
                if (matchingOrder == nullptr){
                    return {.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId}};
                }

                if (matchingOrder->getState() != State::NEW && matchingOrder->getState() != State::PARTIALLY_FILLED){
                    return {.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId}};
                }
                
                auto fillQuantity = std::min(order.getRemaining(), matchingOrder->getRemaining());
                if (fillQuantity == order.getRemaining()){
                    Outcome res = order.apply(Event::FULL_FILL, fillQuantity, false);
                    if (res.outcomeType != OutcomeType::Accept){
                        return {.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId, .event = Event::FULL_FILL}};
                    }

                }
                else if (fillQuantity < order.getRemaining()) {
                    Outcome res = order.apply(Event::PARTIAL_FILL, fillQuantity, false);
                    if (res.outcomeType != OutcomeType::Accept){
                        return {.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId, .event = Event::PARTIAL_FILL}};
                    }

                }

                if (fillQuantity == matchingOrder->getRemaining()){
                    Outcome res = matchingOrder->apply(Event::FULL_FILL, fillQuantity, false);
                    if (res.outcomeType != OutcomeType::Accept){
                        return {.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId, .event = Event::FULL_FILL}};
                    }

                    book.remove(*matchingOrder);

                }
                else if (fillQuantity < matchingOrder->getRemaining()) {
                    Outcome res = matchingOrder->apply(Event::PARTIAL_FILL, fillQuantity, false);
                    if (res.outcomeType != OutcomeType::Accept){
                        return {.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId, .event = Event::PARTIAL_FILL}};
                    }

                }
                trades.push_back({nextTradeId++, incomingId, matchingOrder->getOrderId(), askPrice, fillQuantity});
                break;
            }
            case OrderSide::Sell: {
                auto bidPrice = book.bestAsk().value();
                auto priceLevel = book.idsAt(OrderSide::Buy, bidPrice);
                auto matchingOrderId = priceLevel.front();
                Order* matchingOrder = store.find(matchingOrderId);
                if (matchingOrder == nullptr){
                    return {.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId}};
                }

                if (matchingOrder->getState() != State::NEW && matchingOrder->getState() != State::PARTIALLY_FILLED){
                    return {.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId}};
                }
                
                auto fillQuantity = std::min(order.getRemaining(), matchingOrder->getRemaining());
                if (fillQuantity == order.getRemaining()){
                    Outcome res = order.apply(Event::FULL_FILL, fillQuantity, false);
                    if (res.outcomeType != OutcomeType::Accept){
                        return {.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId, .event = Event::FULL_FILL}};
                    }

                }
                else if (fillQuantity < order.getRemaining()) {
                    Outcome res = order.apply(Event::PARTIAL_FILL, fillQuantity, false);
                    if (res.outcomeType != OutcomeType::Accept){
                        return {.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId, .event = Event::PARTIAL_FILL}};
                    }

                }

                if (fillQuantity == matchingOrder->getRemaining()){
                    Outcome res = matchingOrder->apply(Event::FULL_FILL, fillQuantity, false);
                    if (res.outcomeType != OutcomeType::Accept){
                        return {.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId, .event = Event::FULL_FILL}};
                    }
                    book.remove(*matchingOrder);

                }
                else if (fillQuantity < matchingOrder->getRemaining()) {
                    Outcome res = matchingOrder->apply(Event::PARTIAL_FILL, fillQuantity, false);
                    if (res.outcomeType != OutcomeType::Accept){
                        return {.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId, .event = Event::PARTIAL_FILL}};
                    }

                }
                trades.push_back({nextTradeId++, incomingId, matchingOrder->getOrderId(), bidPrice, fillQuantity});
                break;
            }
        }
    }

    if (order.getRemaining() != 0){
        if (order.getType() == OrderType::Limit){
            book.add(order);
        }
        else {
            return {.trades = trades,  .bug = MatchBug{.orderId = order.getOrderId(), .event = Event::MARKET_NO_LIQUIDITY}};
        }
    }

    return MatchResult{.trades = trades};
}
