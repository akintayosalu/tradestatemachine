# Deterministic Matching Engine (C++20)

A limit order book and matching engine that turns a log of order requests into
a log of execution reports, **byte-for-byte identical every time it runs**.
Project 1 of a small exchange-infrastructure series (next: primary/standby
failover, then a market-data feed).

**~970k requests/s · p50 1.0 µs · p99 2.9 µs** (Apple M2, single thread; [details](docs/benchmarks.md))

## What it does

- **Orders:** limit and market; cancel; cancel/replace (FIX-style: a new
  version, priority kept only if the price is unchanged and the size goes down).
- **Matching:** price-time priority; trades at the resting order's price;
  market orders take what's there and the rest is cancelled.
- **Input/output:** plain-text request log in, execution reports out
  (`ACCEPT`, `REJECT`, `TRADE`, `STATE`). [Format](docs/io_format.md)

```
NEW clOrdId=201 side=Sell type=Limit px=10100 qty=30     ACCEPT request=New clOrdId=201 orderId=1
NEW clOrdId=202 side=Buy type=Limit px=10200 qty=50  ->  STATE clOrdId=201 state=New filled=0 remaining=30
                                                         ACCEPT request=New clOrdId=202 orderId=2
                                                         TRADE tradeId=1 aggressor=202 resting=201 px=10100 qty=30
                                                         STATE clOrdId=201 state=Filled filled=30 remaining=0
                                                         STATE clOrdId=202 state=PartiallyFilled filled=30 remaining=20
```

## Design highlights

- **State machine as a pure function.** Every order state change goes through
  `decide(state, event) -> outcome`, a table with one tested cell per case.
  Orders have no setters. ([state machine](docs/order_state_machine.md))
- **Rejects vs. bugs.** A bad request is rejected and the engine carries on; an
  impossible event (a fill on a cancelled order) is a bug: the engine logs it
  and aborts (fail-stop), since continuing would corrupt state.
- **Store owns, book indexes.** One store owns every order for its whole life;
  the book holds only IDs of resting orders: price levels in ordered maps, each
  a FIFO list with per-order iterators for O(1) cancel.
- **Deterministic by construction.** No clocks, threads or randomness in the
  engine, and nothing output by iterating a hash map. This is what lets a
  standby replay the same log and reach the same state (Project 2).

Every decision and its reasoning: [docs/design.md](docs/design.md).

## Testing

- 186 tests on a small built-in framework, also run under AddressSanitizer + UBSan.
- **Golden file:** a hand-checked session with its exact expected output.
- **Determinism:** a 5,000-request generated log through two engines must give
  identical output.
- Tests were checked by deliberately breaking the code and confirming they fail.

## Performance

1M-request synthetic workload (45% limit, 5% market, 35% cancel, 15% replace),
median of 5 runs, including parsing and output formatting:

| p50 | p99 | p99.9 | p99.99 |
|-----|-----|-------|--------|
| 1.0 µs | 2.9 µs | 5.8 µs | 16 µs |

The benchmark found rare 1-5 ms spikes that recur on the same requests at
doubling intervals: hash maps rehashing as they grow. Logged as the first
optimization to try. [Method, caveats, history](docs/benchmarks.md)

## Build and run

Requires a C++20 compiler (clang or gcc) and make.

```
make                 # build build/debug/app
make test            # run the tests        (make test FILTER=match: for a subset)
make test-asan       # tests under AddressSanitizer + UBSan
make bench           # release-build benchmark
./build/debug/app tests/data/golden_basic.in
```

## Layout

```
include/engine/   headers (types, state machine, order, book, store, matching, engine)
src/              implementation + main.cpp
tests/            tests and golden files      bench/   benchmark
docs/             design, I/O format, state machine, benchmarks
```

## Next

- **v2:** ITCH replay (real NASDAQ order flow as a test and workload), the
  optimizations the benchmark points to, more order types.
- **Project 2:** primary/standby failover by replaying the input log.
- **Component 3:** UDP market-data feed with gap recovery and A/B lines.
