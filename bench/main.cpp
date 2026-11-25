// Benchmark driver. Built and run by `make bench` (release build).
//
// PART 1 (this version): generate the workload and run it through a fresh
// engine WITHOUT timing, then print what happened. The point is to check the
// workload is realistic (trades happen, the reject rate is sane) before we
// measure anything. Timing is Part 2, statistics Part 3.
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "engine/engine.hpp"
#include "workload.hpp"

namespace {

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

}  // namespace

int main(int argc, char** argv) {
    WorkloadConfig config;
    for (int i = 1; i + 1 < argc; i += 2) {  // optional: --requests N --seed S
        std::string_view flag = argv[i];
        if (flag == "--requests") config.requests = std::strtoull(argv[i + 1], nullptr, 10);
        else if (flag == "--seed") config.seed = std::strtoull(argv[i + 1], nullptr, 10);
    }

    std::vector<std::string> lines = generateWorkload(config);

    std::cout << "workload: " << lines.size() << " requests, seed " << config.seed << "\n";
    std::cout << "first lines:\n";
    for (std::size_t i = 0; i < 5 && i < lines.size(); ++i) std::cout << "  " << lines[i] << "\n";

    Engine engine;
    std::string out;
    OutputCounts counts;
    for (const std::string& line : lines) {
        out.clear();  // keep only this request's output: 1M requests would be ~100s of MB
        if (!engine.process(line, out)) {
            std::cerr << "engine hit a Bug: " << out;
            return 1;
        }
        countLines(out, counts);
    }

    std::cout << "engine output:\n"
              << "  accepts    " << counts.accepts << "\n"
              << "  rejects    " << counts.rejects << " (already filled: " << counts.rejectAlreadyFilled << ")\n"
              << "  trades     " << counts.trades << "\n"
              << "  cancelled  " << counts.cancelled << " (cancels + market leftovers)\n"
              << "  malformed  " << counts.malformed << "\n"
              << "  bugs       " << counts.bugs << "\n";
    return 0;
}
