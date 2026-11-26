#include <engine/book.hpp>

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
            auto result_pos = it->second.insert(it->second.end(), order.getOrderId());
            orderHandles[order.getOrderId()] = Handle{OrderSide::Buy, order.getPrice(), result_pos};
        }
    }
    else if (order.getSide() == OrderSide::Sell){
        auto it = asks.find(order.getPrice());
        if (it == asks.end()){
            asks[order.getPrice()] = std::list<uint64_t>{order.getOrderId()};
            orderHandles[order.getOrderId()] = Handle{OrderSide::Sell, order.getPrice(), asks[order.getPrice()].begin()};
        } else {
            // Price level exists, add the order to the back of the list
            auto result_pos = it->second.insert(it->second.end(), order.getOrderId());
            orderHandles[order.getOrderId()] = Handle{OrderSide::Sell, order.getPrice(), result_pos};
        }
    }
    return true;
}

bool OrderBook::remove(const Order& order) { 
    if (!orderHandles.contains(order.getOrderId())) {
        return false;  // order not in the book
    }
    const Handle& handle = orderHandles[order.getOrderId()];
    if (handle.side == OrderSide::Buy) {
        auto it = bids.find(handle.price);
        if (it != bids.end()) {
            auto& orderList = it->second;
            orderList.erase(handle.it);
            if (orderList.empty()){
                bids.erase(it);
            }
            orderHandles.erase(order.getOrderId());
            return true;
        }
    }
    else if (handle.side == OrderSide::Sell){
        auto it = asks.find(handle.price);
        if (it != asks.end()){
            auto& orderList = it->second;
            orderList.erase(handle.it);
            if (orderList.empty()){
                asks.erase(it);
            }
            orderHandles.erase(order.getOrderId());
            return true;
        }
    } 
    return false; 
}

bool OrderBook::replaceInPlace(const Order& oldVersion, const Order& newVersion) {
    if (oldVersion.getPrice() != newVersion.getPrice() ||
        oldVersion.getSide() != newVersion.getSide() ||
        !orderHandles.contains(oldVersion.getOrderId()) ||
        orderHandles.contains(newVersion.getOrderId()) || 
        !(newVersion.getState() == State::NEW or newVersion.getState() == State::PARTIALLY_FILLED) ||
        newVersion.getType() != OrderType::Limit) 
        {
            return false;
        }
    const Handle& handle = orderHandles[oldVersion.getOrderId()];
    if (handle.side == OrderSide::Buy) {
        auto it = bids.find(handle.price);
        if (it != bids.end()){
            auto& orderList = it->second;
            auto newPos = orderList.insert(handle.it, newVersion.getOrderId());
            orderList.erase(handle.it);
            orderHandles.erase(oldVersion.getOrderId());
            orderHandles[newVersion.getOrderId()] = Handle{OrderSide::Buy, newVersion.getPrice(), newPos};
            return true;
        }
    }
    else if (handle.side == OrderSide::Sell) {
        auto it = asks.find(handle.price);
        if (it != asks.end()){
            auto& orderList = it->second;
            auto newPos = orderList.insert(handle.it, newVersion.getOrderId());
            orderList.erase(handle.it);
            orderHandles.erase(oldVersion.getOrderId());
            orderHandles[newVersion.getOrderId()] = Handle{OrderSide::Sell, newVersion.getPrice(), newPos};
            return true;
        }
    }
     return false; 
}

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
