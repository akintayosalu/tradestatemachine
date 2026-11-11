#pragma once

#include <engine/types.hpp>
#include <optional>


enum class RejectReason {
    UNKNOWN_ORDER,
    DUPLICATE_ORDER_ID,
    INVALID_REPLACE,
    ALREADY_FILLED,
    ALREADY_CANCELLED,
    ALREADY_REJECTED,
    ALREADY_REPLACED,
    INVALID_QUANTITY,   // NewOrder with quantity 0
    INVALID_PRICE,      // NewOrder: limit order with price <= 0 (market orders skip this)
};

enum class OutcomeType {
    Accept,
    Reject,
    Bug,
};

struct Outcome {
    OutcomeType outcomeType;
    std::optional<RejectReason> rejectReason = std::nullopt;  // set only when outcomeType == Reject
    State newState = State::NEW;                              // meaningful only when outcomeType == Accept
};

enum class FillLevel {
    Partial,
    Exact, 
    Over,
};

struct Context {
    OrderType orderType;
    bool replaceValid;
    FillLevel fillLevel; 
    std::optional<RejectReason> validationResult;
};
