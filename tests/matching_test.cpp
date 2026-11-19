// Matching tests. Naming: "match: ..." so `make test FILTER=match:` runs these.
// Decisions: docs/design.md section 8. Scenarios: todo/order_book.txt section 2.
// Prices are ticks; IDs are small numbers (used as internal ID AND client ID).
#include <vector>

#include "order_helpers.hpp"
#include "engine/book.hpp"
#include "engine/matching.hpp"
#include "engine/store.hpp"

using IdList = std::vector<uint64_t>;
using PriceList = std::vector<int64_t>;

// ---------------------------------------------------------------------------
// Fixture: a book, a store, the trade counter and a seq counter, the way the
// engine will own them. Every order gets the next seq, so creation order is
// time order.
// ---------------------------------------------------------------------------
struct Market {
    OrderBook book;
    OrderStore store;
    uint64_t nextTradeId = 1;
    uint64_t seq = 0;

    // Create + store an order (not in the book). Returns its internal ID.
    uint64_t submit(uint64_t id, OrderSide side, OrderType type, int64_t price, uint64_t qty) {
        NewOrderResult r = Order::create(OrderIds{.orderId = id, .clOrdId = id, .origClOrdId = 0, .chainId = id},
                                         ++seq, type, side, price, qty);
        REQUIRE_EQ(r.order.getState(), State::NEW);
        REQUIRE(store.insert(r.order));
        return id;
    }

    // A resting limit order: stored AND in the book (no matching involved).
    void rest(uint64_t id, OrderSide side, int64_t price, uint64_t qty) {
        submit(id, side, OrderType::Limit, price, qty);
        REQUIRE(book.add(*store.find(id)));
    }

    uint64_t limit(uint64_t id, OrderSide side, int64_t price, uint64_t qty) {
        return submit(id, side, OrderType::Limit, price, qty);
    }
    uint64_t market(uint64_t id, OrderSide side, uint64_t qty) {
        return submit(id, side, OrderType::Market, /*price=*/0, qty);
    }

    MatchResult run(uint64_t incomingId) {
        return match(incomingId, book, store, nextTradeId);
    }

    const Order& order(uint64_t id) {
        const Order* o = store.find(id);
        REQUIRE(o != nullptr);
        return *o;
    }
};

static void checkTrade(const Trade& t, uint64_t tradeId, uint64_t aggressorId, uint64_t restingId,
                       int64_t price, uint64_t qty) {
    CHECK_EQ(t.tradeId, tradeId);
    CHECK_EQ(t.aggressorId, aggressorId);
    CHECK_EQ(t.restingId, restingId);
    CHECK_EQ(t.price, price);
    CHECK_EQ(t.qty, qty);
}

// ---------------------------------------------------------------------------
// Invariants after every successful match (todo section 2, design.md s.8).
//   filledBefore: the incoming order's filled before match (0 for a new order,
//   the carried amount for a partly filled replacement).
//   firstTradeId: nextTradeId before match.
// ---------------------------------------------------------------------------
static void checkAfterMatch(Market& m, uint64_t incomingId, const MatchResult& r,
                            uint64_t filledBefore, uint64_t firstTradeId) {
    REQUIRE(!r.bug.has_value());

    // the book: live limit orders at their own side/price, time order, not crossed
    std::size_t counted = 0;
    for (OrderSide side : {OrderSide::Buy, OrderSide::Sell}) {
        for (int64_t px : m.book.prices(side)) {
            IdList ids = m.book.idsAt(side, px);
            CHECK(!ids.empty());
            counted += ids.size();
            uint64_t prevSeq = 0;
            for (uint64_t id : ids) {
                const Order& o = m.order(id);
                CHECK(o.getState() == State::NEW || o.getState() == State::PARTIALLY_FILLED);
                CHECK_EQ(o.getType(), OrderType::Limit);
                CHECK_EQ(o.getSide(), side);
                CHECK_EQ(o.getPrice(), px);
                CHECK(o.getRemaining() > 0u);
                CHECK(o.getSeq() > prevSeq);
                prevSeq = o.getSeq();
                checkInvariants(o);
            }
        }
    }
    CHECK_EQ(m.book.size(), counted);
    if (m.book.bestBid() && m.book.bestAsk()) CHECK(*m.book.bestBid() < *m.book.bestAsk());

    // the incoming order is finished
    const Order& in = m.order(incomingId);
    checkInvariants(in);
    if (in.getType() == OrderType::Market) {
        CHECK(in.getState() == State::FILLED || in.getState() == State::CANCELLED);
        CHECK(!m.book.contains(incomingId));
    } else {
        CHECK_EQ(m.book.contains(incomingId), in.getRemaining() > 0u);
        CHECK(in.getState() == State::NEW || in.getState() == State::PARTIALLY_FILLED ||
              in.getState() == State::FILLED);
    }

    // the trades: consecutive IDs, this aggressor, the other side, a price the
    // aggressor accepts, and their quantities add up to what it filled
    uint64_t sum = 0;
    for (std::size_t i = 0; i < r.trades.size(); ++i) {
        const Trade& t = r.trades[i];
        CHECK_EQ(t.tradeId, firstTradeId + i);
        CHECK_EQ(t.aggressorId, incomingId);
        CHECK(t.qty > 0u);
        const Order& rest = m.order(t.restingId);
        CHECK(rest.getSide() != in.getSide());
        CHECK_EQ(t.price, rest.getPrice());
        if (in.getType() == OrderType::Limit) {
            if (in.getSide() == OrderSide::Buy) CHECK(t.price <= in.getPrice());
            else                                CHECK(t.price >= in.getPrice());
        }
        checkInvariants(rest);
        sum += t.qty;
    }
    CHECK_EQ(sum, in.getFilled() - filledBefore);
    CHECK_EQ(m.nextTradeId, firstTradeId + r.trades.size());
}

// ===========================================================================
// crosses
// ===========================================================================

TEST_CASE("match: crosses -> empty book never crosses") {
    Market m;
    m.limit(1, OrderSide::Buy, 10000, 10);
    m.market(2, OrderSide::Sell, 10);
    CHECK(!crosses(m.order(1), m.book));
    CHECK(!crosses(m.order(2), m.book));
}

TEST_CASE("match: crosses -> buy limit crosses iff price >= best ask") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 10);
    m.rest(2, OrderSide::Sell, 10200, 10);
    m.rest(3, OrderSide::Buy, 9900, 10);  // same side: irrelevant
    m.limit(10, OrderSide::Buy, 10000, 5);
    m.limit(11, OrderSide::Buy, 10100, 5);
    m.limit(12, OrderSide::Buy, 10500, 5);
    CHECK(!crosses(m.order(10), m.book));  // below best ask
    CHECK(crosses(m.order(11), m.book));   // equal crosses
    CHECK(crosses(m.order(12), m.book));   // above
}

TEST_CASE("match: crosses -> sell limit crosses iff price <= best bid") {
    Market m;
    m.rest(1, OrderSide::Buy, 9900, 10);
    m.rest(2, OrderSide::Buy, 9800, 10);
    m.rest(3, OrderSide::Sell, 10100, 10);  // same side: irrelevant
    m.limit(10, OrderSide::Sell, 10000, 5);
    m.limit(11, OrderSide::Sell, 9900, 5);
    m.limit(12, OrderSide::Sell, 9500, 5);
    CHECK(!crosses(m.order(10), m.book));
    CHECK(crosses(m.order(11), m.book));
    CHECK(crosses(m.order(12), m.book));
}

TEST_CASE("match: crosses -> market crosses iff the opposite side is non-empty") {
    Market m;
    m.rest(1, OrderSide::Buy, 9900, 10);  // only bids
    m.market(10, OrderSide::Buy, 5);
    m.market(11, OrderSide::Sell, 5);
    CHECK(!crosses(m.order(10), m.book));  // no asks
    CHECK(crosses(m.order(11), m.book));
}

// A market order's price is 0 (ignored), so comparing it like a limit price
// would make market buys never cross. Both sides, both ways:
TEST_CASE("match: crosses -> market buy crosses any ask, market sell needs bids") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 10);  // only asks
    m.market(10, OrderSide::Buy, 5);
    m.market(11, OrderSide::Sell, 5);
    CHECK(crosses(m.order(10), m.book));   // price 0 must not be compared with 10100
    CHECK(!crosses(m.order(11), m.book));  // no bids
}

// ===========================================================================
// No cross: the limit rests
// ===========================================================================

TEST_CASE("match: empty book, limit -> rests, no trades") {
    Market m;
    uint64_t in = m.limit(1, OrderSide::Buy, 10000, 100);
    MatchResult r = m.run(in);
    CHECK(r.trades.empty());
    CHECK_EQ(m.order(1).getState(), State::NEW);
    CHECK_EQ(m.book.idsAt(OrderSide::Buy, 10000), (IdList{1}));
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: no cross -> limit rests at the back of its level, others untouched") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 10);
    m.rest(2, OrderSide::Buy, 9900, 10);
    uint64_t in = m.limit(3, OrderSide::Buy, 9900, 20);
    MatchResult r = m.run(in);
    CHECK(r.trades.empty());
    CHECK_EQ(m.book.idsAt(OrderSide::Buy, 9900), (IdList{2, 3}));
    CHECK_EQ(m.book.idsAt(OrderSide::Sell, 10100), (IdList{1}));
    CHECK_EQ(m.order(1).getFilled(), 0u);
    checkAfterMatch(m, in, r, 0, 1);
}

// ===========================================================================
// Crossing limits
// ===========================================================================

TEST_CASE("match: exact match -> one trade, both Filled, level removed") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 50);
    uint64_t in = m.limit(2, OrderSide::Buy, 10100, 50);
    MatchResult r = m.run(in);

    REQUIRE_EQ(r.trades.size(), 1u);
    checkTrade(r.trades[0], 1, 2, 1, 10100, 50);
    CHECK_EQ(m.order(1).getState(), State::FILLED);
    CHECK_EQ(m.order(2).getState(), State::FILLED);
    CHECK_EQ(m.book.size(), 0u);
    CHECK_EQ(m.book.levelCount(OrderSide::Sell), 0u);
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: trade price is the resting price (buy aggressor)") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 10);
    uint64_t in = m.limit(2, OrderSide::Buy, 10500, 10);  // willing to pay more
    MatchResult r = m.run(in);
    REQUIRE_EQ(r.trades.size(), 1u);
    CHECK_EQ(r.trades[0].price, 10100);
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: trade price is the resting price (sell aggressor)") {
    Market m;
    m.rest(1, OrderSide::Buy, 10500, 10);
    uint64_t in = m.limit(2, OrderSide::Sell, 10000, 10);  // willing to sell for less
    MatchResult r = m.run(in);
    REQUIRE_EQ(r.trades.size(), 1u);
    CHECK_EQ(r.trades[0].price, 10500);
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: aggressor smaller than resting -> resting PartiallyFilled, stays at the front") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 100);
    m.rest(2, OrderSide::Sell, 10100, 100);
    uint64_t in = m.limit(3, OrderSide::Buy, 10100, 30);
    MatchResult r = m.run(in);

    REQUIRE_EQ(r.trades.size(), 1u);
    checkTrade(r.trades[0], 1, 3, 1, 10100, 30);
    CHECK_EQ(m.order(3).getState(), State::FILLED);
    CHECK_EQ(m.order(1).getState(), State::PARTIALLY_FILLED);
    CHECK_EQ(m.order(1).getRemaining(), 70u);
    CHECK_EQ(m.book.idsAt(OrderSide::Sell, 10100), (IdList{1, 2}));  // still first in line
    CHECK(!m.book.contains(3));
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: aggressor walks the queue at one price, front to back") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 30);
    m.rest(2, OrderSide::Sell, 10100, 50);
    m.rest(3, OrderSide::Sell, 10100, 40);
    uint64_t in = m.limit(4, OrderSide::Buy, 10100, 100);
    MatchResult r = m.run(in);

    REQUIRE_EQ(r.trades.size(), 3u);
    checkTrade(r.trades[0], 1, 4, 1, 10100, 30);
    checkTrade(r.trades[1], 2, 4, 2, 10100, 50);
    checkTrade(r.trades[2], 3, 4, 3, 10100, 20);
    CHECK_EQ(m.order(1).getState(), State::FILLED);
    CHECK_EQ(m.order(2).getState(), State::FILLED);
    CHECK_EQ(m.order(3).getState(), State::PARTIALLY_FILLED);
    CHECK_EQ(m.order(3).getRemaining(), 20u);
    CHECK_EQ(m.book.idsAt(OrderSide::Sell, 10100), (IdList{3}));
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: the worked example -> three trades across two prices") {
    // asks: 101 [A 30, B 50], 102 [C 100]; buy limit 102 for 120
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 30);
    m.rest(2, OrderSide::Sell, 10100, 50);
    m.rest(3, OrderSide::Sell, 10200, 100);
    uint64_t in = m.limit(10, OrderSide::Buy, 10200, 120);
    MatchResult r = m.run(in);

    REQUIRE_EQ(r.trades.size(), 3u);
    checkTrade(r.trades[0], 1, 10, 1, 10100, 30);
    checkTrade(r.trades[1], 2, 10, 2, 10100, 50);
    checkTrade(r.trades[2], 3, 10, 3, 10200, 40);
    CHECK_EQ(m.order(10).getState(), State::FILLED);
    CHECK_EQ(m.order(3).getRemaining(), 60u);
    CHECK_EQ(m.book.prices(OrderSide::Sell), (PriceList{10200}));
    CHECK(!m.book.contains(10));
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: aggressor stops at its limit price, rest of it rests") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 10);
    m.rest(2, OrderSide::Sell, 10200, 10);
    m.rest(3, OrderSide::Sell, 10300, 10);  // beyond the limit: untouched
    uint64_t in = m.limit(4, OrderSide::Buy, 10200, 50);
    MatchResult r = m.run(in);

    REQUIRE_EQ(r.trades.size(), 2u);
    checkTrade(r.trades[0], 1, 4, 1, 10100, 10);
    checkTrade(r.trades[1], 2, 4, 2, 10200, 10);
    CHECK_EQ(m.order(4).getState(), State::PARTIALLY_FILLED);
    CHECK_EQ(m.order(4).getRemaining(), 30u);
    CHECK_EQ(m.book.idsAt(OrderSide::Buy, 10200), (IdList{4}));  // rests at its own price
    CHECK_EQ(m.book.prices(OrderSide::Sell), (PriceList{10300}));
    CHECK_EQ(m.order(3).getFilled(), 0u);
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: sell aggressor walks bids highest first, then rests") {
    Market m;
    m.rest(1, OrderSide::Buy, 9800, 10);
    m.rest(2, OrderSide::Buy, 10000, 10);
    m.rest(3, OrderSide::Buy, 9900, 10);
    uint64_t in = m.limit(4, OrderSide::Sell, 9900, 25);
    MatchResult r = m.run(in);

    REQUIRE_EQ(r.trades.size(), 2u);
    checkTrade(r.trades[0], 1, 4, 2, 10000, 10);
    checkTrade(r.trades[1], 2, 4, 3, 9900, 10);
    CHECK_EQ(m.order(4).getRemaining(), 5u);
    CHECK_EQ(m.book.idsAt(OrderSide::Sell, 9900), (IdList{4}));
    CHECK_EQ(m.book.prices(OrderSide::Buy), (PriceList{9800}));
    checkAfterMatch(m, in, r, 0, 1);
}

// ===========================================================================
// Market orders
// ===========================================================================

TEST_CASE("match: market into an empty side -> Cancelled, no trades") {
    Market m;
    m.rest(1, OrderSide::Buy, 9900, 10);  // only the same side has orders
    uint64_t in = m.market(2, OrderSide::Buy, 50);
    MatchResult r = m.run(in);
    CHECK(r.trades.empty());
    CHECK_EQ(m.order(2).getState(), State::CANCELLED);
    CHECK_EQ(m.order(2).getRemaining(), 0u);
    CHECK_EQ(m.order(2).getFilled(), 0u);
    CHECK(!m.book.contains(2));
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: market buy walks asks lowest first -> Filled") {
    Market m;
    m.rest(1, OrderSide::Sell, 10200, 10);
    m.rest(2, OrderSide::Sell, 10100, 10);
    m.rest(3, OrderSide::Sell, 10300, 10);
    uint64_t in = m.market(4, OrderSide::Buy, 25);
    MatchResult r = m.run(in);

    REQUIRE_EQ(r.trades.size(), 3u);
    checkTrade(r.trades[0], 1, 4, 2, 10100, 10);
    checkTrade(r.trades[1], 2, 4, 1, 10200, 10);
    checkTrade(r.trades[2], 3, 4, 3, 10300, 5);
    CHECK_EQ(m.order(4).getState(), State::FILLED);
    CHECK_EQ(m.book.idsAt(OrderSide::Sell, 10300), (IdList{3}));
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: market sell walks bids highest first -> Filled") {
    Market m;
    m.rest(1, OrderSide::Buy, 9900, 10);
    m.rest(2, OrderSide::Buy, 10000, 10);
    uint64_t in = m.market(3, OrderSide::Sell, 15);
    MatchResult r = m.run(in);

    REQUIRE_EQ(r.trades.size(), 2u);
    checkTrade(r.trades[0], 1, 3, 2, 10000, 10);
    checkTrade(r.trades[1], 2, 3, 1, 9900, 5);
    CHECK_EQ(m.order(3).getState(), State::FILLED);
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: market larger than the book -> fills what's there, rest Cancelled") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 10);
    m.rest(2, OrderSide::Sell, 10200, 20);
    uint64_t in = m.market(3, OrderSide::Buy, 100);
    MatchResult r = m.run(in);

    REQUIRE_EQ(r.trades.size(), 2u);
    CHECK_EQ(m.order(3).getState(), State::CANCELLED);
    CHECK_EQ(m.order(3).getFilled(), 30u);
    CHECK_EQ(m.order(3).getRemaining(), 0u);
    CHECK_EQ(m.book.size(), 0u);
    checkAfterMatch(m, in, r, 0, 1);
}

// ===========================================================================
// Price-time priority
// ===========================================================================

TEST_CASE("match: better price fills first even if it arrived later") {
    Market m;
    m.rest(1, OrderSide::Sell, 10200, 10);  // earlier, worse price
    m.rest(2, OrderSide::Sell, 10100, 10);  // later, better price
    uint64_t in = m.limit(3, OrderSide::Buy, 10200, 10);
    MatchResult r = m.run(in);
    REQUIRE_EQ(r.trades.size(), 1u);
    CHECK_EQ(r.trades[0].restingId, 2u);
    CHECK_EQ(m.order(1).getFilled(), 0u);
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: same price -> earlier order fills first") {
    Market m;
    m.rest(1, OrderSide::Buy, 10000, 10);
    m.rest(2, OrderSide::Buy, 10000, 10);
    uint64_t in = m.limit(3, OrderSide::Sell, 10000, 10);
    MatchResult r = m.run(in);
    REQUIRE_EQ(r.trades.size(), 1u);
    CHECK_EQ(r.trades[0].restingId, 1u);
    CHECK_EQ(m.order(2).getFilled(), 0u);
    CHECK_EQ(m.book.idsAt(OrderSide::Buy, 10000), (IdList{2}));
    checkAfterMatch(m, in, r, 0, 1);
}

// ===========================================================================
// Cancel / replace, then matching respects the queue
// ===========================================================================

TEST_CASE("match: cancelled resting order is skipped") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 10);
    m.rest(2, OrderSide::Sell, 10100, 10);
    // cancel 1 the way the engine will: apply, then take it out of the book
    REQUIRE_EQ(m.store.find(1)->apply(Event::CANCEL, 0, true).outcomeType, OutcomeType::Accept);
    REQUIRE(m.book.remove(m.order(1)));

    uint64_t in = m.limit(3, OrderSide::Buy, 10100, 10);
    MatchResult r = m.run(in);
    REQUIRE_EQ(r.trades.size(), 1u);
    CHECK_EQ(r.trades[0].restingId, 2u);
    CHECK_EQ(m.order(1).getState(), State::CANCELLED);
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: replace keeping priority -> still fills first") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 50);
    m.rest(2, OrderSide::Sell, 10100, 50);
    // 1 -> 5: same price, smaller qty: keeps its seq and its place
    ReplaceResult rr = m.store.find(1)->replace(5, 5, 10100, 40, ++m.seq);
    REQUIRE(rr.replacement.has_value());
    REQUIRE(m.store.insert(*rr.replacement));
    REQUIRE(m.book.replaceInPlace(m.order(1), m.order(5)));

    uint64_t in = m.limit(3, OrderSide::Buy, 10100, 40);
    MatchResult r = m.run(in);
    REQUIRE_EQ(r.trades.size(), 1u);
    checkTrade(r.trades[0], 1, 3, 5, 10100, 40);
    CHECK_EQ(m.order(5).getState(), State::FILLED);
    CHECK_EQ(m.book.idsAt(OrderSide::Sell, 10100), (IdList{2}));
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: replace losing priority -> goes to the back, fills after the others") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 10);
    m.rest(2, OrderSide::Sell, 10100, 10);
    // 1 -> 5: bigger qty loses priority. It doesn't cross, so match just rests it.
    REQUIRE(m.book.remove(m.order(1)));
    ReplaceResult rr = m.store.find(1)->replace(5, 5, 10100, 20, ++m.seq);
    REQUIRE(rr.replacement.has_value());
    REQUIRE(m.store.insert(*rr.replacement));
    MatchResult rest = m.run(5);
    CHECK(rest.trades.empty());
    CHECK_EQ(m.book.idsAt(OrderSide::Sell, 10100), (IdList{2, 5}));

    uint64_t in = m.limit(3, OrderSide::Buy, 10100, 15);
    MatchResult r = m.run(in);
    REQUIRE_EQ(r.trades.size(), 2u);
    checkTrade(r.trades[0], 1, 3, 2, 10100, 10);
    checkTrade(r.trades[1], 2, 3, 5, 10100, 5);
    checkAfterMatch(m, in, r, 0, 1);
}

TEST_CASE("match: replace that crosses -> trades instead of resting crossed") {
    Market m;
    m.rest(1, OrderSide::Buy, 9900, 30);
    m.rest(2, OrderSide::Sell, 10100, 20);
    // raise the bid to the ask: remove old, store new, match new
    REQUIRE(m.book.remove(m.order(1)));
    ReplaceResult rr = m.store.find(1)->replace(5, 5, 10100, 30, ++m.seq);
    REQUIRE(rr.replacement.has_value());
    REQUIRE(m.store.insert(*rr.replacement));
    MatchResult r = m.run(5);

    REQUIRE_EQ(r.trades.size(), 1u);
    checkTrade(r.trades[0], 1, 5, 2, 10100, 20);
    CHECK_EQ(m.order(1).getState(), State::REPLACED);
    CHECK_EQ(m.order(5).getState(), State::PARTIALLY_FILLED);
    CHECK_EQ(m.book.idsAt(OrderSide::Buy, 10100), (IdList{5}));
    CHECK_EQ(m.book.levelCount(OrderSide::Sell), 0u);
    checkAfterMatch(m, 5, r, 0, 1);
}

TEST_CASE("match: partly filled order replaced to cross -> keeps its filled, fills more") {
    Market m;
    m.rest(1, OrderSide::Buy, 9900, 100);
    MatchResult first = m.run(m.limit(2, OrderSide::Sell, 9900, 40));  // 2 sells 40 into 1
    REQUIRE_EQ(first.trades.size(), 1u);
    REQUIRE_EQ(m.order(1).getState(), State::PARTIALLY_FILLED);
    REQUIRE_EQ(m.order(1).getFilled(), 40u);

    // an ask appears above; 1 is replaced up to it (price change: loses priority)
    m.rest(3, OrderSide::Sell, 10100, 30);
    REQUIRE(m.book.remove(m.order(1)));
    ReplaceResult rr = m.store.find(1)->replace(5, 5, 10100, 100, ++m.seq);
    REQUIRE(rr.replacement.has_value());
    REQUIRE(m.store.insert(*rr.replacement));
    REQUIRE_EQ(m.order(5).getFilled(), 40u);
    REQUIRE_EQ(m.order(5).getState(), State::PARTIALLY_FILLED);

    uint64_t firstId = m.nextTradeId;
    MatchResult r = m.run(5);
    REQUIRE_EQ(r.trades.size(), 1u);
    checkTrade(r.trades[0], firstId, 5, 3, 10100, 30);
    CHECK_EQ(m.order(5).getFilled(), 70u);
    CHECK_EQ(m.order(5).getRemaining(), 30u);
    CHECK_EQ(m.order(5).getState(), State::PARTIALLY_FILLED);
    CHECK(m.book.contains(5));
    checkAfterMatch(m, 5, r, /*filledBefore=*/40, firstId);
}

// ===========================================================================
// Trade IDs and conservation across many orders
// ===========================================================================

TEST_CASE("match: trade IDs continue across calls") {
    Market m;
    m.nextTradeId = 41;  // whatever the engine's counter says
    m.rest(1, OrderSide::Sell, 10100, 10);
    m.rest(2, OrderSide::Sell, 10100, 10);
    m.rest(3, OrderSide::Sell, 10100, 10);

    MatchResult a = m.run(m.limit(4, OrderSide::Buy, 10100, 20));
    REQUIRE_EQ(a.trades.size(), 2u);
    CHECK_EQ(a.trades[0].tradeId, 41u);
    CHECK_EQ(a.trades[1].tradeId, 42u);
    CHECK_EQ(m.nextTradeId, 43u);

    MatchResult b = m.run(m.limit(5, OrderSide::Buy, 10100, 10));
    REQUIRE_EQ(b.trades.size(), 1u);
    CHECK_EQ(b.trades[0].tradeId, 43u);
    CHECK_EQ(m.nextTradeId, 44u);

    MatchResult c = m.run(m.limit(6, OrderSide::Buy, 9000, 10));  // no cross: no ID used
    CHECK(c.trades.empty());
    CHECK_EQ(m.nextTradeId, 44u);
}

TEST_CASE("match: a sequence of orders -> book never crossed, bought == sold") {
    Market m;
    struct Step { OrderSide side; OrderType type; int64_t price; uint64_t qty; };
    const std::vector<Step> steps{
        {OrderSide::Buy,  OrderType::Limit,  10000, 50},
        {OrderSide::Sell, OrderType::Limit,  10200, 30},
        {OrderSide::Buy,  OrderType::Limit,  10100, 20},
        {OrderSide::Sell, OrderType::Limit,  10000, 60},   // hits 10100 then 10000
        {OrderSide::Sell, OrderType::Limit,  10300, 40},
        {OrderSide::Buy,  OrderType::Market, 0,     50},   // walks asks
        {OrderSide::Buy,  OrderType::Limit,  10300, 100},  // takes the rest, rests
        {OrderSide::Sell, OrderType::Market, 0,     70},
        {OrderSide::Sell, OrderType::Limit,  9000,  500},  // sweeps bids, rests
        {OrderSide::Buy,  OrderType::Market, 0,     10},
    };

    uint64_t id = 1, totalTradeQty = 0;
    for (const Step& s : steps) {
        uint64_t in = m.submit(id++, s.side, s.type, s.price, s.qty);
        uint64_t firstId = m.nextTradeId;
        MatchResult r = m.run(in);
        checkAfterMatch(m, in, r, 0, firstId);
        for (const Trade& t : r.trades) totalTradeQty += t.qty;
    }

    uint64_t bought = 0, sold = 0;
    for (uint64_t i = 1; i < id; ++i) {
        const Order& o = m.order(i);
        (o.getSide() == OrderSide::Buy ? bought : sold) += o.getFilled();
    }
    CHECK_EQ(bought, sold);
    CHECK_EQ(bought, totalTradeQty);
    CHECK(totalTradeQty > 0u);
}

// ===========================================================================
// Bugs: returned, not aborted (design.md s.6, s.8)
// ===========================================================================

TEST_CASE("match: Bug -> incoming ID not in the store") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 10);
    MatchResult r = m.run(99);
    REQUIRE(r.bug.has_value());
    CHECK_EQ(r.bug->orderId, 99u);
    CHECK(!r.bug->event.has_value());
    CHECK(r.trades.empty());
    CHECK_EQ(m.nextTradeId, 1u);
    CHECK_EQ(m.order(1).getFilled(), 0u);
}

TEST_CASE("match: Bug -> incoming order is not live (Rejected)") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 10);
    NewOrderResult bad = Order::create(OrderIds{.orderId = 2, .clOrdId = 2, .origClOrdId = 0, .chainId = 2},
                                       ++m.seq, OrderType::Limit, OrderSide::Buy, /*price=*/0, 10);
    REQUIRE_EQ(bad.order.getState(), State::REJECTED);
    REQUIRE(m.store.insert(bad.order));

    MatchResult r = m.run(2);
    REQUIRE(r.bug.has_value());
    CHECK_EQ(r.bug->orderId, 2u);
    CHECK(!r.bug->event.has_value());
    CHECK(r.trades.empty());
    CHECK(!m.book.contains(2));
    CHECK_EQ(m.order(1).getFilled(), 0u);
}

TEST_CASE("match: Bug -> incoming order is already in the book") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 10);
    m.rest(2, OrderSide::Buy, 9900, 10);
    MatchResult r = m.run(2);
    REQUIRE(r.bug.has_value());
    CHECK_EQ(r.bug->orderId, 2u);
    CHECK(!r.bug->event.has_value());
    CHECK(r.trades.empty());
    CHECK_EQ(m.book.idsAt(OrderSide::Buy, 9900), (IdList{2}));
}

TEST_CASE("match: Bug -> book holds an order the store says is not live") {
    Market m;
    m.rest(1, OrderSide::Sell, 10100, 10);
    // engine bug simulated: cancelled in the store but left in the book
    REQUIRE_EQ(m.store.find(1)->apply(Event::CANCEL, 0, true).outcomeType, OutcomeType::Accept);

    MatchResult r = m.run(m.limit(2, OrderSide::Buy, 10100, 10));
    REQUIRE(r.bug.has_value());
    CHECK_EQ(r.bug->orderId, 1u);  // the stale resting order
}
