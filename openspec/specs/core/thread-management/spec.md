# core/thread-management Specification

## Purpose

Provides one fixed-capacity, allocation-free vocabulary for CPU placement and background-thread attributes, plus deterministic self-application, partial-application diagnostics, and required/best-effort failure semantics on cold paths.

## Requirements

### Requirement: CpuSet represents CPUs without the 64-bit mask limit

`CpuSet` SHALL store CPU IDs 0 through 1023 in fixed inline storage without heap allocation. It SHALL convert legacy uint64 masks and CPU-ID spans without losing representable IDs. An empty set SHALL be valid and mean that no affinity call is requested. Conversion or parsing SHALL reject duplicate IDs, negative IDs, malformed ranges, and IDs above 1023 with `InvalidArgument`.

#### Scenario: CPUs above 63 survive conversion

- **WHEN** a CPU list containing `2`, `64`, and `130` is converted to a `CpuSet`
- **THEN** all three IDs are present, `count()` is 3, and no 64-bit mask conversion is required

#### Scenario: Legacy mask conversion is exact

- **WHEN** a uint64 mask with bits 0, 3, and 63 is converted
- **THEN** the set contains exactly CPUs 0, 3, and 63

#### Scenario: Empty set preserves inherited placement

- **WHEN** an empty `CpuSet` is included in a thread specification
- **THEN** no affinity system call is requested and the thread keeps its inherited affinity

#### Scenario: Duplicate and out-of-range IDs are rejected

- **WHEN** a list repeats an ID, contains a negative ID, or contains ID 1024
- **THEN** conversion returns `InvalidArgument` instead of silently deduplicating or truncating

#### Scenario: Host availability is checked at application

- **WHEN** a representable CPU is offline, nonexistent, or rejected by the process's cpuset permissions
- **THEN** the application reports an OS failure according to its apply mode rather than misclassifying the value type as malformed

#### Scenario: Current affinity is not a subset constraint

- **WHEN** a requested CPU is outside the thread's current affinity mask but the kernel permits widening
- **THEN** the request succeeds rather than being rejected by a userspace subset check

### Requirement: CpuSet has canonical diagnostics and JSON

`CpuSet` SHALL provide member and free `to_string()` using ascending CPU lists/ranges, a `std::formatter` delegation, and non-throwing `to_json`/`from_json`. JSON SHALL be a sorted array of numeric CPU IDs.

#### Scenario: CpuSet string and formatter agree

- **WHEN** a set containing CPUs 0, 2, 3, 4, and 70 is rendered
- **THEN** `to_string()` contains `0,2-4,70` and `std::format("{}", set)` returns the same text

#### Scenario: CpuSet JSON round-trips without exceptions

- **WHEN** a set containing CPUs above 63 passes through `to_json` and `from_json` under `JSON_NOEXCEPTION=1`
- **THEN** the recovered set is equal and the JSON IDs are sorted

#### Scenario: Invalid CpuSet JSON leaves the destination unchanged

- **WHEN** JSON contains a negative ID, a duplicate, an ID above 1023, or a non-numeric entry
- **THEN** parsing reports `InvalidArgument` without partially replacing the destination

### Requirement: ThreadSpec validates names and scheduling attributes

`ThreadSpec` SHALL carry an optional thread name, a `CpuSet`, policy `Other`, `FIFO`, or `RR`, a priority, and apply mode `BestEffort` or `Required`. Empty name and CPU set SHALL skip those operations. The default SHALL leave the scheduler unconfigured. `Other` SHALL require priority 0; `FIFO` and `RR` SHALL require priority 1 through 99. Names longer than 15 bytes SHALL be rejected rather than truncated.

#### Scenario: Default spec preserves inherited attributes

- **WHEN** a default `ThreadSpec` is applied
- **THEN** no name, affinity, or scheduler system call is requested and the mode is `BestEffort`

#### Scenario: FIFO and RR accept valid priorities

- **WHEN** FIFO priority 1 and RR priority 99 are validated
- **THEN** both specifications are accepted

#### Scenario: Invalid priority is rejected before application

- **WHEN** `Other` has nonzero priority, FIFO or RR has priority 0, or an RT priority exceeds 99
- **THEN** validation returns `InvalidArgument` and no thread attribute is changed

#### Scenario: Long and empty names have deterministic behavior

- **WHEN** a name is empty, exactly 15 bytes, or 16 bytes
- **THEN** empty skips renaming, 15 bytes is accepted with a terminating NUL, and 16 bytes returns `InvalidArgument` without truncation

#### Scenario: Explicit Other can reset an inherited scheduler

- **WHEN** a caller explicitly configures `Other` with priority 0
- **THEN** the scheduler operation is requested even though `Other` is the default policy value

### Requirement: Thread enums and specs follow the serialization mandates

`SchedulingPolicy` and `ThreadApplyMode` SHALL provide constexpr `to_string`, non-throwing `from_string`, ADL JSON, and `std::formatter` support using `other`/`fifo`/`rr` and `best_effort`/`required`. `ThreadSpec` SHALL provide member/free `to_string`, a formatter, and non-throwing JSON round-trip of every configured field.

#### Scenario: Enum vocabulary round-trips

- **WHEN** every policy and apply-mode enumerator is formatted and passed through string and JSON conversions
- **THEN** the canonical lowercase names are recovered and unknown names leave destinations unchanged

#### Scenario: ThreadSpec round-trips all attributes

- **WHEN** a spec with a name, CPUs above 63, RR priority 40, and `Required` is serialized and parsed
- **THEN** every field is recovered and `std::format("{}", spec)` equals `to_string()`

#### Scenario: Invalid spec JSON is non-throwing

- **WHEN** JSON contains an unknown policy, invalid priority, or overlong name under `JSON_NOEXCEPTION=1`
- **THEN** parsing reports `InvalidArgument` without throwing or applying attributes

### Requirement: Threads apply their own specs before starting work

A background worker SHALL receive an immutable copy of its `ThreadSpec` and apply it to itself before sink/provider startup and before its main loop. The spawning thread SHALL NOT pin or reschedule an already running worker. Attribute application SHALL be cold-path-only and SHALL NOT change producer, harvest, drain, backoff, or flush algorithms.

#### Scenario: First work observes the requested attributes

- **WHEN** a worker with valid name, affinity, and scheduler attributes starts
- **THEN** those attributes are already applied when its first sink callback, harvest, or snapshot runs

#### Scenario: No parent/worker configuration race exists

- **WHEN** a worker is created with a required spec
- **THEN** the worker applies its own copied spec and no parent-side native-handle operation can race its main loop

#### Scenario: Hot paths remain unchanged

- **WHEN** steady-state logging, metrics, tracing, chrono, and application tick benchmarks run after startup
- **THEN** they perform no thread-spec parsing, allocation, locking, or attribute syscalls and retain their existing latency gates

### Requirement: Required application reports failure and rolls back

With `Required`, any requested attribute rejected by the OS SHALL produce an error status, prevent the worker's main loop from starting, and restore attributes already changed during that application. A report SHALL identify requested, applied, failed, and rollback-failed attributes. Required FIFO/RR failure MUST NOT silently continue as `Other`.

#### Scenario: EPERM for required FIFO aborts startup

- **WHEN** affinity succeeds but FIFO application returns `EPERM` under `Required`
- **THEN** initialization fails with `SystemError`, no main loop runs, and the report records affinity applied and scheduler failed

#### Scenario: Required failure restores earlier changes

- **WHEN** name and affinity were changed before a required scheduler failure
- **THEN** the previous name, affinity, and scheduler are restored before initialization returns, or any restoration failure is explicitly reported

#### Scenario: Invalid input causes no partial application

- **WHEN** a required spec has an invalid policy/priority combination
- **THEN** validation fails before any name, affinity, or scheduler operation

### Requirement: Best-effort application exposes partial success

With `BestEffort`, all requested attributes SHALL be attempted, OS failures SHALL not prevent the normal worker loop, and the report SHALL expose every failed attribute and the first error. Best effort MUST NOT be inferred from a failed `Required` request.

#### Scenario: EPERM for best-effort FIFO leaves a visible partial state

- **WHEN** affinity succeeds but FIFO returns `EPERM` under `BestEffort`
- **THEN** the worker runs with the applied affinity, its startup report records scheduler failure, and initialization does not claim complete application

#### Scenario: Independent failures are all attempted

- **WHEN** both an overridable name operation and affinity operation fail under `BestEffort`
- **THEN** the scheduler request is still attempted and the report records both earlier failures

### Requirement: ThreadApplyReport is a serializable startup snapshot

`ThreadApplyReport` SHALL provide member/free `to_string`, a formatter, and non-throwing JSON round-trip of requested/applied/failed/rollback-failed masks and the first error. It SHALL be published once during startup and SHALL NOT introduce per-record atomic traffic.

#### Scenario: Partial report renders and round-trips

- **WHEN** a report records applied affinity, failed scheduler, and `SystemError`
- **THEN** its text names that partial state and JSON preserves every mask and the error under `JSON_NOEXCEPTION=1`

### Requirement: `apply_thread_spec_self` delegates to single-attribute applicators

The public `apply_thread_spec_self` function SHALL remain the only entry point for applying a `ThreadSpec` to the calling thread. Its body SHALL be composed of small, focused helpers that each handle one attribute class: name, affinity, or scheduler. Each helper SHALL record its result in the supplied `ThreadApplyReport` and SHALL honor the current `ThreadApplyMode` (best-effort vs. required).

#### Scenario: Name application is isolated

- **WHEN** a `ThreadSpec` requests a non-empty name
- **THEN** a dedicated helper attempts `pthread_setname_np`, updates `report.applied` or `report.failed`, and sets `report.first_error` on first failure

#### Scenario: Affinity application is isolated

- **WHEN** a `ThreadSpec` requests a non-empty `CpuSet`
- **THEN** a dedicated helper builds the native `cpu_set_t`, attempts `pthread_setaffinity_np`, and records the outcome in the report

#### Scenario: Scheduler application is isolated

- **WHEN** a `ThreadSpec` has `scheduler_configured()` true
- **THEN** a dedicated helper builds the native `sched_param`, attempts `sched_setscheduler`, and records the outcome in the report

### Requirement: Required-mode rollback remains centralized

The rollback path for `Required` mode SHALL remain in the main function or a single `restore_applied_attributes` helper. It SHALL restore name, affinity, and scheduler in the reverse order of mutation and SHALL update `report.rollback_failed` for any restoration that fails.

#### Scenario: Required FIFO failure rolls back earlier changes

- **WHEN** name and affinity are applied but scheduler fails under `Required`
- **THEN** the function returns `SystemError`, affinity and name are restored, and `report.rollback_failed` is zero if all restorations succeed

### Requirement: `parse_thread_spec_json` delegates to field parsers

The internal `parse_thread_spec_json` function SHALL parse each JSON field through a dedicated helper: `parse_name`, `parse_cpus`, `parse_mode`, and `parse_scheduler`. Each helper SHALL operate on a staging `ThreadSpec` and SHALL return a failure indication rather than mutating the output on a bad field.

#### Scenario: Oversized name leaves the destination unchanged

- **WHEN** the JSON `name` field is 16 bytes or contains a NUL
- **THEN** the helper reports failure and the caller returns `false` without modifying the caller's `ThreadSpec`

#### Scenario: Invalid scheduler pair leaves the destination unchanged

- **WHEN** the JSON `policy`/`priority` pair violates the `Other=0`, `FIFO/RR=1..99` rule
- **THEN** the helper reports failure and the caller returns `false` without modifying the caller's `ThreadSpec`

### Requirement: Refactored internals preserve all observable behavior

After decomposition, the public functions SHALL produce identical results for every input: the same `ThreadApplyReport` masks, the same rollback behavior, the same JSON round-trip, and the same validation errors. No public header SHALL gain or lose a symbol.

#### Scenario: Existing thread-management tests pass unchanged

- **WHEN** the existing unit tests for `ThreadSpec`, `CpuSet`, `apply_thread_spec_self`, and JSON round-trip run
- **THEN** they pass without modification

### Requirement: Complexity of decomposed functions stays within the project target

Every function introduced or modified by this refactor SHALL have cyclomatic complexity ≤ 15 as measured by lizard. The overall average complexity for `include/nexigen/core/thread_management.hpp` SHALL not increase.

#### Scenario: Post-refactor lizard report

- **WHEN** lizard is run over `include/nexigen/core/thread_management.hpp`
- **THEN** no function in the file exceeds complexity 15

### Requirement: Spec emptiness and fallback are explicit

`ThreadSpec::has_requests()` SHALL return true exactly when the spec requests at least one attribute: a non-empty name, a non-empty CPU set, or an explicitly configured scheduler (including an explicit `Other,0` reset). `core::effective_thread_spec(primary, fallback)` SHALL return `primary` when it has requests and `fallback` otherwise, without mutating either argument.

#### Scenario: A default spec requests nothing

- **WHEN** a default-constructed `ThreadSpec` is queried
- **THEN** `has_requests()` is false

#### Scenario: Every individual attribute counts as a request

- **WHEN** a spec sets only a name, only a non-empty CPU set, or only an explicit scheduler
- **THEN** `has_requests()` is true in each case, including an explicit `Other,0` scheduler reset

#### Scenario: Empty primary inherits the fallback

- **WHEN** `effective_thread_spec` is called with an empty primary and a fallback carrying CPUs and FIFO priority
- **THEN** the result equals the fallback

#### Scenario: Primary wins when it requests anything

- **WHEN** the primary sets only a name and the fallback sets CPUs and a scheduler
- **THEN** the result equals the primary and the fallback is ignored

#### Scenario: Neither argument is mutated

- **WHEN** `effective_thread_spec` returns either argument
- **THEN** both inputs still compare equal to their pre-call values
