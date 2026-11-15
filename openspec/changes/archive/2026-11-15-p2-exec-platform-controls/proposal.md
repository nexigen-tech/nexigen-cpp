# Proposal

## Why

P0 and P1 made the suite control path correct and lifecycle-aware. The remaining
AUTOSAR Adaptive platform controls are still missing:

- applications have a CPU affinity but no memory budget, working folder, or
  named resource group;
- any process that can open `Nexigen/EM/StateCommand` can drive any Function
  Group, and a `StateCommand` carries no requester identity;
- the platform has no integrity gate for the executable it starts;
- applications that need cycle-based, reproducible behavior have no
  deterministic client (cycle clock, synchronized time, seeded random).

This change adds those four P2 controls as a coherent exec-platform baseline.

## What Changes

- **Resource groups.** A suite manifest may declare named resource groups with a
  memory limit, a CPU set, and a working folder. An application may reference
  one group; the EM applies the group defaults unless the app overrides them,
  creates the working folder, sets `RLIMIT_AS`, and starts the child there.
- **Trusted executables.** The manifest may require an executable SHA-256 digest
  per application. The EM verifies the digest before forking and refuses to
  start a missing, unreadable, or mismatched executable with a new
  `SecurityError` result.
- **Deterministic execution.** `nexigen::exec::DeterministicClient` provides a
  fixed-cycle clock (`wait_for_next_cycle`, `cycle_index`, `cycle_time_ns`) and
  a seeded splitmix64 PRNG that never allocates.
- **StateClient access control.** `StateCommand` carries a `requester_id`; a
  manifest `function_group_port_mapping` list authorizes
  `(requester_id, function_group)` pairs. A missing or unknown requester is
  rejected with `AccessDenied`; an empty mapping list keeps the previous
  behavior.
- **Error vocabulary.** `core::ErrorCode` gains `SecurityError`, and
  `exec::StateResult` gains `AccessDenied`, both append-only.

## Capabilities

### New Capabilities

- `exec/resource-groups`: named memory/CPU/working-folder groups and their
  enforcement at launch.
- `exec/trusted-executable`: manifest digests and pre-exec integrity
  verification.
- `exec/deterministic-execution`: cycle clock and seeded deterministic random.
- `exec/state-client-access-control`: requester identity and
  Function-Group-port authorization.

### Modified Capabilities

- `exec/app-suite`: manifest gains resource groups, a per-app group reference,
  a trusted-platform policy, and per-app executable digests.
- `exec/state-client`: commands carry a requester, authorization is enforced,
  and the result vocabulary gains `AccessDenied`.
- `exec/process-supervision`: child setup applies the memory limit and working
  folder before `exec`.
- `core/result`: `ErrorCode` gains `SecurityError`.

## Impact

- **Performance:** all new checks are cold-path startup work. Resource-group
  lookup is a small manifest scan; digest verification reads the executable
  once before fork. `DeterministicClient` is allocation-free and uses a
  precomputed cycle period; its wait is a clock sleep, not a busy loop.
- **Memory:** a new per-suite vector of resource groups and a small fixed
  requester field in `StateCommand`; no per-record hot-path state.
- **Compatibility:** manifests without resource groups, trusted policy, or port
  mappings keep the existing behavior. `StateCommand` grows by one fixed
  buffer; all senders are updated in the same change.
