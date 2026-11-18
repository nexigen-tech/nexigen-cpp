# Proposal

## Why

Distributed applications need a common, phase-aligned time grid so that control
loops in different processes start their phases at the same instants. The
existing `com/event-system` provides the transport, sequence integrity, and
loss reporting, but it deliberately stops at the transport boundary: it does
not define a metronome, a phase schedule, late-event policy, or a bounded
wait loop. Building those policies ad hoc in each application would duplicate
subtle drift, overrun, and restart handling.

`nexigen::synchro` adds that missing policy layer on top of
`com::EventChannel<SyncPayload>` and reuses the project's existing time,
threading, logging, and metrics primitives.

## What Changes

- Add a new COM-gated `nexigen::synchro` module with `SyncPayload`,
  `SyncChannelConfig`, `SyncConfig`, `SyncConsumerConfig`, `StartPolicy`,
  `LateEventPolicy`, state vocabularies, statistics, and `SyncError`.
- Add `SyncGenerator<Logger>`: a real-time periodic generator aligned to the
  `CLOCK_MONOTONIC_RAW` grid, with phase decomposition, zero-drift schedule
  arithmetic, hybrid `ppoll`/spin waiting, overrun skipping, and
  `EventProducer<SyncPayload>` publication.
- Add `SyncConsumer<Logger>`: an `IEventConsumer` wrapper over
  `EventConsumer<SyncPayload>` that tracks transport sequence and logical
  `(cycle, phase)` continuity, classifies late events, dispatches phase
  handlers, and reports synchronization loss.
- Add `SyncRunner<Logger>`: owner of one `SyncConsumer` and one
  `EventConsumerSet<16>`, with a bounded wait loop and watchdog handling.
- Add metrics and structured logging integration. The generator and consumer
  receive a dedicated `metrics::Meter&`; instruments use the `MultiThreaded`
  policy so the provider's background harvester is TSan-clean.
- Add OpenSpec capabilities for the shared protocol, the generator, and the
  consumer.
- Add tests, Doxygen contracts, and an architecture update.

**Performance and memory impact:**

- The steady-state generator path performs one raw-clock read, one `ppoll`
  wait (unless already inside the spin window), one 32-byte payload write, and
  one event publication per phase. It performs no heap allocation and takes no
  locks.
- The default spin window is 30 µs; it is configurable. The maximum event rate
  is bounded by the v1 minimum phase period of 1 ms (1000 phases/s).
- The consumer allocates its phase-handler table once during creation. Sample
  handling performs no allocation unless an injected application callback
  allocates.
- Fixed storage grows by one 32-byte payload type, the handler table
  (`phases_per_cycle` × `std::function`), and registry-owned metric slots.
  `SyncConsumerSet` keeps the existing 16-consumer hard limit.
- The generator owns one `WorkerThread` and one `eventfd`; no extra process or
  thread is created for the consumer unless the application runs `SyncRunner`
  on its own thread.

## Capabilities

### New Capabilities

- `synchro/protocol`: shared payload, channel configuration, schedule
  arithmetic, start/late policy, states, statistics, and the `SyncError`
  vocabulary.
- `synchro/generator`: periodic aligned generation, phase publication,
  overrun skipping, lifecycle, logging, and metrics.
- `synchro/consumer`: sequence and logical-phase tracking, late-event policy,
  phase callbacks, synchronization-loss detection, and the runner loop.

### Modified Capabilities

None. Existing `com/event-system`, `chrono`, `core`, `logging`, and `metrics`
capabilities are reused without requirement changes.

## Impact

- New public headers under `include/nexigen/synchro/` and implementation files
  under `src/synchro/`.
- New CMake target `nexigen_synchro`, built only with
  `NEXIGEN_ENABLE_COM=ON`.
- New test target `nexigen_synchro_tests`.
- New specs under `openspec/specs/synchro/`.
- `docs/architecture.md` gains the synchro module and its event-system
  interaction diagram.
- Runtime dependencies: `nexigen_com`, `nexigen_chrono`, `nexigen_logging`,
  `nexigen_metrics`, and `nexigen_core`.
- No existing API or behavior is removed or changed.
