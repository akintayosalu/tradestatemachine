// Engine tests. Naming: "engine: ..." so `make test FILTER=engine:` runs these.
//   "engine: parse"        parseRequest only
//   "engine: ..."          whole requests through runLog, exact output
//   "engine: determinism"  golden file + run-twice
// Formats: docs/io_format.md. Expected outputs are written by hand from it.
// Client IDs are 101, 102, ... so they never equal internal IDs (1, 2, ...):
// mixing the two up shows in the output.
#include <fstream>
#include <sstream>
#include <string>

#include <test_framework.hpp>
#include <engine/engine.hpp>
#include <engine/print.hpp>
#include <log_generator.hpp>

// Run a log through a fresh engine; it must not hit a Bug.
static std::string run(std::string_view input) {
    RunResult r = runLog(input);
    CHECK(!r.bug);
    return r.output;
}

// ===========================================================================
// parseRequest
// ===========================================================================

TEST_CASE("engine: parse -> NEW limit, every field") {
    std::optional<Request> r = parseRequest("NEW clOrdId=100 side=Sell type=Limit px=10100 qty=50");
    REQUIRE(r.has_value());
    CHECK(r->type == RequestType::New);
    CHECK_EQ(r->clOrdId, 100u);
    CHECK_EQ(r->side, OrderSide::Sell);
    CHECK_EQ(r->orderType, OrderType::Limit);
    CHECK_EQ(r->price, 10100);
    CHECK_EQ(r->qty, 50u);
}

TEST_CASE("engine: parse -> fields in any order, extra spaces") {
    std::optional<Request> r = parseRequest("NEW   qty=7  px=9900 type=Limit   side=Buy clOrdId=5");
    REQUIRE(r.has_value());
    CHECK(r->type == RequestType::New);
    CHECK_EQ(r->clOrdId, 5u);
    CHECK_EQ(r->side, OrderSide::Buy);
    CHECK_EQ(r->price, 9900);
    CHECK_EQ(r->qty, 7u);
}

TEST_CASE("engine: parse -> NEW market without px") {
    std::optional<Request> r = parseRequest("NEW clOrdId=1 side=Buy type=Market qty=10");
    REQUIRE(r.has_value());
    CHECK_EQ(r->orderType, OrderType::Market);
    CHECK_EQ(r->price, 0);
    CHECK_EQ(r->qty, 10u);
}

TEST_CASE("engine: parse -> CANCEL") {
    std::optional<Request> r = parseRequest("CANCEL clOrdId=42");
    REQUIRE(r.has_value());
    CHECK(r->type == RequestType::Cancel);
    CHECK_EQ(r->clOrdId, 42u);
}

TEST_CASE("engine: parse -> REPLACE") {
    std::optional<Request> r = parseRequest("REPLACE origClOrdId=100 clOrdId=102 px=10200 qty=80");
    REQUIRE(r.has_value());
    CHECK(r->type == RequestType::Replace);
    CHECK_EQ(r->origClOrdId, 100u);
    CHECK_EQ(r->clOrdId, 102u);
    CHECK_EQ(r->price, 10200);
    CHECK_EQ(r->qty, 80u);
}

TEST_CASE("engine: parse -> zero qty and negative px parse (validation rejects them later)") {
    std::optional<Request> r = parseRequest("NEW clOrdId=1 side=Buy type=Limit px=-5 qty=0");
    REQUIRE(r.has_value());
    CHECK_EQ(r->price, -5);
    CHECK_EQ(r->qty, 0u);
}

TEST_CASE("engine: parse -> malformed lines are refused") {
    const char* bad[] = {
        "",                                                         // empty
        "new clOrdId=1 side=Buy type=Limit px=1 qty=1",             // request type is case-sensitive
        "BUY clOrdId=1 side=Buy type=Limit px=1 qty=1",             // unknown request type
        "NEW clOrdId=1 side=Buy type=Limit px=1",                   // missing qty
        "NEW clOrdId=1 side=Buy type=Limit qty=1",                  // limit without px
        "NEW side=Buy type=Limit px=1 qty=1",                       // missing clOrdId
        "NEW clOrdId=1 side=Buy type=Limit px=1 qty=1 color=red",   // unknown key
        "NEW clOrdId=1 clOrdId=2 side=Buy type=Limit px=1 qty=1",   // same key twice
        "NEW clOrdId=1 side=buy type=Limit px=1 qty=1",             // side is case-sensitive
        "NEW clOrdId=1 side=Buy type=Stop px=1 qty=1",              // unknown order type
        "NEW clOrdId=1 side=Buy type=Limit px=1 qty=abc",           // not a number
        "NEW clOrdId=1 side=Buy type=Limit px=1 qty=10x",           // trailing junk
        "NEW clOrdId=1 side=Buy type=Limit px=1 qty=-5",            // qty is unsigned
        "NEW clOrdId=1 side=Buy type=Limit px=1 qty=",              // empty value
        "NEW clOrdId=1 side=Buy type=Limit px=1 qty",               // token without '='
        "CANCEL",                                                   // missing clOrdId
        "REPLACE clOrdId=2 px=1 qty=1",                             // missing origClOrdId
        "REPLACE origClOrdId=1 clOrdId=2 qty=1",                    // missing px
    };
    for (const char* line : bad) {
        if (parseRequest(line).has_value()) {
            FAIL(std::string("parsed but should be malformed: \"") + line + "\"");
        }
    }
}

// ===========================================================================
// NEW
// ===========================================================================

TEST_CASE("engine: NEW limit, empty book -> ACCEPT then STATE New") {
    CHECK_EQ(run("NEW clOrdId=101 side=Buy type=Limit px=10000 qty=50\n"),
             std::string("ACCEPT request=New clOrdId=101 orderId=1\n"
                         "STATE clOrdId=101 state=New filled=0 remaining=50\n"));
}

TEST_CASE("engine: NEW rejects -> invalid, duplicate; a rejected NEW still uses an internal ID") {
    CHECK_EQ(run("NEW clOrdId=101 side=Buy type=Limit px=0 qty=50\n"       // invalid price, stored as Rejected (ID 1)
                 "NEW clOrdId=101 side=Buy type=Limit px=10000 qty=50\n"   // 101 already used
                 "NEW clOrdId=102 side=Buy type=Limit px=10000 qty=0\n"    // invalid qty (ID 2)
                 "NEW clOrdId=103 side=Buy type=Limit px=10000 qty=5\n"),  // ID 3
             std::string("REJECT request=New clOrdId=101 reason=InvalidPrice\n"
                         "REJECT request=New clOrdId=101 reason=DuplicateOrderId\n"
                         "REJECT request=New clOrdId=102 reason=InvalidQuantity\n"
                         "ACCEPT request=New clOrdId=103 orderId=3\n"
                         "STATE clOrdId=103 state=New filled=0 remaining=5\n"));
}

TEST_CASE("engine: NEW that crosses -> TRADE + resting STATE per trade, incoming STATE last") {
    CHECK_EQ(run("NEW clOrdId=101 side=Sell type=Limit px=10100 qty=30\n"
                 "NEW clOrdId=102 side=Sell type=Limit px=10100 qty=50\n"
                 "NEW clOrdId=103 side=Buy type=Limit px=10200 qty=60\n"),
             std::string("ACCEPT request=New clOrdId=101 orderId=1\n"
                         "STATE clOrdId=101 state=New filled=0 remaining=30\n"
                         "ACCEPT request=New clOrdId=102 orderId=2\n"
                         "STATE clOrdId=102 state=New filled=0 remaining=50\n"
                         "ACCEPT request=New clOrdId=103 orderId=3\n"
                         "TRADE tradeId=1 aggressor=103 resting=101 px=10100 qty=30\n"
                         "STATE clOrdId=101 state=Filled filled=30 remaining=0\n"
                         "TRADE tradeId=2 aggressor=103 resting=102 px=10100 qty=30\n"
                         "STATE clOrdId=102 state=PartiallyFilled filled=30 remaining=20\n"
                         "STATE clOrdId=103 state=Filled filled=60 remaining=0\n"));
}

TEST_CASE("engine: NEW market -> fills what's there, rest Cancelled; into empty side -> Cancelled") {
    CHECK_EQ(run("NEW clOrdId=101 side=Sell type=Limit px=10100 qty=10\n"
                 "NEW clOrdId=102 side=Buy type=Market qty=25\n"
                 "NEW clOrdId=103 side=Sell type=Market qty=5\n"),
             std::string("ACCEPT request=New clOrdId=101 orderId=1\n"
                         "STATE clOrdId=101 state=New filled=0 remaining=10\n"
                         "ACCEPT request=New clOrdId=102 orderId=2\n"
                         "TRADE tradeId=1 aggressor=102 resting=101 px=10100 qty=10\n"
                         "STATE clOrdId=101 state=Filled filled=10 remaining=0\n"
                         "STATE clOrdId=102 state=Cancelled filled=10 remaining=0\n"
                         "ACCEPT request=New clOrdId=103 orderId=3\n"
                         "STATE clOrdId=103 state=Cancelled filled=0 remaining=0\n"));
}

// ===========================================================================
// CANCEL
// ===========================================================================

TEST_CASE("engine: CANCEL -> Cancelled and out of the book; again, unknown -> REJECT") {
    CHECK_EQ(run("NEW clOrdId=101 side=Buy type=Limit px=10000 qty=10\n"
                 "CANCEL clOrdId=101\n"
                 "NEW clOrdId=102 side=Sell type=Limit px=10000 qty=10\n"  // would have crossed 101
                 "CANCEL clOrdId=101\n"
                 "CANCEL clOrdId=999\n"),
             std::string("ACCEPT request=New clOrdId=101 orderId=1\n"
                         "STATE clOrdId=101 state=New filled=0 remaining=10\n"
                         "STATE clOrdId=101 state=Cancelled filled=0 remaining=0\n"
                         "ACCEPT request=New clOrdId=102 orderId=2\n"
                         "STATE clOrdId=102 state=New filled=0 remaining=10\n"
                         "REJECT request=Cancel clOrdId=101 reason=AlreadyCancelled\n"
                         "REJECT request=Cancel clOrdId=999 reason=UnknownOrder\n"));
}

TEST_CASE("engine: CANCEL too late -> AlreadyFilled / AlreadyRejected; partly filled keeps filled") {
    CHECK_EQ(run("NEW clOrdId=101 side=Sell type=Limit px=10000 qty=10\n"
                 "NEW clOrdId=102 side=Buy type=Limit px=10000 qty=4\n"
                 "CANCEL clOrdId=102\n"
                 "CANCEL clOrdId=101\n"
                 "NEW clOrdId=103 side=Buy type=Limit px=-5 qty=1\n"
                 "CANCEL clOrdId=103\n"),
             std::string("ACCEPT request=New clOrdId=101 orderId=1\n"
                         "STATE clOrdId=101 state=New filled=0 remaining=10\n"
                         "ACCEPT request=New clOrdId=102 orderId=2\n"
                         "TRADE tradeId=1 aggressor=102 resting=101 px=10000 qty=4\n"
                         "STATE clOrdId=101 state=PartiallyFilled filled=4 remaining=6\n"
                         "STATE clOrdId=102 state=Filled filled=4 remaining=0\n"
                         "REJECT request=Cancel clOrdId=102 reason=AlreadyFilled\n"
                         "STATE clOrdId=101 state=Cancelled filled=4 remaining=0\n"
                         "REJECT request=New clOrdId=103 reason=InvalidPrice\n"
                         "REJECT request=Cancel clOrdId=103 reason=AlreadyRejected\n"));
}

// ===========================================================================
// REPLACE
// ===========================================================================

TEST_CASE("engine: REPLACE keeping priority -> keeps its place, fills first") {
    CHECK_EQ(run("NEW clOrdId=101 side=Sell type=Limit px=10100 qty=50\n"
                 "NEW clOrdId=102 side=Sell type=Limit px=10100 qty=50\n"
                 "REPLACE origClOrdId=101 clOrdId=103 px=10100 qty=40\n"   // same px, smaller: keeps priority
                 "NEW clOrdId=104 side=Buy type=Limit px=10100 qty=40\n"),
             std::string("ACCEPT request=New clOrdId=101 orderId=1\n"
                         "STATE clOrdId=101 state=New filled=0 remaining=50\n"
                         "ACCEPT request=New clOrdId=102 orderId=2\n"
                         "STATE clOrdId=102 state=New filled=0 remaining=50\n"
                         "ACCEPT request=Replace clOrdId=103 origClOrdId=101 orderId=3\n"
                         "STATE clOrdId=101 state=Replaced filled=0 remaining=0\n"
                         "STATE clOrdId=103 state=New filled=0 remaining=40\n"
                         "ACCEPT request=New clOrdId=104 orderId=4\n"
                         "TRADE tradeId=1 aggressor=104 resting=103 px=10100 qty=40\n"
                         "STATE clOrdId=103 state=Filled filled=40 remaining=0\n"
                         "STATE clOrdId=104 state=Filled filled=40 remaining=0\n"));
}

TEST_CASE("engine: REPLACE losing priority -> back of the queue") {
    CHECK_EQ(run("NEW clOrdId=101 side=Sell type=Limit px=10100 qty=10\n"
                 "NEW clOrdId=102 side=Sell type=Limit px=10100 qty=10\n"
                 "REPLACE origClOrdId=101 clOrdId=103 px=10100 qty=20\n"   // bigger: loses priority
                 "NEW clOrdId=104 side=Buy type=Limit px=10100 qty=15\n"),
             std::string("ACCEPT request=New clOrdId=101 orderId=1\n"
                         "STATE clOrdId=101 state=New filled=0 remaining=10\n"
                         "ACCEPT request=New clOrdId=102 orderId=2\n"
                         "STATE clOrdId=102 state=New filled=0 remaining=10\n"
                         "ACCEPT request=Replace clOrdId=103 origClOrdId=101 orderId=3\n"
                         "STATE clOrdId=101 state=Replaced filled=0 remaining=0\n"
                         "STATE clOrdId=103 state=New filled=0 remaining=20\n"
                         "ACCEPT request=New clOrdId=104 orderId=4\n"
                         "TRADE tradeId=1 aggressor=104 resting=102 px=10100 qty=10\n"
                         "STATE clOrdId=102 state=Filled filled=10 remaining=0\n"
                         "TRADE tradeId=2 aggressor=104 resting=103 px=10100 qty=5\n"
                         "STATE clOrdId=103 state=PartiallyFilled filled=5 remaining=15\n"
                         "STATE clOrdId=104 state=Filled filled=15 remaining=0\n"));
}

TEST_CASE("engine: REPLACE that crosses -> matches like a NEW") {
    CHECK_EQ(run("NEW clOrdId=101 side=Buy type=Limit px=9900 qty=30\n"
                 "NEW clOrdId=102 side=Sell type=Limit px=10100 qty=20\n"
                 "REPLACE origClOrdId=101 clOrdId=103 px=10100 qty=30\n"),
             std::string("ACCEPT request=New clOrdId=101 orderId=1\n"
                         "STATE clOrdId=101 state=New filled=0 remaining=30\n"
                         "ACCEPT request=New clOrdId=102 orderId=2\n"
                         "STATE clOrdId=102 state=New filled=0 remaining=20\n"
                         "ACCEPT request=Replace clOrdId=103 origClOrdId=101 orderId=3\n"
                         "STATE clOrdId=101 state=Replaced filled=0 remaining=0\n"
                         "TRADE tradeId=1 aggressor=103 resting=102 px=10100 qty=20\n"
                         "STATE clOrdId=102 state=Filled filled=20 remaining=0\n"
                         "STATE clOrdId=103 state=PartiallyFilled filled=20 remaining=10\n"));
}

TEST_CASE("engine: REPLACE rejects, in check order; rejected replaces use no internal ID") {
    CHECK_EQ(run("NEW clOrdId=101 side=Buy type=Limit px=10000 qty=10\n"
                 "NEW clOrdId=102 side=Sell type=Limit px=10000 qty=4\n"            // 101 now 4 filled
                 "REPLACE origClOrdId=101 clOrdId=102 px=10000 qty=20\n"            // 102 already used
                 "REPLACE origClOrdId=999 clOrdId=103 px=10000 qty=20\n"            // unknown original
                 "REPLACE origClOrdId=101 clOrdId=104 px=10000 qty=4\n"             // qty not > filled
                 "REPLACE origClOrdId=102 clOrdId=105 px=10000 qty=10\n"            // 102 is Filled
                 "REPLACE origClOrdId=101 clOrdId=106 px=10000 qty=8\n"             // OK, keeps priority (ID 3)
                 "REPLACE origClOrdId=101 clOrdId=107 px=10000 qty=8\n"             // 101 is Replaced now
                 "CANCEL clOrdId=101\n"),
             std::string("ACCEPT request=New clOrdId=101 orderId=1\n"
                         "STATE clOrdId=101 state=New filled=0 remaining=10\n"
                         "ACCEPT request=New clOrdId=102 orderId=2\n"
                         "TRADE tradeId=1 aggressor=102 resting=101 px=10000 qty=4\n"
                         "STATE clOrdId=101 state=PartiallyFilled filled=4 remaining=6\n"
                         "STATE clOrdId=102 state=Filled filled=4 remaining=0\n"
                         "REJECT request=Replace clOrdId=102 origClOrdId=101 reason=DuplicateOrderId\n"
                         "REJECT request=Replace clOrdId=103 origClOrdId=999 reason=UnknownOrder\n"
                         "REJECT request=Replace clOrdId=104 origClOrdId=101 reason=InvalidReplace\n"
                         "REJECT request=Replace clOrdId=105 origClOrdId=102 reason=AlreadyFilled\n"
                         "ACCEPT request=Replace clOrdId=106 origClOrdId=101 orderId=3\n"
                         "STATE clOrdId=101 state=Replaced filled=4 remaining=0\n"
                         "STATE clOrdId=106 state=PartiallyFilled filled=4 remaining=4\n"
                         "REJECT request=Replace clOrdId=107 origClOrdId=101 reason=AlreadyReplaced\n"
                         "REJECT request=Cancel clOrdId=101 reason=AlreadyReplaced\n"));
}

// ===========================================================================
// Lines: malformed, comments, blank, last line without '\n'
// ===========================================================================

TEST_CASE("engine: malformed -> MALFORMED with line number, carries on; # and blank skipped but counted") {
    CHECK_EQ(run("# a comment\n"
                 "\n"
                 "NEW clOrdId=101 side=Buy type=Limit px=10000 qty=5\n"
                 "NEW clOrdId=102 side=buy type=Limit px=10000 qty=5\n"   // line 4
                 "FOO x=1\n"                                              // line 5
                 "CANCEL clOrdId=101\n"),
             std::string("ACCEPT request=New clOrdId=101 orderId=1\n"
                         "STATE clOrdId=101 state=New filled=0 remaining=5\n"
                         "MALFORMED line=4\n"
                         "MALFORMED line=5\n"
                         "STATE clOrdId=101 state=Cancelled filled=0 remaining=0\n"));
}

TEST_CASE("engine: runLog -> last line without newline still runs; empty input -> no output") {
    CHECK_EQ(run("NEW clOrdId=101 side=Buy type=Limit px=10000 qty=5"),
             std::string("ACCEPT request=New clOrdId=101 orderId=1\n"
                         "STATE clOrdId=101 state=New filled=0 remaining=5\n"));
    RunResult empty = runLog("");
    CHECK(!empty.bug);
    CHECK_EQ(empty.output, std::string(""));
}

TEST_CASE("engine: process -> appends to out, counts lines, returns true") {
    Engine e;
    std::string out = "earlier\n";
    CHECK(e.process("CANCEL clOrdId=101", out));
    CHECK(e.process("garbage", out));
    CHECK_EQ(out, std::string("earlier\n"
                              "REJECT request=Cancel clOrdId=101 reason=UnknownOrder\n"
                              "MALFORMED line=2\n"));
}

// ===========================================================================
// Determinism
// ===========================================================================

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) FAIL("can't open " + path + " (run tests from the repo root: make test)");
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

TEST_CASE("engine: determinism -> golden file: exact expected output") {
    std::string input = readFile("tests/data/golden_basic.in");
    std::string expected = readFile("tests/data/golden_basic.out");
    RunResult r = runLog(input);
    CHECK(!r.bug);
    CHECK_EQ(r.output, expected);
}

TEST_CASE("engine: determinism -> same log through two fresh engines, byte-identical output") {
    std::string log = generatedLog(5000);
    RunResult a = runLog(log);
    RunResult b = runLog(log);
    REQUIRE(!a.bug);
    REQUIRE(!b.bug);
    CHECK(a.output == b.output);  // not CHECK_EQ: no point printing two huge strings

    // and the log really exercised the engine
    CHECK(a.output.find("TRADE ") != std::string::npos);
    CHECK(a.output.find("state=Cancelled") != std::string::npos);
    CHECK(a.output.find("ACCEPT request=Replace") != std::string::npos);
    CHECK(a.output.find("REJECT request=Cancel") != std::string::npos);
    CHECK(a.output.find("MALFORMED") != std::string::npos);
}
