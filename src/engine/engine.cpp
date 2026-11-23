#include "engine/engine.hpp"
#include <unordered_map>
#include <string>
#include <sstream>
#include <charconv>
#include <engine/print.hpp>


// Parse a whole value as a number (uint64_t or int64_t). False if it isn't one:
// empty, not a number, out of range, or trailing characters ("10x").
template <typename T>
static bool parseNumber(std::string_view val, T& out) {
    auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), out);
    return ec == std::errc() && ptr == val.data() + val.size();
}

std::optional<Request> handleCancel(const auto& requestInfo) {
    RequestType type = RequestType::Cancel;
    uint64_t clOrdId = 0;
    if (!requestInfo.contains("clOrdId")) {
        return std::nullopt; // Required field for CANCEL
    }
    for (const auto& [key, val] : requestInfo) {
        if (key == "clOrdId") {
            // Convert val to uint64_t and assign to clOrdId
            if (!parseNumber(val, clOrdId)) {
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
            if (!parseNumber(val, clOrdId)) {
                return std::nullopt;
            }
        } else if (key == "origClOrdId") {
            if (!parseNumber(val, origClOrdId)) {
                return std::nullopt;
            }
        } else if (key == "px") {
            if (!parseNumber(val, price)) {
                return std::nullopt;
            }
        } else if (key == "qty") {
            if (!parseNumber(val, qty)) {
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
            if (!parseNumber(val, clOrdId)) {
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
            if (!parseNumber(val, price)) {
                return std::nullopt;
            }
        } else if (key == "qty") {
            if (!parseNumber(val, qty)) {
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
    if (line.empty() || line[0] == '#') {
        lineNumber++; // Increment line number for MALFORMED counting
        return true; // Skip blank and comment lines
    }

    auto requestOpt = parseRequest(line);
    if (!requestOpt.has_value()) {
        out += "MALFORMED line=" + std::to_string(lineNumber) + "\n";
        lineNumber++;
        return true; // Continue processing
    }

    lineNumber++;

    if (requestOpt->type == RequestType::New) {
        // Handle NEW request
        if (store.hasClOrdId(requestOpt->clOrdId)) {
            out += "REJECT request=New clOrdId=" + std::to_string(requestOpt->clOrdId) + " reason=DuplicateOrderId\n";
        } else {
            NewOrderResult orderResult = Order::create({nextOrderId, requestOpt->clOrdId, 0, 0}, nextSeq, requestOpt->orderType, 
                requestOpt->side, requestOpt->price, requestOpt->qty);

            if (orderResult.outcome.outcomeType == OutcomeType::Reject) {
                out += "REJECT request=New clOrdId=" + std::to_string(requestOpt->clOrdId) + " reason=" 
                    + std::string(to_string(orderResult.outcome.rejectReason.value())) + "\n";
                nextOrderId++; // Increment internal order ID even for rejected orders
                nextSeq++; // Increment sequence number
                store.insert(orderResult.order);
                return true;
            } 
            if (orderResult.outcome.outcomeType == OutcomeType::Accept && orderResult.order.getState() == State::REJECTED) {
                out += "REJECT request=New clOrdId=" + std::to_string(requestOpt->clOrdId) + " reason=" 
                    + std::string(to_string(orderResult.outcome.rejectReason.value())) + "\n";
                nextOrderId++; // Increment internal order ID even for rejected orders
                nextSeq++; // Increment sequence number
                store.insert(orderResult.order);
                return true;
            }
            
            else if (orderResult.outcome.outcomeType == OutcomeType::Bug) {
                out += "BUG\n";
                return false; // Stop processing on Bug
            }

            store.insert(orderResult.order);
            out += "ACCEPT request=New clOrdId=" + std::to_string(requestOpt->clOrdId) + " orderId=" + std::to_string(orderResult.order.getOrderId()) + "\n";
            nextOrderId++;
            nextSeq++;
            MatchResult matchResult = match(orderResult.order.getOrderId(), book, store, nextTradeId);
            if (matchResult.trades.size() > 0) {
                for (const auto& trade : matchResult.trades) {
                    out += "TRADE tradeId=" + std::to_string(trade.tradeId) + " aggressor=" + std::to_string(store.find(trade.aggressorId)->getClOrdId()) +
                           " resting=" + std::to_string(store.find(trade.restingId)->getClOrdId()) + " px=" + std::to_string(trade.price) +
                           " qty=" + std::to_string(trade.qty) + "\n";
                    const Order* restingOrder = store.find(trade.restingId);
                    out += "STATE clOrdId=" + std::to_string(restingOrder->getClOrdId()) + " state=" + std::string(to_string(restingOrder->getState())) +
                           " filled=" + std::to_string(restingOrder->getFilled()) + " remaining=" + std::to_string(restingOrder->getRemaining()) + "\n";
                }
            }
            const auto& updatedOrder = store.find(orderResult.order.getOrderId());
            out += "STATE clOrdId=" + std::to_string(updatedOrder->getClOrdId()) + " state=" + std::string(to_string(updatedOrder->getState())) +
                   " filled=" + std::to_string(updatedOrder->getFilled()) + " remaining=" + std::to_string(updatedOrder->getRemaining()) + "\n";
            if (matchResult.bug.has_value()) {
                out += "BUG\n";
                return false; // Stop processing on Bug
            }
        }
    } else if (requestOpt->type == RequestType::Cancel) {
        // Handle CANCEL request
        // Implement logic for CANCEL request here
    } else if (requestOpt->type == RequestType::Replace) {
        // Handle REPLACE request
        // Implement logic for REPLACE request here
    }
    (void)line;
    (void)out;
    return true;
}

RunResult runLog(std::string_view input) {
    Engine engine {};
    std::stringstream ss {std::string{input}};
    std::string line {};
    std::string output {};
    while (std::getline(ss, line)){
        if (!engine.process(line, output)) {
            return RunResult{.output = output, .bug = true};
        }
    }
    return RunResult{output, false};
}
