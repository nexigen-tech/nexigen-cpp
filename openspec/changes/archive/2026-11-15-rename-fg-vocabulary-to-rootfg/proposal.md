# Proposal

## Why

The project used two names for the same reserved Function Group: `MachineFG` in
the Execution Manager and `PlatformFG` in the State Manager. The split made the
vocabulary harder to follow and forced cross-module translation in docs, specs,
and tests.

## What Changes

- Rename the reserved Function Group `MachineFG` to `RootFG` in `nexigen_exec`.
- Rename the SM `PlatformFG` enum, member, accessor, and state file to `RootFG`.
- Rename the related MachineFG/PlatformFG helpers:
  `machine_function_group` → `root_function_group`, `k_machine_state_*` →
  `k_root_state_*`, `MachineLifecycleClient` → `RootLifecycleClient`,
  `InitialMachineStateTransitionResult` → `InitialRootStateTransitionResult`,
  and the query service to `Nexigen/EM/InitialRootStateTransitionResult`.
- Update examples, tests, architecture docs, and main specs to `RootFG`.

## Impact

- **Performance:** none; identifiers only.
- **Compatibility:** `RootFG` is a new reserved name, so manifests that still
  declare `MachineFG` no longer receive the reserved treatment. The project is
  pre-1.0 and examples/tests are updated in the same change.
- **Machine Manifest:** the AUTOSAR `MachineManifest`/`Machine` concepts keep
  their names; only the Function Group vocabulary is unified.

## Capabilities

No new runtime capability. The rename updates the existing
`exec/root-function-group`, `sm/state-machine`, `exec/state-client`, and
related requirement text; specs are updated in place and the change is archived
with `skip_specs`.
