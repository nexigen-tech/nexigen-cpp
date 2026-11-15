# Design

## Context

The suite manifest, `ExecutionManager`, `StateClient`, and `StateCommand` are the
four extension points P0/P1 left stable. P2 adds platform controls without
touching the transition engine or the readiness model.

## Goals / Non-Goals

**Goals:**

- Named resource groups with memory/CPU/working-folder enforcement.
- SHA-256 executable verification when the manifest requires it.
- A deterministic cycle clock and seeded PRNG for applications.
- Requester identity and Function-Group-port authorization on `SetState`.
- Preserve every existing manifest and command that does not opt in.

**Non-Goals:**

- cgroups v2, memory accounting, OOM policing, or per-thread budgets.
- Cryptographic signatures, PKI, measured boot, or TPM integration.
- full AUTOSAR `DeterministicClient` API surface (only cycle/time/random).
- Dynamic authorization, credentials, or per-state port grants.

## Decisions

### D1 — Resource groups are manifest data, resolved at launch

`ResourceGroupConfig` lives beside `FunctionGroupConfig`:

```cpp
struct ResourceGroupConfig {
    std::string name;
    std::uint64_t memory_limit_mb{0}; // 0 = unlimited
    core::CpuSet cpus;
    std::string working_dir;
};
```

`AppSpec::resource_group` names one group (empty = none). Validation rejects
duplicate/unsafe names, a reference to an unknown group, an absolute-invalid
working folder, and an app `cpus` set that conflicts with the platform's online
set. `SuiteLaunchPlan` resolves the effective values:

1. app `cpus` when non-empty, otherwise the group's `cpus`;
2. app/group memory limit (group only for now);
3. group working folder.

### D2 — Enforcement in the child, before `exec`

`StartOptions` gains `memory_limit_bytes` and `working_dir`. In
`launch_application_child`, after `setsid` and before `execvp`:

- `chdir(working_dir)` when non-empty (the EM created it in the parent);
- `setrlimit(RLIMIT_AS, {limit, limit})` when non-zero.

Both failures use the existing setup-handshake failure path, so `start` reports
a visible error and writes no `resource.json`. The parent creates the working
folder with `0750`, under the run root or an absolute manifest path.

### D3 — Trusted executables use a pre-exec digest gate

`TrustedPlatformConfig { bool require_executable_digest; }` is suite-wide.
`AppSpec::executable_digest` is a 64-character hex SHA-256 string. When the
policy is enabled, every app must have an absolute command and a valid digest;
when disabled, a supplied digest is still verified.

`ExecutionManager::start` verifies in the parent before layout/fork:

1. `command.front()` must be an absolute path (no PATH lookup for trust);
2. `sha256_file` reads the file in cold-path chunks;
3. mismatch/missing/unreadable → `core::ErrorCode::SecurityError`.

`sha256_file` is a compact, tested implementation of FIPS 180-4 SHA-256 in
`nexigen::exec` (cold path only). Known-vector tests pin `""`, `"abc"`, and a
multi-block payload.

### D4 — DeterministicClient is allocation-free

`DeterministicClient` owns:

- `cycle_period_ns`, `start_time`, `cycle_index`;
- a splitmix64 state initialized from `seed`.

`wait_for_next_cycle()` advances the index and sleeps until
`start_time + index * period` using `clock_nanosleep(CLOCK_MONOTONIC,
TIMER_ABSTIME)`. If the caller is late, the index still advances by one and the
next deadline is recomputed; no catch-up burst is emitted. `cycle_time_ns()` is
purely derived from the index, so two clients with the same start/period agree.
No atomics are needed: one client belongs to one execution thread.

### D5 — Requester identity travels in `StateCommand`

`StateCommand` gains:

```cpp
char requester_id[k_state_command_name_length];
```

Existing senders that leave it empty keep working while
`function_group_port_mapping` is empty. `SmManifest` gains
`FunctionGroupPortMapping { requester_id, function_group }`; validation checks
safe ids and that the group exists.

`StateClient::execute` rejects a command when the mapping list is non-empty and
no entry matches `(requester_id, function_group)` with
`StateResult::AccessDenied`. The SM copies `TriggerRequest::requester_id` into
its pending transition and then into `StateCommand`; the SM's own MachineFG
lifecycle client uses its app name (or `"sm"`) as the requester.

### D6 — Error vocabulary is append-only

`core::ErrorCode::SecurityError` and `exec::StateResult::AccessDenied` are added
at the end of their tables. Existing numeric values and spellings do not move;
the table sizes and serialization tests are updated.

### D7 — No hot-path synchronization

Resource groups, the trusted policy, and the port mapping are immutable after
manifest load. `DeterministicClient` is single-threaded by contract. No new
atomics, locks, shard sizing, or memory orders are introduced; the existing
`alignas(64)` hot-path rules are untouched.

## Risks / Trade-offs

- **`RLIMIT_AS` is coarse** → it bounds virtual address space, not RSS; the
  manifest documents this as a budget, not an OOM accounting system.
- **SHA-256 is not a signature** → it detects accidental/naive tampering, not a
  determined attacker who can rewrite both manifest and executable. PKI is
  deferred.
- **Clock sleep can drift under load** → the client reports the scheduled cycle
  time, not the wake time; late cycles are visible through `cycle_index`.
- **Access mapping is static** → a requester that is not listed cannot switch
  any group; deployments must list every legitimate SM.

## Migration Plan

1. Add the manifest types, validation, and serialization.
2. Add `SecurityError`/`AccessDenied` and regenerate conversion tests.
3. Enforce resources and digests in `ExecutionManager`.
4. Add requester authorization and update the SM sender.
5. Add `DeterministicClient` and its tests.
6. Update docs/examples and run the four-config matrix.

## Open Questions

None blocking. Whether resource groups should become cgroups is deferred to a
later platform change.
