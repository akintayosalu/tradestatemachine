// OrderBook tests (no matching). Naming: "book: ..." so `make test FILTER=book:`
// runs just these. Prices are ticks; IDs are small numbers so failures read easily.
#include <map>
#include <vector>

#include <order_helpers.hpp>
#include <engine/book.hpp>

using IdList = std::vector<uint64_t>;
using PriceList = std::vector<int64_t>;

// ---------------------------------------------------------------------------
// Book invariants (todo/order_book.txt section 1). Call after every operation.
// `orders` = every order the test knows about, so IDs found in the book can be
// looked up (the book itself only holds IDs).
// ---------------------------------------------------------------------------
static void checkBook(const OrderBook& book, const std::vector<Order>& orders) {
    std::map<uint64_t, const Order*> byId;
    for (const Order& o : orders) byId[o.getOrderId()] = &o;

    std::size_t counted = 0;
    for (OrderSide side : {OrderSide::Buy, OrderSide::Sell}) {
        PriceList ps = book.prices(side);
        CHECK_EQ(ps.size(), book.levelCount(side));

        for (std::size_t i = 0; i < ps.size(); ++i) {
            // levels are best-first: bids strictly descending, asks strictly ascending
            if (i > 0) {
                if (side == OrderSide::Buy) CHECK(ps[i] < ps[i - 1]);
                else                        CHECK(ps[i] > ps[i - 1]);
            }

            IdList ids = book.idsAt(side, ps[i]);
            CHECK(!ids.empty());  // no empty levels
            counted += ids.size();

            uint64_t prevSeq = 0;
            for (std::size_t k = 0; k < ids.size(); ++k) {
                REQUIRE(byId.count(ids[k]) == 1);  // test knows every resting order
                const Order& o = *byId[ids[k]];
                // every resting order is a live limit order...
                CHECK(o.getState() == State::NEW || o.getState() == State::PARTIALLY_FILLED);
                CHECK_EQ(o.getType(), OrderType::Limit);
                // ...at the level matching its own side and price
                CHECK_EQ(o.getSide(), side);
                CHECK_EQ(o.getPrice(), ps[i]);
                // time priority: seq strictly increases front to back
                if (k > 0) CHECK(o.getSeq() > prevSeq);
                prevSeq = o.getSeq();
                CHECK(book.contains(ids[k]));
            }
        }
    }
    CHECK_EQ(book.size(), counted);

    // best prices agree with the level lists
    PriceList bids = book.prices(OrderSide::Buy), asks = book.prices(OrderSide::Sell);
    CHECK_EQ(book.bestBid(), bids.empty() ? std::optional<int64_t>{} : std::optional<int64_t>(bids.front()));
    CHECK_EQ(book.bestAsk(), asks.empty() ? std::optional<int64_t>{} : std::optional<int64_t>(asks.front()));

    // not crossed (holds trivially until matching exists; tests avoid crossing)
    if (book.bestBid() && book.bestAsk()) CHECK(*book.bestBid() < *book.bestAsk());

    // anything final must not be in the book
    for (const Order& o : orders) {
        if (o.getState() != State::NEW && o.getState() != State::PARTIALLY_FILLED) {
            CHECK(!book.contains(o.getOrderId()));
        }
    }
}

// ---------------------------------------------------------------------------
// Empty book
// ---------------------------------------------------------------------------

TEST_CASE("book: empty -> no best prices, no levels") {
    OrderBook book;
    CHECK_EQ(book.bestBid(), std::optional<int64_t>{});
    CHECK_EQ(book.bestAsk(), std::optional<int64_t>{});
    CHECK_EQ(book.size(), 0u);
    CHECK_EQ(book.levelCount(OrderSide::Buy), 0u);
    CHECK_EQ(book.levelCount(OrderSide::Sell), 0u);
    CHECK(!book.contains(1));
    checkBook(book, {});
}

// ---------------------------------------------------------------------------
// add: price levels and best price
// ---------------------------------------------------------------------------

TEST_CASE("book: add one buy -> best bid, one level") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 10000, 100, 1)};

    CHECK(book.add(os[0]));
    CHECK_EQ(book.bestBid(), std::optional<int64_t>(10000));
    CHECK_EQ(book.bestAsk(), std::optional<int64_t>{});
    CHECK(book.contains(1));
    CHECK_EQ(book.size(), 1u);
    CHECK_EQ(book.idsAt(OrderSide::Buy, 10000), (IdList{1}));
    checkBook(book, os);
}

TEST_CASE("book: buys at three prices -> best bid is highest, levels high to low") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 9900, 10, 1),
                          makeLimit(2, OrderSide::Buy, 10000, 10, 2),
                          makeLimit(3, OrderSide::Buy, 9800, 10, 3)};
    for (const Order& o : os) REQUIRE(book.add(o));

    CHECK_EQ(book.bestBid(), std::optional<int64_t>(10000));
    CHECK_EQ(book.prices(OrderSide::Buy), (PriceList{10000, 9900, 9800}));
    CHECK_EQ(book.levelCount(OrderSide::Buy), 3u);
    checkBook(book, os);
}

TEST_CASE("book: sells at three prices -> best ask is lowest, levels low to high") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Sell, 10200, 10, 1),
                          makeLimit(2, OrderSide::Sell, 10100, 10, 2),
                          makeLimit(3, OrderSide::Sell, 10300, 10, 3)};
    for (const Order& o : os) REQUIRE(book.add(o));

    CHECK_EQ(book.bestAsk(), std::optional<int64_t>(10100));
    CHECK_EQ(book.prices(OrderSide::Sell), (PriceList{10100, 10200, 10300}));
    checkBook(book, os);
}

TEST_CASE("book: bids and asks are independent") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 9900, 10, 1),
                          makeLimit(2, OrderSide::Sell, 10100, 10, 2)};
    for (const Order& o : os) REQUIRE(book.add(o));

    CHECK_EQ(book.bestBid(), std::optional<int64_t>(9900));
    CHECK_EQ(book.bestAsk(), std::optional<int64_t>(10100));
    CHECK_EQ(book.levelCount(OrderSide::Buy), 1u);
    CHECK_EQ(book.levelCount(OrderSide::Sell), 1u);
    CHECK_EQ(book.idsAt(OrderSide::Sell, 9900), IdList{});  // no sell level at the bid price
    checkBook(book, os);
}

// ---------------------------------------------------------------------------
// add: FIFO within a level
// ---------------------------------------------------------------------------

TEST_CASE("book: same price -> queue in arrival order") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 10000, 10, 1),
                          makeLimit(2, OrderSide::Buy, 10000, 20, 2),
                          makeLimit(3, OrderSide::Buy, 10000, 30, 3)};
    for (const Order& o : os) REQUIRE(book.add(o));

    CHECK_EQ(book.idsAt(OrderSide::Buy, 10000), (IdList{1, 2, 3}));
    CHECK_EQ(book.levelCount(OrderSide::Buy), 1u);
    CHECK_EQ(book.size(), 3u);
    checkBook(book, os);
}

TEST_CASE("book: add a PartiallyFilled order -> rests (still live)") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 10000, 100, 1)};
    REQUIRE_EQ(os[0].apply(Event::PARTIAL_FILL, 30, true).outcomeType, OutcomeType::Accept);

    CHECK(book.add(os[0]));
    CHECK(book.contains(1));
    checkBook(book, os);
}

// ---------------------------------------------------------------------------
// add: refused (returns false, book unchanged)
// ---------------------------------------------------------------------------

TEST_CASE("book: add a market order -> refused (market orders never rest)") {
    OrderBook book;
    std::vector<Order> os{makeMarketBuy(100)};

    CHECK(!book.add(os[0]));
    CHECK(!book.contains(os[0].getOrderId()));
    CHECK_EQ(book.size(), 0u);
    checkBook(book, os);
}

TEST_CASE("book: add a final (Filled) order -> refused") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 10000, 100, 1)};
    REQUIRE_EQ(os[0].apply(Event::FULL_FILL, 100, true).outcomeType, OutcomeType::Accept);

    CHECK(!book.add(os[0]));
    CHECK_EQ(book.size(), 0u);
    checkBook(book, os);
}

TEST_CASE("book: add a Rejected order -> refused") {
    OrderBook book;
    NewOrderResult r = Order::create(OrderIds{.orderId = 1, .clOrdId = 1, .origClOrdId = 0, .chainId = 1},
                                     1, OrderType::Limit, OrderSide::Buy, /*price=*/0, 100);
    REQUIRE_EQ(r.order.getState(), State::REJECTED);

    CHECK(!book.add(r.order));
    CHECK_EQ(book.size(), 0u);
}

TEST_CASE("book: add the same order twice -> second add refused, one entry") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 10000, 100, 1)};

    REQUIRE(book.add(os[0]));
    CHECK(!book.add(os[0]));
    CHECK_EQ(book.size(), 1u);
    CHECK_EQ(book.idsAt(OrderSide::Buy, 10000), (IdList{1}));
    checkBook(book, os);
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

TEST_CASE("book: remove from the middle of a queue -> others keep their order") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Sell, 10100, 10, 1),
                          makeLimit(2, OrderSide::Sell, 10100, 10, 2),
                          makeLimit(3, OrderSide::Sell, 10100, 10, 3)};
    for (const Order& o : os) REQUIRE(book.add(o));

    CHECK(book.remove(os[1]));
    CHECK_EQ(book.idsAt(OrderSide::Sell, 10100), (IdList{1, 3}));
    CHECK(!book.contains(2));
    CHECK_EQ(book.size(), 2u);
    checkBook(book, os);
}

TEST_CASE("book: remove front and back of a queue") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Sell, 10100, 10, 1),
                          makeLimit(2, OrderSide::Sell, 10100, 10, 2),
                          makeLimit(3, OrderSide::Sell, 10100, 10, 3)};
    for (const Order& o : os) REQUIRE(book.add(o));

    CHECK(book.remove(os[0]));
    CHECK(book.remove(os[2]));
    CHECK_EQ(book.idsAt(OrderSide::Sell, 10100), (IdList{2}));
    checkBook(book, os);
}

TEST_CASE("book: remove last order at a level -> level deleted") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 10000, 10, 1),
                          makeLimit(2, OrderSide::Buy, 9900, 10, 2)};
    for (const Order& o : os) REQUIRE(book.add(o));

    CHECK(book.remove(os[1]));
    CHECK_EQ(book.levelCount(OrderSide::Buy), 1u);
    CHECK_EQ(book.prices(OrderSide::Buy), (PriceList{10000}));
    CHECK_EQ(book.idsAt(OrderSide::Buy, 9900), IdList{});
    checkBook(book, os);
}

TEST_CASE("book: remove the best level -> best moves to the next price") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Sell, 10100, 10, 1),
                          makeLimit(2, OrderSide::Sell, 10200, 10, 2)};
    for (const Order& o : os) REQUIRE(book.add(o));

    REQUIRE(book.remove(os[0]));
    CHECK_EQ(book.bestAsk(), std::optional<int64_t>(10200));
    checkBook(book, os);
}

TEST_CASE("book: remove every order -> empty book") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 10000, 10, 1),
                          makeLimit(2, OrderSide::Sell, 10100, 10, 2)};
    for (const Order& o : os) REQUIRE(book.add(o));
    for (const Order& o : os) REQUIRE(book.remove(o));

    CHECK_EQ(book.size(), 0u);
    CHECK_EQ(book.bestBid(), std::optional<int64_t>{});
    CHECK_EQ(book.bestAsk(), std::optional<int64_t>{});
    checkBook(book, os);
}

TEST_CASE("book: remove an order that isn't there -> refused, book unchanged") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 10000, 10, 1),
                          makeLimit(2, OrderSide::Buy, 10000, 10, 2)};
    REQUIRE(book.add(os[0]));

    CHECK(!book.remove(os[1]));  // never added
    CHECK_EQ(book.size(), 1u);
    REQUIRE(book.remove(os[0]));
    CHECK(!book.remove(os[0]));  // already removed
    checkBook(book, os);
}

TEST_CASE("book: cancel flow -> apply Cancel, then remove") {
    // How the engine will use it: the order changes state, then leaves the book.
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 10000, 100, 1),
                          makeLimit(2, OrderSide::Buy, 10000, 100, 2)};
    for (const Order& o : os) REQUIRE(book.add(o));

    REQUIRE_EQ(os[0].apply(Event::CANCEL, 0, true).outcomeType, OutcomeType::Accept);
    REQUIRE(book.remove(os[0]));

    CHECK_EQ(book.idsAt(OrderSide::Buy, 10000), (IdList{2}));
    checkBook(book, os);  // Cancelled order 1 must not be in the book
}

// ---------------------------------------------------------------------------
// Replace in the book
// ---------------------------------------------------------------------------

TEST_CASE("book: replace keeping priority -> new version takes the old position") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 10000, 100, 1),
                          makeLimit(2, OrderSide::Buy, 10000, 100, 2),
                          makeLimit(3, OrderSide::Buy, 10000, 100, 3)};
    for (const Order& o : os) REQUIRE(book.add(o));

    // order 2: qty 100 -> 60, same price -> keeps seq 2 (priority kept)
    ReplaceResult r = os[1].replace(/*newOrderId=*/20, /*newClOrdId=*/20, 10000, 60, /*freshSeq=*/9);
    REQUIRE(r.replacement.has_value());
    REQUIRE_EQ(r.replacement->getSeq(), 2u);

    CHECK(book.replaceInPlace(os[1], *r.replacement));
    CHECK_EQ(book.idsAt(OrderSide::Buy, 10000), (IdList{1, 20, 3}));  // same spot
    CHECK(!book.contains(2));
    CHECK(book.contains(20));
    CHECK_EQ(book.size(), 3u);

    os.push_back(*r.replacement);
    checkBook(book, os);
}

TEST_CASE("book: replace losing priority -> remove + add, goes to the back") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 10000, 100, 1),
                          makeLimit(2, OrderSide::Buy, 10000, 100, 2),
                          makeLimit(3, OrderSide::Buy, 10000, 100, 3)};
    for (const Order& o : os) REQUIRE(book.add(o));

    // order 1: qty up 100 -> 150 -> fresh seq 9 (priority lost)
    ReplaceResult r = os[0].replace(10, 10, 10000, 150, /*freshSeq=*/9);
    REQUIRE(r.replacement.has_value());
    REQUIRE_EQ(r.replacement->getSeq(), 9u);

    REQUIRE(book.remove(os[0]));
    REQUIRE(book.add(*r.replacement));
    CHECK_EQ(book.idsAt(OrderSide::Buy, 10000), (IdList{2, 3, 10}));

    os.push_back(*r.replacement);
    checkBook(book, os);
}

TEST_CASE("book: replace to a new price -> leaves old level, joins the new one") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Sell, 10100, 50, 1),
                          makeLimit(2, OrderSide::Sell, 10200, 50, 2)};
    for (const Order& o : os) REQUIRE(book.add(o));

    // order 1 moves 10100 -> 10200: price changed -> fresh seq, back of 10200
    ReplaceResult r = os[0].replace(10, 10, 10200, 50, /*freshSeq=*/9);
    REQUIRE(r.replacement.has_value());
    REQUIRE(book.remove(os[0]));
    REQUIRE(book.add(*r.replacement));

    CHECK_EQ(book.prices(OrderSide::Sell), (PriceList{10200}));  // 10100 level gone
    CHECK_EQ(book.idsAt(OrderSide::Sell, 10200), (IdList{2, 10}));
    CHECK_EQ(book.bestAsk(), std::optional<int64_t>(10200));

    os.push_back(*r.replacement);
    checkBook(book, os);
}

TEST_CASE("book: replaceInPlace with a different price -> refused, unchanged") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 10000, 100, 1)};
    REQUIRE(book.add(os[0]));

    Order other = makeLimit(20, OrderSide::Buy, 9900, 100, 1);  // different price
    CHECK(!book.replaceInPlace(os[0], other));
    CHECK(book.contains(1));
    CHECK(!book.contains(20));
    CHECK_EQ(book.idsAt(OrderSide::Buy, 10000), (IdList{1}));
    checkBook(book, os);
}

TEST_CASE("book: replaceInPlace when old version isn't in the book -> refused") {
    OrderBook book;
    Order old = makeLimit(1, OrderSide::Buy, 10000, 100, 1);  // never added
    Order nv  = makeLimit(20, OrderSide::Buy, 10000, 60, 1);

    CHECK(!book.replaceInPlace(old, nv));
    CHECK_EQ(book.size(), 0u);
}

// ---------------------------------------------------------------------------
// A longer sequence, invariants after every step
// ---------------------------------------------------------------------------

TEST_CASE("book: mixed adds and removes on both sides -> invariants hold throughout") {
    OrderBook book;
    std::vector<Order> os{makeLimit(1, OrderSide::Buy, 9900, 10, 1),
                          makeLimit(2, OrderSide::Sell, 10100, 10, 2),
                          makeLimit(3, OrderSide::Buy, 10000, 10, 3),
                          makeLimit(4, OrderSide::Buy, 9900, 10, 4),
                          makeLimit(5, OrderSide::Sell, 10100, 10, 5),
                          makeLimit(6, OrderSide::Sell, 10300, 10, 6)};

    for (const Order& o : os) { REQUIRE(book.add(o)); checkBook(book, os); }

    CHECK_EQ(book.prices(OrderSide::Buy), (PriceList{10000, 9900}));
    CHECK_EQ(book.prices(OrderSide::Sell), (PriceList{10100, 10300}));
    CHECK_EQ(book.idsAt(OrderSide::Buy, 9900), (IdList{1, 4}));
    CHECK_EQ(book.idsAt(OrderSide::Sell, 10100), (IdList{2, 5}));

    REQUIRE(book.remove(os[2])); checkBook(book, os);  // best bid level gone
    CHECK_EQ(book.bestBid(), std::optional<int64_t>(9900));
    REQUIRE(book.remove(os[1])); checkBook(book, os);  // front of best ask
    CHECK_EQ(book.idsAt(OrderSide::Sell, 10100), (IdList{5}));
    CHECK_EQ(book.size(), 4u);
}
