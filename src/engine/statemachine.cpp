#include <iostream>
#include <objects.hpp>


// // Implement the operator overload
// std::ostream& operator<<(std::ostream& os, const Order& o) {
//     os << "Order(ID: " << o.orderId << ", Type: " << static_cast<int>(o.type) << ", Price: " << o.price << 
//     ", Original: " << o.original << ", Filled: " << o.filled << ", Remaining: " << o.remaining << ", State: " << static_cast<int>(o.state) << ")";
//     return os; // Return the stream to allow chaining (e.g., std::cout << a << b;)
// }

// int main(){
//     Order order{static_cast<uint64_t>(1), OrderType::Buy, 100.0, static_cast<uint64_t>(1000)};

//     std::cout << order << std::endl;
//     return 0;

// }
