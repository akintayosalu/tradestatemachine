// Turning raw latency samples into the numbers we report (benchmark part 3).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Summary of a set of latency samples, in nanoseconds.
struct LatencySummary {
    std::size_t count = 0;
    uint64_t min = 0, p50 = 0, p90 = 0, p99 = 0, p999 = 0, p9999 = 0, max = 0;
    double mean = 0;
};

// The p-th percentile of SORTED samples, by the "nearest rank" rule: the
// smallest sample that at least p% of all samples are <= to.
// Index = ceil(p/100 * n) - 1. Example: n = 1000, p = 99 -> index 989, the
// 990th smallest; 10 samples (1%) are larger.
uint64_t percentile(const std::vector<uint64_t>& sorted, double p);

// Sorts a COPY of `samples` (the caller keeps its own in request order) and
// fills a LatencySummary. Empty input gives an all-zero summary.
LatencySummary summarize(std::vector<uint64_t> samples);
