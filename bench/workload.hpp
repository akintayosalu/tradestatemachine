// Benchmark workload: a long, realistic, REPEATABLE stream of request lines.
//
// Generated before timing starts, so generating it is never measured. The same
// seed always gives the same lines, on any machine, so two runs (or a run
// before and after an optimization) measure exactly the same work.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct WorkloadConfig {
    std::size_t requests = 1'000'000;  // number of request lines
    uint64_t seed = 42;                // same seed -> same lines

    // Request mix, in percent (must add up to 100). Real venues see far more
    // cancels than trades; cancels also keep the book from growing forever.
    int pctLimit = 45;
    int pctMarket = 5;
    int pctCancel = 35;
    int pctReplace = 15;

    // Prices: everything happens around one mid price (ticks).
    int64_t mid = 10000;
    int passiveRange = 20;  // passive orders rest 1..passiveRange ticks from mid
    int pctAggressive = 20; // % of limit orders priced to cross the spread
};

// One request line per element, in the format of docs/io_format.md.
std::vector<std::string> generateWorkload(const WorkloadConfig& config);
