# Project Suggestions: Trading / Quant / Systems Portfolio

Six project briefs focused on state machines, resiliency, and distributed systems, suited to C++ or Python. Each is written as a README you can adapt for your own repo.

---

## 1. Limit Order Book & Matching Engine

**Suggested language:** C++

### Overview
A single-node exchange core that accepts orders, maintains a limit order book per instrument, and matches orders using price-time priority. Every order follows an explicit lifecycle state machine, and the engine rejects any invalid transition.

### Order lifecycle (state machine)
- `New` → `PartiallyFilled` → `Filled`
- `New` / `PartiallyFilled` → `Cancelled`
- `New` → `Rejected`
- `New` / `PartiallyFilled` → `Replaced` (cancel/replace semantics)

### Goals
- Correct price-time priority matching for limit and market orders
- Support for cancel and cancel/replace (amend)
- Deterministic behavior: the same input event log always produces byte-identical output
- Low, predictable latency through careful data structure and memory choices

### Key concepts
- Book data structures: price levels, per-level FIFO queues, fast order lookup by ID
- Preallocated memory pools and avoiding allocation on the hot path
- Separating input parsing, matching logic, and output publication
- Enforcing invariants on order state transitions

### Milestones
1. Define order and event types plus the lifecycle state machine
2. Single-instrument book with add/cancel/match
3. Execution reports and trade events written to an output log
4. Rebuild a book from public Nasdaq TotalView-ITCH sample data
5. Benchmarking harness that reports p50 / p99 / p99.9 latency and throughput
6. Multi-instrument support

### Stretch goals
- Pre-trade risk layer: position limits, max order size, price collars, kill switch
- Order types: IOC, FOK, post-only
- Lock-free input queue between the network thread and the matching thread

### What makes it stand out
- Latency percentiles with a documented benchmarking methodology
- Determinism proven by replaying the same log twice and comparing outputs
- Property-based tests (e.g., the book never crosses after matching, and quantities are conserved)

### Resources
- Nasdaq TotalView-ITCH 5.0 specification and sample files
- LMAX Disruptor / LMAX architecture articles

---

## 2. Fault-Tolerant Replicated Matching Engine

**Suggested language:** C++ (Python is fine for the test harness)

### Overview
This extends Project 1 into a replicated system. A sequencer assigns a total order to all inbound events, the primary engine processes them, and the log is replicated to one or more hot standbys. If the primary fails, a standby takes over with no lost or duplicated fills.

### Goals
- Total ordering of inbound events via a sequencer
- Replicate the event log to standbys before acknowledging to clients
- Automatic failover with a clear, documented promotion protocol
- Recovery from snapshot plus log replay
- Guaranteed absence of lost or duplicated fills under failure

### Key concepts
- Event sourcing and deterministic state machine replication
- Primary/backup replication vs. consensus (Raft); document which you chose and why
- Leader election and fencing (preventing split-brain with epochs/terms)
- Snapshotting and log compaction
- Client-side idempotency (client order IDs) so retries are safe

### Milestones
1. Persist the engine's input as an append-only event log
2. Snapshot plus replay recovery on a single node
3. Standby that consumes the replicated log and maintains an identical book
4. Failover: detect primary death, promote a standby, resume service
5. Fault-injection harness: kill processes, drop, delay, reorder, and partition traffic
6. Correctness checker that verifies no fill was lost or duplicated across failovers

### Stretch goals
- Full Raft for sequencer leadership
- Measure the latency cost of synchronous vs. asynchronous replication
- Rolling upgrades without downtime

### What makes it stand out
- A failure-scenario write-up that lists each injected fault, what happened, and how the system recovered
- Failover time measurements (detection time plus promotion time)
- A clear argument for the system's guarantees and where they end

### Resources
- Raft paper: "In Search of an Understandable Consensus Algorithm"
- Jepsen analyses (for testing philosophy)
- *Designing Data-Intensive Applications*, Martin Kleppmann (replication and consensus chapters)

---

## 3. FIX Protocol Session Engine

**Suggested language:** C++ or Python

### Overview
An implementation of the FIX session layer, which trading firms use for order entry and execution reports. The session is a well-specified state machine with strict sequence-number semantics and recovery procedures.

### Session state machine
- `Disconnected` → `LogonSent` / `LogonReceived` → `Active`
- `Active` → `AwaitingResend` (gap detected) → `Active`
- `Active` → `LogoutSent` → `Disconnected`

### Goals
- Logon/logout handshake and heartbeat/test request handling
- Inbound and outbound sequence number tracking
- Gap detection with ResendRequest, plus SequenceReset/GapFill handling
- Session state persisted across process crashes
- Message parsing and validation (checksums, body length, required tags)

### Key concepts
- Tag-value message encoding
- Sequence integrity and at-least-once delivery with deduplication
- Timers and timeouts driving state transitions
- Crash recovery from a persisted message store

### Milestones
1. Message parser/serializer with checksum and length validation
2. Session state machine with logon/heartbeat/logout
3. Sequence number tracking and persistence
4. Gap detection and the resend flow
5. Flaky counterparty simulator that drops, duplicates, and reorders messages and disconnects mid-stream
6. Integration with the application layer (NewOrderSingle, ExecutionReport)

### Stretch goals
- Connect it to the matching engine from Project 1 as the order-entry gateway
- Zero-copy parsing in C++
- Fuzz the parser

### What makes it stand out
- Demonstrated recovery from every failure the flaky counterparty can produce
- Conformance tests built from the published FIX specification

### Resources
- FIX Trading Community: FIX 4.4 / FIXT 1.1 session specifications

---

## 4. Market Data Feed Handler with Gap Recovery

**Suggested language:** C++

### Overview
A feed handler that consumes an exchange-style market data feed over unreliable UDP multicast, arbitrates between redundant A/B lines, detects sequence gaps, and recovers using snapshots, while maintaining an accurate local order book.

### Feed state machine
- `Initializing` → `Synced`
- `Synced` → `GapDetected` → `Recovering` (snapshot + buffered incrementals) → `Synced`
- Any state → `Stale` (no data within timeout)

### Goals
- A/B line arbitration (use the first copy of each sequence number and drop duplicates)
- Gap detection and buffering of out-of-order messages
- Snapshot-based recovery, with buffered incrementals applied after the snapshot
- Accurate book reconstruction, verified against a reference
- Staleness detection and signaling to consumers

### Key concepts
- UDP multicast and its failure modes
- Sequence-based reliability without retransmission
- Bounded buffering and choosing when to give up and resnapshot
- Separating the network receive path from book building

### Milestones
1. Simple publisher that emits incremental updates and periodic snapshots
2. Single-line receiver that builds a book
3. Second line plus arbitration
4. Loss injection in the publisher (random and burst loss)
5. Gap detection and snapshot recovery
6. Metrics: recovery time, time spent stale, messages dropped vs. recovered

### Stretch goals
- Kernel-bypass awareness: document what you would change for production
- Parse a real binary protocol format (e.g., ITCH-style messages)
- Fan out the book to multiple consumers through shared memory

### What makes it stand out
- A reference-book comparison proving correctness after recovery
- Graphs of recovery time vs. loss rate

### Resources
- Public CME MDP 3.0 documentation (concepts of A/B feeds and recovery)
- Nasdaq ITCH / MoldUDP64 specifications

---

## 5. Raft-Based Key-Value Store

**Suggested language:** C++ or Python

### Overview
A replicated, linearizable key-value store built on your own Raft implementation and tested aggressively under network partitions and crashes.

### Node state machine
- `Follower` → `Candidate` → `Leader`
- `Candidate` / `Leader` → `Follower` (higher term observed)

### Goals
- Leader election with randomized timeouts
- Log replication and commit index advancement
- Linearizable reads and writes
- Snapshotting and log compaction
- Survives crashes, restarts, and partitions

### Key concepts
- Terms, votes, and the election safety property
- Log matching and leader completeness
- Persistence of `currentTerm`, `votedFor`, and the log
- Linearizability vs. eventual consistency

### Milestones
1. Leader election only
2. Log replication without failures
3. Persistence and crash recovery
4. Simulated network with drops, delays, and partitions
5. Key-value state machine on top of the log
6. Snapshotting
7. Linearizability checker over recorded client histories

### Stretch goals
- Cluster membership changes
- Read leases or ReadIndex for faster reads
- Sharding across multiple Raft groups

### What makes it stand out
- A deterministic network simulator that makes failures reproducible
- A linearizability checker that verifies histories under partition

### Resources
- Raft paper and the Raft website visualizations
- MIT 6.5840 (Distributed Systems) course labs and lectures
- Jepsen / Knossos for linearizability checking concepts

---

## 6. Resilient Backtesting / Workflow Engine

**Suggested language:** Python

### Overview
A distributed job scheduler for running backtests and research pipelines. It survives worker crashes, retries safely, and produces identical results whether or not failures occurred.

### Job state machine
- `Pending` → `Scheduled` → `Running` → `Succeeded`
- `Running` → `Failed` → `Retrying` → `Scheduled`
- `Running` → `Compensating` → `RolledBack` (for multi-step sagas)

### Goals
- Distributed workers that pull jobs from a coordinator
- Heartbeats and lease-based job ownership, so dead workers' jobs are reassigned
- Idempotent job execution with idempotency keys
- Checkpointing of long-running backtests and resuming from checkpoint
- Deterministic, reproducible results (pinned data versions, seeds, configs)

### Key concepts
- Exactly-once effects vs. exactly-once delivery
- Leases, heartbeats, and failure detection
- Saga pattern for multi-step workflows with compensation
- Reproducibility as a correctness property

### Milestones
1. Single-process job queue with the job state machine
2. Distributed workers with leases and heartbeats
3. Retries with backoff and idempotency
4. Checkpointing and resume
5. Chaos mode that randomly kills workers and the coordinator
6. Verification that results are identical with and without injected failures

### Stretch goals
- DAG workflows with dependencies
- Coordinator high availability (reuse ideas from Project 5)
- Web dashboard for job state and history

### What makes it stand out
- A demonstration that chaos runs and clean runs produce identical outputs
- Clear documentation of the delivery and execution guarantees

### Resources
- *Designing Data-Intensive Applications*, Martin Kleppmann
- Temporal / Airflow documentation (for design ideas, not code)

---

## Making Any of These Resume-Worthy

- **Go deep on one core project** rather than building several shallow ones. Projects 1 → 2 → 3 together form an end-to-end mini exchange.
- **Quantify everything.** Report throughput, tail latency, and recovery time, with your benchmarking methodology.
- **Test failure, not just success.** Use property-based tests, fuzzing, and fault injection.
- **Write a design doc** that explains tradeoffs, alternatives considered, and what broke along the way.
