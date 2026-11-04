# Proposal

## Why

Background thread placement is configured through three incompatible surfaces: `uint64_t core_mask` (limited to CPUs 0–63), runner CPU vectors, and subsystem-specific affinity wrappers. Scheduling policy, priority, and thread names are not configurable uniformly. A real-time deployment therefore cannot apply a precomputed thread plan above CPU 63 or distinguish an explicitly required FIFO/RR configuration from a deliberate best-effort fallback.

This change provides one cold-path vocabulary and application mechanism for every existing observability worker and the runner control/logger paths. It does not make runner a scheduler and does not change producer, harvest, tracing, backoff, or flush algorithms.

## What Changes

- Add `core::CpuSet`, a fixed-capacity 1024-CPU set with no heap storage, conversions from legacy masks/vectors, canonical serialization, and explicit validation of duplicates and out-of-range IDs.
- Add `core::ThreadSpec` (name, CPUs, `Other`/`FIFO`/`RR`, priority, `BestEffort`/`Required`) and `ThreadApplyReport`. A thread applies a copied spec to itself before sink/provider startup; required failures are reported and rolled back, while best-effort partial application remains visible.
- Migrate observability worker configuration from public `core_mask` fields to `ThreadSpec`: FileLogger, ConsoleLogger, MetricProvider harvesting, TracerProvider consumption, and ChronoProvider snapshots. Preserve existing workers, wait algorithms, and default inherited affinity.
- Normalize legacy JSON/environment affinity inputs into the new specs. Keep old persisted keys readable; emit only the canonical new schema. New nested thread settings are file-only, following the existing sink-settings precedent; existing environment overrides keep working.
- **BREAKING (pre-1.0 C++ API)**: replace public config `core_mask` fields and runner `std::vector<uint32_t> cpus` with `ThreadSpec`/`CpuSet`. This avoids two competing sources of truth. Persisted `core_mask`, `flush_cpu_affinity`, `cpus`, `runner_control_cpus`, and `runner_logger_cpus` remain compatibility inputs with deterministic precedence.
- Use `CpuSet` for `StartOptions::cpus` and `AppSpec::cpus`, and `ThreadSpec` for runner control/logger configuration. Runner only applies supplied values; it does not calculate a real-time schedule.
- Extend existing initialization rollback so a required worker-attribute failure stops and joins the worker before its main loop and tears down earlier infrastructure components. Default best-effort behavior remains non-fatal and observable through startup reports.
- Add deterministic fault-injection tests for kernel errors (including `EPERM`) and opt-in real-FIFO/RR integration tests for an explicitly configured RT host; normal CI requires neither root nor `CAP_SYS_NICE`.

## Capabilities

### New Capabilities
- `core/thread-management`: fixed-capacity CPU sets, immutable thread specifications, self-application and partial-application reports, required/best-effort semantics, rollback, validation, and serialization.

### Modified Capabilities
- `core/affinity`: the uint64-mask API becomes a compatibility adapter over the shared implementation; module shims remain forward-only.
- `logging/file-logger`: writer configuration, startup reporting, and required-failure rollback use `ThreadSpec`.
- `logging/console-logger`: the console writer gains the same optional thread specification without changing default placement.
- `logging/logger-options`: the engine configuration carries the canonical worker thread spec instead of a parallel affinity list.
- `logging/logging-settings`: legacy and canonical worker settings merge into the logger configs with documented precedence.
- `metrics/metric-provider`: the harvest worker applies `ThreadSpec` before its first batch; disabled harvesting starts no thread.
- `metrics/metrics-settings`: provider settings read and serialize the canonical worker spec and normalize legacy affinity.
- `tracing/tracer-provider`: the existing consumer applies `ThreadSpec`; sinks acquire no new threads.
- `tracing/tracing-settings`: provider settings read and serialize the canonical worker spec and normalize legacy affinity.
- `chrono/chrono-provider`: the snapshot worker applies `ThreadSpec`; deferred start and zero-interval behavior remain unchanged.
- `chrono/chrono-settings`: provider settings apply the canonical worker spec and normalize legacy affinity.
- `app/application`: required worker-attribute failures participate in the existing reverse-order infrastructure rollback.
- `runner/process-supervision`: child launch consumes `CpuSet` while preserving pre-`exec` validation and failure behavior.
- `runner/app-suite`: manifest and suite metadata use the shared CPU/thread vocabulary without changing application scheduling policy.
- `runner/logging`: runner control/logger thread specs replace mask-only configuration; legacy lists remain readable and best effort by default.

## Impact

- **Code**: new public core thread-management header(s) and cold serialization; observability config/start paths; per-subsystem settings loaders/formatters; app initialization rollback; runner config conversion and pre-`exec` affinity. No producer, SPSC, instrument, sink callback, backoff, harvesting, or flushing algorithm changes.
- **API**: pre-1.0 source migration for config fields listed above; legacy JSON and environment inputs remain accepted. `pin_self`/`self_affinity` remain compatibility functions. No new external dependency and no parallel application-settings system.
- **Performance / latency / memory allocation**: no per-record or per-tick cost and no new hot-path allocations, locks, syscalls, or atomic accesses. A worker copies a 192-byte `ThreadSpec` and performs a bounded set of cold syscalls once at startup. `CpuSet` is 128 bytes inline; no heap is used by either type. Existing Release median gates (logging 20 ns/3 ns, metrics 2–25 ns, tracing 20 ns/2 ns, diagnostics 20–100 ns) must remain unchanged and pass on dedicated hardware.
- **Failure behavior**: invalid specs fail before any attribute is changed. Required OS failures abort initialization and roll back; explicitly requested best effort continues with a report identifying unapplied attributes. No required FIFO/RR request can silently degrade to `Other`.
- **Tests**: Debug, Release with native/LTO and strict latency gates, portable Release, TSan, ASan/UBSan, Clang with `-Werror`/clang-tidy, fault-injection, coverage, packaging, and opt-in RT-host integration. Root-free CI is preserved.
- **Follow-up changes, deliberately separate**: `mlockall`/prefault; HostCaps and host RT setup; an actual-placement ThreadRegistry. SMT control and future compute-loop RT threads are also outside this change.
