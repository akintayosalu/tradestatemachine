// Market-data (L3) tests. Naming: "md: ..." so `make test FILTER=md:` runs these.
// Format: docs/io_format.md "Market data"; decisions: docs/design.md section 10.
// Expected outputs are written by hand from the spec. Client IDs are 101, 102,
// ...; market data uses internal order IDs (1, 2, ...), so mixing them up shows.
#include <fstream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>

#include <engine/engine.hpp>
#include <log_generator.hpp>
#include <test_framework.hpp>

// Market data for a log run through a fresh engine (which must not hit a Bug).
static std::string md(std::string_view input) {
    RunResult r = runLog(input);
    CHECK(!r.bug);
    return r.marketData;
}

// ===========================================================================
// One request at a time
// ===========================================================================

TEST_CASE("md: limit order rests -> ADD with its remaining qty, seq 1, internal ID") {
    CHECK_EQ(md("NEW clOrdId=101 side=Buy type=Limit px=9900 qty=30\n"),
             std::string("ADD seq=1 orderId=1 side=Buy px=9900 qty=30\n"));
}

TEST_CASE("md: rejects, malformed, comments, cancel of unknown, market into empty -> nothing") {
    CHECK_EQ(md("NEW clOrdId=101 side=Buy type=Limit px=0 qty=5\n"      // rejected (invalid price)
                "NEW clOrdId=101 side=Buy type=Limit px=9900 qty=5\n"   // rejected (duplicate)
                "# comment\n"
                "\n"
                "GARBAGE\n"                                             // malformed
                "NEW clOrdId=102 side=Sell type=Market qty=5\n"         // no liquidity -> Cancelled
                "CANCEL clOrdId=999\n"),                                // unknown
             std::string(""));
}

TEST_CASE("md: crossing limit fully filled -> EXEC per resting order, no ADD for the aggressor") {
    CHECK_EQ(md("NEW clOrdId=101 side=Sell type=Limit px=10100 qty=30\n"
                "NEW clOrdId=102 side=Sell type=Limit px=10100 qty=50\n"
                "NEW clOrdId=103 side=Buy type=Limit px=10200 qty=60\n"),
             std::string("ADD seq=1 orderId=1 side=Sell px=10100 qty=30\n"
                         "ADD seq=2 orderId=2 side=Sell px=10100 qty=50\n"
                         "EXEC seq=3 orderId=1 qty=30 px=10100 tradeId=1\n"     // 1 fully filled: no DELETE
                         "EXEC seq=4 orderId=2 qty=30 px=10100 tradeId=2\n"));
}

TEST_CASE("md: crossing limit with quantity left -> EXECs, then ADD of the remainder") {
    CHECK_EQ(md("NEW clOrdId=101 side=Sell type=Limit px=10100 qty=10\n"
                "NEW clOrdId=102 side=Buy type=Limit px=10200 qty=25\n"),
             std::string("ADD seq=1 orderId=1 side=Sell px=10100 qty=10\n"
                         "EXEC seq=2 orderId=1 qty=10 px=10100 tradeId=1\n"
                         "ADD seq=3 orderId=2 side=Buy px=10200 qty=15\n"));    // rests at its own price
}

TEST_CASE("md: market order -> only EXECs on what it hit, at each resting price") {
    CHECK_EQ(md("NEW clOrdId=101 side=Sell type=Limit px=10100 qty=10\n"
                "NEW clOrdId=102 side=Sell type=Limit px=10200 qty=10\n"
                "NEW clOrdId=103 side=Buy type=Market qty=25\n"),        // 5 left over -> Cancelled, unpublished
             std::string("ADD seq=1 orderId=1 side=Sell px=10100 qty=10\n"
                         "ADD seq=2 orderId=2 side=Sell px=10200 qty=10\n"
                         "EXEC seq=3 orderId=1 qty=10 px=10100 tradeId=1\n"
                         "EXEC seq=4 orderId=2 qty=10 px=10200 tradeId=2\n"));
}

TEST_CASE("md: CANCEL -> DELETE (also after a partial fill); rejected cancels -> nothing") {
    CHECK_EQ(md("NEW clOrdId=101 side=Buy type=Limit px=9900 qty=30\n"
                "NEW clOrdId=102 side=Sell type=Limit px=10100 qty=10\n"
                "NEW clOrdId=103 side=Buy type=Limit px=10100 qty=4\n"   // 102 partly filled
                "CANCEL clOrdId=102\n"
                "CANCEL clOrdId=101\n"
                "CANCEL clOrdId=101\n"                                   // already cancelled
                "CANCEL clOrdId=103\n"),                                 // already filled
             std::string("ADD seq=1 orderId=1 side=Buy px=9900 qty=30\n"
                         "ADD seq=2 orderId=2 side=Sell px=10100 qty=10\n"
                         "EXEC seq=3 orderId=2 qty=4 px=10100 tradeId=1\n"
                         "DELETE seq=4 orderId=2\n"
                         "DELETE seq=5 orderId=1\n"));
}

TEST_CASE("md: REPLACE keeping priority -> MODIFY in place; it still fills first") {
    CHECK_EQ(md("NEW clOrdId=101 side=Sell type=Limit px=10100 qty=50\n"
                "NEW clOrdId=102 side=Sell type=Limit px=10100 qty=50\n"
                "REPLACE origClOrdId=101 clOrdId=103 px=10100 qty=40\n"
                "NEW clOrdId=104 side=Buy type=Limit px=10100 qty=40\n"),
             std::string("ADD seq=1 orderId=1 side=Sell px=10100 qty=50\n"
                         "ADD seq=2 orderId=2 side=Sell px=10100 qty=50\n"
                         "MODIFY seq=3 orderId=1 newOrderId=3 qty=40\n"
                         "EXEC seq=4 orderId=3 qty=40 px=10100 tradeId=1\n"));
}

TEST_CASE("md: REPLACE losing priority -> DELETE old, ADD new at the back") {
    CHECK_EQ(md("NEW clOrdId=101 side=Sell type=Limit px=10100 qty=10\n"
                "NEW clOrdId=102 side=Sell type=Limit px=10100 qty=10\n"
                "REPLACE origClOrdId=101 clOrdId=103 px=10100 qty=20\n"
                "NEW clOrdId=104 side=Buy type=Limit px=10100 qty=15\n"),
             std::string("ADD seq=1 orderId=1 side=Sell px=10100 qty=10\n"
                         "ADD seq=2 orderId=2 side=Sell px=10100 qty=10\n"
                         "DELETE seq=3 orderId=1\n"
                         "ADD seq=4 orderId=3 side=Sell px=10100 qty=20\n"
                         "EXEC seq=5 orderId=2 qty=10 px=10100 tradeId=1\n"
                         "EXEC seq=6 orderId=3 qty=5 px=10100 tradeId=2\n"));
}

TEST_CASE("md: REPLACE that crosses -> DELETE old, EXECs, ADD of the remainder") {
    CHECK_EQ(md("NEW clOrdId=101 side=Buy type=Limit px=9900 qty=30\n"
                "NEW clOrdId=102 side=Sell type=Limit px=10100 qty=20\n"
                "REPLACE origClOrdId=101 clOrdId=103 px=10100 qty=30\n"),
             std::string("ADD seq=1 orderId=1 side=Buy px=9900 qty=30\n"
                         "ADD seq=2 orderId=2 side=Sell px=10100 qty=20\n"
                         "DELETE seq=3 orderId=1\n"
                         "EXEC seq=4 orderId=2 qty=20 px=10100 tradeId=1\n"
                         "ADD seq=5 orderId=3 side=Buy px=10100 qty=10\n"));
}

TEST_CASE("md: REPLACE of a partly filled order keeping priority -> MODIFY qty = new remaining") {
    CHECK_EQ(md("NEW clOrdId=101 side=Buy type=Limit px=10000 qty=10\n"
                "NEW clOrdId=102 side=Sell type=Limit px=10000 qty=4\n"            // 101: 4 filled, 6 left
                "REPLACE origClOrdId=101 clOrdId=103 px=10000 qty=8\n"             // total 8 -> 4 left
                "REPLACE origClOrdId=101 clOrdId=104 px=10000 qty=8\n"),           // stale: rejected, nothing
             std::string("ADD seq=1 orderId=1 side=Buy px=10000 qty=10\n"
                         "EXEC seq=2 orderId=1 qty=4 px=10000 tradeId=1\n"
                         "MODIFY seq=3 orderId=1 newOrderId=3 qty=4\n"));
}

TEST_CASE("md: process -> appends to marketData; 2-argument process gives the same reports") {
    Engine withMd;
    Engine withoutMd;
    std::string outA, outB, marketData = "earlier\n";
    const char* lines[] = {"NEW clOrdId=101 side=Sell type=Limit px=10100 qty=10",
                           "NEW clOrdId=102 side=Buy type=Limit px=10100 qty=4"};
    for (const char* line : lines) {
        CHECK(withMd.process(line, outA, marketData));
        CHECK(withoutMd.process(line, outB));
    }
    CHECK_EQ(outA, outB);  // publishing market data never changes the execution reports
    CHECK_EQ(marketData, std::string("earlier\n"
                                     "ADD seq=1 orderId=1 side=Sell px=10100 qty=10\n"
                                     "EXEC seq=2 orderId=1 qty=4 px=10100 tradeId=1\n"));
}

// ===========================================================================
// Whole sessions
// ===========================================================================

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) FAIL("can't open " + path + " (run tests from the repo root: make test)");
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

TEST_CASE("md: golden file -> exact market data for golden_basic.in") {
    RunResult r = runLog(readFile("tests/data/golden_basic.in"));
    CHECK(!r.bug);
    CHECK_EQ(r.output, readFile("tests/data/golden_basic.out"));  // reports unchanged
    CHECK_EQ(r.marketData, readFile("tests/data/golden_basic.md"));
}

// Split "KIND k=v k=v" into its kind and fields.
struct MdLine {
    std::string kind;
    std::map<std::string, std::string> f;
    uint64_t num(const std::string& key) const {
        auto it = f.find(key);
        if (it == f.end()) FAIL("market-data line has no " + key);
        return std::stoull(it->second);
    }
};

static MdLine parseMd(const std::string& line) {
    MdLine m;
    std::istringstream ss(line);
    ss >> m.kind;
    std::string token;
    while (ss >> token) {
        std::size_t eq = token.find('=');
        if (eq == std::string::npos) FAIL("bad market-data token: " + token);
        m.f[token.substr(0, eq)] = token.substr(eq + 1);
    }
    return m;
}

// A feed handler in miniature: rebuilds the L3 book from market data alone and
// checks every line is possible (ADD of a new ID, EXEC of the FRONT order of
// the best level at its own price, MODIFY in place, DELETE of a known order),
// that seq has no gaps, and that the book is never crossed.
struct MdBook {
    struct Entry { bool buy; int64_t px; uint64_t qty; };
    std::unordered_map<uint64_t, Entry> orders;
    std::map<int64_t, std::list<uint64_t>> bids, asks;  // FIFO queue per price
    uint64_t expectedSeq = 1;

    std::list<uint64_t>& queue(const Entry& e) { return e.buy ? bids[e.px] : asks[e.px]; }
    void dropIfEmpty(const Entry& e) {
        auto& side = e.buy ? bids : asks;
        if (side[e.px].empty()) side.erase(e.px);
    }

    void apply(const std::string& line) {
        MdLine m = parseMd(line);
        CHECK_EQ(m.num("seq"), expectedSeq);
        ++expectedSeq;
        uint64_t id = m.num("orderId");
        if (m.kind == "ADD") {
            REQUIRE(orders.count(id) == 0);
            Entry e{m.f.at("side") == "Buy", static_cast<int64_t>(std::stoll(m.f.at("px"))), m.num("qty")};
            CHECK(e.qty > 0u);
            orders[id] = e;
            queue(e).push_back(id);
        } else if (m.kind == "EXEC") {
            REQUIRE(orders.count(id) == 1);
            Entry& e = orders[id];
            CHECK_EQ(static_cast<int64_t>(std::stoll(m.f.at("px"))), e.px);
            // matching always takes the front of the best level on that side
            int64_t best = e.buy ? bids.rbegin()->first : asks.begin()->first;
            CHECK_EQ(e.px, best);
            CHECK_EQ(queue(e).front(), id);
            uint64_t qty = m.num("qty");
            REQUIRE(qty > 0u && qty <= e.qty);
            e.qty -= qty;
            if (e.qty == 0) {  // full execution implies removal
                queue(e).pop_front();
                dropIfEmpty(e);
                orders.erase(id);
            }
        } else if (m.kind == "MODIFY") {
            REQUIRE(orders.count(id) == 1);
            uint64_t newId = m.num("newOrderId");
            REQUIRE(orders.count(newId) == 0);
            Entry e = orders[id];
            e.qty = m.num("qty");
            CHECK(e.qty > 0u);
            for (uint64_t& q : queue(e)) if (q == id) q = newId;  // same place in the queue
            orders.erase(id);
            orders[newId] = e;
        } else if (m.kind == "DELETE") {
            REQUIRE(orders.count(id) == 1);
            Entry e = orders[id];
            queue(e).remove(id);
            dropIfEmpty(e);
            orders.erase(id);
        } else {
            FAIL("unknown market-data line: " + line);
        }
    }

    void checkNotCrossed() const {
        if (!bids.empty() && !asks.empty()) CHECK(bids.rbegin()->first < asks.begin()->first);
    }
};

TEST_CASE("md: generated log -> a handler rebuilding the book from market data alone agrees with the engine") {
    std::string log = generatedLog(5000);
    Engine engine;
    MdBook book;
    std::string out;
    std::unordered_map<std::string, uint64_t> orderIdOf;  // client ID -> internal ID (from ACCEPT lines)
    std::unordered_map<std::string, MdLine> lastState;    // client ID -> its last STATE line

    std::istringstream lines(log);
    std::string line;
    uint64_t mdLines = 0;
    while (std::getline(lines, line)) {
        std::string reports, marketData;
        REQUIRE(engine.process(line, reports, marketData));

        std::istringstream mds(marketData);
        std::string mdLine;
        while (std::getline(mds, mdLine)) {
            book.apply(mdLine);
            ++mdLines;
        }
        book.checkNotCrossed();  // after every request

        std::istringstream reps(reports);
        std::string rep;
        while (std::getline(reps, rep)) {
            MdLine r = parseMd(rep);  // same KIND k=v shape
            if (r.kind == "ACCEPT") orderIdOf[r.f.at("clOrdId")] = r.num("orderId");
            if (r.kind == "STATE") lastState[r.f.at("clOrdId")] = r;
        }
    }
    CHECK(mdLines > 1000u);  // the log really exercised the feed

    // Every order the reports say is live must be in the rebuilt book with the
    // same remaining quantity, and nothing else may be.
    std::size_t live = 0;
    for (const auto& [clOrdId, state] : lastState) {
        const std::string& s = state.f.at("state");
        bool isLive = (s == "New" || s == "PartiallyFilled");
        REQUIRE(orderIdOf.count(clOrdId) == 1);
        uint64_t id = orderIdOf[clOrdId];
        if (isLive) {
            ++live;
            REQUIRE(book.orders.count(id) == 1);
            CHECK_EQ(book.orders[id].qty, state.num("remaining"));
        } else {
            CHECK(book.orders.count(id) == 0);
        }
    }
    CHECK_EQ(book.orders.size(), live);
}

TEST_CASE("md: determinism -> same log through two fresh engines, identical market data") {
    std::string log = generatedLog(5000);
    RunResult a = runLog(log);
    RunResult b = runLog(log);
    REQUIRE(!a.bug);
    REQUIRE(!b.bug);
    CHECK(!a.marketData.empty());
    CHECK(a.marketData == b.marketData);
}
