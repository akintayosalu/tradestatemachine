#include <iostream>

// int main(){
    Order order{static_cast<uint64_t>(1), OrderType::Buy, 100.0, static_cast<uint64_t>(1000)};

    std::cout << order << std::endl;
    return 0;

};
