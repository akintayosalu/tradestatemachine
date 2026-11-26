# Mini Exchange: Replicated Matching Engine + Market Data Feed

A small but realistic exchange stack in C++. It is built as three related components that together tell one story:

> An exchange core that keeps trading correctly through server failures, publishing a public market data feed that clients can rebuild accurately despite packet loss.

---

## Overarching Goal

Build a system where:

1. Client orders are **sequenced** into a single total order.
2. A **deterministic matching engine** turns that sequence into fills and book changes.
3. The engine is **replicated**, so a standby can take over when the primary dies with **no lost or duplicated fills**.
4. Book changes are published as a **lossy multicast market data feed**.
5. A **feed handler** rebuilds an accurate order book from that feed, even with dropped packets, using redundant lines and snapshot recovery.

The defining property of the whole system is **determinism**: the same input log must always produce byte-identical output. Replication, recovery, and seamless failover all depend on it.

---

## System Overview

```
clients ──► [ Sequencer ] ──► replicated input log ──► [ Matching Engine: primary ]
                                       │                          │
                                       └────────────────► [ Matching Engine: standby(s) ]
                                                                  │
                                                       fills / book changes
                                                                  │
                                                                  ▼
                                   [ Market Data Publisher ] ──► UDP multicast (A line, B line)
                                   [ Snapshot Service ]                    │
                                            ▲                              ▼
                                            └──── recovery requests ── [ Feed Handler ]
```

The input side is private, sequenced, and must be reliable. The output side is public, anonymized, and deliberately lossy.

---

## Component 1: Matching Engine (Project 1)

### Goal
A single-node, deterministic limit order book and matching engine with explicit order lifecycle state machines.

### Deliverables
- Order lifecycle state machine with invalid transitions rejected
- Price-time priority matching for limit and market orders
- Cancel and cancel/replace support
- Execution reports and book-change events written to an output log
- **Determinism test:** replaying the same input log twice yields byte-identical output
- **Benchmark report:** throughput plus p50 / p99 / p99.9 latency, with methodology documented
- Book reconstruction from a real Nasdaq ITCH sample file, used as a realistic workload

### Done when
- Invariant tests pass (e.g., the book never crosses after matching, quantities are conserved, and every order ends in exactly one terminal state)
- The benchmark is reproducible by someone else from your README

### Stretch
- Pre-trade risk checks and a kill switch
- IOC / FOK / post-only order types

---

## Component 2: Replication and Failover (Project 2)

### Goal
Make the engine survive the death of its primary with no client-visible inconsistency.

### Deliverables
- Sequencer that assigns a total order to inbound events
- Durable, append-only input log
- Snapshot plus log-replay recovery
- One or more hot standbys that consume the replicated log and maintain identical state
- Failover protocol with fencing (epochs/terms), so two primaries can never both be active
- Client idempotency, so a retried order is never executed twice
- **Fault-injection harness:** kill processes, and drop, delay, reorder, or partition traffic
- **Correctness checker:** proves no fill was lost or duplicated across failovers
- **Failure report:** each injected fault, observed behavior, recovery time, and any bugs found

### Done when
- Repeated randomized fault-injection runs pass the correctness checker
- Failover time (detection plus promotion) is measured and reported

### Design decisions you must make and document
- Primary/backup vs. consensus (Raft / Viewstamped Replication), and why
- Synchronous vs. asynchronous replication: when is a client acknowledged?
- How a failed node rejoins
- What guarantees you provide, and where they end

---

## Component 3: Market Data Publisher and Feed Handler (Project 4, mini)

### Goal
Publish the engine's book changes as a sequenced multicast feed, and build a client that maintains an accurate book despite loss.

### Deliverables
- Publisher that emits sequenced incremental updates on two redundant lines (A/B)
- Snapshot service the handler can use to resynchronize
- Feed handler state machine: synced → gap detected → recovering → synced, plus stale detection
- A/B line arbitration
- Configurable loss injection (random and burst)
- **Correctness check:** the handler's book matches the engine's book after recovery
- **Metrics:** recovery time and time spent stale vs. loss rate

### Done when
- The handler's book matches the engine's book under every loss scenario you test
- **Integration demo:** kill the engine primary mid-session; the feed continues after failover with no sequence gaps or duplicates, and the handler's book stays correct

### Design decisions you must make and document
- How market data sequence numbers stay continuous across an engine failover
- How long to buffer out-of-order messages before giving up and resnapshotting
- Wire format: your own binary format, or an ITCH/MoldUDP64-style framing

---

## Repository Deliverables

- `README.md`: what it is, how to build, run, and test, and headline results
- `docs/design.md`: architecture, decisions, alternatives considered, and guarantees
- `docs/failures.md`: fault-injection scenarios and outcomes
- `docs/benchmarks.md`: methodology, hardware, and results
- Automated tests runnable with one command

---

## Ground Rules

- Build it yourself. Read specs and papers, not other people's implementations.
- Correctness first, then measure, then optimize.
- Don't start Component 2 until the determinism test in Component 1 passes.
- Write down every design decision as you make it. Those notes become your interview answers.

---

## Resources

### Exchange architecture
- **The LMAX Architecture**, Martin Fowler: single-threaded in-memory business logic, event sourcing, and replicas kept in sync by replaying input. <https://martinfowler.com/articles/lmax.html>
- **How to Build an Exchange**, Jane Street tech talk by Brian Nigito, based on Jane Street's JX crossing engine. <https://blog.janestreet.com/how-to-build-an-exchange/>
- **Multicast and the Markets**, Signals and Threads podcast with Brian Nigito, on how modern exchanges are built from simple building blocks. <https://signalsandthreads.com/multicast-and-the-markets>

### Market data formats
- **Nasdaq TotalView-ITCH 5.0 specification** <https://nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf>
- **Nasdaq ITCH sample data files** <https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/>
- **MoldUDP64 specification**: sequenced UDP framing with gap detection and retransmission requests. <https://m.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/moldudp64.pdf>
- **CME MDP 3.0 documentation** (search the CME Client Systems Wiki): A/B feeds, incremental vs. snapshot recovery concepts.

### Replication and consensus
- **Viewstamped Replication Revisited**, Liskov & Cowling: the closest fit to a primary/backup design with view changes. <https://www.cs.princeton.edu/courses/archive/fall19/cos418/papers/vr-revisited.pdf>
- **Raft paper and visualizations** <https://raft.github.io/>
- **MIT 6.5840 Distributed Systems**: lectures and lab test philosophy. <https://pdos.csail.mit.edu/6.824/>
- **Jepsen**: how to test distributed systems under faults. <https://jepsen.io/>
- **Designing Data-Intensive Applications**, Martin Kleppmann (book): replication, logs, and consensus chapters.

### Low-latency C++ and measurement
- **When a Microsecond Is an Eternity**, Carl Cook (CppCon 2017): low-latency trading techniques and how to measure them honestly. <https://isocpp.org/blog/2018/09/cppcon-2017-when-a-microsecond-is-an-eternity-high-performance-trading-syst>
- **Google Benchmark** <https://github.com/google/benchmark>
- **HdrHistogram**: recording latency percentiles correctly. <https://github.com/HdrHistogram/HdrHistogram_c>
