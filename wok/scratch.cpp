// Scratch playground: not part of `make`. Build and run from the repo root:
//   mkdir -p build && c++ -std=c++20 -Wall -Wextra -g -Iinclude wok/scratch.cpp src/engine/*.cpp -o build/scratch && ./build/scratch
#include <iostream>

#include "engine/print.hpp"

int main() {
    // The constructor is private: orders are created through Order::create.
    // ids, seq, type, side, price (ticks), quantity
    NewOrderResult r = Order::create(OrderIds{.orderId = 1, .clOrdId = 100, .origClOrdId = 0, .chainId = 1},
                                     /*seq=*/1, OrderType::Limit, OrderSide::Buy, 10000, 1000);

    std::cout << r.outcome << " " << r.order << std::endl;
    return 0;
}
