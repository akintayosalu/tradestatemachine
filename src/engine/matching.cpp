#include "engine/matching.hpp"
#include <algorithm>

// PLACEHOLDERS so tests/matching_test.cpp links. Each returns a deliberately
// wrong answer, so the matching tests fail until you implement them.

bool crosses(const Order& incoming, const OrderBook& book) {
    if (incoming.getState() == State::CANCELLED || incoming.getState() == State::REJECTED 
        || incoming.getState() == State::REPLACED || incoming.getState() == State::FILLED) {
        return false;  // not live
    }
    if (incoming.getSide() == OrderSide::Buy){
        //check top of ask side
        if (book.bestAsk().has_value() && 
            incoming.getPrice() >= book.bestAsk().value()){
                return true;
        }
    } 
    else if (incoming.getSide() == OrderSide::Sell){
        if (book.bestBid().has_value() && 
            incoming.getPrice() <= book.bestBid().value()){
                return true;
        } 
    }
    return false;
}

MatchResult match(uint64_t incomingId, OrderBook& book, OrderStore& store, uint64_t& nextTradeId) {
    (void)incomingId;
    (void)book;
    (void)store;
    (void)nextTradeId;
    return MatchResult{};
}
