// The engine loop: text requests in, text events out, deterministically.
// design.md section 9 (decisions), docs/io_format.md (exact formats).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <engine/book.hpp>
#include <engine/matching.hpp>
#include <engine/store.hpp>
#include <engine/types.hpp>

enum class RequestType {
    New,
    Cancel,
    Replace,
};

// One parsed input line. Fields a request type doesn't use keep their defaults.
struct Request {
    RequestType type = RequestType::New;
    uint64_t clOrdId = 0;      // NEW: the new order. CANCEL: the order to cancel. REPLACE: the new version.
    uint64_t origClOrdId = 0;  // REPLACE only: the version being replaced
    OrderSide side = OrderSide::Buy;        // NEW only
    OrderType orderType = OrderType::Limit; // NEW only
    int64_t price = 0;         // NEW (limit) and REPLACE; 0 if absent (market NEW)
    uint64_t qty = 0;          // NEW and REPLACE
};

// Parse one request line (docs/io_format.md, "Input").
// Empty if the line is malformed. Blank and '#' lines are the engine's job to
// skip; passed here they count as malformed.
std::optional<Request> parseRequest(std::string_view line);

class Engine {
public:
    // Process one input line and APPEND its output lines (each ending in '\n')
    // to `out`. Blank and '#' lines write nothing. Every call counts as one
    // line for MALFORMED line numbers, starting at 1.
    // Returns false if a Bug was hit: `out` then ends with the BUG line, and
    // the caller must stop (and, in the program, abort).
    bool process(std::string_view line, std::string& out);

private:
    // Your data members go here (design.md section 9):
    //   - the book and the store
    //   - counters: next internal order ID, next seq, next trade ID (all from 1)
    //   - the current line number (for MALFORMED)
    OrderBook book;
    OrderStore store;
    uint64_t nextOrderId = 1;
    uint64_t nextSeq = 1;
    uint64_t nextTradeId = 1;
    uint64_t lineNumber = 0;  // incremented at the start of process: first line is 1

    // One handler per request type; each returns false on a Bug.
    bool onNew(const Request& request, std::string& out);
    bool onCancel(const Request& request, std::string& out);
    bool onReplace(const Request& request, std::string& out);

    // Match a stored live order and write the result: for each trade, TRADE
    // then STATE of the resting order; then STATE of `orderId`. On a Bug:
    // the trades so far, then the BUG line, and returns false. Shared by NEW
    // and by a REPLACE that loses priority.
    bool matchAndReport(uint64_t orderId, std::string& out);
};

struct RunResult {
    std::string output;  // everything written, in order
    bool bug = false;    // true = stopped at a Bug (output ends with the BUG line)
};

// Run a whole input text (lines separated by '\n'; a final line without '\n'
// still counts) through a FRESH engine, stopping at the first Bug.
RunResult runLog(std::string_view input);
