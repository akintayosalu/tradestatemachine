#include "engine/matching.hpp"

// PLACEHOLDERS so tests/matching_test.cpp links. Each returns a deliberately
// wrong answer, so the matching tests fail until you implement them.

bool crosses(const Order& incoming, const OrderBook& book) {
    (void)incoming;
    (void)book;
    return false;
}

MatchResult match(uint64_t incomingId, OrderBook& book, OrderStore& store, uint64_t& nextTradeId) {
    (void)incomingId;
    (void)book;
    (void)store;
    (void)nextTradeId;
    return MatchResult{};
}
