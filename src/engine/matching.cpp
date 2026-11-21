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

    while (order.getRemaining() > 0 && crosses(order, book)){
        switch (side){
            case OrderSide::Buy:
                //get the best ask posiiton
                auto askPrice = book.bestAsk().value();
                auto priceLevel = book.idsAt(OrderSide::Sell, askPrice);
                auto matchingOrderId = priceLevel.front();
                Order* matchingOrder = store.find(matchingOrderId);
                if (matchingOrder == nullptr){
                    //
                }
                
                auto fillQuantity = std::min(order.getRemaining(), matchingOrder->getRemaining());
                if (fillQuantity == order.getRemaining()){

                }
                else {
                    
                }


                break;
            case OrderSide::Sell: 
                break;
        }



    }

    (void)incomingId;
    (void)book;
    (void)store;
    (void)nextTradeId;
    return MatchResult{};
}
