# Design

## Context

See `proposal.md` — Why. The example builds on existing, already-specified primitives: `nexigen::com` (`Node`/`ServiceName`/`Publisher<T,H>`/`Subscriber<T,H>` over iceoryx2), `tracing::TraceHeader`/`current_trace_context()` for cross-process propagation, the three daemons (`nexigen-logd`/`nexigen-metricsd`/`nexigen-traced`) that subscribe to iceoryx2 telemetry frames, `nexigen::app::Infrastructure` for app bootstrap, and `nexigen::exec` for manifest-driven launch with `ThreadSpec` affinity. The only new surface is the example itself under `examples/integration/suite_2_apps/`.

## Goals / Non-Goals

**Goals:**
- One end-to-end scenario proving `com` data exchange carries a trace, and that logs/metrics/traces reach the daemons, all launched via `ExecutionManager` with a concrete CPU-affinity map.
- A self-contained driver (`run_suite_2_apps.sh`) that generates all config into a demo root, plus an idempotent cleanup.

**Non-Goals:**
- No change to any library API, spec (except the example catalogue), settings vocabulary, or daemon behavior.
- No real-time scheduling (see D8); no fault-injection or PHM/SM crash-recovery path in this demo.
- No chrono daemon: chrono uses a null sink (chrono has no iceoryx2 sink).

## Decisions

### D1 — Two separate executables, not one role-based binary

`ex_generator` (producer) and `ex_receiver` (consumer) are asymmetric: different `com` ports, different metric sets, different trace roles. Two small self-contained binaries are clearer than one binary switching on an env var (the `ex_suite_app` role pattern). Each links `nexigen::app` and `nexigen::com`.

### D2 — Payload is a fixed, trivially-copyable struct

`DataSample { std::uint64_t sequence; char message[64]; }` — 72 bytes, `alignof == 8`, satisfies `com::concepts::Message` (trivially copyable), so it crosses shared memory with no pointers or owning members. No atomics: `sequence` is a single-writer counter in the generator. No `alignas(64)` is required because no shared cache-line-resident atomic is introduced; the transport's own shards are already cache-line aligned per the existing `com` specs.

### D3 — Trace rides as the custom header, via loan/write/send

`send_copy` cannot attach the `TraceHeader`, so the generator uses the loan path: `loan_uninit()` → `write_payload(sample)` → `sample.user_header_mut() = tracing::make_trace_header(seq)` → `send()`. `make_trace_header` pulls `trace_id` from the thread's `TraceContext` (set by `NEXIGEN_TRACE_NEW_ROOT` once per second) and `source_span_id` from the active span. The receiver does `receive()` → `sample.user_header()` → writes `trace_id`/`source_span_id` into `current_trace_context()`, then `start_span` inherits it. This is the documented propagation contract (`trace_header.hpp`, `trace_context.hpp`), not a new mechanism.

### D4 — Telemetry via iceoryx2 sinks, wired in the `Infrastructure` policy

The standard `logging::Settings`/`metrics::Settings`/`tracing::Settings` are file-sink-oriented, so the policy's `emplace_logger`/`emplace_metrics_sink`/`emplace_trace_sink` construct the iceoryx2 configs directly rather than through `apply_to`:

- `Iceoryx2LoggerConfig` with `service_name = "Nexigen/Log/nexigen"`, `app_id` = 4-byte code from the app name (`GENS`/`RECV`), `initial_level` from settings, and `worker_thread` from the settings' canonical worker spec.
- `Iceoryx2MetricSinkConfig` with `service_name = "nexigen/metrics/stream"` and the same app code (the metric frame is published by the provider's harvester thread, whose affinity is the settings `worker_thread`).
- `Iceoryx2TraceSinkConfig` with `service_name = "nexigen/trace/stream"`.

Service names are the daemon defaults, so sinks and daemons connect without extra wiring. Chrono uses `chrono::NullSink`.

### D5 — Trace observability via the log daemon

`Iceoryx2TraceSink` is non-movable (copy and move deleted), so it cannot be a `CompositeTraceSink` member (the `Fanout` base injects members by value). The policy therefore uses `Iceoryx2TraceSink` directly, publishing spans to the trace daemon. The propagated `trace_id` is made observable through the log daemon instead: both applications log their per-sample `trace_id`, so a single `trace_id` appears in the generator's publish record and the receiver's receive record in the log daemon's JSON-lines output.

### D6 — Driver generates all config into a demo root

`run_suite_2_apps.sh` mirrors `run_suite_demo.sh` design D6: it generates `suite.json`, the three daemon configs, and the per-app settings under `/tmp/opencode/nexigen-suite-2-apps/` (default `NEXIGEN_DEMO_ROOT`), so no machine-specific absolute path is committed. The daemons require absolute `log_directory`/`storage_directory`/`dump_directory`, so those are derived from the demo root at generation time.

### D7 — Concrete affinity map

```
CPU 2  EM control thread      { cpus:[2], name:"em-ctrl"  }
CPU 3  EM logger thread       { cpus:[3], name:"em-log"   }
CPU 4  nexigen-logd bg, metricsd bg
CPU 5  nexigen-traced bg, metricsd http_thread
CPU 6-7  generator: apps[].cpus=[6,7]; log worker [6], metrics worker [7], trace worker [6]
CPU 8-9  receiver:  apps[].cpus=[8,9]; log worker [8], metrics worker [9], trace worker [8]
```

Two levels: `apps[].cpus` sandboxes the whole child process; the per-subsystem `worker_thread` (a `ThreadSpec`) pins each background worker *within* that sandbox. This is the existing `ThreadSpec`/`CpuSet` contract, not new machinery.

### D8 — `SCHED_OTHER`, priority 0, everywhere

`SCHED_FIFO` requires `root`/`CAP_SYS_NICE` and is rejected by `ThreadSpec` at priority 0; on an unprivileged dev machine it would degrade to `best_effort` warnings and never actually apply. The demo has no real-time workload, so `SCHED_OTHER` (normal priority) is portable and matches the "normal priorities" requirement. Real-time scheduling stays a documented non-goal.

### D9 — Launch order via `dependencies`

`ex_receiver` has no dependencies; `ex_generator` declares `dependencies: ["receiver"]`. The suite supervisor launches the receiver first and delays the generator until the receiver reports Running (the runner's `setup_status=0` handshake, already automatic).

## Risks / Trade-offs

- [iceoryx2 shared memory availability] → The demo depends on `/dev/shm`; the existing examples already do, and the payload is small, so no custom iceoryx2 config is needed.
- [Trace daemon writes nothing in steady state] → D5 composite sink keeps traces observable on disk.
- [Unprivileged run cannot set real-time scheduling] → avoided by D8 (`SCHED_OTHER`).
- [Receiver polling vs event wait] → the receiver polls `receive()` in the ~100 ms `Infrastructure::run()` tick loop; with a 1 Hz producer and a subscriber buffer this loses nothing (discards are counted, not silent).
- [Multiple iceoryx2 nodes per process] → each iceoryx2 sink creates its own node, and the example creates one more for the data service; iceoryx2 supports multiple nodes per process, and each sink already owns its node.

## Migration Plan

Additive only. Rollback = delete `examples/integration/`, revert the `add_subdirectory` line and the README edit.

## Open Questions

None.
