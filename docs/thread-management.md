# Thread management and CPU placement

`nexigen::core` owns the cold-path vocabulary for describing and applying
background-thread attributes: thread name, CPU affinity, and scheduling policy.
It is shared by the logging, metrics, tracing, chrono, and runner modules, so a
worker is configured the same way everywhere.

The authoritative behaviour contract is the `core/thread-management` capability
under `openspec/specs/`. This page is the practical guide.

Hot paths never read these types. `CpuSet`, `ThreadSpec`, and
`ThreadApplyReport` are initialization-time values; applying a spec happens once,
on the calling thread, before its loop starts.

## Quick start

```cpp
#include <nexigen/core/thread_management.hpp>

namespace core = nexigen::core;

core::ThreadSpec spec{};
spec.set_name("hil-worker");                 // optional, <= 15 bytes

core::CpuSet cpus{};
cpus.add(2);
cpus.add(64);                                // CPUs above 63 are supported
spec.set_cpus(cpus);                         // empty set = inherit affinity

spec.set_scheduler(core::SchedulingPolicy::FIFO, 40); // optional; Other,0 = reset
spec.set_apply_mode(core::ThreadApplyMode::BestEffort);

core::ThreadApplyReport report{};
const core::Status applied = core::apply_thread_spec_self(spec, report);

// BestEffort: a failure is visible in `report` and supervision may continue.
// Required:   `applied` is an error, applied attributes were rolled back,
//             and the caller must abort initialization.
if (!applied && spec.apply_mode() == core::ThreadApplyMode::Required) {
    return applied;
}
```

The default-constructed `ThreadSpec` requests nothing: inherited name, affinity,
and scheduling are preserved and no syscall is issued.

## Types

| Type | Purpose | Layout |
|---|---|---|
| `core::CpuSet` | Fixed set of CPU ids `0..1023` | 128 bytes, `alignas(64)`, inline |
| `core::ThreadSpec` | Name, CPU set, scheduler, and apply mode | 192 bytes, `alignas(64)`, inline |
| `core::ThreadApplyReport` | Per-attribute outcome of one application | 64 bytes, `alignas(64)` |

All three are trivially copyable, equal-comparable value types. They have the
usual cold-path serialization surface (`to_string`, `to_json`/`from_json`,
`std::formatter`) and no heap storage.

### `CpuSet`

- `add(cpu)` rejects negative ids, ids above 1023, and duplicates.
- `from_cpus(span)` / `from_core_mask(uint64)` are all-or-nothing conversions.
- `to_string()` renders compact ascending ranges, e.g. `0,2-4,70`; empty is `[]`.
- A zero legacy mask is a valid empty set here. The legacy
  `core::affinity::pin_self(0)` error is a separate API.

### `ThreadSpec`

| Attribute | Setter | Empty/default | Validation |
|---|---|---|---|
| Name | `set_name` | inherited name | at most 15 bytes, embedded NUL rejected |
| CPUs | `set_cpus` | inherited affinity | ids `0..1023`, no duplicates |
| Scheduler | `set_scheduler` | inherited scheduling | `Other` requires priority `0`; `FIFO`/`RR` use `1..99` |
| Apply mode | `set_apply_mode` | `BestEffort` | `BestEffort` or `Required` |

`clear_scheduler()` returns to inherited scheduling. Because a default spec
preserves inheritance, an explicit `Other, 0` reset is tracked separately by
`scheduler_configured()` and round-trips through JSON as an explicit request.

`validate()` checks the whole spec before any operating-system call.

### `ThreadApplyReport`

The report is a bitmask pair plus the first error:

| Accessor | Meaning |
|---|---|
| `requested()` | attributes the spec asked to change |
| `applied()` | attributes that succeeded during this attempt (even if later rolled back) |
| `failed()` | attributes the kernel rejected |
| `rollback_failed()` | applied attributes that could not be restored after a `Required` failure |
| `first_error()` | first error observed, `Ok` when nothing failed |
| `complete()` | no failed and no rollback-failed attributes |

Bits are `name_mask`, `affinity_mask`, and `scheduler_mask`. For operator-facing
text use `nexigen::runner::describe_thread_apply_report(report)`, which names the
attributes instead of printing raw masks.

## Application semantics

`apply_thread_spec_self(spec, report)` applies attributes in this order:

1. thread name (`pthread_setname_np`);
2. CPU affinity (`pthread_setaffinity_np`);
3. scheduling policy and priority (`sched_setscheduler`).

Before the first change it snapshots the current name, affinity, and scheduler.

- **`BestEffort`** attempts every requested attribute, records each failure, and
  returns success. Supervision continues with whatever was applied.
- **`Required`** stops at the first failure, restores already-applied attributes
  in reverse order, and returns `SystemError`. A failed rollback is itself
  recorded in `rollback_failed()`.
- **Validation** happens before any syscall. Invalid configuration reports the
  validation error without touching the thread.
- **Non-Linux** builds have no native application path: every requested
  attribute is reported failed, and `Required` returns an error.

Applying a spec only affects the **calling thread**. Forked children start with
the inherited affinity reset by the runner, and an application with explicit
`cpus` applies its own list afterwards.

## Module integration

The same spec type is accepted by every background worker:

| Module | Configuration point |
|---|---|
| logging | `LoggerOptions::worker_thread` / `worker_thread` in `logging.json` |
| metrics | `MetricProvider_config::worker_thread` |
| tracing | `TracerProvider_config::worker_thread` |
| chrono | `ChronoProvider_config::worker_thread` |
| runner | suite manifest `runner_control_thread` / `runner_logger_thread` |

Each provider exposes the resulting `ThreadApplyReport` (for example
`MetricProvider::worker_thread_report()`) so a host application can log the
per-attribute outcome without re-querying the kernel. A `Required` failure
prevents the provider's worker loop from starting.

The runner applies the control spec on the CLI/suite-supervisor calling thread
and the logger spec on the logging worker before its first write. See
[`runner-suite.md`](runner-suite.md#runner-thread-affinity) for the manifest
fields and the best-effort warning contract.

### JSON manifest form

```json
{
  "runner_control_thread": {
    "name": "suite-control",
    "cpus": [0, 1],
    "policy": "other",
    "priority": 0,
    "mode": "best_effort"
  },
  "runner_logger_thread": {
    "name": "runner-logger",
    "cpus": [2, 64],
    "policy": "rr",
    "priority": 40,
    "mode": "required"
  }
}
```

Omitting `policy` and `priority` preserves inherited scheduling. Omitting `cpus`
preserves inherited affinity. The legacy `runner_control_cpus` and
`runner_logger_cpus` arrays remain valid and normalize to best-effort specs.

## Diagnostics

Best-effort warnings are rendered from the report, not from the requested spec.
A message therefore distinguishes:

- a scheduler-only rejection while name and affinity applied;
- a partial application (for example `applied=name,affinity`,
  `failed=scheduler`);
- a name or affinity failure that has nothing to do with an "invalid CPU" list.

In particular, a warning never claims the thread is unpinned when only a
non-CPU attribute failed. The runner CLI logs
`event=thread_spec_warning thread=control detail=<report>`; the suite supervisor,
which forks before the logger bridge exists, writes the same report-derived detail
to stderr.

## See also

- `openspec/specs/core/thread-management/spec.md` — normative requirements and
  scenarios.
- [`runner-suite.md`](runner-suite.md) — manifest fields, restarts, and runner logging.
- [`architecture.md`](architecture.md) — hot/cold separation and module mandates.
