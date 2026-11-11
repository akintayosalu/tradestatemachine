#pragma once

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
