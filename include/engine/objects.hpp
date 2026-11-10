#pragma once

#include <iostream>
#include <cstdint>

enum class State {
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELLED,
    REJECTED,
    REPLACED,
};

enum class Event {
    NEW_ORDER,
    CANCEL,
    REPLACE,
    PARTIAL_FILL,
    FULL_FILL,
    MARKET_NO_LIQUIDITY,
};

enum class OrderType {
    Limit,
    Market,
};

enum class OrderSide {
    Buy,
    Sell,
};

class Order {
    uint64_t orderId;
    OrderType type;
    int price;
    uint64_t original;
    uint64_t filled;
    uint64_t remaining;
    State state;

    public:
        friend std::ostream& operator<<(std::ostream& os, const Order& o);

        Order(uint64_t id, const OrderType& orderType, double price, uint64_t size) :
            orderId{id}, type{orderType}, price{price}, original{size}, filled{0}, remaining{size}, state{State::NEW}
        {
        }
};
