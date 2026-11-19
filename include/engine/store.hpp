// The order store: owns EVERY order for its whole life (live, final, rejected,
// replaced versions) and finds them by internal ID or client ID.
// design.md section 7. The book only holds IDs; the store turns them into Orders.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects.hpp"

class OrderStore {
public:
    // Store a copy of `order`. Refuses (returns false, changes nothing) if its
    // internal ID or its client ID (ClOrdID) is already in the store.
    bool insert(const Order& order);

    // Find by internal ID. nullptr if unknown.
    // The pointer stays valid while other orders are inserted (the matcher
    // relies on this), and changes made through it (e.g. apply) are the
    // store's own copy: there is only ever one copy of each order.
    Order* find(uint64_t orderId);
    const Order* find(uint64_t orderId) const;

    // Find by client ID (ClOrdID). Works for every version, including
    // Replaced and final ones. nullptr if unknown.
    Order* findByClOrdId(uint64_t clOrdId);
    const Order* findByClOrdId(uint64_t clOrdId) const;

    // Has this client ID been used by any order ever stored?
    // (The duplicate-order-ID reject.)
    bool hasClOrdId(uint64_t clOrdId) const;

    // Number of orders stored (every state).
    std::size_t size() const;

private:
    // Your data members go here (design.md section 7):
    //   - internal ID -> Order          (this owns the orders)
    //   - client ID   -> internal ID
    // Pick containers whose element references stay valid when other
    // elements are inserted (std::unordered_map / std::map yes, std::vector no).
    std::unordered_map<uint64_t, Order> ordersById;  // internal ID -> Order
    std::unordered_map<uint64_t, uint64_t> clOrdIdToOrderId;  // client ID -> internal ID
};
