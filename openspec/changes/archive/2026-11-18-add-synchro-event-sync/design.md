# Design

## Context

See `proposal.md` for motivation. The design builds on the existing
`com/event-system` transport (`EventChannel`, `EventProducer`,
`EventConsumer`, `IEventConsumer`, `EventConsumerSet`), the project
`CLOCK_MONOTONIC_RAW` clock domain, `core::WorkerThread`, the
`logging::concepts::Logger` contract, and `metrics::Meter`.

There is no existing synchronization module. The new module is COM-gated and
does not modify the transport contracts.

## Goals / Non-Goals

**Goals:**

- Publish phase-aligned events on a raw-monotonic grid with no cumulative
  drift.
- Support 1 ms minimum phase period (1000 events/s) with configurable phase
  count.
- Keep the steady-state generator and consumer paths allocation-free and
  lock-free.
- Reuse `com` sequence and epoch integrity, adding logical phase continuity on
  top.
- Provide a bounded, stop-aware consumer loop over `EventConsumerSet<16>`.
- Integrate structured logging and multi-threaded metrics without adding
  transport dependencies to `com`.

**Non-Goals:**

- Sub-millisecond phases in v1.
- More than 16 consumers in one `EventConsumerSet`; multiple channels/runners
  are the supported scale-out.
- UTC/wall-clock synchronization, PTP, or hardware clock calibration.
- Dynamic reconfiguration of a running generator.
- History replay for late joiners.
- Authentication, authorization, or encrypted transport.

## Decisions

### D1 — New `nexigen::synchro` module, not a `com` submodule

`com` is the transport layer and must not depend on `logging` or `metrics`.
The sync policy layer needs both, plus `chrono` and `core::WorkerThread`, so it
belongs at the application-service layer.

The module is:

```text
include/nexigen/synchro/
src/synchro/
nexigen_synchro (STATIC, only with NEXIGEN_ENABLE_COM=ON)
```

Dependencies: `nexigen_com`, `nexigen_chrono`, `nexigen_logging`,
`nexigen_metrics`, `nexigen_core`.

Alternative considered: `nexigen::com::synchro`. Rejected because it would
invert the layer direction and force telemetry dependencies into `com`.

### D2 — `CLOCK_MONOTONIC_RAW` is the single sync time domain

All schedule instants, payload `scheduled_time_ns`, header timestamps, and
consumer receive times use `CLOCK_MONOTONIC_RAW`, matching `chrono::Timestamp`
and the default event-header domain.

`clock_nanosleep` is not used because Linux does not provide
`CLOCK_MONOTONIC_RAW` as a sleep clock. The generator waits with `ppoll`,
whose timeout is relative and therefore clock-independent:

```text
remaining = deadline_raw - now_raw
sleep_for = remaining - spin_threshold_ns
```

The generator recomputes the remaining time after every `EINTR` or eventfd
wake-up. The final `spin_threshold_ns` is a clock-read spin.

Alternatives considered: `CLOCK_MONOTONIC` (simpler absolute sleep, but a
different domain from `chrono` and the event header); relative sleep plus
periodic re-anchoring (more complex and still needs a final spin).

### D3 — 32-byte self-describing payload

`SyncPayload` is `alignas(8)`, trivially copyable, and exactly 32 bytes:

| Offset | Field | Type |
|---:|---|---|
| 0 | `cycle_number` | `uint64_t` |
| 8 | `phase_index` | `uint32_t` |
| 12 | `phases_total` | `uint32_t` |
| 16 | `scheduled_time_ns` | `uint64_t` |
| 24 | `cycle_period_ns` | `uint64_t` |

`SyncChannelConfig` carries the service name, `com::ChannelConfig`, cycle
period, and phases per cycle. The payload repeats the period and phase count so
a consumer can validate them against its configuration and a late joiner does
not need a second lookup.

`SyncConsumerConfig` also carries the shared `SyncChannelConfig`, because
phase-handler storage is sized before the first event and watchdog defaults
need the cycle period.

### D4 — Absolute-grid schedule and explicit start policy

The generator computes:

```text
T_start = resolved start instant
T_scheduled(c, p) = T_start + c * Tc + floor(p * Tc / N)
```

No deadline is derived from the previous wake-up. `cycle_number` is relative:
zero at `T_start`; restarts are distinguished by the event producer epoch.

`StartPolicy` separates mode (`NextCycle`, `AtTarget`), target alignment, and
past-target behavior (`Reject`, `RealignToNextCycle`). `start_at(target)` is a
one-shot runtime override and does not rewrite the stored configuration.

`Tc / N >= 1 ms` is validated as a v1 product limit. It is not a transport
limit.

### D5 — Generator uses `core::WorkerThread` and an eventfd

`SyncGenerator` owns one `core::WorkerThread<Body>` as its last member. The
worker applies `ThreadSpec` before `on_start`, then runs the publication loop.
The generator is non-copyable and non-movable.

An `eventfd(EFD_NONBLOCK | EFD_CLOEXEC)` is created in `create()`. A
`std::stop_callback` created on the worker/start path writes one `uint64_t` to
the eventfd when stop is requested. `ppoll` watches that eventfd with the
computed relative timeout.

`std::stop_callback` construction and eventfd creation are cold-path
operations; the publication loop performs no allocation.

### D6 — Overrun skips slots on the same grid

When the actual time reaches or passes the next phase deadline, the generator
skips every stale phase slot and publishes the next future slot. It counts one
`sync_overruns_total` increment per skipped slot and emits one WARN per
episode with the skipped count.

The cycle and phase indices are recomputed from the absolute grid, never
incremented in a catch-up loop. This keeps the schedule anchored and avoids a
burst of stale publications.

### D7 — `SyncConsumer` wraps `EventConsumer`

`EventConsumer` is `final`, so `SyncConsumer<Logger>` is a separate
`IEventConsumer` implementation that owns one `EventConsumer<SyncPayload>` and
delegates `listener`, `drain`, attachment state, and received count.

`drain()` returns `com::Status` because that is the interface contract.
Protocol anomalies (late, skip, loss) are state and telemetry, not drain
failures.

Phase handlers are stored in a fixed-size table,
`std::unique_ptr<Handler[]>` with `Handler = std::function<void(
const SyncContext&)>`, allocated once during `create()` and sized to
`phases_per_cycle`. `on_cycle_start` is an alias for `on_phase(0)`.
`on_any_phase` is a separate slot and runs before the phase-specific slot.
Registration allocates; dispatch does not, and the table never resizes after
creation.

### D8 — Two-layer continuity tracking

Transport continuity comes from `EventConsumer` (`LossInfo` for gap,
duplicate, unexpected reset, and producer restart). Logical continuity comes
from `SyncTracker`:

```text
absolute_phase = cycle_number * phases_total + phase_index
```

On transport loss, the tracker advances its expected position by
`lost_count + 1`. Any remaining forward difference is a generator skip.
`producer_epoch` or `phases_total` changes, backward cycles without an epoch
change, and unexpected sequence resets trigger a resync without a false
overrun.

### D9 — `SyncRunner` owns one consumer and one wait set

`SyncRunner<Logger>` owns one `SyncConsumer<Logger>` and one
`EventConsumerSet<16>`, and attaches the consumer during creation.

`run()` registers a `std::stop_callback` that calls `wait_set.wake_up()`,
then loops:

```text
outcome = wait_set.wait_for(consumer.next_wait_timeout())
Events  -> consumer already drained by the set
Timeout -> consumer.check_sync_loss(raw_now)
WokenUp -> check stop, otherwise continue
```

`run_once()` provides the same step for applications that own their outer
loop. Multiple synchronization channels use multiple runners; v1 does not
multiplex several sync channels in one set.

### D10 — Metrics use a dedicated `Meter` and `MultiThreaded` instruments

`SyncGenerator::create` and `SyncConsumer::create` take `metrics::Meter&` and
create their own instruments. The meter must be dedicated to one sync channel,
because metric names are fixed and duplicate registration fails.

All instruments use `ConcurrencyPolicy::MultiThreaded` because the provider's
background harvester reads while the generator/consumer records. The metrics
module already provides 32 cache-line-aligned shards (`alignas(64)`) and
relaxed atomic recording; `synchro` does not add its own shards.

`SyncGenerator` stores a private `SyncMetrics` struct of raw pointers. It does
not own the meter, provider, or instruments. No `shared_ptr` is used: the
instruments are registry-owned and a shared pointer with a no-op deleter would
not extend their lifetime.

Metric names:

```text
sync_cycles_total
sync_phases_total
sync_overruns_total
sync_jitter_ns
sync_state
sync_consumer_received_total
sync_consumer_dropped_late_total
sync_consumer_transport_lost_total
sync_consumer_generator_skips_total
sync_consumer_ipc_latency_ns
sync_consumer_e2e_latency_ns
sync_consumer_locked
```

### D11 — Structured logging is a template parameter

`SyncGenerator<Logger>`, `SyncConsumer<Logger>`, and `SyncRunner<Logger>` take
`Logger&` and use the project `logging::concepts::Logger` contract. The default
is `NullLogger`. No polymorphic `ILogger` is introduced.

Lifecycle and anomaly logging uses the logger's deferred formatting. The
steady-state publication path does not log every phase; it logs only lifecycle
events and overrun/loss anomalies.

### D12 — `SyncError` is a structured value

`SyncErrorKind` classifies the failing area. `SyncError` carries the kind, a
`com::Error`, a `core::ErrorCode`, and an `os_errno`. The cause fields are
non-optional sentinels (`com::Error::Ok`, `core::ErrorCode::Ok`, `0`); each is
meaningful only for the matching kind, and no field is `std::optional`.
`SyncResult<T>` and `SyncStatus` are `std::expected` aliases.

The `kind` discriminates which cause field is meaningful:

```text
InvalidConfig -> core = InvalidArgument
Transport     -> transport = original com::Error
Metrics       -> core = original core::ErrorCode
Clock/EventFd/Thread -> core = SystemError, os_errno = errno
Lifecycle     -> core = Conflict/ResourceExhausted
```

`SyncErrorKind` and `SyncError` follow the project serialization mandates.

### D13 — Memory layout and atomics

Hot value layout:

| Type | Layout |
|---|---|
| `SyncPayload` | `alignas(8)`, 32 bytes |
| `SyncContext` | `alignas(8)`, 88 bytes: cycle 0, phase 8, phases 12, scheduled 16, period 24, publish 32, receive 40, sequence 48, epoch 56, ipc latency 64, e2e latency 72, `is_late` 80, padding to 88 |
| `SyncMetrics` | cold struct of pointers; no atomics |
| `SyncGeneratorStats` | cold snapshot; read from atomic counters written only by the worker |
| `SyncConsumerStats` | cold snapshot, plain counters; read on the runner thread or after the run |

Cross-thread scalars:

```cpp
alignas(64) std::atomic<SyncState> state_{SyncState::Stopped};
alignas(64) std::atomic<SyncConsumerState> state_{SyncConsumerState::Unsynced};
```

Every generator counter lives in `StatsStorage` as a
`std::atomic<std::uint64_t>` (or `std::int64_t` for jitter). Each field has
exactly one writer, the generator worker, so relaxed operations preserve the
value and never race with a concurrent `stats()` reader; `snapshot()` copies
them into the plain `SyncGeneratorStats` value. The snapshot is telemetry, not a
transactionally consistent view, and may straddle one publication step.
`SyncConsumer::state_` is atomic because the runner thread writes it while
observers may read `state()` concurrently; it is updated only by the dispatch
thread and aligned to keep it off the hot fields' cache line.

Stores use `std::memory_order_release`; the corresponding readers use
`std::memory_order_acquire`. No other sync-level atomics are added:
`WorkerThread`, `EventConsumerSet`, and the metrics instruments own their
existing synchronization.

## Risks / Trade-offs

- **`ppoll` timeout granularity** → the kernel may round the sleep; the final
  spin covers the last `spin_threshold_ns`. The threshold must stay below the
  phase period and is validated.
- **Spin CPU burn** → default 30 µs per phase; configurable, and the minimum
  1 ms phase period bounds the duty cycle.
- **Metric name collisions** → one meter per channel is required and validated
  by the duplicate-name failure.
- **Provider lifetime** → meter, provider, and instruments must outlive the
  generator/consumer; documented as a `@warning` and enforced by keeping raw
  pointers private.
- **Handler allocation** → the phase-handler table is allocated during
  `create()`; applications must not allocate inside handlers.
- **16-consumer ceiling** → documented; scale-out uses multiple channels and
  runners.
- **Legacy `EventConsumerSet::wait()` semantics** → `SyncRunner` uses
  `wait_for()` and owns the loop, so no behavior change is required in `com`.

## Migration Plan

No existing behavior changes. The module is additive and COM-gated. Rollback
is deletion of the new headers, sources, target, tests, and specs; no data or
wire-format migration is required.

## Open Questions

None blocking. Future changes may relax the 1 ms minimum, add multi-channel
runner attachment, or add hardware-clock calibration.
