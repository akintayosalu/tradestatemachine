// Human-readable printing for engine types, for logs and test failures.
//   std::cout << State::PARTIALLY_FILLED;   // PartiallyFilled
//   to_string(Event::CANCEL)                // "Cancel"
//   std::cout << order;  // Order{id=1 cl=100 orig=0 chain=1 seq=1 Buy Limit px=10050 qty=1000 filled=0 rem=1000 New}
#pragma once

#include <ostream>
#include <string_view>

#include "engine/objects.hpp"
#include <engine/types.hpp>

std::string_view to_string(State s);
std::string_view to_string(Event e);
std::string_view to_string(OrderType t);
std::string_view to_string(OrderSide s);

std::ostream& operator<<(std::ostream& os, State s);
std::ostream& operator<<(std::ostream& os, Event e);
std::ostream& operator<<(std::ostream& os, OrderType t);
std::ostream& operator<<(std::ostream& os, OrderSide s);

// Declared as a friend inside Order (objects.hpp) so it can read private fields.
std::ostream& operator<<(std::ostream& os, const Order& o);
