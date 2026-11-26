// A long, mixed request log from a fixed-seed generator, shared by the engine
// and market-data tests. Same text on every run and every platform.
#pragma once

#include <cstdint>
#include <string>

// A long mixed log from a fixed-seed generator (same text every run): limit
// and market NEWs around one price, CANCELs and REPLACEs of earlier (and some
// unknown) client IDs, and the odd invalid or malformed line.
inline std::string generatedLog(int lines) {
    uint64_t state = 12345;
    auto next = [&state](uint64_t n) {  // fixed LCG: deterministic on every platform
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return (state >> 33) % n;
    };
    std::string log;
    uint64_t clOrdId = 1000;
    for (int i = 0; i < lines; ++i) {
        uint64_t kind = next(100);
        std::string side = next(2) ? "Buy" : "Sell";
        int64_t px = 10000 + static_cast<int64_t>(next(21)) - 10;
        uint64_t qty = 1 + next(100);
        uint64_t earlier = clOrdId > 1000 ? 1000 + next(clOrdId - 1000 + 5) : 999;  // sometimes unknown
        if (kind < 55) {
            log += "NEW clOrdId=" + std::to_string(++clOrdId) + " side=" + side +
                   " type=Limit px=" + std::to_string(px) + " qty=" + std::to_string(qty) + "\n";
        } else if (kind < 65) {
            log += "NEW clOrdId=" + std::to_string(++clOrdId) + " side=" + side +
                   " type=Market qty=" + std::to_string(qty) + "\n";
        } else if (kind < 80) {
            log += "CANCEL clOrdId=" + std::to_string(earlier) + "\n";
        } else if (kind < 95) {
            log += "REPLACE origClOrdId=" + std::to_string(earlier) + " clOrdId=" + std::to_string(++clOrdId) +
                   " px=" + std::to_string(px) + " qty=" + std::to_string(qty) + "\n";
        } else if (kind < 98) {
            log += "NEW clOrdId=" + std::to_string(++clOrdId) + " side=" + side + " type=Limit px=0 qty=5\n";
        } else {
            log += "NEW clOrdId=oops\n";
        }
    }
    return log;
}

