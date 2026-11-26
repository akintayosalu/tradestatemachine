// OrderStore tests. Naming: "store: ..." so `make test FILTER=store:` runs these.
#include <order_helpers.hpp>
#include <engine/store.hpp>

// A limit buy with separate internal ID and client ID, so tests can tell the
// two lookups apart.
static Order makeWithIds(uint64_t orderId, uint64_t clOrdId, uint64_t qty = 100) {
    NewOrderResult r = Order::create(OrderIds{.orderId = orderId, .clOrdId = clOrdId, .origClOrdId = 0, .chainId = orderId},
                                     /*seq=*/orderId, OrderType::Limit, OrderSide::Buy, 10000, qty);
    REQUIRE_EQ(r.order.getState(), State::NEW);
    return r.order;
}

// ---------------------------------------------------------------------------
// Empty store
// ---------------------------------------------------------------------------

TEST_CASE("store: empty -> nothing found") {
    OrderStore store;
    CHECK_EQ(store.size(), 0u);
    CHECK(store.find(1) == nullptr);
    CHECK(store.findByClOrdId(100) == nullptr);
    CHECK(!store.hasClOrdId(100));
}

// ---------------------------------------------------------------------------
// insert + find
// ---------------------------------------------------------------------------

TEST_CASE("store: insert -> findable by internal ID and by client ID") {
    OrderStore store;
    REQUIRE(store.insert(makeWithIds(/*orderId=*/1, /*clOrdId=*/100)));

    CHECK_EQ(store.size(), 1u);

    Order* byId = store.find(1);
    REQUIRE(byId != nullptr);
    CHECK_EQ(byId->getOrderId(), 1u);
    CHECK_EQ(byId->getClOrdId(), 100u);

    Order* byCl = store.findByClOrdId(100);
    REQUIRE(byCl != nullptr);
    CHECK(byCl == byId);  // same object, not a copy
    CHECK(store.hasClOrdId(100));
}

TEST_CASE("store: internal and client IDs are separate keys") {
    OrderStore store;
    REQUIRE(store.insert(makeWithIds(1, 100)));

    CHECK(store.find(100) == nullptr);        // 100 is a client ID, not an internal ID
    CHECK(store.findByClOrdId(1) == nullptr); // 1 is an internal ID, not a client ID
    CHECK(!store.hasClOrdId(1));
}

TEST_CASE("store: several orders -> each found by its own IDs") {
    OrderStore store;
    REQUIRE(store.insert(makeWithIds(1, 100, 10)));
    REQUIRE(store.insert(makeWithIds(2, 200, 20)));
    REQUIRE(store.insert(makeWithIds(3, 300, 30)));

    CHECK_EQ(store.size(), 3u);
    REQUIRE(store.find(2) != nullptr);
    CHECK_EQ(store.find(2)->getOriginal(), 20u);
    REQUIRE(store.findByClOrdId(300) != nullptr);
    CHECK_EQ(store.findByClOrdId(300)->getOrderId(), 3u);
    CHECK(store.find(4) == nullptr);
    CHECK(store.findByClOrdId(400) == nullptr);
}

// ---------------------------------------------------------------------------
// Duplicates refused (store unchanged)
// ---------------------------------------------------------------------------

TEST_CASE("store: duplicate internal ID -> refused, original kept") {
    OrderStore store;
    REQUIRE(store.insert(makeWithIds(1, 100, /*qty=*/10)));

    CHECK(!store.insert(makeWithIds(1, 999, /*qty=*/50)));  // same internal ID, new client ID
    CHECK_EQ(store.size(), 1u);
    REQUIRE(store.find(1) != nullptr);
    CHECK_EQ(store.find(1)->getOriginal(), 10u);  // original untouched
    CHECK(!store.hasClOrdId(999));                // refused order left no trace
}

TEST_CASE("store: duplicate client ID -> refused, original kept") {
    OrderStore store;
    REQUIRE(store.insert(makeWithIds(1, 100, /*qty=*/10)));

    CHECK(!store.insert(makeWithIds(2, 100, /*qty=*/50)));  // new internal ID, same client ID
    CHECK_EQ(store.size(), 1u);
    CHECK(store.find(2) == nullptr);              // refused order left no trace
    REQUIRE(store.findByClOrdId(100) != nullptr);
    CHECK_EQ(store.findByClOrdId(100)->getOrderId(), 1u);
}

// ---------------------------------------------------------------------------
// One copy: changes through find() are the store's own order
// ---------------------------------------------------------------------------

TEST_CASE("store: apply through find() -> visible on the next lookup") {
    OrderStore store;
    REQUIRE(store.insert(makeWithIds(1, 100, 100)));

    Order* o = store.find(1);
    REQUIRE(o != nullptr);
    REQUIRE_EQ(o->apply(Event::PARTIAL_FILL, 30, true).outcomeType, OutcomeType::Accept);

    // looked up again, both ways: the change is there (no stale copy)
    CHECK_EQ(store.find(1)->getFilled(), 30u);
    CHECK_EQ(store.findByClOrdId(100)->getFilled(), 30u);
    CHECK_EQ(store.find(1)->getState(), State::PARTIALLY_FILLED);
    checkInvariants(*store.find(1));
}

TEST_CASE("store: inserting stores a copy -> later changes to the original don't leak in") {
    OrderStore store;
    Order local = makeWithIds(1, 100, 100);
    REQUIRE(store.insert(local));

    REQUIRE_EQ(local.apply(Event::PARTIAL_FILL, 30, true).outcomeType, OutcomeType::Accept);

    CHECK_EQ(store.find(1)->getFilled(), 0u);  // the store's order is its own
}

// ---------------------------------------------------------------------------
// Reference stability (the matcher holds a pointer while orders are inserted)
// ---------------------------------------------------------------------------

TEST_CASE("store: pointer from find() still valid after many inserts") {
    OrderStore store;
    REQUIRE(store.insert(makeWithIds(1, 100, 100)));
    Order* held = store.find(1);
    REQUIRE(held != nullptr);

    for (uint64_t id = 2; id <= 2000; ++id) {
        // client IDs 100002..102000: distinct from each other and from order 1's 100
        REQUIRE(store.insert(makeWithIds(id, 100000 + id)));
    }

    // same object, still readable and writable through the old pointer
    CHECK(store.find(1) == held);
    CHECK_EQ(held->getClOrdId(), 100u);
    REQUIRE_EQ(held->apply(Event::PARTIAL_FILL, 40, true).outcomeType, OutcomeType::Accept);
    CHECK_EQ(store.find(1)->getFilled(), 40u);
    CHECK_EQ(store.size(), 2000u);
}

// ---------------------------------------------------------------------------
// Final and rejected orders stay in the store
// ---------------------------------------------------------------------------

TEST_CASE("store: Rejected order is stored -> findable, client ID counts as used") {
    OrderStore store;
    NewOrderResult r = Order::create(OrderIds{.orderId = 1, .clOrdId = 100, .origClOrdId = 0, .chainId = 1},
                                     1, OrderType::Limit, OrderSide::Buy, /*price=*/0, 100);
    REQUIRE_EQ(r.order.getState(), State::REJECTED);

    REQUIRE(store.insert(r.order));
    REQUIRE(store.findByClOrdId(100) != nullptr);
    CHECK_EQ(store.findByClOrdId(100)->getState(), State::REJECTED);
    CHECK(store.hasClOrdId(100));  // reusing 100 is a duplicate-ID reject
}

TEST_CASE("store: Filled order stays findable -> late Cancel gets AlreadyFilled") {
    OrderStore store;
    REQUIRE(store.insert(makeWithIds(1, 100, 100)));
    REQUIRE_EQ(store.find(1)->apply(Event::FULL_FILL, 100, true).outcomeType, OutcomeType::Accept);

    Order* o = store.findByClOrdId(100);
    REQUIRE(o != nullptr);
    Outcome out = o->apply(Event::CANCEL, 0, true);
    REQUIRE_EQ(out.outcomeType, OutcomeType::Reject);
    CHECK_EQ(out.rejectReason, RejectReason::ALREADY_FILLED);
}

// ---------------------------------------------------------------------------
// Replace chain: every version findable by its own client ID
// ---------------------------------------------------------------------------

TEST_CASE("store: replace chain A->B->C -> each version findable, old ones Replaced") {
    OrderStore store;
    REQUIRE(store.insert(makeWithIds(/*orderId=*/1, /*clOrdId=*/100, 100)));

    // A -> B: replace the stored A in place, store B
    ReplaceResult rb = store.find(1)->replace(/*newOrderId=*/2, /*newClOrdId=*/200, 10000, 90, /*freshSeq=*/9);
    REQUIRE(rb.replacement.has_value());
    REQUIRE(store.insert(*rb.replacement));

    // B -> C: found by B's client ID, as an incoming request would name it
    ReplaceResult rc = store.findByClOrdId(200)->replace(3, 300, 10000, 80, 10);
    REQUIRE(rc.replacement.has_value());
    REQUIRE(store.insert(*rc.replacement));

    CHECK_EQ(store.size(), 3u);
    CHECK_EQ(store.findByClOrdId(100)->getState(), State::REPLACED);
    CHECK_EQ(store.findByClOrdId(200)->getState(), State::REPLACED);
    CHECK_EQ(store.findByClOrdId(300)->getState(), State::NEW);
    CHECK_EQ(store.findByClOrdId(300)->getOrigClOrdId(), 200u);
    CHECK_EQ(store.findByClOrdId(300)->getChainId(), 1u);

    // a stale request naming A gets "already replaced"
    Outcome out = store.findByClOrdId(100)->apply(Event::CANCEL, 0, true);
    REQUIRE_EQ(out.outcomeType, OutcomeType::Reject);
    CHECK_EQ(out.rejectReason, RejectReason::ALREADY_REPLACED);
}

// ---------------------------------------------------------------------------
// const access
// ---------------------------------------------------------------------------

TEST_CASE("store: const store -> lookups work, read-only") {
    OrderStore store;
    REQUIRE(store.insert(makeWithIds(1, 100, 100)));

    const OrderStore& view = store;
    const Order* byId = view.find(1);
    const Order* byCl = view.findByClOrdId(100);
    REQUIRE(byId != nullptr);
    CHECK(byCl == byId);
    CHECK(view.hasClOrdId(100));
    CHECK_EQ(view.size(), 1u);
    CHECK(view.find(2) == nullptr);
}
