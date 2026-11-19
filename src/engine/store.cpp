#include "engine/store.hpp"

// PLACEHOLDERS so tests/store_test.cpp links. Every function returns a
// deliberately wrong/empty answer, so those tests fail until you implement them.

bool OrderStore::insert(const Order& order) { 
    if (ordersById.contains(order.getOrderId()) || clOrdIdToOrderId.contains(order.getClOrdId())) {
        return false;  // order with this internal ID or client ID already exists
    }
    //dont do ordersById[order.getOrderId()] = order; leads to order being default intialized
    Order copy = order;
    ordersById.insert({order.getOrderId(), copy});
    clOrdIdToOrderId.insert({order.getClOrdId(), order.getOrderId()});
    return true; 
}

Order* OrderStore::find(uint64_t orderId) { 
    if (ordersById.contains(orderId)){
        auto& order = ordersById.find(orderId)->second;
        return &order;
    }
    return nullptr; 
}

const Order* OrderStore::find(uint64_t orderId) const { 
    if (ordersById.contains(orderId)){
        auto& order = ordersById.find(orderId)->second;
        return &order;
    }
    return nullptr; 
}

Order* OrderStore::findByClOrdId(uint64_t clOrdId) { 
    if (clOrdIdToOrderId.contains(clOrdId)){
        auto orderId = clOrdIdToOrderId.find(clOrdId)->second;
        if (ordersById.contains(orderId)){
            auto& order = ordersById.find(orderId)->second;
            return &order;
        }
    }
    return nullptr; 
}

const Order* OrderStore::findByClOrdId(uint64_t clOrdId) const { 
    if (clOrdIdToOrderId.contains(clOrdId)){
        auto orderId = clOrdIdToOrderId.find(clOrdId)->second;
        if (ordersById.contains(orderId)){
            auto& order = ordersById.find(orderId)->second;
            return &order;
        }
    }
    return nullptr; 
 }

bool OrderStore::hasClOrdId(uint64_t clOrdId) const { 
    return clOrdIdToOrderId.contains(clOrdId); 
}

std::size_t OrderStore::size() const { 
    return ordersById.size();
}
