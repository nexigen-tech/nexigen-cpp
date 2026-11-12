# Spec Delta

## Purpose

Defines one header-only owner for a background `std::jthread`: the worker applies its own `ThreadSpec`, publishes a readiness snapshot, runs a caller-supplied body, and stops on request, removing the duplicated start/rollback/join protocol from the providers.

## ADDED Requirements

### Requirement: WorkerThread owns the thread and body without type erasure

`nexigen::core::WorkerThread<Body>` SHALL own exactly one `std::jthread` and store `Body` inline, without `std::function`, virtual dispatch, or per-tick indirection. A successful `start` SHALL be single-use: a second `start` while the thread is joinable SHALL return `Conflict`. The destructor SHALL request stop and join.

#### Scenario: The body runs on the owned thread

- **WHEN** a worker starts with a body that records its `std::this_thread::get_id()`
- **THEN** the recorded id differs from the caller's id and the body runs exactly once

#### Scenario: A second start is rejected

- **WHEN** `start` is called on a worker that is already joinable
- **THEN** it returns `Conflict` and the running body is unaffected

#### Scenario: Destruction joins the body

- **WHEN** a started worker is destroyed
- **THEN** the body observes the stop request and the destructor returns only after the thread has joined

### Requirement: The worker applies its own ThreadSpec before the body

The worker SHALL call `apply_thread_spec_self` on its own thread before invoking any body hook, snapshot its post-application affinity, and publish the `ThreadApplyReport` and first start error through a release store. The spawning thread SHALL NOT pin or reschedule the worker. With `Required`, an attribute failure SHALL prevent `on_start` and `run` from executing.

#### Scenario: Attributes are applied before the first body step

- **WHEN** a worker with a valid name, CPU set, and scheduler starts
- **THEN** its first `on_start`/`run` step observes the requested attributes and the report marks them applied

#### Scenario: Required failure suppresses the body

- **WHEN** a required scheduler application is rejected by the OS
- **THEN** `on_start` and `run` never execute, already-applied attributes are rolled back, and the report records the failure

#### Scenario: Best-effort failure still runs the body

- **WHEN** one requested attribute is rejected in `BestEffort` mode
- **THEN** the report records the partial application and `run` proceeds

#### Scenario: No parent-side pinning

- **WHEN** the caller's thread has a different affinity than the spec
- **THEN** the worker's affinity matches the spec and the caller's affinity is unchanged

### Requirement: Start publishes a readable readiness snapshot

The worker SHALL publish `ready`, `start_code`, `ThreadApplyReport`, and the post-pin mask with release/acquire ordering. In `Required` mode `start` SHALL block until the snapshot is published, then return the attribute error and join on failure; in `BestEffort` mode it SHALL return after the thread is created. An `on_start` error SHALL be recorded in `start_code` but SHALL NOT suppress `run`.

#### Scenario: Required start returns the attribute failure

- **WHEN** a required attribute fails during startup
- **THEN** `start` returns `SystemError`, the worker is joined, and `start_code` carries the first error

#### Scenario: Best-effort start is non-blocking

- **WHEN** a best-effort worker starts with a slow `on_start`
- **THEN** `start` returns success before `on_start` completes and `ready()` becomes true later

#### Scenario: on_start failure does not suppress the loop

- **WHEN** `on_start` returns `SystemError`
- **THEN** `start_code` reports it and `run` still executes

### Requirement: Body contract is two-phase and stop-aware

`Body` SHALL provide a `run(std::stop_token) noexcept` member and MAY provide `on_start() noexcept -> core::Status`. The worker SHALL invoke `on_start` before `run`, propagate the first error into `start_code`, and pass a stop token whose request is observed by the body. The wrapper SHALL NOT allocate or synchronize on each loop iteration.

#### Scenario: on_start runs before run

- **WHEN** a body records the order of its hooks
- **THEN** the sequence is `on_start` then `run`

#### Scenario: stop token reaches the body

- **WHEN** `request_stop` is called while the body waits on a stop-aware condition
- **THEN** the wait ends, `run` returns, and `join` completes

#### Scenario: The loop path adds no work

- **WHEN** the body runs its steady-state loop
- **THEN** the wrapper performs no allocation, locking, or attribute call between body invocations

### Requirement: WorkerThread keeps its lifetime contract observable

The worker SHALL provide `joinable`, `ready`, `start_code`, `report`, and `mask`, and SHALL document that the owning object must outlive the thread and declare the worker after the members its body touches. `mask` SHALL be a diagnostic snapshot (cores above 63 dropped by the legacy mask). `request_stop` and `join` SHALL be safe to call when no thread is running.

#### Scenario: Stop and join are idempotent with no thread

- **WHEN** `request_stop` and `join` are called before `start` or after a completed join
- **THEN** both return without error or blocking

#### Scenario: Mask reflects the worker's view

- **WHEN** a worker with a one-CPU spec starts
- **THEN** `mask()` contains exactly that CPU (within the 0..63 diagnostic range)

### Requirement: WorkerThread diagnostics follow the serialization contract

`WorkerThread` SHALL provide a cold-path `to_string()` member, a free helper, a `std::formatter` specialization, and `to_json` naming the thread spec, readiness, start code, report, and mask; it SHALL deliberately provide no `from_json`.

#### Scenario: Formatter and JSON agree with the diagnostic

- **WHEN** `to_string()`, `std::format("{}", worker)`, and `to_json` are evaluated after a start
- **THEN** the formatter output equals `to_string()` and the JSON names the spec, ready, start code, report, and mask

#### Scenario: Live worker is not reconstructible

- **WHEN** a reader attempts `from_json` for `WorkerThread`
- **THEN** the call does not compile because the live worker deliberately has no `from_json`
