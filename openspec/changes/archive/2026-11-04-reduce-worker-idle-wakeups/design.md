# Design

## Context

Every asynchronous observability consumer — the logging `detail::AsyncWorker` (shared by `FileLogger` and `ConsoleLogger`) and the tracing `TracerProvider` consumer — runs a `std::jthread` that drains SPSC shards and, when the rings are empty, sleeps between sweeps. Before this change both hard-coded the same two constants: spin for up to 64 empty sweeps, then `sleep_for(200 µs)` every iteration. On this machine that yields ~4 000 voluntary context switches/s per idle worker, and the deploy pins many applications' loggers/tracers to one or two shared cores.

`LoggerOptions` already parsed and carried `worker_poll_interval_us` (default 1000 µs), but the engine never read it — a known drift item (code analysis #18). The tracing consumer had no equivalent knob.

## Goals / Non-Goals

**Goals:**
- Cut idle wake-ups ~5× at the default and further at a larger ceiling, without touching the producer hot path.
- One shared, header-only policy so logging and tracing stop duplicating the spin/sleep constants.
- Make the ceiling configurable end-to-end (config → settings → worker) for both subsystems.

**Non-Goals:**
- No producer-side wake (futex/eventfd) in this change — that is the larger follow-up that also needs `ShardCapacity` headroom.
- No `ShardCapacity` change (logging stays at 128 slots/shard; tracing stays at 1024).
- No `NEXIGEN_LOG_WORKER_POLL_INTERVAL_US` environment override (file/config path is sufficient; keeps the override surface unchanged).

## Decisions

- **Shared `nexigen::core::WorkerTuning` (header-only).** Rationale: the two consumers had byte-identical spin/sleep constants, so the policy lives once in core, mirroring the `core::affinity` and `core::fixed_writer` dedup precedents. Alternatives considered: duplicating the tuning in each consumer (rejected — the exact drift that prompted this); putting it in logging and importing from tracing (rejected — a sibling→sibling dependency).
- **Geometric backoff with a floor, not a fixed sleep.** A flat sleep at the full ceiling would make the first record after idle wait up to 1 ms (and up to 20 ms at larger ceilings), which is unnecessary when bursts arrive back-to-back. A floor of 100 µs, doubling each empty wake-up to the ceiling, gives ~100 µs first-record latency right after activity while still collapsing to one wake-up/interval when long-idle.
- **Spin budget 4 (down from 64).** The 64-sweep spin re-reads every shard's tail cache line and, on a shared core, only steals time from co-located consumers. A small budget (4) keeps the common case of back-to-back bursts spin-free of sleep, then hands off to the sleep.
- **Timer slack = ceiling / 2, never below 50 µs.** Raising the worker's own `PR_SET_TIMERSLACK` lets the kernel coalesce the idle timers of many pinned workers into shared bursts. Halving the ceiling bounds the added wake-up jitter to the ceiling. Applied on the worker thread only, so producers and the `flush()` waiter (which sleeps on the caller thread) are unaffected.
- **Clamp `[50, 100000]` µs.** Values outside this range are meaningless or pathological; clamping (never erroring) keeps `initialize()` from gaining a new failure mode.
- **`set_tuning()` before `start()`.** The worker reads its tuning once, on its own thread after thread creation, keeping the policy a single-writer plain read (no atomics) and avoiding any pin/schedule window hazard. The tracing consumer reads the same fields from its already-copied config.

## Risks / Trade-offs

- **Burst drops.** A long-idle worker sleeps up to the ceiling, so a burst arriving right after idle can overflow a shard ring and count as `buffer_full`. At the default 1 ms / 128-slot logging shard this tolerates ~128 k records/s per thread; raising the ceiling without raising `ShardCapacity` shrinks that headroom (the design doc for the follow-up covers capacity math).
- **`flush()` latency.** `flush()` completes at the next wake-up, so its worst-case latency equals the ceiling; the existing escape hatch (50 000 × 200 µs) still bounds a stuck worker.
- **No allocation.** Constants and one `prctl` per worker start; the hot path and the sleep loop stay allocation-free.
