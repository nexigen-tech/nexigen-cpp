# core/affinity Specification

## Purpose

Defines the single, exception-free thread-affinity contract shared by the observability modules: pinning the **calling thread** by a uint64 core mask, querying the calling thread's mask back, and the first-failure error mapping — exactly one implementation lives in `nexigen::core::affinity`, and the per-module `detail/affinity` declarations forward to it without private copies.

## Requirements

### Requirement: Pinning the calling thread by core mask

The system SHALL provide one pinning function that applies a uint64 core mask to the **calling thread only** through the pthread affinity interface: a zero mask is an argument error; set bits below bit 64 select their CPU; an ungrantable CPU surfaces a system-error status; functions are `noexcept`, allocation-free, cold-path-only (worker/provider start), and callers pin the to-be-pinned thread as its first act (the spawning thread stays unpinned).

#### Scenario: Zero mask is rejected, thread state untouched

- **WHEN** the pinning function is invoked with a zero mask
- **THEN** the result is an argument-error status (`InvalidArgument`), no affinity system call runs, and the calling thread's affinity is unchanged

#### Scenario: Each thread pins only itself

- **WHEN** two threads pin themselves to different single-core masks with the same function
- **THEN** each thread's subsequent affinity query sees exactly its own pinned core (the spawn-thread call convention cannot pin a different thread)

#### Scenario: Ungrantable CPU surfaces a system error

- **WHEN** the mask names a CPU absent from the platform's online set (the probe pins a bit far beyond the host's core count)
- **THEN** the result is a first-failure error status (`SystemError`) rather than success, silence, or an exception

### Requirement: Querying the calling thread's affinity

The system SHALL provide one query function that reads the calling thread's current affinity into a uint64 mask. Values beyond bit 63 are outside the uint64 domain and SHALL be dropped silently as documented (the mask surface is cores 0–63); a syscall-level failure SHALL surface a system-error status.

#### Scenario: Round-trip pin then query agrees

- **WHEN** a thread pins itself to a single-core mask and then queries its affinity
- **THEN** the queried mask equals the pinned mask

#### Scenario: Query reports the pre-pinning platform state

- **WHEN** a fresh, unpinned thread queries its affinity without a preceding pin call
- **THEN** the query succeeds and yields a non-empty representable mask (the scheduler default), never an uninitialized state

### Requirement: Single implementation behind module shims

`nexigen::core::affinity` SHALL remain the compatibility implementation of the uint64-mask contract and SHALL delegate CPU-set conversion/application to the shared `core/thread-management` machinery. The chrono, logging, metrics, and tracing `detail/affinity` declarations SHALL remain forward-only shims, and no module unit SHALL keep a private `cpu_set_t`-converting copy. ExecutionManager CPU lists and thread specs SHALL use the same public CPU vocabulary rather than a third representation.

#### Scenario: Audit finds one implementation and four forwarders

- **WHEN** the affinity-related units are audited
- **THEN** legacy pin/query behavior is preserved, each module header forwards to core, and only the shared core machinery converts CPU values for operating-system calls

#### Scenario: Existing call sites and suites unchanged

- **WHEN** legacy `detail::pin_self`/`detail::self_affinity` calls are exercised
- **THEN** zero-mask rejection, calling-thread pinning, low-64-bit readback, and error mapping behave as before, including for workers not yet migrated by a caller
