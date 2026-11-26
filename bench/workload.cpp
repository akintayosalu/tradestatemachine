#include <workload.hpp>

#include <random>

namespace {

// Random numbers that are the same on every platform.
// std::mt19937_64 is fully specified by the C++ standard, so a seed gives the
// same sequence everywhere. std::uniform_int_distribution is NOT (each
// standard library may implement it differently), so we don't use it: a plain
// modulo has a tiny bias, which doesn't matter for a workload.
class Rng {
public:
    explicit Rng(uint64_t seed) : engine(seed) {}
    uint64_t below(uint64_t n) { return engine() % n; }  // 0 .. n-1
    bool percent(int pct) { return below(100) < static_cast<uint64_t>(pct); }

private:
    std::mt19937_64 engine;
};

// What the generator remembers about a limit order it sent, so later
// cancels and replaces can name a real order. It can't know which orders the
// engine has since filled: those get "already filled" rejects, which also
// happens on real venues (a cancel racing a fill).
struct SentOrder {
    uint64_t clOrdId;
    bool buy;
    int64_t price;
};

}  // namespace

std::vector<std::string> generateWorkload(const WorkloadConfig& c) {
    Rng rng(c.seed);
    std::vector<std::string> lines;
    lines.reserve(c.requests);

    std::vector<SentOrder> live;  // limit orders we believe are still resting
    uint64_t nextClOrdId = 1;

    // A limit price: usually passive (rests behind the spread), sometimes
    // aggressive (priced through the other side, so it trades).
    auto limitPrice = [&](bool buy) -> int64_t {
        if (rng.percent(c.pctAggressive)) {
            int64_t through = 1 + static_cast<int64_t>(rng.below(5));
            return buy ? c.mid + through : c.mid - through;
        }
        int64_t away = 1 + static_cast<int64_t>(rng.below(c.passiveRange));
        return buy ? c.mid - away : c.mid + away;
    };

    auto newLimit = [&]() {
        bool buy = rng.percent(50);
        int64_t px = limitPrice(buy);
        uint64_t qty = 1 + rng.below(100);
        uint64_t id = nextClOrdId++;
        lines.push_back("NEW clOrdId=" + std::to_string(id) + (buy ? " side=Buy" : " side=Sell") +
                        " type=Limit px=" + std::to_string(px) + " qty=" + std::to_string(qty));
        live.push_back({id, buy, px});
    };

    // Pick a remembered order at random and take it out of `live` in O(1):
    // swap it with the last element, then pop the back.
    auto takeRandomLive = [&]() -> SentOrder {
        std::size_t i = rng.below(live.size());
        SentOrder picked = live[i];
        live[i] = live.back();
        live.pop_back();
        return picked;
    };

    for (std::size_t n = 0; n < c.requests; ++n) {
        uint64_t roll = rng.below(100);
        int limitEnd = c.pctLimit;
        int marketEnd = limitEnd + c.pctMarket;
        int cancelEnd = marketEnd + c.pctCancel;

        if (roll < static_cast<uint64_t>(limitEnd) || live.empty()) {
            newLimit();  // also the fallback when there's nothing to cancel/replace yet
        } else if (roll < static_cast<uint64_t>(marketEnd)) {
            bool buy = rng.percent(50);
            uint64_t qty = 1 + rng.below(50);
            lines.push_back("NEW clOrdId=" + std::to_string(nextClOrdId++) + (buy ? " side=Buy" : " side=Sell") +
                            " type=Market qty=" + std::to_string(qty));
        } else if (roll < static_cast<uint64_t>(cancelEnd)) {
            SentOrder o = takeRandomLive();
            lines.push_back("CANCEL clOrdId=" + std::to_string(o.clOrdId));
        } else {
            // Replace: nudge the price by up to 2 ticks either way, new quantity.
            SentOrder o = takeRandomLive();
            int64_t px = o.price + static_cast<int64_t>(rng.below(5)) - 2;
            if (px <= 0) px = 1;
            uint64_t qty = 1 + rng.below(100);
            uint64_t id = nextClOrdId++;
            lines.push_back("REPLACE origClOrdId=" + std::to_string(o.clOrdId) + " clOrdId=" + std::to_string(id) +
                            " px=" + std::to_string(px) + " qty=" + std::to_string(qty));
            live.push_back({id, o.buy, px});  // the new version is the one to cancel later
        }
    }
    return lines;
}
