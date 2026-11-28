# Design

## Context

See `proposal.md` for motivation. The existing `nexigen::canopen` target is a pure data-plane
library: EDS/DCF parsing, PDO unpacking, SDO assembly, and COB-ID routing. It links only
`nexigen_core`, `nexigen_chrono`, and `nexigen_hw_can`; the core spec requires it to build without
COM, GUI, viewer, or application code.

`nexigen::synchro` already demonstrates the intended runtime pattern: a class templated on
`logging::concepts::Logger`, a borrowed `metrics::Meter&`, one-time metric registration, and a hot
loop that only increments instruments. `nexigen::com` demonstrates the same meter-as-template
pattern with a `core::NullMeter` default for counters.

The metrics subsystem has no per-metric labels: identity is meter name + version + metric name +
unit. The default provider capacity is 16 meters, 64 counters, 64 gauges, 16 state gauges, and 16
explicit histograms. These constraints drive the per-bus meter decision and the startup capacity
check.

## Goals / Non-Goals

**Goals:**

- Keep `nexigen::canopen` free of logger, meter, tracer, and diagnostic-sink dependencies.
- Provide a runtime-facing observability component that a future CANopen bus runtime and
  `nexigen-canopend` can consume without re-inventing logging and metrics.
- Make hot-path errors observable without allowing a repeated error to flood the log ring.
- Bound memory and metric cardinality per bus.
- Document the reusable `OBS-*` embedding scenarios for the rest of the project.

**Non-Goals:**

- Implementing the CANopen multi-node runtime, `nexigen-canopend`, NMT state machines, PDO
  scheduling, or SDO client/server logic.
- Changing `nexigen::canopen` APIs or adding stats getters to core classes.
- Adding per-node or per-PDO metric labels; the metrics API does not support them.
- Implementing Tier 2 metrics in this change.
- Implementing trace spans and PHM/SM escalation mapping in this change; their correlation points
  are recorded below as deferred work for the runtime change.
- Refactoring existing modules (`synchro`, `com`, `exec`) onto the new documentation in this
  change; they already follow most of the patterns and are reference examples.

## Decisions

### D1: Runtime owns telemetry; core stays pure

The runtime layer owns the logger and meter. Core functions return structured outcomes:
`PdoDecodeResult`, `SdoEvent`, `EdsParseResult`, `Status`, and `Result`. The runtime maps those
outcomes to Tier 1 metrics and to the log gate. `detail::metric_mapping` carries that mapping as
one-line helpers, so every call site records the same instrument and the drop-total, decode/mapping,
and metric-first rules cannot drift between call sites.

Alternative rejected: templating core classes on logger/meter. It would pull `nexigen_logging` and
`nexigen_metrics` into `nexigen_canopen`, turn pure decoders into stateful templates, and require a
broader null-meter type that supports gauges and histograms. The `core::NullMeter` only satisfies
`MeterLike` (counters).

### D2: One runtime per bus, one meter `canopen.<bus>`

Each `CanOpenBusRuntime` owns one meter obtained from the provider and names it
`canopen.<bus>` (for example, `canopen.can0`). The meter identity supplies the bus dimension that
labels would otherwise provide. Duplicate bus names are rejected because `get_meter` returns the
same handle for the same name/version, which would make two runtimes share metric names and fail
registration or double-count.

Alternative rejected for the current deployment: one aggregate `canopen` meter. It would lose
per-bus visibility. It remains the fallback for deployments with many virtual buses; the per-bus
decision is safe for the stated 1-2, rarely 3, buses.

### D3: Tier 1 catalogue is normative; Tier 2 is deferred

Tier 1 per bus:

- Counters (25): `canopen_cycles_total`, `canopen_cycle_overruns_total`,
  `canopen_frames_received_total`, `canopen_frames_processed_total`,
  `canopen_frames_unrouted_total`, `canopen_frames_dropped_total`,
  `canopen_rx_overflow_total`, `canopen_tx_queue_full_total`, `canopen_driver_drop_total`,
  `canopen_pdo_rx_total`, `canopen_pdo_tx_total`, `canopen_pdo_decode_errors_total`,
  `canopen_pdo_mapping_errors_total`, `canopen_sdo_requests_total`, `canopen_sdo_aborts_total`,
  `canopen_sdo_timeouts_total`, `canopen_nmt_transitions_total`,
  `canopen_heartbeat_misses_total`, `canopen_emcy_total`, `canopen_signal_updates_total`,
  `canopen_signal_stale_total`, `canopen_signal_out_of_range_total`, `canopen_tx_frames_total`,
  `canopen_tx_failures_total`, `canopen_bus_off_total`.
- Gauges (4): `canopen_rx_queue_depth`, `canopen_tx_queue_depth`,
  `canopen_sdo_active_transfers`, `canopen_nodes_online`.
- State gauge (1): `canopen_state`.
- Explicit histogram (1): `canopen_loop_latency_ns`.

All instruments use `MultiThreaded`, `Readable`, and `Cumulative`. The runtime thread is the only
writer, but the provider harvester may read concurrently, so the sharded multi-threaded storage is
required. Counter recording is a relaxed RMW on the caller's own shard; no cross-core CAS loop.

Hot-path budget on the reference machine: at most 10 ns per counter increment (the existing
`mt_inc` latency gate is 6 ns median), at most 50 ns for one error-gate decision, zero heap
allocations, and no locks or virtual dispatch. The benchmark task verifies the budget.

`canopen_frames_dropped_total` is a total, not an independently observed event. The runtime
increments it together with exactly one cause counter. A test verifies that the snapshot total
equals the sum of the three cause counters.

`canopen_frames_unrouted_total` is separate from drops: the frame was received and inspected but
had no route, so it is not counted as processed or dropped.

### D4: Deferred Tier 2 catalogue

Tier 2 is recorded here so it is not lost; it is non-normative in this change. Activate it only
after the runtime exists and the provider capacity is provisioned.

- Byte and frame detail: `canopen_rx_bytes_total`, `canopen_tx_bytes_total`,
  `canopen_error_frames_total`, `canopen_overload_frames_total`.
- SDO detail: `canopen_sdo_responses_total`, `canopen_sdo_bytes_total`,
  `canopen_sdo_segmented_total`, `canopen_sdo_expedited_total`, `canopen_sdo_duration_ns`.
- PDO detail: `canopen_pdo_jitter_ns`, `canopen_pdo_period_ns`, `canopen_pdo_dropped_total`.
- NMT detail: `canopen_bootups_total`.
- EMCY detail: `canopen_emcy_critical_total`, `canopen_emcy_history_evictions_total`.
- Signal detail: `canopen_signal_saturation_total`, `canopen_signal_conversion_errors_total`.
- Health detail: `canopen_phm_escalations_total`.

Deliberately excluded: an `ipc_publish_failures_total` CANopen counter. If the runtime publishes
through `com::EventProducer`, the producer already owns `published` and `publish_failed`; the
runtime should pass an appropriate meter to the producer and log only the error edge.

### D5: Error gate is a fixed-size, single-writer array

The gate covers 128 node slots (0-127) and 9 domains: `Bus`, `Transport`, `Nmt`, `Sdo`, `Pdo`,
`Emcy`, `Signal`, `Ipc`, `Config`. It is preallocated once per runtime.

```cpp
struct alignas(8) EpisodeGate {
    std::uint64_t last_error_ns;    // 8
    std::uint64_t last_log_ns;      // 8
    std::uint64_t episode_start_ns; // 8
    std::uint32_t suppressed;       // 4
    std::uint32_t last_code;        // 4
    std::uint8_t active;            // 1
    std::uint8_t logged;            // 1
    std::uint8_t reserved[6];       // 6
};                                  // 40 bytes
static_assert(sizeof(EpisodeGate) == 40);
```

Storage is `std::array<std::array<EpisodeGate, 9>, 128>`: 46,080 bytes (45 KiB) per bus. There are
no atomics and no cache-line padding because the runtime thread is the only reader and writer.
`poll_recovery` and the periodic summary run on the same thread; if a later design moves them to a
control thread, they must take a snapshot under an explicit synchronization edge rather than read
the gate concurrently.

The gate state is not shared with the metrics harvester. The metrics instruments are the only
concurrent surface.

### D6: Episode policy and suppression ladder

The policy follows proposal A:

1. Record the metric before any level or suppression check.
2. On an idle key, open an episode and emit one log at `hot_error_level`.
3. On the same code inside an episode, increment `suppressed`; emit at most one repeat per
   `repeat_interval_ms`, or immediately when `suppressed >= max_suppressed_per_key`.
4. On a code change, emit one record immediately and update `last_code`.
5. On `poll_recovery`, after `recovery_hold_ms` without an error, emit one recovery record with the
   total suppressed count and close the episode.
6. First-error and recovery records bypass the global budget; repeat records consume a per-runtime
   token bucket of `max_hot_logs_per_second`.
7. Keep `SpamProtection` enabled as a safety net. The component gate is the primary semantic
   suppressor; the logger remains the last line of defense for every module.
8. Before logging, check `logger.level() <= hot_error_level` so a filtered record does not mark the
   episode as logged. The metric is already recorded.

Defaults: `repeat_interval_ms=5000`, `recovery_hold_ms=5000`, `max_suppressed_per_key=10000`,
`summary_interval_ms=60000`, `max_hot_logs_per_second=50`, `hot_error_level=Error`,
`recovery_level=Info`, `summary_level=Info`, `max_cold_diagnostics=32`.

The interval values are deliberately conservative. A 1-second repeat per key still produces 12
lines per minute per persistent error; 5 seconds reduces that to 12 lines per minute only if the
error is observed once per interval, and much less under a burst because `max_suppressed` and the
global budget bound the rest.

Alternative rejected: leaving suppression entirely to `SpamProtection`. It cannot express
"first error, code change, recovery" or produce a domain summary, and identical-message hashing
does not collapse errors whose payload changes on every occurrence.

### D7: Cold path and EDS diagnostics are bounded

EDS load, configuration, lifecycle, and NMT transitions are logged with context. The EDS parser can
return an unbounded diagnostics vector, so the runtime logs the first `max_cold_diagnostics`
entries and one trailing "omitted N" record. Cold-path logging may allocate; it never runs in the
per-frame loop.

Successful SDO transactions and PDO mapping setup are `Debug`, not `Info`, so a normal production
run does not log per transaction.

### D8: Startup registration is fail-fast

The runtime computes the required instruments from the Tier 1 catalogue and creates them before
opening the transport. On failure `RuntimeMetrics::register_metrics()` returns a `RegistrationFailure`
carrying the failing instrument name and the mapped error (`ResourceExhausted` for an exhausted pool,
`InvalidArgument` for an invalid or duplicate name); the runtime logs that diagnostic and does not
open the transport. It does not run a partially instrumented bus.

Capacity guidance for deployment:

- Tier 1 per bus: 25 counters, 4 gauges, 1 state, 1 explicit histogram.
- Two buses: 50 counters; the default 64 leaves 14 for other modules.
- Three buses: 75 counters; the provider must be configured with at least 91 counters to leave 16
  slots for other modules.
- Recommended formula: `max_counters >= 25 * buses + 16`.
- `max_meters >= buses` plus the meters used by `com`, `logging`, `synchro`, and other modules.

### D9: New runtime-facing target, not a core change

Add `nexigen_canopen_observability` as a static library with headers under
`include/nexigen/canopen/observability/` and sources under `src/canopen/observability/`. It links
`nexigen_canopen`, `nexigen_logging`, and `nexigen_metrics`. `nexigen_canopen` remains unchanged and
does not link the new target. Public vocabulary lives in `nexigen::canopen::observability`;
runtime metrics and the gate stay in that namespace's `detail`.

Public types:

- `CanOpenLogPolicy`: thresholds, levels, and `max_cold_diagnostics`; JSON/`to_string`/formatter.
- `CanOpenRuntimeState`: `Stopped`, `Starting`, `Running`, `Degraded`, `Error`; canonical
  `to_string`/`from_string`, JSON, formatter.
- `CanOpenErrorDomain`: the nine domains; canonical `to_string`/`from_string`, JSON, formatter.

`detail::RuntimeMetrics` holds the instrument pointers and a `register_metrics(Meter&)` factory.
`detail::ErrorGate` owns the fixed episode array, the global budget, and the summary counters.
`detail::metric_mapping` provides the runtime-facing `record_*`/`set_*` helpers and the
`record_hot_error()` composition that increments the metric before calling the gate.

### D10: Document the project-wide embedding scenarios

Add `docs/observability-embedding.md` with named scenarios:

- `OBS-BOOT`: pre-logger bootstrap diagnostics.
- `OBS-COLD`: detailed lifecycle/configuration journal.
- `OBS-EDGE`: log on state transition, first error, and recovery.
- `OBS-HOT-ERR`: bounded hot-path error event.
- `OBS-HOT-MET`: metrics-first hot path.
- `OBS-SUM`: windowed cold-path summary.
- `OBS-SNAP`: pull-based diagnostic snapshot.
- `OBS-SPAN`: low-frequency transaction span.

Add `docs/adr/0005-observability-embedding.md` recording the decision that runtime/service layers
own telemetry, core modules stay telemetry-free where practical, logger types are template
parameters, meters are borrowed references, and every observable event has one owner. Reference
`synchro`, `com`, and this CANopen layer as examples.

### D11: Trace spans and PHM/SM correlation are deferred

The runtime does not exist yet, so this change only reserves the `OBS-SPAN` scenario and records
the intended correlation points. The runtime change will add:

- `canopen.eds.load`: cold-path span around EDS/DCF load with path, content hash, object count, and
  duration.
- `canopen.sdo.transaction`: span from request to completion/abort, with node, index/subindex,
  direction, segmented flag, and size. Only failed transactions are also logged by the hot-error
  gate.
- `canopen.pdo.cycle`: low-frequency or sampled span around a PDO/SYNC processing cycle, never one
  span per frame.

PHM/SM escalation is also deferred: bus-off, heartbeat timeout, critical EMCY, and cycle deadline
miss are the events that will map to `DiagnosticEvent`/PHM recovery. The runtime change will own
that mapping so the CANopen core stays free of PHM and SM dependencies.

## Risks / Trade-offs

- [Capacity exhaustion with other modules] -> startup check, documented formula, and a clear
  `ResourceExhausted` diagnostic naming the instrument. Deployments with 3+ buses must raise
  `max_counters`.
- [45 KiB gate per bus] -> acceptable for 1-3 buses; a sparse map can replace it later without
  changing the spec if memory becomes a constraint.
- [Gate is single-threaded] -> recovery and summary polling must stay on the runtime thread, or a
  future control-thread design must snapshot under synchronization.
- [Logger ring drops under pressure] -> the metric still counts; the logger's own drop summary and
  metrics expose the loss. The runtime does not retry log records.
- [Per-bus meter loses cross-bus aggregation] -> each meter identity carries the bus; a future
  aggregate view can sum across meters in `hub` without adding labels.
- [Tier 2 pressure] -> deferred until runtime exists and capacity is provisioned; the design keeps
  the names and ownership so the extension is additive.

## Migration Plan

1. Add the new target, public vocabulary, metric registration, and error gate with unit tests.
2. Add the observability embedding doc and ADR; update glossary entries.
3. Validate with Debug, Release, TSan, and ASan builds and run the full CTest suites.
4. The future CANopen runtime change links `nexigen_canopen_observability`, creates one runtime per
   bus, and wires the metric/gate call sites; no migration of existing data or APIs is required.

Rollback is removing the new target and docs; `nexigen_canopen` and existing modules are untouched.

## Open Questions

- Per-domain repeat intervals (for example, a longer interval for signal-quality warnings) are
  deferred. The current contract uses one policy for all domains; a later additive change can add
  per-domain overrides without breaking the defaults.
- The exact runtime configuration file shape belongs to the future runtime change. This design only
  fixes the policy defaults and the capacity formula.
