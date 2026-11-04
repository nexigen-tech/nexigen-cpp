# Design

## Context

See proposal.md — Why. The current implementation has two affinity representations and no common scheduling-attribute model. The change must preserve the existing wait-free producer paths and the different worker lifecycles rather than impose a generic worker manager.

Dependency direction remains `module → core`; no module may include another module's `detail/`. The shared implementation therefore lives in public `nexigen::core`, beside `affinity.hpp` and `worker_tuning.hpp`. Linux/glibc affinity and naming interfaces are already build requirements; no dependency is added.

### Actual thread inventory

| Owner | Actual background execution | Start and stop | Attribute integration |
|---|---|---|---|
| `FileLogger` / `ConsoleLogger` | One `detail::AsyncWorker` `std::jthread` per logger; console and file sinks do not create threads | `initialize()` starts; last-declared `jthread` joins during destruction and final-drains | Apply a copied `ThreadSpec` in `run()` before `sink.start()`; preserve the existing loop, backoff, flush, and member order |
| `MetricProvider` | One harvest `std::jthread` only when `collect_interval > 0`; metric sinks have no worker | Constructor starts; destructor requests stop, notifies, joins, final-collects and flushes | Apply before the first harvest. Required failure publishes `startup_status()` and leaves no running harvester; Infrastructure checks the status and rolls back |
| `TracerProvider` | One consumer `std::jthread` after successful `initialize()`; every tracing sink is callback-driven and has no thread | `initialize()` starts; destruction joins and final-drains | Apply before `binding_.start`; preserve drain and flush behavior |
| `ChronoProvider` | One snapshot `std::jthread` only for a positive interval when `start()` is called; chrono sinks have no worker | Two-phase start; destructor joins and emits the final snapshot | Apply before the first snapshot; `start()` returns a required-application failure |
| `Infrastructure::SignalWatcher` | One `signalfd` watcher | Started last and stopped first during rollback/shutdown | Left unchanged: app has no independent thread-settings document, and adding one would create the prohibited parallel configuration system |
| Runner | CLI or suite-supervisor control runs on its calling thread; the runner file logger has its normal worker; per-instance supervisors are processes, not new thread types | Control attributes are applied on the calling thread before its control loop; logger uses the normal worker path | Apply only supplied specs. Child `cpus` is still applied in the child immediately before `exec` |

No worker is merged with another, and no sink acquires a thread. `NullLogger`, `CompositeLogger`, and `Infrastructure::run()` add no worker of their own.

## Goals / Non-Goals

**Goals:**
- One value-semantic CPU/thread vocabulary with deterministic validation, cold serialization, and self-application.
- Explicit required versus best-effort failure behavior and a report that distinguishes unapplied attributes.
- Initialization rollback without changing default asynchronous startup or any steady-state algorithm.

**Non-Goals:**
- No runtime thread registry, placement verification service, host-capability discovery, memory locking/prefault, SMT policy, stack sizing, or common infrastructure worker.
- No configuration for future application compute threads and no runner-side scheduling decisions.
- No new stop/restart API, no change to flush semantics, and no repair of unrelated runner/reset or settings omissions.
- No new environment-variable family for nested thread objects; existing legacy environment affinity inputs remain supported.

## Decisions

### D1. Fixed 1024-bit `CpuSet`, not a dynamic bitset

```cpp
class alignas(64) CpuSet {
    std::array<std::uint64_t, 16> words_{}; // 128 bytes, CPU ids 0..1023
};
static_assert(sizeof(CpuSet) == 128);
```

The layout matches glibc's fixed `cpu_set_t`/`CPU_SETSIZE` representation, so conversion needs no `CPU_ALLOC` and no heap. A 128-byte value copy occurs only while constructing a cold config/worker start. The alternative `std::vector<uint64_t>` supports arbitrary kernel sizes but adds allocation, indirection, and less predictable copying for no requirement in this project. IDs above 1023 are rejected with `InvalidArgument`; they are never truncated.

`CpuSet` provides bounded `contains`, `empty`, `count`, insertion, `from_core_mask(uint64_t)`, and `from_cpus(std::span<const uint32_t>)` conversions. The vector conversion is a view and does not allocate. Empty is valid and means “do not issue an affinity call; preserve inherited placement.” Duplicate IDs in a parsed list are rejected with `InvalidArgument` rather than silently changing meaning. JSON is a sorted array of numeric CPU IDs; `to_string()` uses compact ascending ranges (for example `0,2-4,70`). Parsing rejects negatives, malformed ranges, duplicates, and IDs above 1023.

Structural validity and host availability are deliberately separate:

- An ID above 1023 is always a configuration error.
- An offline/nonexistent ID within the representation is accepted by the value type and reported by the kernel as `SystemError` when applied. Runner preserves its stricter pre-fork/pre-launch online validation and reports `InvalidArgument` before creating a process.
- The requested set need not be a subset of the thread's current affinity mask. Widening is valid when the kernel and the process's cpuset permissions allow it; a kernel/cpuset rejection follows the selected apply mode. There is no userspace “current mask subset” pre-check.

### D2. `ThreadSpec` is a fixed, immutable-at-start value

```cpp
class alignas(64) ThreadSpec {
    CpuSet cpus_;                         // offset 0,   128 bytes
    std::array<char, 16> name_{};         // offset 128,  15 bytes + NUL
    std::int32_t priority_{0};            // offset 144
    SchedulingPolicy policy_{Other};      // offset 148
    ThreadApplyMode mode_{BestEffort};     // offset 149
    bool scheduler_configured_{false};     // offset 150
    // padding to 192 bytes / 64-byte alignment
};
static_assert(sizeof(ThreadSpec) == 192);
```

Accessors and cold setters preserve invariants. `set_scheduler(policy, priority)` marks the scheduler configured; this is necessary because the default must preserve an inherited scheduler, while an explicitly supplied `Other, 0` must be able to reset it. Empty name and empty CPUs similarly skip their respective calls. The default spec therefore issues no attribute syscalls and preserves current behavior.

- Names longer than 15 **bytes** are rejected with `InvalidArgument`; they are never silently truncated. Empty means no rename. The stored array always has a trailing NUL.
- `Other` requires priority `0`. `FIFO` and `RR` require a priority in the inclusive OS range 1–99. Invalid combinations are rejected before any attribute is changed.
- `BestEffort` is the default for compatibility. `Required` is explicit; it never silently falls back to `Other` or an unplaced worker.
- Scheduler policy and priority are one `sched_setscheduler` operation, so there is no separate “policy applied but priority not applied” state.

`SchedulingPolicy` (`other`, `fifo`, `rr`) and `ThreadApplyMode` (`best_effort`, `required`) implement the project's enum `to_string`/`from_string`/JSON/`std::formatter` contract. `CpuSet`, `ThreadSpec`, and `ThreadApplyReport` implement member/free `to_string`, non-throwing JSON round-trip, and formatter support. Serialization is cold and may use standard strings/JSON; applying a spec never does.

### D3. Self-application with an explicit report and bounded rollback

The core entry point is:

```cpp
core::Status apply_thread_spec_self(const ThreadSpec& spec,
                                    ThreadApplyReport& report) noexcept;
```

The caller is always the thread to be configured. A copied spec is captured before `std::jthread` creation, so there is no race with a running worker and no parent/native-handle pinning window. Application happens before sink/provider startup and before the main loop.

`ThreadApplyReport` is a 64-byte cold-start snapshot with 32-bit requested/applied/failed/rollback-failed masks for `Name`, `Affinity`, and `Scheduler`, plus the first `core::ErrorCode`. The masks make partial application explicit (for example, affinity applied while FIFO returned `EPERM`).

Application order is name → affinity → scheduler:

1. Validate the complete spec. Invalid input returns `InvalidArgument` with no syscall and no state change.
2. If at least one attribute is requested, snapshot the current name, affinity, policy, and priority into fixed cold storage.
3. **Required:** stop at the first OS failure, restore already-changed attributes in reverse order, publish the original failure and any rollback failure, and return `SystemError`. No sink/provider loop starts.
4. **Best effort:** attempt every requested attribute, record every failed mask and the first error, return OK, and enter the normal worker loop. It never hides partial application.

Rollback uses only bounded cold syscalls. If restoration itself fails, the report exposes that fact and initialization still fails; there is no claim of a clean RT state. This same helper works for a new worker and for the runner's calling control thread.

### D4. Startup publication is the only new shared state

There is no atomic `ThreadSpec`, no runtime reconfiguration, and no registry. Each worker owns its copied spec. Its immutable report is written once before the existing/new startup-ready flag is published with `memory_order_release`; readers use `memory_order_acquire` on that flag and then read the report. Individual report fields need no stronger ordering and remain outside the main loop. Existing `writer_start_code()`/`startup_status()` surfaces map to the report's first error, while `writer_mask()` remains a low-64-bit compatibility view only.

Best-effort startup remains asynchronous as it is today. Required startup waits for the report so the caller can fail deterministically:

- Logging/tracing/chrono: on required failure, request stop and join before returning the error, reset the not-started sink/worker state, and leave destruction or a later reinitialization safe.
- Metrics: the constructor waits only for a required startup report. On failure no harvest loop runs and `startup_status()` is an error; Infrastructure observes it immediately and destroys the provider through normal rollback.
- No required path waits on a condition variable in a producer or worker loop; this is initialization-only synchronization.

### D5. Integration does not alter worker algorithms

Only the cold prefix of each worker changes. Existing drain limits, SPSC memory orders, condition-variable waits, interval calculations, backoff and timer slack, sink callbacks, final drains, flushes, and member destruction order remain byte-for-byte algorithmic behavior.

Infrastructure keeps its current order: logger → chrono → metrics → tracing → signal watcher. A required failure returned by any provider enters the existing reverse-order rollback (watcher, tracer, metrics, chrono, logger, signal mask, ownership). A zero metrics/chrono interval still creates no worker and therefore performs no thread application. Tracing and metric/chrono sinks remain callback-driven.

### D6. One settings pipeline, with compatibility at the boundary

Canonical per-subsystem JSON uses a `worker_thread` object with `name`, `cpus`, `policy`, `priority`, and `mode`. It is merged field by field by the existing global → local → environment pipeline and serialized through the existing settings objects. The nested object is file-only, matching the established nested sink-file policy; no second application settings file is introduced.

For each source, legacy affinity is applied first and canonical `worker_thread` fields second, so canonical values win within one source while a higher-priority legacy environment override still wins over lower-priority files:

- Logging `core_mask`/`flush_cpu_affinity` and `NEXIGEN_LOG_CPU_AFFINITY` supply file-worker CPUs. Console ignores legacy affinity exactly as before and uses only an explicitly supplied `worker_thread`.
- Metrics/tracing/chrono `core_mask` and their existing environment overrides supply worker CPUs.
- An explicitly supplied empty canonical `cpus` array clears a lower-priority pin. Omitted fields inherit. Old keys are accepted but are not emitted by canonical serialization.

Public C++ config structs migrate to `ThreadSpec` rather than retaining both a spec and a mask. This is a deliberate pre-1.0 source cleanup: it prevents contradictory `core_mask` plus `ThreadSpec` states and leaves one engine-side source of truth. `core::affinity::pin_self` and `self_affinity` remain compatibility adapters for callers that only need the old mask API; conversion helpers support call-site migration.

### D7. Runner applies, never computes

`StartOptions::cpus`, `AppSpec::cpus`, and persisted application CPU arrays use `CpuSet`; child affinity is still applied after inherited-affinity reset and before `exec`. Empty retains current reset behavior. Invalid application CPUs still fail validation/launch, and the runner does not assign policy or priority to application processes.

Runner control/logger configuration uses `runner_control_thread` and `runner_logger_thread` specs. Legacy `runner_control_cpus`, `runner_logger_cpus`, and global logging affinity remain readable compatibility inputs. Canonical runner logger CPUs take precedence over the legacy runner list, then global logging worker affinity. Control attributes are applied on the calling thread before its control loop; a required failure is returned before applications are launched, with prior attributes restored. Best-effort failures remain warnings and do not stop supervision. Logger-worker behavior uses the common logger startup path.

### D8. Root-free deterministic verification

Low-level syscall wrappers gain test-only fault points under the existing `NEXIGEN_ENABLE_FAULT_INJECTION` option. Injection can return `EPERM`, `EINVAL`, or another errno for name, affinity, scheduler, and snapshot/restore calls. This deterministically covers required failure, best-effort continuation, partial application, and rollback without root. Production builds compile the hooks away; even testing builds add no hot-path work because these wrappers are startup-only.

Real FIFO/RR checks are opt-in with `NEXIGEN_RT_INTEGRATION_TESTS=1` on a configured host with `CAP_SYS_NICE`. They verify `sched_getscheduler`, priority, name, and affinity. Default CI never sets it and never requires elevated privileges. CPUs above 63 are tested through value/conversion and fault-injection seams even on hosts with fewer than 64 online CPUs.

## Hot-path cost proof

The producer-facing headers and hot functions are not changed: no argument packing, level checks, shard claims, ring operations, instrument updates, span lifecycle, or application tick gains a field access or branch. The worker loop starts only after the cold spec/report operations and then executes the existing code. The 192-byte spec is not read per record, and the 64-byte report is not read by the worker loop. No new heap allocation, lock, system call, or atomic RMW appears after startup. Consequently the Release median latency gates measure the same instruction paths; the full matrix is a regression check, not a permission to relax the budgets.

## Risks / Trade-offs

- **1024-CPU fixed ceiling** → IDs above it fail explicitly; moving to a dynamic kernel bitset later would be a separate compatibility decision, not silent truncation.
- **Best effort can run partially configured** → this is only the explicit/default compatibility mode; the report exposes every failed attribute, while `Required` prevents the loop from starting.
- **Required startup now waits for cold startup** → bounded to initialization and never affects record/tick latency; best-effort startup timing stays asynchronous.
- **Metrics has constructor-based startup** → it cannot return a new initializer status without a larger API rewrite, so required failure is published through `startup_status()` and Infrastructure checks it immediately; no harvester runs in the failed state.
- **Rollback can itself fail** → rollback failures are reported rather than hidden, and initialization still fails without entering the worker loop.
- **C++ config migration is source-breaking pre-1.0** → conversion helpers and persisted-input compatibility limit the migration cost while avoiding permanent dual configuration.

## Migration Plan

1. Introduce `CpuSet`, `ThreadSpec`, report, application helper, and core tests/fault points.
2. Migrate logging, tracing, metrics, and chrono configs/workers one subsystem at a time, preserving legacy settings parsing.
3. Add Infrastructure required-failure checks and rollback tests.
4. Migrate runner CPU values and control/logger specs, keeping child launch and suite precedence tests intact.
5. Update examples, documentation, and serialization expectations; run the complete build/test/latency matrix.

No runtime data migration is needed. Old persisted settings continue to load; rolling back the code does not require changing those old files. New canonical files containing `worker_thread` require the new code and are not expected to be understood by older binaries.
