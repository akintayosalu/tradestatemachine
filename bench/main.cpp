// Benchmark driver. Built and run by `make bench` (release build).
//
// Three passes over the SAME workload, each with a FRESH engine (the engine is
// deterministic, so every pass does exactly the same work):
//   1. check:      untimed; counts the output, proves the workload is sane and Bug-free
//   2. throughput: one clock reading before and after; requests per second
//   3. latency:    a clock reading around EACH request; per-request times
// The two timed passes are repeated --runs times (default 5) and every number
// is reported as the MEDIAN across runs, so one noisy run can't skew it.
// Requests that are slow in EVERY run are listed: those come from the engine
// itself, not from the machine. Results are recorded in docs/benchmarks.md.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <iterator>
#include <thread>
#include <vector>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include <engine/engine.hpp>
#include <stats.hpp>
#include <workload.hpp>

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
// One measured run = throughput pass + latency pass, summarized
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

constexpr uint64_t kSlowNanos = 100'000;  // "slow" = over 100 us (~100x the typical request)

struct BenchRun {
    double requestsPerSecond = 0;
    LatencySummary all;
    LatencySummary byKind[KindCount];
    std::size_t over10us = 0, over100us = 0, over1ms = 0;
    std::vector<std::size_t> slowRequests;  // request numbers (1-based, warm-up included) over kSlowNanos
    std::vector<uint64_t> nanos;            // raw samples, kept to look up slow requests later
};

BenchRun measureRun(const std::vector<std::string>& lines, std::size_t warmup) {
    BenchRun r;
    std::size_t measured = lines.size() - warmup;
    r.requestsPerSecond = measured / runThroughput(lines, warmup);

    r.nanos = runLatency(lines, warmup);
    std::vector<uint64_t> byKind[KindCount];
    for (std::size_t i = 0; i < r.nanos.size(); ++i) {  // nanos[i] belongs to lines[warmup + i]
        uint64_t ns = r.nanos[i];
        byKind[kindOf(lines[warmup + i])].push_back(ns);
        if (ns > 10'000) ++r.over10us;
        if (ns > 100'000) ++r.over100us;
        if (ns > 1'000'000) ++r.over1ms;
        if (ns > kSlowNanos) r.slowRequests.push_back(warmup + i + 1);
    }
    r.all = summarize(r.nanos);
    for (int k = 0; k < KindCount; ++k) r.byKind[k] = summarize(byKind[k]);
    return r;
}

// ---------------------------------------------------------------------------
// Combining runs (part 4)
// ---------------------------------------------------------------------------

// Median of a few values (one per run). Runs differ because the machine is
// never perfectly quiet; the median ignores one unlucky (or lucky) run.
double medianOf(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    std::size_t n = values.size();
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
}

// The median across runs of whatever `pick` reads from each run.
template <typename Pick>
double medianAcross(const std::vector<BenchRun>& runs, Pick pick) {
    std::vector<double> values;
    for (const BenchRun& r : runs) values.push_back(static_cast<double>(pick(r)));
    return medianOf(values);
}

// One table row: each column is the median across runs of that percentile.
// `summaryOf` picks which summary (overall, or one request kind) from a run.
template <typename SummaryOf>
void printMedianRow(const char* label, const std::vector<BenchRun>& runs, SummaryOf summaryOf) {
    auto col = [&](uint64_t LatencySummary::*field) {
        return medianAcross(runs, [&](const BenchRun& r) { return summaryOf(r).*field; });
    };
    std::printf("  %-11s %8zu %7.0f %7.0f %7.0f %7.0f %8.0f %9.0f %10.0f\n", label, summaryOf(runs[0]).count,
                col(&LatencySummary::min), col(&LatencySummary::p50), col(&LatencySummary::p90),
                col(&LatencySummary::p99), col(&LatencySummary::p999), col(&LatencySummary::p9999),
                col(&LatencySummary::max));
}

void printEnvironment() {
#ifdef __APPLE__
    char cpu[128] = "unknown";
    std::size_t size = sizeof(cpu);
    sysctlbyname("machdep.cpu.brand_string", cpu, &size, nullptr, 0);
    std::printf("machine: %s, %u logical CPUs\n", cpu, std::thread::hardware_concurrency());
#else
    std::printf("machine: %u logical CPUs\n", std::thread::hardware_concurrency());
#endif
#ifdef __clang__
    std::printf("compiler: clang %s, C++%ld\n", __clang_version__, __cplusplus / 100 % 100);
#else
    std::printf("compiler: %s, C++%ld\n", __VERSION__, __cplusplus / 100 % 100);
#endif
#ifdef NDEBUG
    std::printf("build: optimized (make bench = BUILD=release: -O3 -DNDEBUG)\n");
#else
    std::printf("build: NOT optimized -- these numbers mean nothing; use make bench\n");
#endif
}

}  // namespace

int main(int argc, char** argv) {
    WorkloadConfig config;
    std::size_t warmup = 100'000;
    int runCount = 5;
    for (int i = 1; i + 1 < argc; i += 2) {  // optional: --requests N --seed S --warmup W --runs R
        std::string_view flag = argv[i];
        if (flag == "--requests") config.requests = std::strtoull(argv[i + 1], nullptr, 10);
        else if (flag == "--seed") config.seed = std::strtoull(argv[i + 1], nullptr, 10);
        else if (flag == "--warmup") warmup = std::strtoull(argv[i + 1], nullptr, 10);
        else if (flag == "--runs") runCount = std::atoi(argv[i + 1]);
    }
    if (warmup >= config.requests || runCount < 1) {
        std::cerr << "need --warmup < --requests and --runs >= 1\n";
        return 2;
    }

    printEnvironment();
    ClockInfo clock = measureClock();
    std::printf("clock: one now() call costs ~%.1f ns (each sample includes ~2 of them), resolution %llu ns\n",
                clock.costNanos, (unsigned long long)clock.resolutionNanos);

    std::vector<std::string> lines = generateWorkload(config);
    std::size_t measured = lines.size() - warmup;
    std::printf("workload: %zu requests (seed %llu), %zu warm-up, %zu measured, %d runs\n", lines.size(),
                (unsigned long long)config.seed, warmup, measured, runCount);

    if (!runCheck(lines)) return 1;

    // --- the runs ----------------------------------------------------------------
    std::vector<BenchRun> runs;
    std::printf("runs:      req/s      p50      p99    p99.9   p99.99        max  >100us\n");
    for (int r = 0; r < runCount; ++r) {
        runs.push_back(measureRun(lines, warmup));
        const BenchRun& x = runs.back();
        std::printf("  %d  %10.0f %8llu %8llu %8llu %8llu %10llu %7zu\n", r + 1, x.requestsPerSecond,
                    (unsigned long long)x.all.p50, (unsigned long long)x.all.p99, (unsigned long long)x.all.p999,
                    (unsigned long long)x.all.p9999, (unsigned long long)x.all.max, x.over100us);
    }

    // --- medians across runs -----------------------------------------------------
    std::vector<double> rps;
    for (const BenchRun& r : runs) rps.push_back(r.requestsPerSecond);
    std::sort(rps.begin(), rps.end());
    std::printf("\nRESULT (median of %d runs)\n", runCount);
    std::printf("throughput: %.0f requests/s  (runs ranged %.0f - %.0f)\n", medianOf(rps), rps.front(), rps.back());
    std::printf("latency per request (ns):\n");
    std::printf("  %-11s %8s %7s %7s %7s %7s %8s %9s %10s\n", "", "count", "min", "p50", "p90", "p99", "p99.9",
                "p99.99", "max");
    printMedianRow("all", runs, [](const BenchRun& r) -> const LatencySummary& { return r.all; });
    for (int k = 0; k < KindCount; ++k) {
        printMedianRow(kindName[k], runs, [k](const BenchRun& r) -> const LatencySummary& { return r.byKind[k]; });
    }
    std::printf("slow requests (median count): >10us %.0f, >100us %.0f, >1ms %.0f\n",
                medianAcross(runs, [](const BenchRun& r) { return r.over10us; }),
                medianAcross(runs, [](const BenchRun& r) { return r.over100us; }),
                medianAcross(runs, [](const BenchRun& r) { return r.over1ms; }));

    // --- repeatable slow requests --------------------------------------------------
    // Slow in EVERY run -> caused by the engine (same work each run), not by
    // the machine (random interruptions land on different requests each run).
    std::vector<std::size_t> repeatable = runs[0].slowRequests;
    for (std::size_t r = 1; r < runs.size(); ++r) {
        std::vector<std::size_t> both;
        std::set_intersection(repeatable.begin(), repeatable.end(), runs[r].slowRequests.begin(),
                              runs[r].slowRequests.end(), std::back_inserter(both));
        repeatable = both;
    }
    std::printf("repeatable slow requests (over %llu us in all %d runs): %zu\n",
                (unsigned long long)(kSlowNanos / 1000), runCount, repeatable.size());
    for (std::size_t req : repeatable) {
        double ns = medianAcross(runs, [&](const BenchRun& r) { return r.nanos[req - 1 - warmup]; });
        std::printf("  request #%-8zu median %9.0f ns   %s\n", req, ns, lines[req - 1].c_str());
    }
    return 0;
}
