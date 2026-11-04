# Spec Delta

## Purpose

Provides one fixed-capacity, allocation-free vocabulary for CPU placement and background-thread attributes, plus deterministic self-application, partial-application diagnostics, and required/best-effort failure semantics on cold paths.

## ADDED Requirements

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
