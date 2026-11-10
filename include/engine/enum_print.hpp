// Human-readable names for the engine enums, for logs and test failures.
//   std::cout << State::PARTIALLY_FILLED;   // prints "PartiallyFilled"
//   to_string(Event::CANCEL)                // "Cancel"
#pragma once

#include <ostream>
#include <string_view>

#include "engine/objects.hpp"

std::string_view to_string(State s);
std::string_view to_string(Event e);
std::string_view to_string(OrderType t);
std::string_view to_string(OrderSide s);

std::ostream& operator<<(std::ostream& os, State s);
std::ostream& operator<<(std::ostream& os, Event e);
std::ostream& operator<<(std::ostream& os, OrderType t);
std::ostream& operator<<(std::ostream& os, OrderSide s);
