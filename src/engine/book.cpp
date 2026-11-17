#include "engine/book.hpp"

// PLACEHOLDERS so the book tests link. Every function returns a deliberately
// wrong/empty answer, so tests/book_test.cpp fails until you implement them.

bool OrderBook::add(const Order& /*order*/) { return false; }

bool OrderBook::remove(const Order& /*order*/) { return false; }

bool OrderBook::replaceInPlace(const Order& /*oldVersion*/, const Order& /*newVersion*/) { return false; }

std::optional<int64_t> OrderBook::bestBid() const { return std::nullopt; }

std::optional<int64_t> OrderBook::bestAsk() const { return std::nullopt; }

bool OrderBook::contains(uint64_t /*orderId*/) const { return false; }

std::size_t OrderBook::size() const { return 0; }

std::size_t OrderBook::levelCount(OrderSide /*side*/) const { return 0; }

std::vector<int64_t> OrderBook::prices(OrderSide /*side*/) const { return {}; }

std::vector<uint64_t> OrderBook::idsAt(OrderSide /*side*/, int64_t /*price*/) const { return {}; }
