#include "engine/store.hpp"

// PLACEHOLDERS so tests/store_test.cpp links. Every function returns a
// deliberately wrong/empty answer, so those tests fail until you implement them.

bool OrderStore::insert(const Order& /*order*/) { return false; }

Order* OrderStore::find(uint64_t /*orderId*/) { return nullptr; }

const Order* OrderStore::find(uint64_t /*orderId*/) const { return nullptr; }

Order* OrderStore::findByClOrdId(uint64_t /*clOrdId*/) { return nullptr; }

const Order* OrderStore::findByClOrdId(uint64_t /*clOrdId*/) const { return nullptr; }

bool OrderStore::hasClOrdId(uint64_t /*clOrdId*/) const { return false; }

std::size_t OrderStore::size() const { return 0; }
