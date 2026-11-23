#include "engine/engine.hpp"
#include <unordered_map>
#include <string>
#include <sstream>
#include <charconv>


// PLACEHOLDERS so tests/engine_test.cpp links. Each returns a deliberately
// wrong answer, so the engine tests fail until you implement them.

std::optional<Request> handleCancel(const auto& requestInfo) {
    RequestType type = RequestType::Cancel;
    uint64_t clOrdId = 0;
    if (!requestInfo.contains("clOrdId")) {
        return std::nullopt; // Required field for CANCEL
    }
    for (const auto& [key, val] : requestInfo) {
        if (key == "clOrdId") {
            // Convert val to uint64_t and assign to clOrdId
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), clOrdId);
            if (!(ec == std::errc()) || ptr != (val.data() + val.size())) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    return Request{.type = type, .clOrdId = clOrdId};
}

//Parse for REPLACE request type
std::optional<Request> handleReplace(const auto& requestInfo) {
    RequestType type = RequestType::Replace;
    uint64_t clOrdId = 0;
    uint64_t origClOrdId = 0;
    int64_t price = 0;
    uint64_t qty = 0;
    if (!requestInfo.contains("clOrdId") || !requestInfo.contains("origClOrdId") || 
        !requestInfo.contains("px") || !requestInfo.contains("qty")) {
        return std::nullopt; // Required fields for REPLACE
    }
    for (const auto& [key, val] : requestInfo) {
        if (key == "clOrdId") {
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), clOrdId);
            if (!(ec == std::errc()) || ptr != (val.data() + val.size())) {
                return std::nullopt;
            }
        } else if (key == "origClOrdId") {
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), origClOrdId);
            if (!(ec == std::errc()) || ptr != (val.data() + val.size())) {
                return std::nullopt;
            }
        } else if (key == "px") {
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), price);
            if (!(ec == std::errc()) || ptr != (val.data() + val.size())) {
                return std::nullopt;
            }
        } else if (key == "qty") {
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), qty);
            if (!(ec == std::errc()) || ptr != (val.data() + val.size())) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }

    //use . assignment
    return Request{.type = type, .clOrdId = clOrdId, .origClOrdId = origClOrdId, .price = price, .qty = qty};
}

std::optional<Request> handleNew(const auto& requestInfo) {
    RequestType type = RequestType::New;
    uint64_t clOrdId = 0;
    OrderSide side = OrderSide::Buy;
    OrderType orderType = OrderType::Limit;
    int64_t price = 0;
    uint64_t qty = 0;

    if (!requestInfo.contains("clOrdId") || !requestInfo.contains("side") || 
        !requestInfo.contains("type") || !requestInfo.contains("qty") ||
        (requestInfo.at("type") == "Limit" && !requestInfo.contains("px"))) {
        return std::nullopt; // Required fields for NEW
    }

    for (const auto& [key, val] : requestInfo) {
        if (key == "clOrdId") {
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), clOrdId);
            if (!(ec == std::errc()) || ptr != (val.data() + val.size())) {
                return std::nullopt;
            }
        } else if (key == "side") {
            if (val == "Buy") {
                side = OrderSide::Buy;
            } else if (val == "Sell") {
                side = OrderSide::Sell;
            } else {
                return std::nullopt;
            }
        } else if (key == "type") {
            if (val == "Limit") {
                orderType = OrderType::Limit;
            } else if (val == "Market") {
                orderType = OrderType::Market;
            } else {
                return std::nullopt;
            }
        } else if (key == "px") {
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), price);
            if (!(ec == std::errc()) || ptr != (val.data() + val.size())) {
                return std::nullopt;
            }
        } else if (key == "qty") {
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), qty);
            if (!(ec == std::errc()) || ptr != (val.data() + val.size())) {
                return std::nullopt;
            }
        } else {
            return std::nullopt; // Unknown key
        }
    }
    
    return Request{.type = type, .clOrdId = clOrdId, .side = side, .orderType = orderType, .price = price, .qty = qty};
}

std::optional<Request> buildRequestBody(const auto& requestInfo, std::string_view requestType){
    if (requestType == "CANCEL") {
        return handleCancel(requestInfo);
    } else if (requestType == "REPLACE") {
        return handleReplace(requestInfo);
    } else if (requestType == "NEW") {
        return handleNew(requestInfo);
    } else {
        return std::nullopt;
    }
}

std::optional<Request> parseRequest(std::string_view line) {
    std::stringstream ss(std::string{line});
    std::unordered_map<std::string, std::string> requestInfo;
    std::string requestType {};
    ss >> requestType;

    if (requestType != "NEW" && requestType != "CANCEL" && requestType != "REPLACE") {
        return std::nullopt;
    }

    std::string pair;
    while (ss >> pair){
        auto pos = pair.find('=');
        if (pos == std::string::npos) {
            return std::nullopt;
        }
        std::string key = pair.substr(0, pos);
        if (requestInfo.contains(key)){
            return std::nullopt;
        }
        std::string value = pair.substr(pos + 1);
        requestInfo.insert({key, value});
    }

    //dictionary stores key -> value (as strings)
    return buildRequestBody(requestInfo, requestType);
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
