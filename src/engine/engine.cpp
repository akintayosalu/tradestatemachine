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

// ---------------------------------------------------------------------------
// Output lines (docs/io_format.md). Each appends exactly one line.
// ---------------------------------------------------------------------------

static void appendState(std::string& out, const Order& o) {
    out += "STATE clOrdId=" + std::to_string(o.getClOrdId()) +
           " state=" + std::string(to_string(o.getState())) +
           " filled=" + std::to_string(o.getFilled()) +
           " remaining=" + std::to_string(o.getRemaining()) + "\n";
}

// REJECT request=<R> clOrdId=<id>[ origClOrdId=<id>] reason=<reason>
// origClOrdId is written only for REPLACE (pass it there; NEW/CANCEL leave it out).
static void appendReject(std::string& out, std::string_view request, uint64_t clOrdId, RejectReason reason,
                         std::optional<uint64_t> origClOrdId = std::nullopt) {
    out += "REJECT request=" + std::string(request) + " clOrdId=" + std::to_string(clOrdId);
    if (origClOrdId.has_value()) {
        out += " origClOrdId=" + std::to_string(*origClOrdId);
    }
    out += " reason=" + std::string(to_string(reason)) + "\n";
}

// BUG orderId=<id>[ event=<Event>]
static void appendBug(std::string& out, uint64_t orderId, std::optional<Event> event = std::nullopt) {
    out += "BUG orderId=" + std::to_string(orderId);
    if (event.has_value()) {
        out += " event=" + std::string(to_string(*event));
    }
    out += "\n";
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

bool Engine::process(std::string_view line, std::string& out) {
    ++lineNumber;  // every line counts, including skipped and malformed ones
    if (line.empty() || line[0] == '#') {
        return true;  // blank / comment: no output
    }

    std::optional<Request> request = parseRequest(line);
    if (!request.has_value()) {
        out += "MALFORMED line=" + std::to_string(lineNumber) + "\n";
        return true;  // bad input is not a Bug: carry on
    }

    switch (request->type) {
        case RequestType::New:     return onNew(*request, out);
        case RequestType::Cancel:  return onCancel(*request, out);
        case RequestType::Replace: return onReplace(*request, out);
    }
    return true;
}

bool Engine::onNew(const Request& request, std::string& out) {
    if (store.hasClOrdId(request.clOrdId)) {
        appendReject(out, "New", request.clOrdId, RejectReason::DUPLICATE_ORDER_ID);
        return true;  // nothing stored, no internal ID used
    }

    uint64_t orderId = nextOrderId++;  // used even if validation rejects it
    OrderIds ids{.orderId = orderId, .clOrdId = request.clOrdId, .origClOrdId = 0, .chainId = orderId};
    NewOrderResult result = Order::create(ids, nextSeq++, request.orderType, request.side,
                                          request.price, request.qty);

    // false = the store already has this internal ID or client ID, which the
    // check above and the counter rule out: the maps disagree -> Bug
    if (!store.insert(result.order)) {
        appendBug(out, orderId);
        return false;
    }

    // create always returns Accept; an invalid request comes back in Rejected
    if (result.order.getState() == State::REJECTED) {
        appendReject(out, "New", request.clOrdId, result.outcome.rejectReason.value());
        return true;  // stays stored: its client ID now counts as used
    }

    out += "ACCEPT request=New clOrdId=" + std::to_string(request.clOrdId) +
           " orderId=" + std::to_string(orderId) + "\n";
    return matchAndReport(orderId, out);
}

bool Engine::onCancel(const Request& request, std::string& out) {
    auto requestedOrder = store.findByClOrdId(request.clOrdId);
    if (requestedOrder == nullptr) {
        appendReject(out, "Cancel", request.clOrdId, RejectReason::UNKNOWN_ORDER);
        return true;
    }
    Outcome res = requestedOrder->apply(Event::CANCEL, 0, /*validReplace=*/true);  // cancels ignore it
    if (res.outcomeType == OutcomeType::Reject) {
        appendReject(out, "Cancel", request.clOrdId, res.rejectReason.value());
        return true;
    } else if (res.outcomeType == OutcomeType::Bug) {
        appendBug(out, requestedOrder->getOrderId(), Event::CANCEL);
        return false;
    }

    // Accepted -> it was live, and a live order is always in the book, so
    // remove() returning false means the book and store disagree -> Bug
    if (!book.remove(*requestedOrder)) {
        appendBug(out, requestedOrder->getOrderId());
        return false;
    }
    appendState(out, *requestedOrder);
    return true;
}

bool Engine::onReplace(const Request& request, std::string& out) {
    // TODO (docs/io_format.md, REPLACE): checks in the spec's order; note
    // whether the old version is in the book BEFORE replace(); ACCEPT, STATE
    // of old, then replaceInPlace + STATE of new (same seq) or remove old +
    // matchAndReport(new) (fresh seq). REJECT lines for REPLACE also carry
    // origClOrdId (pass request.origClOrdId to appendReject).
    if (store.hasClOrdId(request.clOrdId)) {
        appendReject(out, "Replace", request.clOrdId, RejectReason::DUPLICATE_ORDER_ID, request.origClOrdId);
        return true;
    }
    if (!store.hasClOrdId(request.origClOrdId)) {
        appendReject(out, "Replace", request.clOrdId, RejectReason::UNKNOWN_ORDER, request.origClOrdId);
        return true;
    }

    Order* current = store.findByClOrdId(request.origClOrdId);
    auto replaceResult = current->replace(nextOrderId, request.clOrdId, request.price, request.qty, nextSeq++);
    if (replaceResult.outcome.outcomeType == OutcomeType::Reject) {
        appendReject(out, "Replace", request.clOrdId, replaceResult.outcome.rejectReason.value(), request.origClOrdId);
        return true;
    } else if (replaceResult.outcome.outcomeType == OutcomeType::Bug) {
        appendBug(out, current->getOrderId(), Event::REPLACE);
        return false;
    }
    auto& replacementOrder = replaceResult.replacement.value();
    out += "ACCEPT request=Replace clOrdId=" + std::to_string(request.clOrdId) +
           " origClOrdId=" + std::to_string(request.origClOrdId) +
           " orderId=" + std::to_string(replacementOrder.getOrderId()) + "\n";
    appendState(out, *current);
    nextOrderId++;  // used even if the new version is in the book (replaceInPlace)
    store.insert(replacementOrder);  // false = the store already has this internal ID or client ID, which the check above and the counter rule out: the maps disagree -> Bug
    auto newOrder = store.find(replacementOrder.getOrderId());

    if (newOrder->getSeq() == current->getSeq()) {
        // replaceInPlace: same seq, so the new version is in the book
        if (!book.replaceInPlace(*current, *newOrder)) {
            appendBug(out, current->getOrderId());
            return false;
        }
        appendState(out, *newOrder);
        return true;
    } else {
        // replace that loses priority: remove old, then match new
        if (!book.remove(*current)) {
            appendBug(out, current->getOrderId());
            return false;
        }

        return matchAndReport(newOrder->getOrderId(), out);
    }
}

bool Engine::matchAndReport(uint64_t orderId, std::string& out) {
    MatchResult result = match(orderId, book, store, nextTradeId);

    for (const Trade& trade : result.trades) {
        const Order* aggressor = store.find(trade.aggressorId);
        const Order* resting = store.find(trade.restingId);
        if (aggressor == nullptr || resting == nullptr) {  // a trade names an unknown order
            appendBug(out, aggressor == nullptr ? trade.aggressorId : trade.restingId);
            return false;
        }
        out += "TRADE tradeId=" + std::to_string(trade.tradeId) +
               " aggressor=" + std::to_string(aggressor->getClOrdId()) +
               " resting=" + std::to_string(resting->getClOrdId()) +
               " px=" + std::to_string(trade.price) +
               " qty=" + std::to_string(trade.qty) + "\n";
        appendState(out, *resting);
    }

    if (result.bug.has_value()) {  // trades so far, then BUG; the order's state can't be trusted
        appendBug(out, result.bug->orderId, result.bug->event);
        return false;
    }

    appendState(out, *store.find(orderId));
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
