#include "engine/engine.hpp"

// PLACEHOLDERS so tests/engine_test.cpp links. Each returns a deliberately
// wrong answer, so the engine tests fail until you implement them.

std::optional<Request> parseRequest(std::string_view line) {
    (void)line;
    return std::nullopt;
}

bool Engine::process(std::string_view line, std::string& out) {
    (void)line;
    (void)out;
    return true;
}

RunResult runLog(std::string_view input) {
    (void)input;
    return RunResult{};
}
