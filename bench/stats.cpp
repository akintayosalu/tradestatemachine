#include "stats.hpp"

#include <algorithm>
#include <cmath>

uint64_t percentile(const std::vector<uint64_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    double rank = std::ceil(p / 100.0 * static_cast<double>(sorted.size()));  // 1-based
    std::size_t index = rank < 1 ? 0 : static_cast<std::size_t>(rank) - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

// `samples` is taken BY VALUE on purpose: it's our own copy to sort.
LatencySummary summarize(std::vector<uint64_t> samples) {
    LatencySummary s;
    if (samples.empty()) return s;
    std::sort(samples.begin(), samples.end());

    s.count = samples.size();
    s.min = samples.front();
    s.max = samples.back();
    s.p50 = percentile(samples, 50);
    s.p90 = percentile(samples, 90);
    s.p99 = percentile(samples, 99);
    s.p999 = percentile(samples, 99.9);
    s.p9999 = percentile(samples, 99.99);

    long double total = 0;
    for (uint64_t ns : samples) total += ns;
    s.mean = static_cast<double>(total / samples.size());
    return s;
}
