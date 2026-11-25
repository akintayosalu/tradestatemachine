# Benchmarks

How fast the engine is, measured the same way every time, so later changes
(see `todo/order_book.txt` section 4) can be compared against a fixed baseline.
Rule: change one thing at a time, keep every test green, rerun `make bench`,
and add a row to the history table below, including changes that didn't help.

## How to run

```
make bench                                  # release build, 1M requests, 5 runs
make bench ARGS="--runs 9"                  # more runs = steadier tail numbers
make bench ARGS="--requests 200000 --warmup 20000 --runs 3"   # quick look
```

Close heavy apps and keep the laptop on power: the tail (p99.9 and above) is
sensitive to anything else the machine is doing.

## What is measured (bench/)

- **Workload** (`bench/workload.cpp`): 1,000,000 request lines from a fixed seed
  (42), generated before timing. Mix: 45% limit NEW, 5% market NEW, 35% CANCEL,
  15% REPLACE, all around one mid price; 20% of limit orders are priced to
  cross. Same seed -> identical lines on any machine.
- **Per run**, each pass with a fresh engine (deterministic: identical work):
  - *throughput pass*: one clock reading before and after the measured requests.
  - *latency pass*: a clock reading around every `Engine::process` call.
  - The first 100,000 requests are warm-up (run, not measured): the book fills
    to a realistic depth and the caches warm up.
- **What one request includes**: parsing the line, the engine work (store,
  book, matching) and formatting its output lines. No file I/O.
- **5 runs**; every reported number is the **median across runs**.
- **Percentiles** use the nearest-rank rule over the 900,000 measured samples.
- **Clock**: `std::chrono::steady_clock`. On this machine one call costs
  ~16-28 ns and the resolution is 41 ns (24 MHz), so each sample includes
  ~30-60 ns of clock cost and is rounded to a multiple of ~41.7 ns.
- **Slow requests**: over 100 us. "Repeatable" = slow in every run. Those come
  from the engine (same work each run); slow requests that move between runs
  are the machine (OS scheduling, other processes, core switching).

## Baseline (v1, 2026-11-25)

Machine: Apple M2 (8 logical CPUs), macOS. Compiler: Apple clang 21.0.0,
C++20, `-O3 -DNDEBUG`. Commit: benchmark part 4 on `worktree-test-framework`.

**Throughput: ~972,000 requests/s** (5 runs ranged 938k - 976k), about 1.03 us
per request.

Latency per request, ns (median of 5 runs):

|            | count   | min | p50   | p90   | p99   | p99.9 | p99.99 | max       |
|------------|---------|-----|-------|-------|-------|-------|--------|-----------|
| all        | 900,000 | 291 | 1,000 | 1,542 | 2,917 | 5,791 | 16,083 | 2,689,958 |
| NEW limit  | 405,037 | 833 | 1,000 | 1,583 | 3,250 | 6,375 | 17,959 | 2,689,958 |
| NEW market | 44,829  | 1,083 | 1,459 | 2,459 | 4,666 | 8,750 | 26,833 | 39,917 |
| CANCEL     | 315,350 | 291 | 625   | 958   | 1,459 | 2,708 | 13,458 | 58,375    |
| REPLACE    | 134,784 | 708 | 1,292 | 1,708 | 2,375 | 5,208 | 18,791 | 60,667    |

Slow requests (median count per run): 235 over 10 us, 6 over 100 us, 2 over 1 ms.

Run-to-run spread: p50 and p99 barely move (959-1,000 and 2,750-3,250 ns);
p99.99 moved from 13 us to 45 us and the max from 2.1 to 5.0 ms. Trust p50/p99
from a single run; compare p99.9+ only across medians of several runs.

### Finding: hash-map rehash spikes

Five requests are slow in **every** run, all limit NEWs that rest an order:

| request  | median   | spacing                  |
|----------|----------|--------------------------|
| #172,829 | 0.52 ms  | series A                 |
| #345,391 | 1.04 ms  | series A, x2.00          |
| #401,996 | 0.17 ms  | series B                 |
| #691,359 | 2.32 ms  | series A, x2.00          |
| #809,401 | 0.42 ms  | series B, x2.01          |

Each series doubles, and the cost doubles with it: the signature of a
`std::unordered_map` growing. When it fills, it allocates a table about twice
as big and moves every element in one go, inside one unlucky request. Two
series = two maps growing at different sizes (the store and client-ID maps
grow with every order; the book's handle map with resting orders only).

Not fixed in v1 (measure first, optimize later). Candidate fix: `reserve()`
the maps up front, or a map that grows incrementally. Logged in
`todo/order_book.txt` section 4.

## History

| date       | change                  | req/s  | p50   | p99   | p99.9 | p99.99 | max     | notes |
|------------|-------------------------|--------|-------|-------|-------|--------|---------|-------|
| 2026-11-25 | v1 baseline             | 972k   | 1,000 | 2,917 | 5,791 | 16,083 | 2.69 ms | rehash spikes, see above |
