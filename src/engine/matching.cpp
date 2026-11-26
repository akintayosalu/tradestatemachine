#include <engine/matching.hpp>
#include <algorithm>

bool crosses(const Order& incoming, const OrderBook& book) {
    if (incoming.getSide() == OrderSide::Buy){
        //check top of ask side
        if (book.bestAsk().has_value() && 
            ((incoming.getPrice() >= book.bestAsk().value()) ||
            incoming.getType() == OrderType::Market)){
                return true;
        }
    } 
    else if (incoming.getSide() == OrderSide::Sell){
        if (book.bestBid().has_value() && 
            ((incoming.getPrice() <= book.bestBid().value())
            || incoming.getType() == OrderType::Market)){
                return true;
        } 
    }
    return false;
}

//Checks if resting matching order is valid
std::optional<MatchResult> checkValidMatch(const Order* matchingOrder, uint64_t matchingOrderId, std::vector<Trade>& trades) {
    if (matchingOrder == nullptr){
        return MatchResult{.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId}};
    }

    if (matchingOrder->getState() != State::NEW && matchingOrder->getState() != State::PARTIALLY_FILLED){
        return MatchResult{.trades = trades,  .bug = MatchBug{.orderId = matchingOrderId}};
    }
    return std::nullopt;

}

//Executes fill for singular order on one side of the trade
std::optional<MatchResult> executeTrade(const Event& event, Order& order, auto fillQuantity, std::vector<Trade>& trades){
    Outcome res = order.apply(event, fillQuantity, /*validReplace=*/true);  // fills ignore it
    if (res.outcomeType != OutcomeType::Accept){
        return MatchResult{.trades = trades,  .bug = MatchBug{.orderId = order.getOrderId(), .event = event}};
    }
    return std::nullopt;
}

//Executes fills for incoming + resting order.
//The caller has already checked matchingOrder (checkValidMatch) and computed
//fillQuantity BEFORE the fills, so the same quantity goes into the Trade.
std::optional<MatchResult> executeFill(Order& order, Order* matchingOrder, uint64_t fillQuantity, OrderBook& book, std::vector<Trade>& trades) {
    Event orderEvent = (fillQuantity == order.getRemaining()) ? Event::FULL_FILL : Event::PARTIAL_FILL;
    std::optional<MatchResult> result = executeTrade(orderEvent, order, fillQuantity, trades);
    if (result.has_value()){
        return result.value();
    }

    Event matchingEvent = (fillQuantity == matchingOrder->getRemaining()) ? Event::FULL_FILL : Event::PARTIAL_FILL;
    std::optional<MatchResult> matchingResult = executeTrade(matchingEvent, *matchingOrder, fillQuantity, trades);
    if (matchingResult.has_value()){
        return matchingResult.value();
    }

    if (matchingEvent == Event::FULL_FILL){
        // false = it wasn't in the book: book and store disagree -> Bug
        if (!book.remove(*matchingOrder)){
            return MatchResult{.trades = trades, .bug = MatchBug{.orderId = matchingOrder->getOrderId()}};
        }
    }
    return std::nullopt;
}

MatchResult match(uint64_t incomingId, OrderBook& book, OrderStore& store, uint64_t& nextTradeId) {
    //Preconditions
    Order* o = store.find(incomingId);
    if (o == nullptr){
        return {.bug = MatchBug{.orderId = incomingId, .event = std::nullopt}};
    }
    Order& order = *o;

    if (order.getState() != State::NEW && order.getState() != State::PARTIALLY_FILLED){
        return {.bug = MatchBug{.orderId = incomingId, .event = std::nullopt}};
    }

    if (book.contains(incomingId)){
        return {.bug = MatchBug{.orderId = incomingId, .event = std::nullopt}};
    }
    OrderSide side = order.getSide();

    std::vector<Trade> trades {};

    while (order.getRemaining() > 0 && crosses(order, book)){
        // The Buy and Sell cases are identical apart from the opposite side and
        // its best price. They could be merged: work out those two first, then
        // one shared trade step with no switch.
        switch (side) {
            case OrderSide::Buy: {
                //get the best ask posiiton
                auto askPrice = book.bestAsk().value();
                auto priceLevel = book.idsAt(OrderSide::Sell, askPrice);
                auto matchingOrderId = priceLevel.front();
                Order* matchingOrder = store.find(matchingOrderId);
                std::optional<MatchResult> validMatch = checkValidMatch(matchingOrder, matchingOrderId, trades);
                if (validMatch.has_value()){
                    return validMatch.value();
                }
                // quantity computed once, before the fills change either remaining
                uint64_t fillQuantity = std::min(order.getRemaining(), matchingOrder->getRemaining());
                std::optional<MatchResult> fillResult = executeFill(order, matchingOrder, fillQuantity, book, trades);
                if (fillResult.has_value()){
                    return fillResult.value();
                }
                trades.push_back({nextTradeId++, incomingId, matchingOrderId, askPrice, fillQuantity});
                break;
            }
            case OrderSide::Sell: {
                auto bidPrice = book.bestBid().value();
                auto priceLevel = book.idsAt(OrderSide::Buy, bidPrice);
                auto matchingOrderId = priceLevel.front();
                Order* matchingOrder = store.find(matchingOrderId);
                std::optional<MatchResult> validMatch = checkValidMatch(matchingOrder, matchingOrderId, trades);
                if (validMatch.has_value()){
                    return validMatch.value();
                }
                // quantity computed once, before the fills change either remaining
                uint64_t fillQuantity = std::min(order.getRemaining(), matchingOrder->getRemaining());
                std::optional<MatchResult> fillResult = executeFill(order, matchingOrder, fillQuantity, book, trades);
                if (fillResult.has_value()){
                    return fillResult.value();
                }
                trades.push_back({nextTradeId++, incomingId, matchingOrderId, bidPrice, fillQuantity});
                break;
            }
        }
    }

    if (order.getRemaining() != 0){
        if (order.getType() == OrderType::Limit){
            // false = refused (already there / not live): can't happen after the
            // preconditions, so it's a Bug
            if (!book.add(order)){
                return {.trades = trades, .bug = MatchBug{.orderId = incomingId}};
            }
        }
        else {
            // normal: no liquidity left -> Cancelled. Anything but Accept is a Bug.
            Outcome res = order.apply(Event::MARKET_NO_LIQUIDITY, 0, /*validReplace=*/true);
            if (res.outcomeType != OutcomeType::Accept){
                return {.trades = trades, .bug = MatchBug{.orderId = incomingId, .event = Event::MARKET_NO_LIQUIDITY}};
            }
        }
    }

    return MatchResult{.trades = trades};
}
