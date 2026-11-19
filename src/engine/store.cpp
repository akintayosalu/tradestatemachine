#include "engine/store.hpp"

bool OrderStore::insert(const Order& order) {
    if (ordersById.contains(order.getOrderId()) || clOrdIdToOrderId.contains(order.getClOrdId())) {
        return false;  // order with this internal ID or client ID already exists
    }
    // not ordersById[id] = order: operator[] needs a default constructor, which Order doesn't have
    ordersById.insert({order.getOrderId(), order});  // insert stores its own copy
    clOrdIdToOrderId.insert({order.getClOrdId(), order.getOrderId()});
    return true;
}

Order* OrderStore::find(uint64_t orderId) {
    auto it = ordersById.find(orderId);
    return it == ordersById.end() ? nullptr : &it->second;
}

const Order* OrderStore::find(uint64_t orderId) const {
    auto it = ordersById.find(orderId);
    return it == ordersById.end() ? nullptr : &it->second;
}

Order* OrderStore::findByClOrdId(uint64_t clOrdId) {
    auto it = clOrdIdToOrderId.find(clOrdId);
    return it == clOrdIdToOrderId.end() ? nullptr : find(it->second);
}

const Order* OrderStore::findByClOrdId(uint64_t clOrdId) const {
    auto it = clOrdIdToOrderId.find(clOrdId);
    return it == clOrdIdToOrderId.end() ? nullptr : find(it->second);
}

bool OrderStore::hasClOrdId(uint64_t clOrdId) const {
    return clOrdIdToOrderId.contains(clOrdId);
}

std::size_t OrderStore::size() const {
    return ordersById.size();
}
