#include "engine/book.hpp"

// PLACEHOLDERS so the book tests link. Every function returns a deliberately
// wrong/empty answer, so tests/book_test.cpp fails until you implement them.

bool OrderBook::add(const Order& order) { 
    if (order.getType() == OrderType::Market) {
        return false;  // market orders cannot rest
    }
    if (order.getState() != State::NEW && order.getState() != State::PARTIALLY_FILLED) {
        return false;  // only live orders can rest 
    }
    if (orderHandles.contains(order.getOrderId())) {
        return false;  // order already exists
    }

    if (order.getSide() == OrderSide::Buy){
        auto it = bids.find(order.getPrice());
        if (it == bids.end()) {
            // Price level does not exist, create it and add the order
            bids[order.getPrice()] = std::list<uint64_t>{order.getOrderId()};
            orderHandles[order.getOrderId()] = Handle{OrderSide::Buy, order.getPrice(), bids[order.getPrice()].begin()};
        } else {
            // Price level exists, add the order to the back of the list
            it->second.push_back(order.getOrderId());
            orderHandles[order.getOrderId()] = Handle{OrderSide::Buy, order.getPrice(), it->second.end()};
        }
    }
    else if (order.getSide() == OrderSide::Sell){
        auto it = asks.find(order.getPrice());
        if (it == asks.end()){
            asks[order.getPrice()] = std::list<uint64_t>{order.getOrderId()};
            orderHandles[order.getOrderId()] = Handle{OrderSide::Sell, order.getPrice(), asks[order.getPrice()].begin()};
        } else {
            // Price level exists, add the order to the back of the list
            it->second.push_back(order.getOrderId());
            orderHandles[order.getOrderId()] = Handle{OrderSide::Sell, order.getPrice(), it->second.end()};
        }
    }
    return true;
}

bool OrderBook::remove(const Order& /*order*/) { return false; }

bool OrderBook::replaceInPlace(const Order& /*oldVersion*/, const Order& /*newVersion*/) { return false; }

std::optional<int64_t> OrderBook::bestBid() const { 
    if (!bids.empty()){
        return bids.begin()->first; //the highest bid price
    }
    return std::nullopt; 
}

std::optional<int64_t> OrderBook::bestAsk() const { 
    if (!asks.empty()){
        return asks.begin()->first;  // lowest ask price
    }
    return std::nullopt; 
}

bool OrderBook::contains(uint64_t orderId) const { 
    return orderHandles.contains(orderId); 
}

std::size_t OrderBook::size() const { 
    return orderHandles.size();
}

std::size_t OrderBook::levelCount(OrderSide side) const { 
    if (side == OrderSide::Buy){
        return bids.size();

    } 
    else if (side == OrderSide::Sell){
        return asks.size();
    }
    return 0;
}

std::vector<int64_t> OrderBook::prices(OrderSide side) const { 
    std::vector<int64_t> result;
    if (side == OrderSide::Buy){
        for (const auto& pair : bids) {
            result.push_back(pair.first);
        }
    } 
    else if (side == OrderSide::Sell){
        for (const auto& pair : asks) {
            result.push_back(pair.first);
        }
    }
    return result;
}

std::vector<uint64_t> OrderBook::idsAt(OrderSide side, int64_t price) const { 
    if (side == OrderSide::Buy){
        auto it = bids.find(price);
        if (it != bids.end()) {
            return std::vector<uint64_t>(it->second.begin(), it->second.end());
        }
    } 
    else if (side == OrderSide::Sell){
        auto it = asks.find(price);
        if (it != asks.end()) {
            return std::vector<uint64_t>(it->second.begin(), it->second.end());
        }
    }
    return {};
}
