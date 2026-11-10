// Scratch playground: not part of `make`. Build and run from the repo root:
//   mkdir -p build && c++ -std=c++20 -Wall -Wextra -g -Iinclude wok/scratch.cpp src/engine/print.cpp -o build/scratch && ./build/scratch
#include <iostream>

#include "engine/print.hpp"

int main() {
    // id, type, side, price (ticks), quantity
    Order order{1, OrderType::Limit, OrderSide::Buy, 10000, 1000};

    std::cout << order << std::endl;
    return 0;
}
