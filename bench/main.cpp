// Benchmark driver. Built and run by `make bench` (release build).
//
// Three passes over the SAME workload, each with a FRESH engine (the engine is
// deterministic, so every pass does exactly the same work):
//   1. check:      untimed; counts the output, proves the workload is sane and Bug-free
//   2. throughput: one clock reading before and after; requests per second
//   3. latency:    a clock reading around EACH request; per-request times
// Then the latency samples become percentiles (stats.hpp), overall and per
// request type, plus a look at the slowest requests.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "engine/engine.hpp"
#include "stats.hpp"
#include "workload.hpp"

namespace {

using Clock = std::chrono::steady_clock;  // monotonic: never jumps (unlike system_clock)
static_assert(Clock::is_steady);

// ---------------------------------------------------------------------------
// Pass 1: check (untimed)
// ---------------------------------------------------------------------------

// Counts of each output line kind (docs/io_format.md).
struct OutputCounts {
    uint64_t accepts = 0, rejects = 0, trades = 0, states = 0, malformed = 0, bugs = 0;
    uint64_t rejectAlreadyFilled = 0, cancelled = 0;
};

void countLines(std::string_view out, OutputCounts& c) {
    std::size_t start = 0;
    while (start < out.size()) {
        std::size_t end = out.find('\n', start);
        std::string_view line = out.substr(start, end - start);
        if (line.starts_with("ACCEPT")) ++c.accepts;
        else if (line.starts_with("REJECT")) {
            ++c.rejects;
            if (line.ends_with("reason=AlreadyFilled")) ++c.rejectAlreadyFilled;
        }
        else if (line.starts_with("TRADE")) ++c.trades;
        else if (line.starts_with("STATE")) {
            ++c.states;
            if (line.find("state=Cancelled") != std::string_view::npos) ++c.cancelled;
        }
        else if (line.starts_with("MALFORMED")) ++c.malformed;
        else if (line.starts_with("BUG")) ++c.bugs;
        start = end + 1;
    }
}

// Returns false if the engine hit a Bug (then nothing should be measured).
bool runCheck(const std::vector<std::string>& lines) {
    Engine engine;
    std::string out;
    OutputCounts counts;
    for (const std::string& line : lines) {
        out.clear();
        if (!engine.process(line, out)) {
            std::cerr << "engine hit a Bug: " << out;
            return false;
        }
        countLines(out, counts);
    }
    std::cout << "check pass (untimed):\n"
              << "  accepts    " << counts.accepts << "\n"
              << "  rejects    " << counts.rejects << " (already filled: " << counts.rejectAlreadyFilled << ")\n"
              << "  trades     " << counts.trades << "\n"
              << "  cancelled  " << counts.cancelled << " (cancels + market leftovers)\n"
              << "  malformed  " << counts.malformed << ", bugs " << counts.bugs << "\n";
    return counts.bugs == 0;
}

// ---------------------------------------------------------------------------
// Pass 2: throughput
// ---------------------------------------------------------------------------

// Runs every line; only the measured part (after warm-up) is inside the two
// clock readings. Returns the measured time in seconds.
double runThroughput(const std::vector<std::string>& lines, std::size_t warmup) {
    Engine engine;
    std::string out;
    out.reserve(4096);  // allocate the output buffer once, before timing

    for (std::size_t i = 0; i < warmup; ++i) {  // warm-up: run, don't measure
        out.clear();
        engine.process(lines[i], out);
    }

    Clock::time_point start = Clock::now();
    for (std::size_t i = warmup; i < lines.size(); ++i) {
        out.clear();
        engine.process(lines[i], out);
    }
    Clock::time_point end = Clock::now();

    return std::chrono::duration<double>(end - start).count();
}

// ---------------------------------------------------------------------------
// Pass 3: latency
// ---------------------------------------------------------------------------

// Returns one duration (nanoseconds) per measured request, in request order.
std::vector<uint64_t> runLatency(const std::vector<std::string>& lines, std::size_t warmup) {
    Engine engine;
    std::string out;
    out.reserve(4096);

    // Allocate ALL the result slots before timing: growing a vector inside the
    // timed loop would add reallocations (and copies of millions of values)
    // to some requests' measured time.
    std::vector<uint64_t> nanos(lines.size() - warmup);

    for (std::size_t i = 0; i < warmup; ++i) {
        out.clear();
        engine.process(lines[i], out);
    }

    for (std::size_t i = warmup; i < lines.size(); ++i) {
        out.clear();  // outside the timed region: not part of the engine's work
        Clock::time_point t0 = Clock::now();
        engine.process(lines[i], out);
        Clock::time_point t1 = Clock::now();
        nanos[i - warmup] = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }
    return nanos;
}

// Two facts about the clock, reported alongside the latencies:
//   costNanos:       average cost of one Clock::now() call. Each latency sample
//                    makes two calls, so it includes roughly this much extra.
//   resolutionNanos: the smallest step the clock can show. Samples are
//                    multiples of it: on Apple Silicon it ticks at 24 MHz, so
//                    ~41.7 ns, and two back-to-back reads usually differ by 0.
struct ClockInfo {
    double costNanos;
    uint64_t resolutionNanos;
};

ClockInfo measureClock() {
    constexpr int calls = 1'000'000;
    Clock::time_point start = Clock::now();
    Clock::time_point last = start;
    uint64_t smallestStep = UINT64_MAX;
    for (int i = 0; i < calls; ++i) {
        Clock::time_point t = Clock::now();
        uint64_t step = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t - last).count());
        if (step > 0) smallestStep = std::min(smallestStep, step);
        last = t;
    }
    double total = std::chrono::duration<double, std::nano>(last - start).count();
    return ClockInfo{total / calls, smallestStep};
}

// ---------------------------------------------------------------------------
// Reporting (part 3)
// ---------------------------------------------------------------------------

// The four request kinds do very different work, so they get separate
// percentiles. Decided from the request line itself.
enum Kind { LimitNew, MarketNew, Cancel, Replace, KindCount };
const char* kindName[KindCount] = {"NEW limit", "NEW market", "CANCEL", "REPLACE"};

Kind kindOf(std::string_view line) {
    if (line.starts_with("CANCEL")) return Cancel;
    if (line.starts_with("REPLACE")) return Replace;
    return line.find("type=Market") != std::string_view::npos ? MarketNew : LimitNew;
}

// One table row: label, sample count, then the percentiles in nanoseconds.
void printRow(const char* label, const LatencySummary& s) {
    std::printf("  %-11s %8zu %7llu %7llu %7llu %7llu %8llu %9llu %10llu %8.0f\n", label, s.count,
                (unsigned long long)s.min, (unsigned long long)s.p50, (unsigned long long)s.p90,
                (unsigned long long)s.p99, (unsigned long long)s.p999, (unsigned long long)s.p9999,
                (unsigned long long)s.max, s.mean);
}

void printLatencyReport(const std::vector<std::string>& lines, std::size_t warmup,
                        const std::vector<uint64_t>& nanos) {
    // Split the samples by request kind (nanos[i] belongs to lines[warmup + i]).
    std::vector<uint64_t> byKind[KindCount];
    for (std::size_t i = 0; i < nanos.size(); ++i) {
        byKind[kindOf(lines[warmup + i])].push_back(nanos[i]);
    }

    std::printf("latency per request (ns):\n");
    std::printf("  %-11s %8s %7s %7s %7s %7s %8s %9s %10s %8s\n", "", "count", "min", "p50", "p90",
                "p99", "p99.9", "p99.99", "max", "mean");
    printRow("all", summarize(nanos));
    for (int k = 0; k < KindCount; ++k) printRow(kindName[k], summarize(byKind[k]));

    // The tail, counted: how many requests were slower than each threshold.
    const uint64_t thresholds[] = {10'000, 100'000, 1'000'000};
    const char* names[] = {"10 us", "100 us", "1 ms"};
    std::printf("slow requests:\n");
    for (int t = 0; t < 3; ++t) {
        std::size_t n = std::count_if(nanos.begin(), nanos.end(), [&](uint64_t ns) { return ns > thresholds[t]; });
        std::printf("  > %-6s %7zu  (%.4f%%)\n", names[t], n, 100.0 * n / nanos.size());
    }

    // The 5 slowest, with their position in the run and the request itself.
    std::vector<std::size_t> order(nanos.size());
    std::iota(order.begin(), order.end(), 0);
    std::size_t top = std::min<std::size_t>(5, order.size());
    std::partial_sort(order.begin(), order.begin() + top, order.end(),
                      [&](std::size_t a, std::size_t b) { return nanos[a] > nanos[b]; });
    std::printf("slowest requests:\n");
    for (std::size_t i = 0; i < top; ++i) {
        std::size_t idx = order[i];
        std::printf("  %9llu ns  request #%zu  %s\n", (unsigned long long)nanos[idx], warmup + idx + 1,
                    lines[warmup + idx].c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    WorkloadConfig config;
    std::size_t warmup = 100'000;
    for (int i = 1; i + 1 < argc; i += 2) {  // optional: --requests N --seed S --warmup W
        std::string_view flag = argv[i];
        if (flag == "--requests") config.requests = std::strtoull(argv[i + 1], nullptr, 10);
        else if (flag == "--seed") config.seed = std::strtoull(argv[i + 1], nullptr, 10);
        else if (flag == "--warmup") warmup = std::strtoull(argv[i + 1], nullptr, 10);
    }
    if (warmup >= config.requests) {
        std::cerr << "--warmup must be smaller than --requests\n";
        return 2;
    }

    std::vector<std::string> lines = generateWorkload(config);
    std::size_t measured = lines.size() - warmup;
    std::cout << "workload: " << lines.size() << " requests (seed " << config.seed << "), "
              << warmup << " warm-up, " << measured << " measured\n";

    if (!runCheck(lines)) return 1;

    double seconds = runThroughput(lines, warmup);
    std::printf("throughput: %.0f requests/s  (%zu requests in %.1f ms, %.0f ns per request)\n",
                measured / seconds, measured, seconds * 1000.0, seconds * 1e9 / measured);

    std::vector<uint64_t> nanos = runLatency(lines, warmup);
    ClockInfo clock = measureClock();
    printLatencyReport(lines, warmup, nanos);
    std::printf("clock: one now() call costs ~%.1f ns (each sample includes ~2 of them), "
                "resolution %llu ns\n", clock.costNanos, (unsigned long long)clock.resolutionNanos);
    return 0;
}
